from __future__ import annotations

import argparse
import sys
import time
from typing import Iterable

try:
    import readline
except ImportError:  # pragma: no cover
    readline = None

from pc.metadata import ParameterCatalog
from pc.transport import (
    CMD_READ,
    CMD_REPLY,
    CMD_WRITE,
    DIR_TX,
    DIR_TX_RX,
    DIR_RX,
    find_port,
    format_value,
    list_params,
    open_port,
    parse_scalar_raw,
    read_param,
    write_param,
)


def _print_param_list(items: Iterable[dict]):
    print("ID      NAME         DIR  FMT  SIZE")
    for item in items:
        dir_name = {
            DIR_TX: "TX",
            DIR_RX: "RX",
            DIR_TX_RX: "TX/RX",
        }.get(item["direction"], f"0x{item['direction']:02x}")
        name = item.get("name") or f"0x{item['id']:04x}"
        print(f"0x{item['id']:04x}  {name:<12} {dir_name:<5} {item['fmt_name']:<4} {item['size']}")


def _load_catalog(port) -> ParameterCatalog:
    catalog = ParameterCatalog()
    try:
        catalog.update(list_params(port))
    except (TimeoutError, ValueError):
        pass
    return catalog


def _refresh_catalog(port, catalog: ParameterCatalog) -> bool:
    catalog.clear()
    try:
        catalog.update(list_params(port))
        return bool(catalog.items())
    except (TimeoutError, ValueError):
        return False


def _run_read(port, raw_id: str, catalog: ParameterCatalog):
    obj_id = catalog.resolve(raw_id)
    value = read_param(port, obj_id, catalog=catalog)
    print(f"id=0x{value['id']:04x} name={value['name']} fmt={value['fmt_name']} value={value['formatted']} raw={value['raw'].hex()}")


def _run_write(port, raw_id: str, raw_value: str, catalog: ParameterCatalog):
    obj_id = catalog.resolve(raw_id)
    info = catalog.get(obj_id)
    fmt_name = info.get("format") or info.get("fmt_name") or "u16"
    value = parse_scalar_raw(raw_value, fmt_name)
    status = write_param(port, obj_id, value, fmt=fmt_name, catalog=catalog)
    print(f"write id=0x{obj_id:04x} name={catalog.get(obj_id).get('name', 'unknown')} value={raw_value} status={status}")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Terminal client for the Magnetic Levitation USB parameter bridge")
    parser.add_argument("--port", help="Serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--baud", type=int, default=115200, help="Serial baud rate")
    parser.add_argument("--list-at-startup", action="store_true", help="Refresh the parameter catalog before executing a command")

    subparsers = parser.add_subparsers(dest="command", required=True)

    list_parser = subparsers.add_parser("list", help="List available parameters")
    list_parser.set_defaults(func="list")

    read_parser = subparsers.add_parser("read", help="Read a parameter by ID")
    read_parser.add_argument("id", help="Parameter ID, e.g. 0x0001 or 1")
    read_parser.set_defaults(func="read")

    write_parser = subparsers.add_parser("write", help="Write a parameter by ID")
    write_parser.add_argument("id", help="Parameter ID, e.g. 0x0002 or 2")
    write_parser.add_argument("value", help="Value as int/float/hex: 1234, 0x1234, 0.75")
    write_parser.set_defaults(func="write")

    monitor_parser = subparsers.add_parser("monitor", help="Repeat-read TX parameters")
    monitor_parser.add_argument("--id", type=str, help="Optional single parameter ID to monitor")
    monitor_parser.add_argument("--interval", type=float, default=0.5, help="Polling interval in seconds")
    monitor_parser.add_argument("--count", type=int, default=None, help="Stop after N polls")
    monitor_parser.set_defaults(func="monitor")

    repl_parser = subparsers.add_parser("repl", help="Interactive command loop")
    repl_parser.set_defaults(func="repl")

    return parser


