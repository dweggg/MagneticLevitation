from __future__ import annotations

import glob
import os
import struct
import time
from collections import deque, namedtuple
from typing import Callable

from pc.metadata import normalize_format_name

try:
    import serial
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "pyserial is required. Install it with: python3 -m pip install pyserial"
    ) from exc

CMD_READ = 0x01
CMD_WRITE = 0x02
CMD_LIST_VARS = 0x03
CMD_LIST_STREAMS = 0x04
STATUS_OK = 0x00

FRAME_SYNC = 0xA5
FRAME_LOG = 0x01
FRAME_REPLY = 0x02
FRAME_STREAM = 0x03
STREAM_NONE = 0xFF

DIR_TX = 0x01
DIR_RX = 0x02
DIR_TX_RX = 0x03

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


RAW_LOG_FILENAME = "pc/dump.log"


class PortLogger:
    def __init__(self, port: serial.Serial, log_path: str | None = None):
        self._port = port
        self._log_path = log_path or os.path.join(os.getcwd(), RAW_LOG_FILENAME)
        self._log_file = open(self._log_path, "wb")

    def write(self, data: bytes) -> int:
        payload = bytes(data)
        self._log_file.write(payload)
        self._log_file.flush()
        return self._port.write(payload)

    def read(self, size: int = -1, *args, **kwargs):
        payload = self._port.read(size, *args, **kwargs)
        if payload:
            self._log_file.write(payload)
            self._log_file.flush()
        return payload

    def close(self):
        try:
            self._log_file.close()
        finally:
            self._port.close()

    def __getattr__(self, name):
        return getattr(self._port, name)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc, tb):
        self.close()
        return False


def open_port(port: str, baud: int = 115200, timeout: float = 0.25, log_path: str | None = None) -> "Link":
    return Link(PortLogger(serial.Serial(port, baud, timeout=timeout), log_path=log_path))


def fix16_to_float(value: int) -> float:
    return value / 65536.0


def float_to_fix16(value: float) -> int:
    return int(round(float(value) * 65536.0))


def decode_value(raw: bytes, fmt: int | str) -> int | float | bytes:
    format_name = normalize_format_name(fmt)
    if format_name == "raw":
        return bytes(raw)
    if format_name == "u8":
        return raw[0]
    if format_name == "i8":
        return int.from_bytes(raw, byteorder="little", signed=True)
    if format_name == "u16":
        return int.from_bytes(raw, byteorder="little", signed=False)
    if format_name == "i16":
        return int.from_bytes(raw, byteorder="little", signed=True)
    if format_name == "u32":
        return int.from_bytes(raw, byteorder="little", signed=False)
    if format_name == "i32":
        return int.from_bytes(raw, byteorder="little", signed=True)
    if format_name == "f16":
        if len(raw) != 4:
            raise ValueError(f"Fix16 requires 4 bytes, got {len(raw)}")
        return fix16_to_float(int.from_bytes(raw, byteorder="little", signed=True))
    if format_name == "float":
        if len(raw) != 4:
            raise ValueError(f"float requires 4 bytes, got {len(raw)}")
        return struct.unpack("<f", raw)[0]
    raise ValueError(f"Unsupported format: {fmt!r}")


def encode_value(value: int | float | bytes, fmt: int | str) -> bytes:
    format_name = normalize_format_name(fmt)
    if isinstance(value, (bytes, bytearray)):
        return bytes(value)
    if format_name == "raw":
        return bytes(value)
    if format_name == "u8":
        return bytes([int(value) & 0xFF])
    if format_name == "i8":
        return int(value).to_bytes(1, byteorder="little", signed=True)
    if format_name == "u16":
        return int(value).to_bytes(2, byteorder="little", signed=False)
    if format_name == "i16":
        return int(value).to_bytes(2, byteorder="little", signed=True)
    if format_name == "u32":
        return int(value).to_bytes(4, byteorder="little", signed=False)
    if format_name == "i32":
        return int(value).to_bytes(4, byteorder="little", signed=True)
    if format_name == "f16":
        return float_to_fix16(float(value)).to_bytes(4, byteorder="little", signed=True)
    if format_name == "float":
        return struct.pack("<f", float(value))
    raise ValueError(f"Unsupported format: {fmt!r}")


def parse_scalar_raw(raw: str, fmt: int | str) -> int | float | bytes:
    text = raw.strip()
    format_name = normalize_format_name(fmt)
    if format_name == "raw":
        if text.lower().startswith("0x"):
            return bytes.fromhex(text[2:])
        return bytes.fromhex(text)
    if format_name in {"f16", "float"}:
        if text.lower() in {"nan", "inf", "+inf", "-inf"}:
            return float(text)
        if text.lower().startswith("-0x") or text.lower().startswith("0x"):
            return float(int(text, 16))
        return float(text)
    if text.lower().startswith(("-0x", "0x")):
        return int(text, 0)
    return int(text, 0)


def format_value(value: int | float | bytes, fmt: int | str) -> str:
    format_name = normalize_format_name(fmt)
    if format_name == "raw":
        if isinstance(value, (bytes, bytearray)):
            return value.hex()
        return str(value)
    if format_name in {"u8", "u16", "u32", "i8", "i16", "i32"}:
        return str(int(value))
    if format_name == "f16":
        return f"{float(value):.6g}"
    if format_name == "float":
        return f"{float(value):.6g}"
    return str(value)


StreamSample = namedtuple("StreamSample", "stream tick data")


