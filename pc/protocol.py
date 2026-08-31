#!/usr/bin/env python3

import argparse
import glob
import struct
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
LOG_PREFIX = b"[LOG]"

DIR_TX = 0x01
DIR_RX = 0x02
DIR_TX_RX = 0x03

_SERIAL_LOG_TAIL = b""

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

FMT_ALIASES = {
    "fix16": "f16",
    "q16": "f16",
    "q16.16": "f16",
    "float": "float",
    "f32": "float",
    "fp32": "float",
    "raw": "raw",
    "bytes": "raw",
    "u8": "u8",
    "i8": "i8",
    "u16": "u16",
    "i16": "i16",
    "u32": "u32",
    "i32": "i32",
    "f16": "f16",
}

PARAMETER_INFO = {
    0x0001: {"name": "temp_raw", "format": "u16"},
    0x0002: {"name": "target_current", "format": "u16"},
    0x0003: {"name": "enable", "format": "u8"},
    0x0004: {"name": "duty_a", "format": "f16"},
    0x0005: {"name": "duty_b", "format": "f16"},
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


def _normalize_format_name(fmt: int | str) -> str:
    if isinstance(fmt, int):
        return FMT_ALIASES.get(FMT_NAMES.get(fmt, f"fmt{fmt}"), FMT_NAMES.get(fmt, f"fmt{fmt}"))
    name = str(fmt).strip().lower()
    return FMT_ALIASES.get(name, name)


def fix16_to_float(value: int) -> float:
    return value / 65536.0


def float_to_fix16(value: float) -> int:
    return int(round(float(value) * 65536.0))


def decode_value(raw: bytes, fmt: int | str) -> int | float | bytes:
    format_name = _normalize_format_name(fmt)
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
    format_name = _normalize_format_name(fmt)
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
    format_name = _normalize_format_name(fmt)
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
    format_name = _normalize_format_name(fmt)
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
            count = payload[0]
            if 0 <= count <= 32:
                list_total = 1 + 1 + count * 5
                if len(payload) >= 1 + count * 5:
                    candidate = raw[start:start + list_total]
                    if len(candidate) > best_len:
                        best_match = candidate
                        best_len = len(candidate)

        if len(payload) >= 3:
            length = payload[2]
            if length <= 32:
                read_total = 1 + 2 + 1 + length
                if len(payload) >= 3 + length:
                    candidate = raw[start:start + read_total]
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
        if pos + 5 > len(payload):
            raise ValueError(f"Truncated list response: {resp!r}")
        obj_id = unpack_u16(payload[pos:pos + 2])
        pos += 2
        direction = payload[pos]
        fmt = payload[pos + 1]
        size = payload[pos + 2]
        pos += 3
        name = PARAMETER_INFO.get(obj_id, {}).get("name", f"param_{obj_id:04x}")
        items.append(
            {
                "id": obj_id,
                "name": name,
                "direction": direction,
                "format": fmt,
                "size": size,
                "fmt_name": _normalize_format_name(fmt),
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

    info = PARAMETER_INFO.get(reply_id, {"name": f"param_{reply_id:04x}", "format": "raw"})
    fmt_name = info.get("format", "raw")
    value = decode_value(raw, fmt_name)
    return {
        "id": reply_id,
        "length": length,
        "raw": raw,
        "name": info["name"],
        "format": fmt_name,
        "fmt_name": _normalize_format_name(fmt_name),
        "value": value,
        "formatted": format_value(value, fmt_name),
    }


def write_param(port: serial.Serial, obj_id: int, value: int | float | bytes, fmt: int | str | None = None) -> int:
    format_name = fmt if fmt is not None else PARAMETER_INFO.get(obj_id, {}).get("format", "u16")
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


def print_param_list(items: Iterable[dict]):
    print("ID      NAME         DIR  FMT  SIZE")
    for item in items:
        dir_name = {
            DIR_TX: "TX",
            DIR_RX: "RX",
            DIR_TX_RX: "TX/RX",
        }.get(item["direction"], f"0x{item['direction']:02x}")
        name = item.get("name") or f"0x{item['id']:04x}"
        print(f"0x{item['id']:04x}  {name:<12} {dir_name:<5} {item['fmt_name']:<4} {item['size']}")


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
                        print(f"0x{item['id']:04x}: {value['name']} = {value['formatted']}")
                    except Exception as exc:  # pragma: no cover
                        print(f"0x{item['id']:04x}: error: {exc}")
        else:
            try:
                value = read_param(port, obj_id)
                print(f"0x{obj_id:04x}: {value['name']} = {value['formatted']}")
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
    write_parser.add_argument("value", help="Value as int/float/hex: 1234, 0x1234, 0.75")
    write_parser.set_defaults(func=lambda args, port: _run_write(port, args.id, args.value))

    monitor_parser = subparsers.add_parser("monitor", help="Repeat-read TX parameters")
    monitor_parser.add_argument("--id", type=str, help="Optional single parameter ID to monitor")
    monitor_parser.add_argument("--interval", type=float, default=0.5, help="Polling interval in seconds")
    monitor_parser.add_argument("--count", type=int, default=None, help="Stop after N polls")
    monitor_parser.set_defaults(func=lambda args, port: monitor(port, parse_id(args.id) if args.id else None, args.interval, args.count))

    repl_parser = subparsers.add_parser("repl", help="Interactive command loop")
    repl_parser.set_defaults(func=lambda args, port: run_repl(port))

    return parser


def _resolve_parameter_name(name: str) -> int:
    text = name.strip().lower()
    if not text:
        raise ValueError("parameter name is required")

    matches = []
    for obj_id, info in PARAMETER_INFO.items():
        candidate = str(info.get("name", "")).strip().lower()
        if candidate == text:
            return obj_id
        if candidate.startswith(text):
            matches.append(obj_id)

    if matches:
        matches_str = ", ".join(f"0x{obj_id:04x}" for obj_id in matches)
        raise ValueError(f"Ambiguous parameter name '{name}'; matches: {matches_str}")

    known = ", ".join(sorted(str(info["name"]) for info in PARAMETER_INFO.values()))
    raise ValueError(f"Unknown parameter '{name}'. Use an ID or one of: {known}")


def parse_id(raw: str | None) -> int:
    if raw is None:
        raise ValueError("id is required")
    text = raw.strip()
    if not text:
        raise ValueError("id is required")
    if text.lower().startswith("0x"):
        return int(text, 16)
    try:
        return int(text, 0)
    except ValueError:
        return _resolve_parameter_name(text)


def _run_read(port: serial.Serial, raw_id: str):
    obj_id = parse_id(raw_id)
    value = read_param(port, obj_id)
    print(f"id=0x{value['id']:04x} name={value['name']} fmt={value['fmt_name']} value={value['formatted']} raw={value['raw'].hex()}")


def _run_write(port: serial.Serial, raw_id: str, raw_value: str):
    obj_id = parse_id(raw_id)
    fmt_name = PARAMETER_INFO.get(obj_id, {}).get("format", "u16")
    value = parse_scalar_raw(raw_value, fmt_name)
    status = write_param(port, obj_id, value, fmt=fmt_name)
    print(f"write id=0x{obj_id:04x} name={PARAMETER_INFO.get(obj_id, {}).get('name', 'unknown')} value={raw_value} status={status}")


def _handle_repl_command(port: serial.Serial, line: str):
    tokens = line.strip().split()
    if not tokens or tokens[0] in {"", "help", "h"}:
        print("Commands: list (l), read (r) <id>, write (w) <id> <value>, monitor (m) [id] [interval], quit (q)")
        return True
    try:
        cmd = tokens[0].lower()
        if cmd in {"list", "l"}:
            print_param_list(list_params(port))
            return True
        if cmd in {"read", "r"}:
            if len(tokens) != 2:
                print("Usage: read <id>")
                return True
            _run_read(port, tokens[1])
            return True
        if cmd in {"write", "w"}:
            if len(tokens) != 3:
                print("Usage: write <id> <value>")
                return True
            _run_write(port, tokens[1], tokens[2])
            return True
        if cmd in {"monitor", "m"}:
            obj_id = parse_id(tokens[1]) if len(tokens) > 1 else None
            interval = float(tokens[2]) if len(tokens) > 2 else 0.5
            monitor(port, obj_id, interval, count=1)
            return True
        if cmd in {"quit", "q", "exit"}:
            return False
        print(f"Unknown command: {tokens[0]}")
        return True
    except (TimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}")
        return True
    except serial.SerialException as exc:
        print(f"ERROR: device disconnected: {exc}")
        return False


def run_repl(port: serial.Serial):
    print("MagneticLevitation console")
    print("Type 'help' for commands or 'quit' to exit.")
    while True:
        try:
            line = input("ml> ")
        except EOFError:
            print()
            break
        try:
            if not _handle_repl_command(port, line):
                break
        except serial.SerialException as exc:
            print(f"ERROR: device disconnected: {exc}")
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
        raise SystemExit(f"ERROR: device disconnected: {exc}") from exc
    except (TimeoutError, ValueError) as exc:
        raise SystemExit(f"ERROR: {exc}") from exc


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
