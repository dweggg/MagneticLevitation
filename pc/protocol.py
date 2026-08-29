#!/usr/bin/env python3

import argparse
import glob
import sys
import time
from typing import Iterable

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "pyserial is required. Install it with: python3 -m pip install pyserial"
    ) from exc

CMD_READ = 0x01
CMD_WRITE = 0x02
CMD_LIST = 0x03
CMD_REPLY = 0x80

DIR_TX = 0x01
DIR_RX = 0x02
DIR_TX_RX = 0x03

FMT_NAMES = {
    0: "u8",
    1: "i8",
    2: "u16",
    3: "i16",
    4: "u32",
    5: "i32",
    6: "f16",
    7: "raw",
}


def find_port(preferred: str | None = None) -> str:
    if preferred:
        return preferred

    candidates = [
        *glob.glob("/dev/ttyACM*"),
        *glob.glob("/dev/ttyUSB*"),
        *glob.glob("/dev/cu.usbmodem*"),
    ]
    if not candidates:
        raise FileNotFoundError("No serial port found. Plug in the board or pass --port.")
    candidates.sort()
    return candidates[0]


def pack_u16(value: int) -> bytes:
    return bytes([value & 0xFF, (value >> 8) & 0xFF])


def unpack_u16(buf: bytes) -> int:
    return buf[0] | (buf[1] << 8)


def open_port(port: str, baud: int = 115200, timeout: float = 0.25):
    return serial.Serial(port, baud, timeout=timeout)


def read_exact(port: serial.Serial, length: int, deadline: float = 1.0) -> bytes:
    end = time.monotonic() + deadline
    chunks = []
    total = 0
    while total < length:
        if time.monotonic() >= end:
            raise TimeoutError(f"Timed out waiting for {length} bytes")
        chunk = port.read(length - total)
        if not chunk:
            time.sleep(0.01)
            continue
        chunks.append(chunk)
        total += len(chunk)
    return b"".join(chunks)


def request(port: serial.Serial, payload: bytes, expect_reply: bool = True) -> bytes:
    port.write(payload)
    port.flush()
    if not expect_reply:
        return b""

    response = bytearray()
    start = time.monotonic()
    while time.monotonic() - start < 1.0:
        chunk = port.read(64)
        if chunk:
            response.extend(chunk)
            if response and response[0] != CMD_REPLY:
                raise ValueError(f"Unexpected response prefix: 0x{response[0]:02x}")

            # The protocol is variable-length; wait a brief idle window so we don't
            # return a truncated packet as soon as the first 0x80 byte arrives.
            idle_wait = 0.002
            idle_deadline = time.monotonic() + idle_wait
            while time.monotonic() < idle_deadline:
                more = port.read(64)
                if more:
                    response.extend(more)
                    idle_deadline = time.monotonic() + idle_wait
                else:
                    time.sleep(0.01)
            return bytes(response)
        time.sleep(0.01)

    if len(response) < 1:
        raise TimeoutError("Timed out waiting for reply")
    return bytes(response)


def list_params(port: serial.Serial) -> list[dict]:
    resp = request(port, bytes([CMD_LIST]))
    if resp[0] != CMD_REPLY:
        raise ValueError(f"Unexpected response prefix: 0x{resp[0]:02x}")

    payload = resp[1:]
    if len(payload) < 1:
        return []

    count = payload[0]
    pos = 1
    items = []
    for _ in range(count):
        if pos + 5 > len(payload):
            raise ValueError(f"Truncated list response: {resp!r}")
        obj_id = unpack_u16(payload[pos:pos + 2])
        pos += 2
        direction = payload[pos]
        fmt = payload[pos + 1]
        size = payload[pos + 2]
        pos += 3
        items.append(
            {
                "id": obj_id,
                "name": None,
                "direction": direction,
                "format": fmt,
                "size": size,
                "fmt_name": FMT_NAMES.get(fmt, f"fmt{fmt}"),
            }
        )
    return items


def read_param(port: serial.Serial, obj_id: int) -> dict:
    frame = bytes([CMD_READ]) + pack_u16(obj_id)
    resp = request(port, frame)
    if resp[0] != CMD_REPLY:
        raise ValueError(f"Unexpected response prefix: 0x{resp[0]:02x}")

    payload = resp[1:]
    if len(payload) == 1:
        if payload[0] == 0x00:
            raise ValueError(f"Read for id 0x{obj_id:04x} unexpectedly returned success status with no payload")
        raise ValueError(f"Read for id 0x{obj_id:04x} failed with status=0x{payload[0]:02x}")
    if len(payload) < 3:
        raise ValueError(f"Malformed read response: {resp!r}")

    reply_id = unpack_u16(payload[0:2])
    length = payload[2]
    raw = payload[3:3 + length]
    if len(raw) != length:
        raise ValueError(f"Read payload truncated: expected {length}, got {len(raw)}")

    return {"id": reply_id, "length": length, "raw": raw}


def write_param(port: serial.Serial, obj_id: int, value: int | bytes) -> int:
    if isinstance(value, int):
        value_bytes = value.to_bytes(2, byteorder="little", signed=False)
    elif isinstance(value, (bytes, bytearray)):
        value_bytes = bytes(value)
    else:
        raise TypeError("value must be int or bytes")

    frame = bytes([CMD_WRITE]) + pack_u16(obj_id) + bytes([len(value_bytes)]) + value_bytes
    resp = request(port, frame)
    if resp[0] != CMD_REPLY:
        raise ValueError(f"Unexpected response prefix: 0x{resp[0]:02x}")
    if len(resp) < 2:
        raise ValueError(f"Malformed write response: {resp!r}")
    status = resp[1]
    if status not in (0x00, 0x01):
        raise ValueError(f"Unknown write status: 0x{status:02x}")
    return status