class FrameDecoder:
    """Resynchronising decoder for [A5][type][len][payload][xor] frames."""

    def __init__(self) -> None:
        self._buf = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, bytes]]:
        self._buf.extend(data)
        frames: list[tuple[int, bytes]] = []
        buf = self._buf
        while True:
            start = buf.find(bytes([FRAME_SYNC]))
            if start < 0:
                buf.clear()
                break
            del buf[:start]
            if len(buf) < 3:
                break
            total = buf[2] + 4
            if len(buf) < total:
                break
            check = 0
            for byte in buf[:total - 1]:
                check ^= byte
            if check != buf[total - 1]:
                del buf[0]
                continue
            frames.append((buf[1], bytes(buf[3:total - 1])))
            del buf[:total]
        return frames


class Link:
    """Framed connection to the device.

    Log frames are printed, stream frames go to ``stream_sink`` (a callable
    taking a StreamSample) and replies are queued for ``request``. Because one
    decoder owns the byte stream, telemetry keeps flowing while commands run.
    """

    def __init__(self, port) -> None:
        self.port = port
        self.decoder = FrameDecoder()
        self.stream_sink: Callable[[StreamSample], None] | None = None
        self._replies: deque[bytes] = deque()

    def close(self) -> None:
        self.port.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()
        return False

    def pump(self) -> None:
        """Read whatever is waiting and dispatch every complete frame."""
        waiting = self.port.in_waiting
        if not waiting:
            return
        for kind, payload in self.decoder.feed(self.port.read(waiting)):
            if kind == FRAME_LOG:
                print("[LOG] " + payload.decode("ascii", errors="replace"), flush=True)
            elif kind == FRAME_STREAM and len(payload) >= 5 and self.stream_sink is not None:
                self.stream_sink(StreamSample(payload[0], struct.unpack_from("<I", payload, 1)[0], payload[5:]))
            elif kind == FRAME_REPLY and payload:
                self._replies.append(payload)

    def _send(self, payload: bytes) -> None:
        self._replies.clear()
        self.port.write(payload)
        self.port.flush()

    def _wait(self, accept, timeout: float) -> bytes:
        end = time.monotonic() + timeout
        while time.monotonic() < end:
            self.pump()
            for reply in list(self._replies):
                if accept(reply):
                    self._replies.remove(reply)
                    return reply
            time.sleep(0.001)
        raise TimeoutError("Timed out waiting for reply")

    def request(self, payload: bytes, match=None, timeout: float = 1.0) -> bytes:
        self._send(payload)
        return self._wait(lambda r: r[0] == payload[0] and (match is None or match(r)), timeout)

    def request_list(self, cmd: int, timeout: float = 1.0) -> list[bytes]:
        """Collect one reply frame per entry: [cmd][status][index][total]..."""
        self._send(bytes([cmd]))
        entries: dict[int, bytes] = {}
        total = None
        while total is None or len(entries) < total:
            reply = self._wait(lambda r: r[0] == cmd and len(r) >= 4, timeout)
            entries[reply[2]] = reply
            total = reply[3]
        return [entries[i] for i in sorted(entries)]


def list_params(link: Link) -> list[dict]:
    """Discover every registered variable (parameters, monitors, streamed)."""
    items = []
    for r in link.request_list(CMD_LIST_VARS):
        name_len = r[11]
        fmt = normalize_format_name(r[7])
        items.append({
            "id": unpack_u16(r[4:6]), "direction": r[6], "format": fmt, "fmt_name": fmt,
            "size": r[8], "stream": r[9], "offset": r[10],
            "name": r[12:12 + name_len].decode("ascii", errors="replace"),
        })
    return items


def list_streams(link: Link) -> list[dict]:
    """Discover the telemetry streams: id, nominal rate, variable count, bytes."""
    items = []
    for r in link.request_list(CMD_LIST_STREAMS):
        name_len = r[11]
        items.append({
            "id": r[4], "rate_hz": struct.unpack_from("<I", r, 5)[0], "count": r[9], "bytes": r[10],
            "name": r[12:12 + name_len].decode("ascii", errors="replace"),
        })
    return items


def read_param(link: Link, obj_id: int, catalog=None) -> dict:
    resp = link.request(bytes([CMD_READ]) + pack_u16(obj_id), match=lambda r: len(r) < 4 or unpack_u16(r[2:4]) == obj_id)
    if resp[1] != STATUS_OK or len(resp) < 4:
        raise ValueError(f"Read for id 0x{obj_id:04x} failed with status=0x{resp[1]:02x}")
    raw = resp[4:]
    info = catalog.get(obj_id) if catalog is not None else {}
    info = {**{"name": f"param_{obj_id:04x}", "format": "raw", "fmt_name": "raw"}, **info}
    fmt_name = info.get("format", info.get("fmt_name", "raw"))
    value = decode_value(raw, fmt_name)
    return {
        "id": obj_id,
        "length": len(raw),
        "raw": raw,
        "name": info["name"],
        "format": fmt_name,
        "fmt_name": normalize_format_name(fmt_name),
        "value": value,
        "formatted": format_value(value, fmt_name),
    }


def write_param(link: Link, obj_id: int, value: int | float | bytes, fmt: int | str | None = None, catalog=None) -> int:
    info = catalog.get(obj_id) if catalog is not None else {}
    format_name = fmt if fmt is not None else info.get("format") or info.get("fmt_name") or "u16"
    if isinstance(value, str):
        value = parse_scalar_raw(value, format_name)
    value_bytes = encode_value(value, format_name)
    resp = link.request(bytes([CMD_WRITE]) + pack_u16(obj_id) + bytes([len(value_bytes)]) + value_bytes)
    return resp[1]
