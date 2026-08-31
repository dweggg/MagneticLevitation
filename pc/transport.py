from __future__ import annotations

import glob
import os
import struct
import time

from pc.metadata import normalize_format_name

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
LOG_PREFIX = b"[LOG]"

DIR_TX = 0x01
DIR_RX = 0x02
DIR_TX_RX = 0x03

_SERIAL_LOG_TAIL = b""


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


def open_port(port: str, baud: int = 115200, timeout: float = 0.25, log_path: str | None = None):
    serial_port = serial.Serial(port, baud, timeout=timeout)
    return PortLogger(serial_port, log_path=log_path)


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


def drain_input_buffer(port: serial.Serial, timeout: float = 0.05) -> None:
    end = time.monotonic() + timeout
    while True:
        try:
            waiting = port.in_waiting
        except AttributeError:
            waiting = 0
        if waiting <= 0:
            if time.monotonic() >= end:
                break
            time.sleep(0.005)
            continue
        try:
            port.read(waiting)
        except Exception:
            break
        if time.monotonic() >= end:
            break
    try:
        port.reset_input_buffer()
    except Exception:
        pass


def _parse_list_payload(payload: bytes) -> int | None:
    if len(payload) < 1:
        return None

    count = payload[0]
    if not 0 <= count <= 32:
        return None

    pos = 1
    for _ in range(count):
        if pos + 6 > len(payload):
            return None

        obj_id = unpack_u16(payload[pos:pos + 2])
        pos += 2
        direction = payload[pos]
        pos += 1
        fmt = payload[pos]
        pos += 1
        size = payload[pos]
        pos += 1
        name_len = payload[pos]
        pos += 1

        if pos + name_len > len(payload):
            return None
        name_bytes = payload[pos:pos + name_len]
        if len(name_bytes) != name_len:
            return None
        if not all(32 <= byte < 127 or byte in (9, 10, 13) for byte in name_bytes):
            return None
        pos += name_len

    return pos


def extract_reply_frame(data: bytes) -> bytes | None:
    raw = bytes(data)
    best_match = None
    best_len = -1
    for start in range(len(raw)):
        if raw[start] != CMD_REPLY:
            continue

        payload = raw[start + 1:]

        if len(payload) == 1 and payload[0] in (0x00, 0x01):
            candidate = raw[start:start + 2]
            if len(candidate) > best_len:
                best_match = candidate
                best_len = len(candidate)
            continue

        if len(payload) >= 1:
            list_end = _parse_list_payload(payload)
            if list_end is not None and list_end == len(payload):
                candidate = raw[start:start + 1 + list_end]
                if len(candidate) > best_len:
                    best_match = candidate
                    best_len = len(candidate)
                continue

        if len(payload) >= 3:
            length = payload[2]
            if length <= 32:
                read_total = 2 + 1 + length
                if len(payload) == read_total:
                    candidate = raw[start:start + 1 + read_total]
                    if len(candidate) > best_len:
                        best_match = candidate
                        best_len = len(candidate)

    return best_match


def extract_log_messages(raw: bytes, tail: bytes = b"") -> tuple[list[str], bytes]:
    pending = tail + raw
    lines: list[str] = []

    while True:
        start = pending.find(LOG_PREFIX)
        if start < 0:
            return lines, pending[-256:] if len(pending) > 256 else pending

        end = pending.find(b"\r", start)
        if end < 0:
            end = pending.find(b"\n", start)
        if end < 0:
            return lines, pending[start:]

        line = pending[start:end].decode("utf-8", errors="replace")
        lines.append(line)
        pending = pending[end + 1:]
        if pending.startswith(b"\n"):
            pending = pending[1:]


_SERIAL_LOG_TAIL = b""


def _print_received_logs(raw: bytes) -> None:
    global _SERIAL_LOG_TAIL
    lines, _SERIAL_LOG_TAIL = extract_log_messages(raw, _SERIAL_LOG_TAIL)
    for line in lines:
        print(line, flush=True)


def request(port: serial.Serial, payload: bytes, expect_reply: bool = True) -> bytes:
    drain_input_buffer(port, timeout=0.05)
    port.write(payload)
    port.flush()
    if not expect_reply:
        return b""

    response = bytearray()
    start = time.monotonic()
    while time.monotonic() - start < 1.0:
        try:
            if hasattr(port, "in_waiting") and port.in_waiting > 0:
                chunk = port.read(port.in_waiting)
            else:
                chunk = port.read(64)
        except serial.SerialException:
            chunk = b""

        if chunk:
            _print_received_logs(chunk)
            response.extend(chunk)
            frame = extract_reply_frame(bytes(response))
            if frame is not None:
                drain_input_buffer(port, timeout=0.05)
                return frame
        else:
            time.sleep(0.01)

    if not response:
        raise TimeoutError("Timed out waiting for reply")
    frame = extract_reply_frame(bytes(response))
    if frame is not None:
        drain_input_buffer(port, timeout=0.05)
        return frame
    raise ValueError("No valid reply packet received from device")


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
        if pos + 6 > len(payload):
            raise ValueError(f"Truncated list response: {resp!r}")
        obj_id = unpack_u16(payload[pos:pos + 2])
        pos += 2
        direction = payload[pos]
        pos += 1
        fmt = payload[pos]
        pos += 1
        size = payload[pos]
        pos += 1
        name_len = payload[pos]
        pos += 1

        if pos + name_len > len(payload):
            raise ValueError(f"Truncated name field in list response: {resp!r}")
        name = payload[pos:pos + name_len].decode("ascii", errors="replace")
        pos += name_len

        items.append(
            {
                "id": obj_id,
                "name": name,
                "direction": direction,
                "format": normalize_format_name(fmt),
                "size": size,
                "fmt_name": normalize_format_name(fmt),
            }
        )
    return items


def read_param(port: serial.Serial, obj_id: int, catalog=None) -> dict:
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

    info = {}
    if catalog is not None:
        info = catalog.get(reply_id)
    info = {**{"name": f"param_{reply_id:04x}", "format": "raw", "fmt_name": "raw"}, **info}
    fmt_name = info.get("format", info.get("fmt_name", "raw"))
    value = decode_value(raw, fmt_name)
    return {
        "id": reply_id,
        "length": length,
        "raw": raw,
        "name": info["name"],
        "format": fmt_name,
        "fmt_name": normalize_format_name(fmt_name),
        "value": value,
        "formatted": format_value(value, fmt_name),
    }


def write_param(port: serial.Serial, obj_id: int, value: int | float | bytes, fmt: int | str | None = None, catalog=None) -> int:
    info = catalog.get(obj_id) if catalog is not None else {}
    format_name = fmt if fmt is not None else info.get("format") or info.get("fmt_name") or "u16"
    if isinstance(value, str):
        value = parse_scalar_raw(value, format_name)
    if format_name is None:
        raise TypeError("format is required when writing a raw byte sequence")
    value_bytes = encode_value(value, format_name)

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
