"""Catalog providers for configured repositories and public release feeds."""

from .projects import GITHUB_PC_PROJECTS, GITHUB_PS5_PROJECTS, GitHubProject
from .prospero import ProsperoStoreProvider
from .providers import CatalogError, GitHubProvider, ItchProvider, ManifestProvider

__all__ = [
    "CatalogError", "GITHUB_PC_PROJECTS", "GITHUB_PS5_PROJECTS", "GitHubProject",
    "GitHubProvider", "ItchProvider", "ManifestProvider", "ProsperoStoreProvider",
]
