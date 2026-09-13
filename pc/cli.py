from __future__ import annotations

import argparse
import os
import select
import sys
import time
from typing import Iterable

try:
    import termios
except ImportError:  # pragma: no cover - non-POSIX fallback
    termios = None

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
from pc.telemetry import LiveTelemetryPlot, TELEMETRY_CHANNELS, plot_live


STREAM_CHANNELS_ID = 0x000E
STREAM_RATE_HZ_ID = 0x000F
STREAM_DROPPED_ID = 0x0010
STREAM_METADATA_PARAMETERS = (
    {"id": STREAM_CHANNELS_ID, "name": "stream_channels", "format": "u8", "size": 1},
    {"id": STREAM_RATE_HZ_ID, "name": "stream_rate_hz", "format": "u32", "size": 4},
    {"id": STREAM_DROPPED_ID, "name": "stream_dropped", "format": "u32", "size": 4},
)


class _ReplLineEditor:
    """Minimal non-blocking terminal editor that preserves command history."""

    PROMPT = "[ML]> "

    def __init__(self) -> None:
        self._fd = sys.stdin.fileno()
        self._saved_settings = None
        self._buffer: list[str] = []
        self._cursor = 0
        self._history: list[str] = []
        self._history_index: int | None = None
        self._escape = bytearray()

    def start(self) -> None:
        if termios is None or not sys.stdin.isatty():
            raise RuntimeError("Interactive REPL requires a POSIX terminal")
        self._saved_settings = termios.tcgetattr(self._fd)
        settings = termios.tcgetattr(self._fd)
        settings[3] &= ~(termios.ICANON | termios.ECHO)
        settings[6][termios.VMIN] = 0
        settings[6][termios.VTIME] = 0
        termios.tcsetattr(self._fd, termios.TCSANOW, settings)
        self.render()

    def close(self) -> None:
        if self._saved_settings is not None:
            termios.tcsetattr(self._fd, termios.TCSANOW, self._saved_settings)
            self._saved_settings = None

    def render(self) -> None:
        text = "".join(self._buffer)
        sys.stdout.write(f"\r\033[2K{self.PROMPT}{text}")
        if self._cursor < len(self._buffer):
            sys.stdout.write(f"\033[{len(self._buffer) - self._cursor}D")
        sys.stdout.flush()

    def _set_buffer(self, text: str) -> None:
        self._buffer = list(text)
        self._cursor = len(self._buffer)
        self.render()

    def _history_up(self) -> None:
        if not self._history:
            return
        self._history_index = len(self._history) - 1 if self._history_index is None else max(0, self._history_index - 1)
        self._set_buffer(self._history[self._history_index])

    def _history_down(self) -> None:
        if self._history_index is None:
            return
        self._history_index += 1
        if self._history_index >= len(self._history):
            self._history_index = None
            self._set_buffer("")
        else:
            self._set_buffer(self._history[self._history_index])

    def _handle_escape(self, byte: int) -> None:
        self._escape.append(byte)
        if len(self._escape) < 3:
            return
        if self._escape == b"\x1b[A":
            self._history_up()
        elif self._escape == b"\x1b[B":
            self._history_down()
        elif self._escape == b"\x1b[C" and self._cursor < len(self._buffer):
            self._cursor += 1
            self.render()
        elif self._escape == b"\x1b[D" and self._cursor > 0:
            self._cursor -= 1
            self.render()
        self._escape.clear()

    def feed(self, data: bytes) -> list[str | None]:
        completed: list[str | None] = []
        for byte in data:
            if self._escape:
                self._handle_escape(byte)
                continue
            if byte == 0x1B:
                self._escape.append(byte)
            elif byte in (10, 13):
                line = "".join(self._buffer)
                sys.stdout.write("\r\n")
                sys.stdout.flush()
                if line:
                    self._history.append(line)
                self._buffer.clear()
                self._cursor = 0
                self._history_index = None
                completed.append(line)
            elif byte == 4 and not self._buffer:  # Ctrl-D
                completed.append(None)
            elif byte in (8, 127) and self._cursor:
                del self._buffer[self._cursor - 1]
                self._cursor -= 1
                self.render()
            elif 32 <= byte < 127:
                self._buffer.insert(self._cursor, chr(byte))
                self._cursor += 1
                self.render()
        return completed


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

    telemetry_parser = subparsers.add_parser("telemetry", help="Inspect or live-plot the binary telemetry stream")
    telemetry_subparsers = telemetry_parser.add_subparsers(dest="telemetry_command", required=True)
    telemetry_list_parser = telemetry_subparsers.add_parser("list", help="List stream variables and device stream metadata")
    telemetry_list_parser.set_defaults(func="telemetry_list")
    telemetry_plot_parser = telemetry_subparsers.add_parser("plot", help="Open a live telemetry plot")
    telemetry_plot_parser.add_argument("variables", nargs="*", choices=TELEMETRY_CHANNELS, help="Variables to plot (default: all)")
    telemetry_plot_parser.add_argument("--window", type=float, default=5.0, help="Visible history in seconds (default: 5)")
    telemetry_plot_parser.set_defaults(func="telemetry_plot")

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
        print("Commands: list (l), read (r) <id>, write (w) <id> <value>, monitor (m) [id] [interval]")
        print("          plot open [variables], plot close, plot pause, plot resume")
        print("          plot add <variables>, plot remove <variables>, plot list, quit (q)")
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
        if cmd in {"plot", "p"}:
            return _handle_repl_plot_command(port, catalog, tokens[1:], state)
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