def monitor(port, catalog: ParameterCatalog, obj_id: str | None, interval: float = 0.5, count: int | None = None, timeout: float | None = None):
    reads = 0
    deadline = None if timeout is None else time.monotonic() + timeout
    while True:
        if count is not None and reads >= count:
            break
        if deadline is not None and time.monotonic() >= deadline:
            break
        if obj_id is None:
            items = catalog.items()
            if not items:
                print("No parameters")
                return
            for item in items:
                if item["direction"] & DIR_TX:
                    try:
                        value = read_param(port, item["id"], catalog=catalog)
                        print(f"0x{item['id']:04x}: {value['name']} = {value['formatted']}")
                    except Exception as exc:  # pragma: no cover
                        print(f"0x{item['id']:04x}: error: {exc}")
        else:
            try:
                resolved = catalog.resolve(obj_id)
                value = read_param(port, resolved, catalog=catalog)
                print(f"0x{resolved:04x}: {value['name']} = {value['formatted']}")
            except Exception as exc:
                print(f"0x{catalog.resolve(obj_id):04x}: error: {exc}")

        reads += 1
        if count is not None and reads >= count:
            break
        if deadline is not None and time.monotonic() >= deadline:
            break
        time.sleep(interval)


def _handle_repl_command(port, catalog: ParameterCatalog, line: str, state: dict | None = None):
    if state is None:
        state = {"interval": 0.5}
    tokens = line.strip().split()
    if not tokens or tokens[0] in {"", "help", "h"}:
        print("Commands: list (l), read (r) <id>, write (w) <id> <value>, monitor (m) [id] [interval], quit (q)")
        return True
    try:
        cmd = tokens[0].lower()
        if cmd in {"list", "l"}:
            _refresh_catalog(port, catalog)
            _print_param_list(catalog.items())
            return True
        if cmd in {"read", "r"}:
            if len(tokens) != 2:
                print("Usage: read <id>")
                return True
            if not catalog.items():
                _refresh_catalog(port, catalog)
            _run_read(port, tokens[1], catalog)
            return True
        if cmd in {"write", "w"}:
            if len(tokens) != 3:
                print("Usage: write <id> <value>")
                return True
            if not catalog.items():
                _refresh_catalog(port, catalog)
            _run_write(port, tokens[1], tokens[2], catalog)
            return True
        if cmd in {"monitor", "m"}:
            if not catalog.items():
                _refresh_catalog(port, catalog)

            obj_id = tokens[1] if len(tokens) > 1 else None
            if len(tokens) > 2:
                state["interval"] = float(tokens[2])
            interval = state["interval"]
            monitor(port, catalog, obj_id, interval, timeout=5.0)
            return True
        if cmd in {"quit", "q", "exit"}:
            return False
        print(f"Unknown command: {tokens[0]}")
        return True
    except (TimeoutError, ValueError) as exc:
        print(f"ERROR: {exc}")
        return True
    except Exception as exc:
        print(f"ERROR: device disconnected: {exc}")
        return False


def run_repl(port, catalog: ParameterCatalog):
    if readline is not None:
        readline.set_history_length(100)

    _refresh_catalog(port, catalog)
    state = {"interval": 0.5}

    print("MagneticLevitation console")
    print("Type 'help' for commands or 'quit' to exit.")
    while True:
        try:
            line = input("[ML]> ")
        except EOFError:
            print()
            break

        if line and readline is not None:
            readline.add_history(line)

        try:
            if not _handle_repl_command(port, catalog, line, state):
                break
        except Exception as exc:
            print(f"ERROR: device disconnected: {exc}")
            break


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    port_name = find_port(args.port)
    try:
        with open_port(port_name, baud=args.baud) as port:
            catalog = ParameterCatalog()
            if args.command in {"list", "repl"} or args.list_at_startup:
                _refresh_catalog(port, catalog)

            if args.command == "list":
                _print_param_list(catalog.items())
                return 0
            if args.command == "read":
                _run_read(port, args.id, catalog)
                return 0
            if args.command == "write":
                _run_write(port, args.id, args.value, catalog)
                return 0
            if args.command == "monitor":
                monitor(port, catalog, args.id, args.interval, args.count)
                return 0
            if args.command == "repl":
                run_repl(port, catalog)
                return 0
            parser.error(f"Unsupported command: {args.command}")
            return 2
    except FileNotFoundError as exc:
        raise SystemExit(str(exc)) from exc
    except Exception as exc:
        raise SystemExit(f"ERROR: {exc}") from exc


if __name__ == "__main__":
    sys.exit(main())
