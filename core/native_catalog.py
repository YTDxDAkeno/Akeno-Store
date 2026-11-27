"""Export the Hub's catalog and installed library for the native application."""

import argparse
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile

from core.config import add_catalog_arguments, providers_for
from core.service import Hub


# Match Game in apps/ps5-hub/native/catalog.h. Limits exclude the NUL terminator.
MAX_ENTRIES = 1024
FIELDS = (("id", 95), ("title", 255), ("provider", 95), ("version", 95))


def _field(value, max_bytes=None) -> str:
    cleaned = "".join(" " if ord(char) < 32 or ord(char) == 127 else char for char in str(value or ""))
    if max_bytes is None:
        return cleaned
    return cleaned.encode("utf-8")[:max_bytes].decode("utf-8", errors="ignore")


def write_catalog(path: Path, entries: list[dict]):
    with NamedTemporaryFile(mode="w", encoding="utf-8", newline="\n", dir=path.parent, delete=False) as output:
        temporary_path = Path(output.name)
        try:
            for entry in entries[:MAX_ENTRIES]:
                provider = entry.get("provider") or "Lokal"
                if (platform := entry.get("target_platform")) in {"ps5", "pc"}:
                    provider = f"{provider} / {platform.upper()}"
                entry = entry | {"provider": provider}
                row = "\t".join(_field(entry.get(key), limit) for key, limit in FIELDS)
                output.write(row + "\n")
            output.close()
            temporary_path.replace(path)
        finally:
            temporary_path.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Katalog und Bibliothek für die native Oberfläche exportieren")
    add_catalog_arguments(parser)
    parser.add_argument("--output", type=Path, default=Path("dist/native/data"))
    args = parser.parse_args(argv)

    hub = Hub(args.data_dir.expanduser(), providers_for(args))
    try:
        hub.sync()
    finally:
        hub.close()
    catalog = hub.catalog()
    games = hub.library()["games"]
    args.output.mkdir(parents=True, exist_ok=True)
    truncated = False
    for name, entries in (("catalog", catalog["games"]), ("library", games)):
        write_catalog(args.output / f"{name}.tsv", entries)
        if len(entries) > MAX_ENTRIES:
            truncated = True
            print(f"{name}.tsv – Die native Ansicht enthält die ersten {MAX_ENTRIES} Einträge.", file=sys.stderr)
    for error in catalog["errors"]:
        print(f'{error["source"]} – {_field(error["message"])}', file=sys.stderr)
    print(f"{min(len(catalog['games']), MAX_ENTRIES)} Katalogeinträge, {min(len(games), MAX_ENTRIES)} installiert ({args.output})")
    return 1 if catalog["errors"] or truncated else 0


if __name__ == "__main__":
    raise SystemExit(main())
