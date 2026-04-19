from __future__ import annotations

import argparse
import json
from pathlib import Path

from retroportal.hunter.manifest_resolver import (
    DependencyManifest,
    ManifestResolver,
    optional_playwright_fetch,
)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Resolve dependency manifests into cache (no bot bypass).",
    )
    parser.add_argument("manifest", type=Path, help="YAML manifest path")
    parser.add_argument(
        "--cache",
        type=Path,
        default=Path.home() / ".retroportal" / "deps_cache",
        help="Download cache directory",
    )
    parser.add_argument(
        "--playwright-url",
        help="Optional single URL fetch via Playwright (must match --allowed-host)",
    )
    parser.add_argument(
        "--allowed-host",
        help="Hostname allowlist entry for Playwright fetch",
    )
    args = parser.parse_args(argv)

    resolver = ManifestResolver(args.cache)
    mf = DependencyManifest.from_yaml(args.manifest)
    data = resolver.resolve_manifest(mf)
    print(json.dumps(data, indent=2))

    if args.playwright_url and args.allowed_host:
        dest = args.cache / "playwright_snapshot.html"
        optional_playwright_fetch(args.allowed_host, args.playwright_url, dest)
        print(json.dumps({"playwright_snapshot": str(dest)}, indent=2))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
