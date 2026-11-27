"""Shared catalog and library options for the server and native catalog exporter."""

import os
from pathlib import Path

from stores import (
    GITHUB_PC_PROJECTS,
    GITHUB_PS5_PROJECTS,
    GitHubProvider,
    ItchProvider,
    ManifestProvider,
    ProsperoStoreProvider,
)


def add_catalog_arguments(parser):
    default_data = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    parser.add_argument("--data-dir", type=Path, default=default_data / "akeno-hub")
    parser.add_argument("--offline", action="store_true", help="Öffentliche Quellen deaktivieren")
    parser.add_argument("--manifest", action="append", default=[], type=Path)
    parser.add_argument("--catalog-url", action="append", default=[])


def providers_for(args):
    providers = []
    if not args.offline:
        providers.extend([
            ("GitHub", GitHubProvider(GITHUB_PC_PROJECTS)),
            ("PS5-Projekte", GitHubProvider(GITHUB_PS5_PROJECTS)),
            ("ProsperoStore", ProsperoStoreProvider()),
            ("itch.io", ItchProvider()),
        ])
    providers.extend(
        (f"Manifest {index}", ManifestProvider(path))
        for index, path in enumerate(args.manifest, 1)
    )
    providers.extend(
        (f"Repository {index}", ManifestProvider.from_url(url))
        for index, url in enumerate(args.catalog_url, 1)
    )
    return providers
