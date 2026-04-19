from __future__ import annotations

import argparse
import csv
import hashlib
import json
import re
import subprocess
import threading
import time
from collections import deque
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable, Iterable, Sequence


def main() -> None:
    raise SystemExit(cli_main())


def cli_main(argv: Sequence[str] | None = None) -> int:
    p = argparse.ArgumentParser(description="ADB-based RetroPortal auto launcher.")
    p.add_argument("--adb", default="adb", help="adb executable path")
    p.add_argument("--serial", help="device serial")
    p.add_argument(
        "--remote-dir",
        default="/data/local/tmp/retroportal_staging",
        help="remote staging directory",
    )
    p.add_argument(
        "--component",
        default="com.retroportal.app/.MainActivity",
        help="activity component",
    )
    p.add_argument("--push", nargs="*", default=[], help="local files to hash-sync push")
    p.add_argument("--launch", action="store_true", help="send start intent after push")
    p.add_argument(
        "--monitor",
        action="store_true",
        help="poll SurfaceFlinger latency after launch",
    )
    p.add_argument("--seconds", type=float, default=12.0, help="monitor duration")
    ns = p.parse_args(argv)

    launcher = AutoLauncher(adb_path=ns.adb, serial=ns.serial)
    launcher.ensure_server()

    staged: list[str] = []
    for local in ns.push:
        staged.append(
            launcher.push_if_changed(Path(local), Path(ns.remote_dir)),
        )

    if ns.launch:
        launcher.start_activity(ns.component, extras={"STAGED": ",".join(staged)})

    if ns.monitor:
        launcher.monitor_surface_flinger_fps(duration_s=ns.seconds)

    return 0


@dataclass
class FpsSample:
    timestamp_s: float
    frames_per_second_est: float | None
    raw_line: str


