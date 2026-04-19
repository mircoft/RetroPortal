from __future__ import annotations

import hashlib
import json
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import yaml


@dataclass
class DependencyArtifact:
    name: str
    sha256_expected: str | None = None
    url: str | None = None
    local_path: Path | None = None
    notes: str = ""


@dataclass
class DependencyManifest:
    title: str
    dependencies: list[DependencyArtifact] = field(default_factory=list)

    @staticmethod
    def from_yaml(path: Path) -> DependencyManifest:
        raw = yaml.safe_load(path.read_text(encoding="utf-8"))
        if not isinstance(raw, dict):
            raise ValueError("manifest root must be a mapping")
        title = str(raw.get("title", path.stem))
        deps: list[DependencyArtifact] = []
        for item in raw.get("dependencies", []) or []:
            if not isinstance(item, dict):
                continue
            deps.append(
                DependencyArtifact(
                    name=str(item["name"]),
                    sha256_expected=item.get("sha256"),
                    url=item.get("url"),
                    local_path=Path(item["local_path"])
                    if item.get("local_path")
                    else None,
                    notes=str(item.get("notes", "")),
                ),
            )
        return DependencyManifest(title=title, dependencies=deps)


class ManifestResolver:
    """Resolves and optionally fetches dependency artifacts declared in a manifest."""

    def __init__(self, cache_dir: Path) -> None:
        self.cache_dir = cache_dir
        self.cache_dir.mkdir(parents=True, exist_ok=True)

    def sha256_file(self, path: Path) -> str:
        h = hashlib.sha256()
        with path.open("rb") as f:
            for chunk in iter(lambda: f.read(1024 * 1024), b""):
                h.update(chunk)
        return h.hexdigest()

    def ensure_artifact(self, art: DependencyArtifact) -> Path | None:
        if art.local_path is not None and art.local_path.is_file():
            if art.sha256_expected:
                got = self.sha256_file(art.local_path)
                if got.lower() != art.sha256_expected.lower():
                    raise ValueError(
                        f"SHA256 mismatch for {art.name}: expected {art.sha256_expected} got {got}",
                    )
            return art.local_path.resolve()
        if art.url:
            dest = self.cache_dir / art.name
            if dest.is_file() and art.sha256_expected:
                if self.sha256_file(dest).lower() == art.sha256_expected.lower():
                    return dest.resolve()
            self._download(art.url, dest)
            if art.sha256_expected:
                got = self.sha256_file(dest)
                if got.lower() != art.sha256_expected.lower():
                    dest.unlink(missing_ok=True)
                    raise ValueError(
                        f"Downloaded SHA256 mismatch for {art.name}: expected "
                        f"{art.sha256_expected} got {got}",
                    )
            return dest.resolve()
        return None

    def _download(self, url: str, dest: Path) -> None:
        req = urllib.request.Request(
            url,
            headers={
                "User-Agent": "RetroPortalManifestResolver/0.1 (+https://example.invalid)",
            },
        )
        try:
            with urllib.request.urlopen(req, timeout=120) as resp:
                dest.write_bytes(resp.read())
        except urllib.error.URLError as exc:
            raise RuntimeError(f"download failed for {url}: {exc}") from exc

    def resolve_manifest(self, manifest: DependencyManifest) -> dict[str, Any]:
        resolved: dict[str, Any] = {"title": manifest.title, "artifacts": []}
        for dep in manifest.dependencies:
            path = self.ensure_artifact(dep)
            resolved["artifacts"].append(
                {
                    "name": dep.name,
                    "resolved_path": str(path) if path else None,
                    "notes": dep.notes,
                },
            )
        return resolved

    def report_json(self, manifest_path: Path) -> str:
        mf = DependencyManifest.from_yaml(manifest_path)
        data = self.resolve_manifest(mf)
        return json.dumps(data, indent=2)


def optional_playwright_fetch(
    allowed_host: str,
    url: str,
    dest: Path,
    *,
    headless: bool = True,
) -> None:
    """Fetch a single URL inside a hardened browser context (allowed_host enforced)."""
    try:
        from playwright.sync_api import sync_playwright
    except ImportError as exc:
        raise RuntimeError(
            "Playwright not installed; pip install playwright && playwright install chromium",
        ) from exc

    from urllib.parse import urlparse

    host = urlparse(url).hostname or ""
    if host != allowed_host:
        raise ValueError(f"Host {host!r} not in explicit allowlist ({allowed_host!r})")

    with sync_playwright() as pw:
        browser = pw.chromium.launch(headless=headless)
        context = browser.new_context(
            java_script_enabled=True,
            user_agent="RetroPortalFetcher/0.1",
        )
        page = context.new_page()
        page.goto(url, wait_until="networkidle", timeout=120_000)
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(page.content().encode("utf-8"))
        context.close()
        browser.close()
