from __future__ import annotations

import bisect
import re
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Iterator


_RVA_LINE = re.compile(
    r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})\s+"
    r"(?:\?\S+|[\w@?.$]+)\s+"
    r"([0-9A-Fa-f]{8})\s+",
    re.MULTILINE,
)

_ALTERNATE_RVA = re.compile(
    r"^\s*([0-9A-Fa-f]{8})\s+([A-Za-z_][\w@?.$]*)\s",
    re.MULTILINE,
)

_HEX_ADDR_IN_TRACE = re.compile(
    r"(?:0x)?([0-9A-Fa-f]{6,16})\b",
)

_STACK_FRAME = re.compile(
    r"(?:ip|pc|eip|rip)\s*[=:]\s*(?:0x)?([0-9A-Fa-f]{4,16})",
    re.IGNORECASE,
)


def _parse_hex_u32(tok: str) -> int:
    return int(tok, 16) & 0xFFFFFFFF


@dataclass(frozen=True)
class SymbolRecord:
    rva: int
    name: str


class MapFileIndex:
    """Nearest-symbol lookup for RVA addresses parsed from MSVC-style .map exports."""

    def __init__(self, image_base: int = 0x00400000) -> None:
        self.image_base = image_base & 0xFFFFFFFF
        self._symbols: list[SymbolRecord] = []
        self._sorted_rva: list[int] = []

    def add_symbol(self, rva: int, name: str) -> None:
        self._symbols.append(SymbolRecord(rva=rva & 0xFFFFFFFF, name=name.strip()))

    def finalize(self) -> None:
        self._symbols.sort(key=lambda s: s.rva)
        self._sorted_rva = [s.rva for s in self._symbols]

    def resolve_rva(self, rva: int) -> SymbolRecord | None:
        if not self._sorted_rva:
            return None
        addr = rva & 0xFFFFFFFF
        idx = bisect.bisect_right(self._sorted_rva, addr) - 1
        if idx < 0:
            return None
        return self._symbols[idx]

    def absolute_to_rva(self, absolute: int) -> int:
        return (absolute - self.image_base) & 0xFFFFFFFF


def parse_msvc_map(text: str, image_base: int = 0x00400000) -> MapFileIndex:
    idx = MapFileIndex(image_base=image_base)
    preferred_preferred = re.compile(
        r"Preferred load address is ([0-9A-Fa-f]{8})",
        re.IGNORECASE,
    )
    m = preferred_preferred.search(text)
    if m:
        idx.image_base = _parse_hex_u32(m.group(1))

    for match in _RVA_LINE.finditer(text):
        seg = int(match.group(1), 16)
        if seg == 0:
            continue
        rva = _parse_hex_u32(match.group(3))
        rest_line = match.group(0)
        name_field = rest_line.split()
        if len(name_field) >= 3:
            token = name_field[2]
            if token.startswith("?"):
                name = token
            else:
                name = token
            idx.add_symbol(rva, name)

    if not idx._symbols:
        for match in _ALTERNATE_RVA.finditer(text):
            rva = _parse_hex_u32(match.group(1))
            name = match.group(2)
            idx.add_symbol(rva, name)

    idx.finalize()
    return idx


def load_map_file(path: Path, image_base: int | None = None) -> MapFileIndex:
    raw = path.read_text(encoding="utf-8", errors="replace")
    if image_base is None:
        return parse_msvc_map(raw)
    return parse_msvc_map(raw, image_base=image_base)


@dataclass(frozen=True)
class ResolvedCrash:
    raw_address: int
    rva: int | None
    nearest_symbol: str | None
    symbol_offset_bytes: int | None


class StackTraceParser:
    """Cross-references textual crash logs with a loaded .map index."""

    def __init__(self, index: MapFileIndex) -> None:
        self._index = index

    def extract_code_addresses(self, crash_log: str) -> list[int]:
        addresses: list[int] = []
        for m in _HEX_ADDR_IN_TRACE.finditer(crash_log):
            val = int(m.group(1), 16)
            if val >= 0x1000:
                addresses.append(val)
        for m in _STACK_FRAME.finditer(crash_log):
            val = int(m.group(1), 16)
            addresses.append(val)
        dedup: list[int] = []
        seen: set[int] = set()
        for a in addresses:
            if a not in seen:
                seen.add(a)
                dedup.append(a)
        return dedup

    def resolve_absolute(self, absolute: int) -> ResolvedCrash:
        rva = self._index.absolute_to_rva(absolute)
        sym = self._index.resolve_rva(rva)
        if sym is None:
            return ResolvedCrash(
                raw_address=absolute,
                rva=rva,
                nearest_symbol=None,
                symbol_offset_bytes=None,
            )
        offset = (rva - sym.rva) & 0xFFFFFFFF
        return ResolvedCrash(
            raw_address=absolute,
            rva=rva,
            nearest_symbol=sym.name,
            symbol_offset_bytes=offset,
        )

    def analyze_log(self, crash_log: str) -> Iterator[ResolvedCrash]:
        for addr in self.extract_code_addresses(crash_log):
            yield self.resolve_absolute(addr)


def parse_min_gw_map_line_iter(lines: Iterable[str]) -> Iterator[tuple[int, str]]:
    for line in lines:
        parts = line.split()
        if len(parts) >= 2 and len(parts[0]) == 8:
            try:
                rva = int(parts[0], 16)
                name = parts[1]
                yield rva, name
            except ValueError:
                continue
