"""Decoder and plotting helpers for the USB CDC telemetry stream."""

from __future__ import annotations

from collections import deque
from dataclasses import dataclass
import struct
import time
from typing import Iterable


TELEMETRY_SYNC = b"\xa5\x5a"
TELEMETRY_FRAME_SIZE = 20
TELEMETRY_CHANNEL_COUNT = 4


@dataclass(frozen=True)
class TelemetryFrame:
    sequence: int
    values: tuple[float, float, float, float]


class TelemetryDecoder:
    """Incrementally extract valid frames from a mixed CDC byte stream."""

    def __init__(self) -> None:
        self._buffer = bytearray()

    def feed(self, data: bytes) -> list[TelemetryFrame]:
        self._buffer.extend(data)
        frames: list[TelemetryFrame] = []

        while True:
            start = self._buffer.find(TELEMETRY_SYNC)
            if start < 0:
                # Keep one possible leading sync byte for the next read.
                del self._buffer[:-1]
                break
            if start:
                del self._buffer[:start]
            if len(self._buffer) < TELEMETRY_FRAME_SIZE:
                break

            candidate = self._buffer[:TELEMETRY_FRAME_SIZE]
            checksum = 0
            for byte in candidate[:-1]:
                checksum ^= byte
            if checksum != candidate[-1]:
                del self._buffer[0]
                continue

            sequence = candidate[2]
            raw_values = struct.unpack_from("<4i", candidate, 3)
            frames.append(TelemetryFrame(sequence, tuple(value / 65536.0 for value in raw_values)))
            del self._buffer[:TELEMETRY_FRAME_SIZE]

        return frames


def selected_channels(names: Iterable[str], available_channels: Iterable[str]) -> tuple[int, ...]:
    """Return channel indices, rejecting unknown names and duplicates."""
    available = tuple(available_channels)
    result: list[int] = []
    for name in names:
        try:
            index = available.index(name)
        except ValueError as exc:
            choices = ", ".join(available)
            raise ValueError(f"Unknown telemetry variable '{name}'. Choose from: {choices}") from exc
        if index not in result:
            result.append(index)
    return tuple(result)


