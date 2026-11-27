"""Validate a local catalog with the same rules used by the hub."""

import argparse
from pathlib import Path

from .providers import CatalogError, ManifestProvider


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path, help="Path to a catalog JSON file")
    args = parser.parse_args()
    try:
        games = ManifestProvider(args.catalog).list_games()
    except CatalogError as error:
        parser.error(str(error))
    print(f"Valid catalog. {len(games)} visible game(s).")


if __name__ == "__main__":
    main()
