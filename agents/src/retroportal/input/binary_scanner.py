from __future__ import annotations

import json
import mmap
import struct
from collections import deque
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Sequence


@dataclass(frozen=True)
class PatternHit:
    pattern_name: str
    file_offset: int
    confidence: float
    hex_snippet: str


class _TrieNode:
    __slots__ = ("next", "fail", "out")

    def __init__(self) -> None:
        self.next: dict[int, _TrieNode] = {}
        self.fail: _TrieNode | None = None
        self.out: list[tuple[str, int]] = []


class AhoCorasickAutomaton:
    """Classic Aho–Corasick multi-pattern matcher over byte streams."""

    def __init__(self, patterns: Sequence[tuple[str, bytes]]) -> None:
        self._root = _TrieNode()
        for name, pat in patterns:
            if not pat:
                continue
            node = self._root
            for b in pat:
                node = node.next.setdefault(b, _TrieNode())
            node.out.append((name, len(pat)))
        self._build_failure_links()

    def _build_failure_links(self) -> None:
        queue: deque[_TrieNode] = deque()
        for _, child in self._root.next.items():
            child.fail = self._root
            queue.append(child)
        while queue:
            state = queue.popleft()
            for byte_edge, child in state.next.items():
                queue.append(child)
                fail = state.fail
                while fail is not None and byte_edge not in fail.next:
                    fail = fail.fail
                child.fail = (
                    fail.next[byte_edge]
                    if fail is not None and byte_edge in fail.next
                    else self._root
                )
                child.out.extend(child.fail.out)

    def search_all(self, data: bytes | memoryview) -> Iterable[tuple[int, str]]:
        state = self._root
        mv = memoryview(data)
        for pos in range(len(mv)):
            byte = int(mv[pos])
            while state is not self._root and byte not in state.next:
                fail = state.fail
                state = fail if fail is not None else self._root
            if byte in state.next:
                state = state.next[byte]
            else:
                state = self._root
            for name, plen in state.out:
                yield pos - plen + 1, name


def build_api_patterns() -> list[tuple[str, bytes]]:
    """Short x86-ish opcode fragments often adjacent to notable Win32 API calls."""
    return [
        ("call_GetAsyncKeyState_ff15", bytes.fromhex("FF15")),  # call dword ptr [...]
        ("mov_reg_imm_b8", bytes.fromhex("B8")),  # mov eax, imm32 prefix family
        ("directinput_uuid_hint", b" IDirectInput"),
        ("raw_input_register", b"RegisterRawInputDevices"),
        ("GetKeyboardState_ff15", bytes.fromhex("FF15")),
        ("GetCursorPos_ff15", bytes.fromhex("FF15")),
    ]


class BinaryPatternScanner:
    def __init__(self, patterns: Sequence[tuple[str, bytes]] | None = None) -> None:
        self._patterns = list(patterns or build_api_patterns())
        self._automaton = AhoCorasickAutomaton(self._patterns)

    def scan_file(self, path: Path, chunk_size: int = 8 * 1024 * 1024) -> list[PatternHit]:
        hits: list[PatternHit] = []
        size = path.stat().st_size
        with path.open("rb") as f:
            with mmap.mmap(f.fileno(), length=0, access=mmap.ACCESS_READ) as mm:
                if size <= chunk_size:
                    view = mm[:size]
                    for hit_off, pname in self._automaton.search_all(view):
                        snippet = mm[hit_off : hit_off + 16]
                        hex_snippet = snippet.hex()
                        hits.append(
                            PatternHit(
                                pattern_name=pname,
                                file_offset=hit_off,
                                confidence=_score_hit(pname, snippet),
                                hex_snippet=hex_snippet,
                            ),
                        )
                else:
                    overlap = max([len(pat) for _, pat in self._patterns], default=8) + 16
                    offset = 0
                    while offset < size:
                        end = min(offset + chunk_size + overlap, size)
                        window = mm[offset:end]
                        for hit_off, pname in self._automaton.search_all(window):
                            abs_off = offset + hit_off
                            if abs_off >= size:
                                continue
                            snippet = mm[abs_off : abs_off + 16]
                            hex_snippet = snippet.hex()
                            hits.append(
                                PatternHit(
                                    pattern_name=pname,
                                    file_offset=abs_off,
                                    confidence=_score_hit(pname, snippet),
                                    hex_snippet=hex_snippet,
                                ),
                            )
                        offset += chunk_size
        hits.sort(key=lambda h: (-h.confidence, h.file_offset))
        return hits

    def scan_many(self, paths: Iterable[Path]) -> dict[str, list[PatternHit]]:
        report: dict[str, list[PatternHit]] = {}
        for p in paths:
            report[str(p)] = self.scan_file(p)
        return report

    def report_json(self, paths: Iterable[Path]) -> str:
        raw = self.scan_many(paths)
        serializable = {
            k: [
                {
                    "pattern_name": h.pattern_name,
                    "file_offset": h.file_offset,
                    "confidence": h.confidence,
                    "hex_snippet": h.hex_snippet,
                }
                for h in v
            ]
            for k, v in raw.items()
        }
        return json.dumps(serializable, indent=2)


def _score_hit(name: str, snippet: bytes) -> float:
    score = 0.55
    if b"DirectInput" in snippet or b"RawInput" in snippet:
        score += 0.25
    if name.startswith("call_"):
        score += 0.1
    if len(snippet) >= 8:
        score += 0.05
    return min(score, 1.0)


def read_pe_machine(path: Path) -> int | None:
    try:
        with path.open("rb") as f:
            hdr = f.read(64)
            if len(hdr) < 64:
                return None
            pe_off = struct.unpack_from("<I", hdr, 0x3C)[0]
            f.seek(pe_off)
            sig = f.read(4)
            if sig != b"PE\x00\x00":
                return None
            machine = struct.unpack("<H", f.read(2))[0]
            return machine
    except OSError:
        return None