class LiveTelemetryPlot:
    """Non-blocking matplotlib telemetry view owned by the caller's event loop."""

    def __init__(self, available_channels: Iterable[str], channel_names: Iterable[str], rate_hz: int, window_seconds: float = 5.0) -> None:
        try:
            import matplotlib.pyplot as plt
        except ImportError as exc:  # pragma: no cover - depends on local environment
            raise RuntimeError("Live plotting requires matplotlib. Install it with: python3 -m pip install matplotlib") from exc

        if rate_hz <= 0:
            raise ValueError(f"Invalid telemetry rate: {rate_hz} Hz")
        if window_seconds <= 0:
            raise ValueError("--window must be greater than zero")
        self._plt = plt
        self.rate_hz = rate_hz
        self.window_seconds = window_seconds
        self._available_channels = tuple(available_channels)
        if len(self._available_channels) != TELEMETRY_CHANNEL_COUNT:
            raise ValueError(f"Unsupported telemetry layout: expected {TELEMETRY_CHANNEL_COUNT} channels, got {len(self._available_channels)}")
        self._indices = list(selected_channels(channel_names, self._available_channels) or range(len(self._available_channels)))
        max_samples = max(2, int(rate_hz * window_seconds))
        self._samples = deque(maxlen=max_samples)
        self._values = [deque(maxlen=max_samples) for _ in self._available_channels]
        self._decoder = TelemetryDecoder()
        self._last_sequence: int | None = None
        self._sample_number = 0
        self._missing = 0
        self._last_draw = 0.0
        self.paused = False
        self._follow_time = True
        self._dirty = False
        self._figure = None
        self._axes = []
        self._lines = []
        self._create_figure()

    @property
    def channel_names(self) -> tuple[str, ...]:
        return tuple(self._available_channels[index] for index in self._indices)

    @property
    def is_open(self) -> bool:
        return self._figure is not None and self._plt.fignum_exists(self._figure.number)

    def _create_figure(self) -> None:
        self._figure, axes = self._plt.subplots(len(self._indices), 1, sharex=True, squeeze=False)
        self._axes = [row[0] for row in axes]
        self._lines = []
        for axis, index in zip(self._axes, self._indices):
            (line,) = axis.plot([], [], lw=0.8)
            axis.set_ylabel(self._available_channels[index])
            axis.grid(True, alpha=0.3)
            self._lines.append(line)
        self._axes[-1].set_xlabel("time (s)")
        self._figure.canvas.manager.set_window_title("Magnetic Levitation telemetry")
        self._figure.tight_layout()
        self._plt.show(block=False)
        self._dirty = True

    def set_channels(self, channel_names: Iterable[str]) -> None:
        indices = list(selected_channels(channel_names, self._available_channels))
        if not indices:
            raise ValueError("Select at least one telemetry variable")
        if indices == self._indices:
            return
        self.close()
        self._indices = indices
        self._create_figure()

    def add_channels(self, channel_names: Iterable[str]) -> None:
        additions = selected_channels(channel_names, self._available_channels)
        self.set_channels([*self.channel_names, *(self._available_channels[index] for index in additions)])

    def remove_channels(self, channel_names: Iterable[str]) -> None:
        unwanted = set(selected_channels(channel_names, self._available_channels))
        self.set_channels(self._available_channels[index] for index in self._indices if index not in unwanted)

    def feed(self, data: bytes) -> None:
        for frame in self._decoder.feed(data):
            if self._last_sequence is not None:
                gap = (frame.sequence - self._last_sequence) & 0xFF
                if gap == 0:
                    continue
                self._missing += gap - 1
                self._sample_number += gap
            self._last_sequence = frame.sequence
            self._samples.append(self._sample_number / self.rate_hz)
            for bucket, value in zip(self._values, frame.values):
                bucket.append(value)
            self._sample_number += 1
            self._dirty = True

    def discard(self, data: bytes) -> None:
        """Consume frames without changing the displayed history while paused."""
        for frame in self._decoder.feed(data):
            # Keep the next resumed frame adjacent to the visible history.
            # The host still drains USB continuously, avoiding device-side drops.
            self._last_sequence = frame.sequence

    def pause(self) -> None:
        self.paused = True
        # Stop applying automatic limits so toolbar pan/zoom remains intact.
        self._follow_time = False

    def resume(self) -> None:
        self.paused = False
        self._follow_time = True

    def refresh(self) -> bool:
        """Process GUI events and redraw at most 30 times per second."""
        if not self.is_open:
            return False
        now = time.monotonic()
        if self._dirty and self._samples and now - self._last_draw >= 1 / 30:
            x_values = list(self._samples)
            for axis, line, index in zip(self._axes, self._lines, self._indices):
                line.set_data(x_values, list(self._values[index]))
                axis.relim()
                axis.autoscale_view(scalex=False, scaley=True)
            if self._follow_time:
                right = x_values[-1]
                self._axes[-1].set_xlim(max(0.0, right - self.window_seconds), max(self.window_seconds, right))
            self._figure.suptitle(f"{self.rate_hz} Hz  |  host sequence gaps: {self._missing}")
            self._figure.canvas.draw_idle()
            self._last_draw = now
            self._dirty = False
        self._plt.pause(0.001)
        return self.is_open

    def close(self) -> None:
        if self._figure is not None:
            self._plt.close(self._figure)
            self._figure = None


def plot_live(port, available_channels: Iterable[str], channel_names: Iterable[str], rate_hz: int, window_seconds: float = 5.0) -> None:
    """Run a standalone live plot outside the interactive REPL."""
    plot = LiveTelemetryPlot(available_channels, channel_names, rate_hz, window_seconds)
    print("Telemetry plot is open; close its window or press Ctrl-C to stop.")
    try:
        while plot.is_open:
            waiting = getattr(port, "in_waiting", 0)
            plot.feed(port.read(waiting or 4096))
            plot.refresh()
    finally:
        plot.close()
