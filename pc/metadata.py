from __future__ import annotations

from typing import Any, Iterable

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


def normalize_format_name(fmt: int | str) -> str:
    if isinstance(fmt, int):
        lookup = FMT_NAMES.get(fmt, f"fmt{fmt}")
        return FMT_ALIASES.get(lookup, lookup)
    name = str(fmt).strip().lower()
    return FMT_ALIASES.get(name, name)


class ParameterCatalog:
    def __init__(self) -> None:
        self._by_id: dict[int, dict[str, Any]] = {}
        self._by_name: dict[str, int] = {}

    def clear(self) -> None:
        self._by_id.clear()
        self._by_name.clear()

    def update(self, items: Iterable[dict[str, Any]]) -> list[dict[str, Any]]:
        normalized: list[dict[str, Any]] = []
        for item in items:
            info = dict(item)
            obj_id = int(info.get("id"))
            info.setdefault("name", f"param_{obj_id:04x}")
            info["direction"] = int(info.get("direction", 0))
            raw_format = info.get("format", "raw")
            normalized_format = normalize_format_name(raw_format)
            info["format"] = normalized_format
            info["size"] = int(info.get("size", 0))
            info["fmt_name"] = normalized_format
            self._by_id[obj_id] = info
            self._by_name[str(info["name"]).strip().lower()] = obj_id
            normalized.append(info)
        return normalized

    def has(self, obj_id: int) -> bool:
        return obj_id in self._by_id

    def get(self, obj_id: int) -> dict[str, Any]:
        info = self._by_id.get(obj_id)
        if info is not None:
            return info
        fallback = {
            "id": obj_id,
            "name": f"param_{obj_id:04x}",
            "direction": 0,
            "format": "raw",
            "size": 0,
            "fmt_name": "raw",
        }
        return fallback

    def resolve(self, raw: str | int | None) -> int:
        if raw is None:
            raise ValueError("id is required")
        if isinstance(raw, int):
            return raw

        text = raw.strip()
        if not text:
            raise ValueError("id is required")
        if text.lower().startswith("0x"):
            return int(text, 16)

        normalized = text.lower()
        if normalized in self._by_name:
            return self._by_name[normalized]

        try:
            return int(text, 0)
        except ValueError as exc:
            matches = []
            for obj_id, info in self._by_id.items():
                candidate = str(info.get("name", "")).strip().lower()
                if candidate == normalized:
                    return obj_id
                if candidate.startswith(normalized):
                    matches.append(obj_id)

            if matches:
                matches_str = ", ".join(f"0x{obj_id:04x}" for obj_id in matches)
                raise ValueError(f"Ambiguous parameter name '{raw}'; matches: {matches_str}")

            known = ", ".join(sorted(str(info["name"]) for info in self._by_id.values()))
            raise ValueError(f"Unknown parameter '{raw}'. Use an ID or one of: {known}") from exc

    def names(self) -> list[str]:
        return sorted(str(info["name"]) for info in self._by_id.values())

    def items(self) -> list[dict[str, Any]]:
        return sorted(self._by_id.values(), key=lambda item: int(item["id"]))