def _handle_repl_plot_command(port, catalog: ParameterCatalog, arguments: list[str], state: dict) -> bool:
    if not arguments:
        print("Usage: plot open [variables] | close | add <variables> | remove <variables> | list")
        return True
    action = arguments[0].lower()
    plot = state.get("plot")
    if action == "list":
        if plot is None or not plot.is_open:
            print("Plot: closed")
        else:
            print(f"Plot: {', '.join(plot.channel_names)} ({'paused' if plot.paused else 'live'})")
        return True
    if action == "close":
        if plot is not None:
            plot.close()
            state["plot"] = None
        print("Telemetry plot closed")
        return True
    if action == "open":
        if plot is not None:
            plot.close()
        metadata = _stream_metadata(port, catalog)
        if metadata["channels"] != len(TELEMETRY_CHANNELS):
            raise ValueError(f"Unsupported telemetry layout: device reports {metadata['channels']} channels")
        port.reset_input_buffer()
        state["plot"] = LiveTelemetryPlot(arguments[1:], metadata["rate_hz"], state.get("plot_window", 5.0))
        print("Telemetry plot opened: " + ", ".join(state["plot"].channel_names))
        return True
    if plot is None or not plot.is_open:
        print("Telemetry plot is not open. Use: plot open [variables]")
        return True
    if action == "pause":
        plot.pause()
        print("Telemetry plot paused; the stream is still drained so parameter commands stay responsive")
        return True
    if action in {"resume", "play"}:
        plot.resume()
        print("Telemetry plot resumed")
        return True
    if action == "add":
        if len(arguments) < 2:
            print("Usage: plot add <variables>")
            return True
        plot.add_channels(arguments[1:])
        print("Plot: " + ", ".join(plot.channel_names))
        return True
    if action in {"remove", "rm"}:
        if len(arguments) < 2:
            print("Usage: plot remove <variables>")
            return True
        plot.remove_channels(arguments[1:])
        print("Plot: " + ", ".join(plot.channel_names))
        return True
    print(f"Unknown plot command: {arguments[0]}")
    return True


def run_repl(port, catalog: ParameterCatalog):
    if readline is not None:
        readline.set_history_length(100)

    _refresh_catalog(port, catalog)
    state = {"interval": 0.5, "plot": None, "plot_window": 5.0}
    editor = _ReplLineEditor()

    print("MagneticLevitation console")
    print("Type 'help' for commands or 'quit' to exit.")
    editor.start()
    try:
        while True:
            plot = state["plot"]
            if plot is not None:
                if not plot.is_open:
                    state["plot"] = None
                else:
                    waiting = getattr(port, "in_waiting", 0)
                    if waiting:
                        chunk = port.read(waiting)
                        if plot.paused:
                            plot.discard(chunk)
                        else:
                            plot.feed(chunk)
                    plot.refresh()
            try:
                readable, _, _ = select.select([sys.stdin], [], [], 0.02)
            except (OSError, ValueError) as exc:
                raise RuntimeError("Interactive REPL requires a selectable terminal input stream") from exc
            if not readable:
                continue
            input_bytes = os.read(sys.stdin.fileno(), 64)
            if not input_bytes:
                break
            for line in editor.feed(input_bytes):
                if line is None or not _handle_repl_command(port, catalog, line, state):
                    return
                editor.render()
    except KeyboardInterrupt:
        print()
    finally:
        editor.close()
        plot = state.get("plot")
        if plot is not None:
            plot.close()


def _stream_metadata(port, catalog: ParameterCatalog) -> dict[str, int]:
    # These IDs and formats are part of the CDC telemetry protocol, so reads do
    # not depend on the optional parameter-list request succeeding first.
    catalog.update(item for item in STREAM_METADATA_PARAMETERS if not catalog.has(item["id"]))
    metadata: dict[str, int] = {}
    for obj_id, key in ((STREAM_CHANNELS_ID, "channels"), (STREAM_RATE_HZ_ID, "rate_hz"), (STREAM_DROPPED_ID, "dropped")):
        metadata[key] = int(read_param(port, obj_id, catalog=catalog)["value"])
    return metadata


def _run_telemetry_list(port, catalog: ParameterCatalog) -> None:
    metadata = _stream_metadata(port, catalog)
    print("Telemetry variables:")
    for index, name in enumerate(TELEMETRY_CHANNELS):
        print(f"  {index}: {name} (Q16.16)")
    print(f"Stream: {metadata['channels']} channels, {metadata['rate_hz']} Hz, device drops: {metadata['dropped']}")


def _run_telemetry_plot(port, catalog: ParameterCatalog, variables: list[str], window: float) -> None:
    metadata = _stream_metadata(port, catalog)
    if metadata["channels"] != len(TELEMETRY_CHANNELS):
        raise ValueError(f"Unsupported telemetry layout: device reports {metadata['channels']} channels; this CLI supports {len(TELEMETRY_CHANNELS)}")
    # Do not let replies or partially received frames contaminate the plot decoder.
    port.reset_input_buffer()
    plot_live(port, variables, metadata["rate_hz"], window)


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    port_name = find_port(args.port)
    try:
        with open_port(port_name, baud=args.baud) as port:
            catalog = ParameterCatalog()
            if args.command in {"list", "repl", "telemetry"} or args.list_at_startup:
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
            if args.command == "telemetry":
                if args.telemetry_command == "list":
                    _run_telemetry_list(port, catalog)
                    return 0
                if args.telemetry_command == "plot":
                    _run_telemetry_plot(port, catalog, args.variables, args.window)
                    return 0
            parser.error(f"Unsupported command: {args.command}")
            return 2
    except FileNotFoundError as exc:
        raise SystemExit(str(exc)) from exc
    except Exception as exc:
        raise SystemExit(f"ERROR: {exc}") from exc


if __name__ == "__main__":
    sys.exit(main())