@dataclass
class AutoLauncher:
    adb_path: str = "adb"
    serial: str | None = None
    _adb_prefix: list[str] = field(init=False, repr=False)

    def __post_init__(self) -> None:
        self._adb_prefix = [self.adb_path]
        if self.serial:
            self._adb_prefix += ["-s", self.serial]

    def ensure_server(self) -> None:
        self._run_checked(["start-server"])

    def devices(self) -> list[str]:
        out = self._run_capture(["devices"])
        lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
        serials: list[str] = []
        for ln in lines[1:]:
            parts = ln.split()
            if len(parts) >= 2 and parts[1] == "device":
                serials.append(parts[0])
        return serials

    def remote_sha256(self, remote_path: str) -> str | None:
        script = f"sha256sum '{remote_path}' 2>/dev/null || exit 0"
        out = self._shell_capture(script).strip()
        if not out:
            return None
        parts = out.split()
        if len(parts) >= 1 and len(parts[0]) == 64:
            return parts[0].lower()
        return None

    def push_if_changed(self, local_path: Path, remote_dir: str) -> str:
        data = local_path.read_bytes()
        digest = hashlib.sha256(data).hexdigest().lower()
        remote_path = f"{remote_dir.rstrip('/')}/{local_path.name}"
        self._shell_checked(f"mkdir -p '{remote_dir}'")
        existing = self.remote_sha256(remote_path)
        if existing != digest:
            self._run_checked(["push", str(local_path), remote_path])
        return remote_path

    def start_activity(self, component: str, extras: dict[str, str] | None = None) -> None:
        args = ["shell", "am", "start", "-n", component]
        if extras:
            for k, v in extras.items():
                args += ["--es", k, v]
        self._run_checked(args)

    def dumpsys_surfaceflinger_latency(self, surface_name: str | None = None) -> str:
        cmd = ["shell", "dumpsys", "SurfaceFlinger", "--latency"]
        if surface_name:
            cmd.append(surface_name)
        return self._run_capture(cmd)

    def estimate_fps_from_latency_dump(self, dump: str) -> float | None:
        lines = [ln.strip() for ln in dump.splitlines() if ln.strip()]
        if len(lines) < 2:
            return None
        nums: list[list[int]] = []
        num_re = re.compile(r"^-?\d+(\s+-?\d+)+$")
        for ln in lines:
            if num_re.match(ln):
                parts = [int(x) for x in ln.split() if x.lstrip("-").isdigit()]
                if len(parts) >= 3:
                    nums.append(parts)
        if len(nums) < 3:
            return None
        recent = nums[-128:]
        deltas: list[int] = []
        for row in recent:
            if len(row) >= 3:
                deltas.append(row[2] - row[1])
        filt = [d for d in deltas if d > 0]
        if not filt:
            return None
        avg_ns = sum(filt) / len(filt)
        if avg_ns <= 0:
            return None
        return 1_000_000_000.0 / avg_ns

    def monitor_surface_flinger_fps(
        self,
        *,
        duration_s: float,
        surface_name: str | None = None,
        poll_hz: float = 2.0,
        sink: Callable[[FpsSample], None] | None = None,
    ) -> list[FpsSample]:
        samples: list[FpsSample] = []
        end = time.monotonic() + duration_s
        interval = 1.0 / max(poll_hz, 0.25)
        while time.monotonic() < end:
            dump = self.dumpsys_surfaceflinger_latency(surface_name)
            fps = self.estimate_fps_from_latency_dump(dump)
            sample = FpsSample(
                timestamp_s=time.monotonic(),
                frames_per_second_est=fps,
                raw_line=dump.splitlines()[0] if dump else "",
            )
            samples.append(sample)
            if sink:
                sink(sample)
            time.sleep(interval)
        return samples

    def monitor_to_csv(self, path: Path, **kwargs: object) -> None:
        duration = float(kwargs.get("duration_s", 10.0))
        surface = kwargs.get("surface_name")
        samples = self.monitor_surface_flinger_fps(
            duration_s=duration,
            surface_name=str(surface) if surface else None,
        )
        with path.open("w", newline="", encoding="utf-8") as f:
            w = csv.writer(f)
            w.writerow(["timestamp_s", "fps_est", "raw_first_line"])
            for s in samples:
                w.writerow([s.timestamp_s, s.frames_per_second_est, s.raw_line])

    def reverse_tcp(self, device_port: int, host_port: int) -> None:
        self._run_checked(
            ["reverse", f"tcp:{device_port}", f"tcp:{host_port}"],
        )

    def _run_checked(self, args: list[str]) -> None:
        full = self._adb_prefix + args
        proc = subprocess.run(
            full,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if proc.returncode != 0:
            raise RuntimeError(
                f"adb failed ({proc.returncode}): {' '.join(full)}\n"
                f"{proc.stderr or proc.stdout}",
            )

    def _run_capture(self, args: list[str]) -> str:
        full = self._adb_prefix + args
        proc = subprocess.run(
            full,
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        if proc.returncode != 0:
            raise RuntimeError(proc.stderr or proc.stdout or "adb error")
        return proc.stdout

    def _shell_capture(self, script: str) -> str:
        return self._run_capture(["shell", script])

    def _shell_checked(self, script: str) -> None:
        self._run_checked(["shell", script])


class BackgroundPerfMonitor:
    """Runs FPS polling on a worker thread until stop event."""

    def __init__(self, launcher: AutoLauncher) -> None:
        self._launcher = launcher
        self._thread: threading.Thread | None = None
        self._stop = threading.Event()
        self._samples: deque[FpsSample] = deque(maxlen=4096)

    def start(self, surface_name: str | None = None, poll_hz: float = 2.0) -> None:
        def worker() -> None:
            while not self._stop.is_set():
                dump = self._launcher.dumpsys_surfaceflinger_latency(surface_name)
                fps = self._launcher.estimate_fps_from_latency_dump(dump)
                self._samples.append(
                    FpsSample(
                        timestamp_s=time.monotonic(),
                        frames_per_second_est=fps,
                        raw_line=dump.splitlines()[0] if dump else "",
                    ),
                )
                time.sleep(1.0 / max(poll_hz, 0.25))

        self._thread = threading.Thread(target=worker, daemon=True)
        self._thread.start()

    def stop(self) -> list[FpsSample]:
        self._stop.set()
        if self._thread and self._thread.is_alive():
            self._thread.join(timeout=2.0)
        return list(self._samples)

    def export_json(self, path: Path) -> None:
        payload = [
            {
                "timestamp_s": s.timestamp_s,
                "fps_est": s.frames_per_second_est,
                "raw_line": s.raw_line,
            }
            for s in self._samples
        ]
        path.write_text(json.dumps(payload, indent=2), encoding="utf-8")
