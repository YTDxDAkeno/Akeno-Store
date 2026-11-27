"""Command-line inspection without starting applications."""

import argparse
from dataclasses import asdict
import json
from pathlib import Path

from compatibility.inspection import RuntimeCapability, inspect_path, resolve_runtime


def main():
    parser = argparse.ArgumentParser(description="Identify game files and resolve a compatible runtime")
    parser.add_argument("path", type=Path, help="Local file or game directory")
    parser.add_argument("--entrypoint", help="Known relative entry point in a game directory")
    parser.add_argument("--platform", choices=("host", "ps5"), default="host")
    args = parser.parse_args()
    capabilities = (RuntimeCapability("html5", "host", ("HTML5",), version=1),)
    inspection = inspect_path(args.path, entrypoint=args.entrypoint)
    resolution = resolve_runtime(inspection.primary, capabilities, platform=args.platform)
    print(json.dumps(asdict(inspection) | {"runtime": asdict(resolution)}, ensure_ascii=False, indent=2))
    return 0 if inspection.primary is not None else 1


if __name__ == "__main__":
    raise SystemExit(main())