def print_param_list(items: Iterable[dict]):
    print("ID      DIR  FMT  SIZE")
    for item in items:
        dir_name = {
            DIR_TX: "TX",
            DIR_RX: "RX",
            DIR_TX_RX: "TX/RX",
        }.get(item["direction"], f"0x{item['direction']:02x}")
        print(f"0x{item['id']:04x}  {dir_name:<5} {item['fmt_name']:<4} {item['size']}")


def monitor(port: serial.Serial, obj_id: int | None, interval: float = 0.5, count: int | None = None):
    reads = 0
    while True:
        if count is not None and reads >= count:
            break
        if obj_id is None:
            items = list_params(port)
            if not items:
                print("No parameters")
                return
            for item in items:
                if item["direction"] & DIR_TX:
                    try:
                        value = read_param(port, item["id"])
                        print(f"0x{item['id']:04x}: {value['raw'].hex()}")
                    except Exception as exc:  # pragma: no cover
                        print(f"0x{item['id']:04x}: error: {exc}")
        else:
            try:
                value = read_param(port, obj_id)
                print(f"0x{obj_id:04x}: {value['raw'].hex()} ")
            except Exception as exc:
                print(f"0x{obj_id:04x}: error: {exc}")

        reads += 1
        if count is not None and reads >= count:
            break
        time.sleep(interval)


def build_parser():
    parser = argparse.ArgumentParser(description="Terminal client for the Magnetic Levitation USB parameter bridge")
    parser.add_argument("--port", help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")

    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List available parameters")
    list_parser.set_defaults(func=lambda args, port: print_param_list(list_params(port)))

    read_parser = subparsers.add_parser("read", help="Read a parameter by ID")
    read_parser.add_argument("id", help="Parameter ID, e.g. 0x0001 or 1")
    read_parser.set_defaults(func=lambda args, port: _run_read(port, args.id))

    write_parser = subparsers.add_parser("write", help="Write a parameter by ID")
    write_parser.add_argument("id", help="Parameter ID, e.g. 0x0002 or 2")
    write_parser.add_argument("value", help="Value as int or hex bytes, e.g. 1234 or 0x1234")
    write_parser.set_defaults(func=lambda args, port: _run_write(port, args.id, args.value))

    monitor_parser = subparsers.add_parser("monitor", help="Repeat-read TX parameters")
    monitor_parser.add_argument("--id", type=str, help="Optional single parameter ID to monitor")
    monitor_parser.add_argument("--interval", type=float, default=0.5, help="Polling interval in seconds")
    monitor_parser.add_argument("--count", type=int, default=None, help="Stop after N polls")
    monitor_parser.set_defaults(func=lambda args, port: monitor(port, parse_id(args.id) if args.id else None, args.interval, args.count))

    repl_parser = subparsers.add_parser("repl", help="Interactive command loop")
    repl_parser.set_defaults(func=lambda args, port: run_repl(port))

    return parser


def parse_id(raw: str | None) -> int:
    if raw is None:
        raise ValueError("id is required")
    text = raw.strip()
    if text.lower().startswith("0x"):
        return int(text, 16)
    return int(text, 0)


def _run_read(port: serial.Serial, raw_id: str):
    obj_id = parse_id(raw_id)
    value = read_param(port, obj_id)
    print(f"id=0x{value['id']:04x} len={value['length']} data={value['raw'].hex()}")


def _run_write(port: serial.Serial, raw_id: str, raw_value: str):
    obj_id = parse_id(raw_id)
    text = raw_value.strip()
    if text.lower().startswith("0x") and len(text) > 2:
        value = int(text, 16)
    else:
        try:
            value = int(text, 0)
        except ValueError as exc:
            raise SystemExit(f"Invalid value: {raw_value!r}") from exc

    status = write_param(port, obj_id, value)
    print(f"write id=0x{obj_id:04x} status={status}")


def _handle_repl_command(port: serial.Serial, line: str):
    tokens = line.strip().split()
    if not tokens or tokens[0] in {"", "help", "h"}:
        print("Commands: list, read <id>, write <id> <value>, monitor [id] [interval], quit")
        return True
    try:
        if tokens[0] == "list":
            print_param_list(list_params(port))
            return True
        if tokens[0] == "read":
            if len(tokens) != 2:
                print("Usage: read <id>")
                return True
            _run_read(port, tokens[1])
            return True
        if tokens[0] == "write":
            if len(tokens) != 3:
                print("Usage: write <id> <value>")
                return True
            _run_write(port, tokens[1], tokens[2])
            return True
        if tokens[0] == "monitor":
            obj_id = parse_id(tokens[1]) if len(tokens) > 1 else None
            interval = float(tokens[2]) if len(tokens) > 2 else 0.5
            monitor(port, obj_id, interval, count=1)
            return True
        if tokens[0] in {"quit", "exit"}:
            return False
        print(f"Unknown command: {tokens[0]}")
        return True
    except (TimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}")
        return True


def run_repl(port: serial.Serial):
    print("MagneticLevitation console")
    print("Type 'help' for commands or 'quit' to exit.")
    while True:
        try:
            line = input("ml> ")
        except EOFError:
            print()
            break
        if not _handle_repl_command(port, line):
            break


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    port_name = find_port(args.port)
    try:
        with open_port(port_name, baud=args.baud) as port:
            time.sleep(0.5)
            return args.func(args, port) or 0
    except FileNotFoundError as exc:
        raise SystemExit(str(exc)) from exc
    except serial.SerialException as exc:
        raise SystemExit(f"Failed to open serial port {port_name}: {exc}") from exc
    except (TimeoutError, ValueError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc


if __name__ == "__main__":
    sys.exit(main())
