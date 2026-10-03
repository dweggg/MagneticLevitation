# Communications overview

This directory contains the firmware side of the link between the board and the PC. The PC tools live in the repository's `pc` directory. Together, they let a computer inspect and change selected firmware variables, receive live measurements, and display firmware log messages over the board's USB connection.

The board presents itself as a USB CDC device, which appears to the PC as a serial port. It is a USB data connection; the baud rate setting is there for serial-port compatibility and does not set the speed of a physical UART.

## How it fits together

When firmware starts, each application module registers the variables it wants to expose. The registry assigns IDs in registration order and keeps each variable connected to the storage owned by its module. The PC asks the board for the current variable and stream lists when it connects, so IDs and names do not need to be maintained separately on the two sides.

The PC sends short commands to read, write, or discover variables. The firmware checks each request against the registry and replies with the result. Separately, control tasks can publish groups of measurements as telemetry streams. The PC receives those samples while commands are in progress and can plot them against the firmware's clock.


The direction names describe the board's point of view. `TX` means readable by the PC, `RX` means writable by the PC, and `TX/RX` allows both. Streamed variables are always readable too.

## Firmware side

`usb_config.h` describes the USB CDC device and its endpoints. `usb_cdc.c` handles the USB data endpoints and buffers incoming and outgoing bytes. The scheduled USB task passes received bytes to the protocol bridge and starts sending queued data when the host is ready.

`vars.c` and `vars.h` provide the shared registry. Application modules register their own storage during initialization, so a host write changes the module's value directly and a host read or telemetry sample reads that same value. The built-in `tick_hz` and `stream_dropped` values describe the firmware clock and samples that could not be queued.

`protocol.c` handles host commands, variable-list replies, logs, and outgoing frames. `protocol.h` defines the command and frame types. `usb_cdc.c` is the USB transport; `protocol.c` is the message handling above it.

For telemetry, a module creates a named stream, registers its variables in sample order, and calls `stream_emit()` or `stream_emit_at()` from its task. The stream list reports the nominal rate and sample size. Each sample carries the stream ID, a firmware tick, and the variable bytes in their registered order. If the transmit buffer cannot take a whole sample, that sample is dropped and `stream_dropped` increases.

## PC side

Run the PC client from the repository root with `python3 -m pc.protocol`. `pc/protocol.py` is the entry point and `pc/cli.py` implements the commands and interactive console.

`pc/transport.py` opens the serial port, sends commands, decodes messages from the board, and matches replies to requests. It also keeps reading in the background so telemetry and replies can arrive together. `pc/metadata.py` stores the discovered variable and stream descriptions and lets commands use either a name or an ID. `pc/telemetry.py` decodes stream samples and provides the live plot.

## Message shape

Host commands are unframed bytes. A read includes a command and a 16-bit variable ID; a write includes a command, ID, byte count, and value. List requests are a single command byte. The IDs and numeric fields are little-endian.

Every message sent by the board is wrapped in a frame: a sync byte, message type, payload length, payload, and XOR checksum. The type distinguishes log text, command replies, and telemetry samples. List results arrive as one reply per entry, and the PC combines them using each entry's index and total count.
# Communications overview

The firmware in this directory and the Python tools in `pc/` share a small binary protocol over USB CDC. The board appears to the computer as a serial port, but this is USB data, not a physical UART; the configured baud rate is compatibility bookkeeping and does not set the USB link speed.

## The path through the system

1. Firmware modules register named variables in `vars.c`'s shared registry. Each descriptor includes an ID, format, size, access direction, and optional stream/offset. IDs are assigned at startup in registration order, not fixed in the PC code.
2. The PC opens the CDC serial port and asks for the variable and stream lists. Those replies give it the metadata needed to resolve names and decode later values.
3. Host commands arrive as unframed bytes on USB OUT. `usb_cdc.c` buffers them; `protocol.c` handles reads, writes, and discovery, and queues framed device responses.
4. Telemetry tasks copy registered variables into samples and queue them as stream frames. USB IN drains the transmit buffer; on the PC, one background reader reassembles frames and routes logs, replies, and samples while commands are running.
5. The PC uses the discovered metadata to decode values. `pc/telemetry.py` places samples on a time axis using the firmware tick, rather than the time they happened to arrive over USB.

