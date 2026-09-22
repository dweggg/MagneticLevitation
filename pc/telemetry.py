"""Live plotting for the multi-stream telemetry, x axis = firmware time."""

from __future__ import annotations

from collections import deque
import time
from typing import Iterable

from pc.metadata import ParameterCatalog
from pc.transport import Link, StreamSample, decode_value

MAX_POINTS = 200_000


class LiveTelemetryPlot:
    """Non-blocking matplotlib view of any variables registered on a stream.

    Variables may come from different streams at different rates; each
    sample is placed on the x axis by its firmware tick (unwrapped from 32
    bits and divided by the device tick rate), never by arrival time.
    """

    def __init__(self, catalog: ParameterCatalog, tick_hz: int, names: Iterable[str] = (), window_seconds: float = 5.0) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:  # pragma: no cover - depends on local environment
            raise RuntimeError("Live plotting requires matplotlib. Install it with: python3 -m pip install matplotlib") from exc
        if tick_hz <= 0:
            raise ValueError(f"Invalid firmware tick rate: {tick_hz} Hz")
        if window_seconds <= 0:
            raise ValueError("--window must be greater than zero")
        self._plt = plt
        self.catalog = catalog
        self.tick_hz = tick_hz
        self.window_seconds = window_seconds
        self._available = {v["name"]: v for v in catalog.stream_vars()}
        if not self._available:
            raise ValueError("The device has no streamed variables")
        self._names: list[str] = []
        self._x: dict[str, deque] = {}
        self._y: dict[str, deque] = {}
        self._last_tick: int | None = None
        self._unwrapped = 0
        self._base: int | None = None
        self._last_draw = 0.0
        self.paused = False
        self._follow_time = True
        self._dirty = False
        self._figure = None
        self._axes = []
        self._lines = []
        self._set_names(list(names) or list(self._available))
        self._create_figure()

    @property
    def channel_names(self) -> tuple[str, ...]:
        return tuple(self._names)

    @property
    def is_open(self) -> bool:
        return self._figure is not None and self._plt.fignum_exists(self._figure.number)

    def _validate(self, names: Iterable[str]) -> list[str]:
        result: list[str] = []
        for name in names:
            if name not in self._available:
                raise ValueError(f"Unknown streamed variable '{name}'. Choose from: {', '.join(self._available)}")
            if name not in result:
                result.append(name)
        return result

    def _set_names(self, names: list[str]) -> None:
        self._names = self._validate(names)
        for name in self._names:
            self._x.setdefault(name, deque(maxlen=MAX_POINTS))
            self._y.setdefault(name, deque(maxlen=MAX_POINTS))

    def _create_figure(self) -> None:
        self._figure, axes = self._plt.subplots(len(self._names), 1, sharex=True, squeeze=False)
        self._axes = [row[0] for row in axes]
        self._lines = []
        for axis, name in zip(self._axes, self._names):
            (line,) = axis.plot([], [], lw=0.8)
            axis.set_ylabel(name)
            axis.grid(True, alpha=0.3)
            self._lines.append(line)
        self._axes[-1].set_xlabel("firmware time (s)")
        self._figure.canvas.manager.set_window_title("Magnetic Levitation telemetry")
        self._figure.tight_layout()
        self._plt.show(block=False)
        self._dirty = True

    def set_channels(self, names: Iterable[str]) -> None:
        names = self._validate(names)
        if not names:
            raise ValueError("Select at least one telemetry variable")
        if names == self._names:
            return
        self.close()
        self._set_names(names)
        self._create_figure()

    def add_channels(self, names: Iterable[str]) -> None:
        self.set_channels([*self._names, *names])

    def remove_channels(self, names: Iterable[str]) -> None:
        unwanted = set(self._validate(names))
        self.set_channels(n for n in self._names if n not in unwanted)

    def on_sample(self, sample: StreamSample) -> None:
        """Link stream sink: place one sample on the firmware time axis."""
        if self._last_tick is None:
            self._unwrapped = self._base = sample.tick
        else:
            # Signed 32-bit delta tolerates wrap-around and slight cross-stream reordering.
            self._unwrapped += ((sample.tick - self._last_tick + 0x80000000) & 0xFFFFFFFF) - 0x80000000
        self._last_tick = sample.tick
        if self.paused:
            return  # keep the tick unwrapper continuous so resume has no jump
        t = (self._unwrapped - self._base) / self.tick_hz
        for name in self._names:
            var = self._available[name]
            if var["stream"] != sample.stream:
                continue
            raw = sample.data[var["offset"]:var["offset"] + var["size"]]
            if len(raw) == var["size"]:
                self._x[name].append(t)
                self._y[name].append(float(decode_value(raw, var["format"])))
                self._dirty = True

    def pause(self) -> None:
        self.paused = True
        self._follow_time = False  # keep toolbar pan/zoom intact

    def resume(self) -> None:
        self.paused = False
        self._follow_time = True

    def refresh(self) -> bool:
        """Process GUI events and redraw at most 30 times per second."""
        if not self.is_open:
            return False
        now = time.monotonic()
        if self._dirty and now - self._last_draw >= 1 / 30:
            right = max((x[-1] for x in self._x.values() if x), default=0.0)
            for axis, line, name in zip(self._axes, self._lines, self._names):
                xs, ys = self._x[name], self._y[name]
                while xs and xs[0] < right - 1.1 * self.window_seconds:
                    xs.popleft()
                    ys.popleft()
                line.set_data(list(xs), list(ys))
                axis.relim()
                axis.autoscale_view(scalex=False, scaley=True)
            if self._follow_time:
                self._axes[-1].set_xlim(right - self.window_seconds, max(right, self.window_seconds))
            streams = ", ".join(f"{s['name']}@{s['rate_hz']}Hz" for s in self.catalog.streams.values())
            self._figure.suptitle(f"streams: {streams}")
            self._figure.canvas.draw_idle()
            self._last_draw = now
            self._dirty = False
        self._plt.pause(0.001)
        return self.is_open

    def close(self) -> None:
        if self._figure is not None:
            self._plt.close(self._figure)
            self._figure = None


def plot_live(link: Link, catalog: ParameterCatalog, tick_hz: int, names: Iterable[str], window_seconds: float = 5.0) -> None:
    """Run a standalone live plot outside the interactive REPL."""
    plot = LiveTelemetryPlot(catalog, tick_hz, names, window_seconds)
    link.stream_sink = plot.on_sample
    print("Telemetry plot is open; close its window or press Ctrl-C to stop.")
    try:
        while plot.is_open:
            link.pump()
            plot.refresh()
    finally:
        link.stream_sink = None
        plot.close()