The firmware descriptor is the source of truth for names, IDs, formats, and stream layout.

## Device-to-host frames

Every device-to-host message uses the same outer frame. The length is the payload length in bytes; the final checksum is the XOR of every preceding frame byte, including the sync byte.

```text
+------+-------+----------------+-------------------+----------+
| 0xA5 | type  | payload length | payload           | checksum|
+------+-------+----------------+-------------------+----------+
	1 B    1 B       1 B           length bytes          1 B
```

The one-byte length limits a payload to 255 bytes. The type tells the receiver how to interpret it:

| Type | Payload |
| --- | --- |
| `0x01` log | ASCII log text |
| `0x02` reply | command byte, status byte, then command-specific body |
| `0x03` stream | stream ID (1 byte), firmware tick (4-byte little-endian), then sample data |

The XOR is a lightweight corruption check, not a cryptographic integrity check. The PC decoder searches for the sync byte, waits until the declared frame is complete, checks the XOR, and skips forward to resynchronize after a bad frame. USB packet boundaries are not protocol frame boundaries.

## Commands and variable types

Host-to-device commands are not wrapped in frames:

| Command | Bytes sent |
| --- | --- |
| Read (`0x01`) | command, variable ID (`u16`, little-endian) |
| Write (`0x02`) | command, ID (`u16`, little-endian), value length (`u8`), value bytes |
| List variables (`0x03`) | command |
| List streams (`0x04`) | command |

Read replies include the ID and raw value bytes; write replies report status. List replies are sent one entry per reply frame. Each entry includes its index and total count, allowing the PC to assemble the list. Variable descriptors include the format code, byte size, stream ID (`0xFF` means not streamed), and offset within a stream sample.

The PC does not guess a value's type from its bytes. It maps the format code returned by `LIST_VARS` to the decoder, and uses the descriptor's size to take exactly the right bytes. Firmware format codes are:

| Code | Type | Size | Interpretation |
| --- | --- | --- | --- |
| `0` | `u8` | 1 B | unsigned integer |
| `1` | `i8` | 1 B | signed integer |
| `2` | `u16` | 2 B | unsigned, little-endian |
| `3` | `i16` | 2 B | signed, little-endian |
| `4` | `u32` | 4 B | unsigned, little-endian |
| `5` | `i32` | 4 B | signed, little-endian |
| `6` | `f16` | 4 B | signed Q16.16 fixed point, little-endian; Python divides the decoded integer by 65536 |
| `7` | `raw` | registered size | uninterpreted bytes |

Despite its name, `f16` is **not** a 16-bit float: it is a 32-bit fixed-point value. Firmware currently registers these scalar values by their format, and copies their bytes into reads and samples. The Python client also has an IEEE-754 `float` codec for explicitly requested PC-side values, but it is not one of the firmware registry's format codes.

## Telemetry layout

A stream is a named, ordered group of variables. Firmware registers the members in sample order and records each member's byte offset and size. The stream list reports the ID, nominal rate, variable count, and sample byte count; the rate is descriptive, while the actual samples are emitted by the task calling `stream_emit()` or `stream_emit_at()`.

```text
stream payload = [stream_id][tick: u32 LE][value 0][value 1]...
```

There are no names, IDs, or type tags repeated inside each sample. The PC matches the stream ID to discovered descriptors, slices each variable using its offset and size, then decodes it using its format. This keeps samples compact while allowing multiple streams and different emit rates. The tick is the firmware scheduler tick; `tick_hz` converts it to seconds. If a complete sample will not fit in the transmit queue, firmware drops the whole sample and increments `stream_dropped`.

## Code map and running the client

- `usb_config.h` describes the CDC endpoints; `usb_cdc.c` buffers USB bytes and runs the protocol bridge task.
- `vars.c` / `vars.h` implement the variable and stream registry; `protocol.c` / `protocol.h` implement commands, replies, logs, framing, and sample emission.
- `pc/transport.py` opens the serial port, reads and validates frames, and encodes/decodes values. `pc/metadata.py` stores discovered descriptors; `pc/telemetry.py` decodes and plots samples; `pc/cli.py` provides commands and the interactive console.

From the repository root, run `python3 -m pc.protocol`. The client needs `pyserial`; live plotting also needs `matplotlib`. Without plotting, listing, reading, writing, monitoring, and telemetry metadata remain available.
