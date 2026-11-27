"""Read catalog metadata without granting permission to download or launch games."""

import hashlib
import ipaddress
import json
import re
import socket
from collections.abc import Sequence
from datetime import datetime, timezone
from email.utils import parsedate_to_datetime
from pathlib import Path, PureWindowsPath
from urllib.error import HTTPError, URLError
from urllib.parse import unquote, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener
from xml.etree import ElementTree

from .projects import GitHubProject

MAX_CATALOG_BYTES = 2 * 1024 * 1024
MAX_GAMES = 200
GAME_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}\Z")
REPOSITORY = re.compile(r"[a-zA-Z0-9][a-zA-Z0-9-]{0,38}/[a-zA-Z0-9][a-zA-Z0-9_.-]{0,99}\Z")
HOST_LABEL = re.compile(r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\Z")
SHA256 = re.compile(r"[0-9a-fA-F]{64}\Z")
ATOM_NAMESPACE = {"atom": "http://www.w3.org/2005/Atom"}
ITCH_FEED = "https://itch.io/games/free.xml"
RESTRICTED = re.compile(r"(?:\b(?:nsfw|adult|mature|explicit|erotic|pornographic)\b|\b18\+)", re.IGNORECASE)
REQUIRED_GAME_FIELDS = {
    "id", "title", "version", "runtime", "entrypoint", "archive_url", "sha256",
    "license", "architecture",
}
OPTIONAL_GAME_FIELDS = {
    "description", "source_url", "age_rating", "cover_url", "screenshots", "genre", "developer",
}


def _reserved_windows_name(segment: str) -> bool:
    stem = segment.split(".")[0].rstrip(" ").upper()
    return stem in {"CON", "PRN", "AUX", "NUL"} or (len(stem) == 4 and stem[:3] in {"COM", "LPT"} and stem[3] in "123456789")


class CatalogError(ValueError):
    """A feed could not be retrieved or validated."""


def _https_url(value: object) -> str:
    if (
        not isinstance(value, str)
        or len(value) > 2048
        or not value.isascii()
        or any(character.isspace() or ord(character) < 32 or ord(character) == 127 for character in value)
        or "\\" in value
    ):
        raise CatalogError("HTTPS URL is malformed")
    try:
        parsed = urlsplit(value)
        host = parsed.hostname or ""
        port = parsed.port
    except ValueError as error:
        raise CatalogError("HTTPS URL is malformed") from error
    if (
        parsed.scheme != "https"
        or parsed.username is not None
        or port not in (None, 443)
        or parsed.fragment
        or len(host) > 253
        or "." not in host
        or not all(HOST_LABEL.fullmatch(label) for label in host.split("."))
        or host.endswith((".localhost", ".local", ".internal", ".home.arpa"))
        or host.split(".")[-1].isdigit()
    ):
        raise CatalogError("URL must use HTTPS with a public DNS host")
    return value


class _SameHostRedirect(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, new_url):
        _https_url(new_url)
        if urlsplit(request.full_url).hostname != urlsplit(new_url).hostname:
            raise CatalogError("Cross-host catalog redirect was rejected")
        return super().redirect_request(request, fp, code, message, headers, new_url)


def _bounded_read(stream) -> bytes:
    body = stream.read(MAX_CATALOG_BYTES + 1)
    if len(body) > MAX_CATALOG_BYTES:
        raise CatalogError("Catalog exceeds the size limit")
    return body


def _fetch(url: str, accept: str) -> bytes:
    _https_url(url)
    request = Request(url, headers={"Accept": accept, "User-Agent": "Akeno-Gaming-Hub/0.1"})
    try:
        with build_opener(_SameHostRedirect()).open(request, timeout=10) as response:
            return _bounded_read(response)
    except HTTPError as error:
        raise CatalogError(f"Catalog request failed with HTTP {error.code}") from error
    except (URLError, OSError) as error:
        host = urlsplit(url).hostname
        proxy_status = re.search(r"Tunnel connection failed: (\d{3})", str(error))
        if proxy_status:
            raise CatalogError(f"Catalog proxy rejected {host} with HTTP {proxy_status[1]}") from error
        raise CatalogError(f"Catalog host unavailable: {host}") from error


def _xml(body: bytes) -> ElementTree.Element:
    try:
        source = body.decode("utf-8-sig")
        if re.search(r"<!\s*(?:DOCTYPE|ENTITY)\b", source, re.IGNORECASE):
            raise CatalogError("DTD and entity declarations are forbidden")
        return ElementTree.fromstring(source)
    except (UnicodeError, ElementTree.ParseError) as error:
        raise CatalogError("Invalid XML catalog") from error


def _text(value: object, field: str, limit: int) -> str:
    if (
        not isinstance(value, str)
        or not value.strip()
        or len(value) > limit
        or any(
            (ord(character) < 32 and character not in "\n\t")
            or 0xD800 <= ord(character) <= 0xDFFF
            for character in value
        )
    ):
        raise CatalogError(f"Invalid {field}")
    return value.strip()


def _timestamp(value: str | None, decode) -> str | None:
    if not value or len(value) > 100:
        return None
    try:
        parsed = decode(value)
        if parsed.tzinfo is not None:
            return parsed.astimezone(timezone.utc).isoformat().replace("+00:00", "Z")
    except (TypeError, ValueError, OverflowError):
        return None
    return None


class GitHubProvider:
    """List the latest published release for each explicitly configured repository."""

    def __init__(self, repositories: Sequence[str | GitHubProject]):
        if isinstance(repositories, str):
            raise CatalogError("Supply GitHub repositories as a sequence of owner/name values")
        if len(repositories) > 20:
            raise CatalogError("Supply up to 20 GitHub repositories in owner/name form")
        unique = {}
        for project in repositories:
            repo = project.repository if isinstance(project, GitHubProject) else project
            if not isinstance(repo, str) or not REPOSITORY.fullmatch(repo):
                raise CatalogError("Supply GitHub repositories in owner/name form")
            unique.setdefault(repo.lower(), (repo, project))
        self.repositories = tuple(unique.values())

    def list_games(self) -> list[dict]:
        games = []
        for repository, project in self.repositories:
            canonical = repository.lower()
            release_base = f"https://github.com/{canonical}/releases"
            feed = _xml(_fetch(f"{release_base}.atom", "application/atom+xml"))
            if feed.tag != f"{{{ATOM_NAMESPACE['atom']}}}feed":
                raise CatalogError("Expected a GitHub Atom feed")
            entry = feed.find("atom:entry", ATOM_NAMESPACE)
            if entry is None:
                continue
            release_title = _text(entry.findtext("atom:title", namespaces=ATOM_NAMESPACE), "release title", 200)
            if RESTRICTED.search(release_title):
                continue
            source_url, tag = self._release_link(entry, release_base)
            owner, repository_name = repository.split("/")
            digest = hashlib.sha256(canonical.encode("ascii")).hexdigest()[:12]
            game = {
                "id": f"github-{repository_name.lower()[:50]}-{digest}",
                "title": repository_name,
                "version": tag or release_title,
                "release_title": release_title,
                "source_url": source_url,
                "provider": "github",
                "developer": owner,
                "age_rating": "unrated",
                "repository": repository,
            }
            for label, field in (("published", "published_at"), ("updated", "updated_at")):
                date = _timestamp(
                    entry.findtext(f"atom:{label}", namespaces=ATOM_NAMESPACE),
                    datetime.fromisoformat,
                )
                if date:
                    game[field] = date
            if isinstance(project, GitHubProject):
                game.update(
                    title=project.title, genre=project.genre, description=project.description,
                    license=project.license, target_platform=project.target_platform,
                    age_rating=project.age_rating,
                )
            games.append(game)
        return games

    @staticmethod
    def _release_link(entry: ElementTree.Element, release_base: str) -> tuple[str, str | None]:
        release_link = entry.find("atom:link[@rel='alternate']", ATOM_NAMESPACE)
        if release_link is not None:
            candidate = release_link.get("href", "")
            prefix = f"{release_base}/tag/"
            if candidate.casefold().startswith(prefix):
                try:
                    _https_url(candidate)
                    parts = urlsplit(candidate)
                    tag = unquote(parts.path[len(urlsplit(prefix).path):], errors="strict")
                    if (
                        not parts.query and "\\" not in tag
                        and all(segment not in ("", ".", "..") for segment in tag.split("/"))
                    ):
                        return candidate, _text(tag, "release tag", 200)
                except (CatalogError, UnicodeError):
                    pass
        return release_base, None


class ItchProvider:
    """Discover free titles through itch.io's public feed."""

    def list_games(self) -> list[dict]:
        root = _xml(_fetch(ITCH_FEED, "application/rss+xml, application/xml, text/xml"))
        channel = root.find("channel")
        if root.tag != "rss" or channel is None:
            raise CatalogError("Expected an itch.io RSS channel")
        games = {}
        for item in channel.findall("item")[:MAX_GAMES]:
            try:
                game = self._game(item)
            except CatalogError:
                continue
            if game:
                games[game["id"]] = game
        return list(games.values())

    @staticmethod
    def _game(item: ElementTree.Element) -> dict | None:
        categories = [
            category.text.strip()
            for category in item.findall("category")
            if category.text and 0 < len(category.text.strip()) <= 60
        ]
        title = _text(item.findtext("title"), "title", 160)
        searchable = " ".join([title, *categories, item.findtext("description") or ""])
        if RESTRICTED.search(searchable):
            return None
        link = _https_url(item.findtext("link"))
        parsed = urlsplit(link)
        domain = (parsed.hostname or "").split(".")
        path = parsed.path.strip("/")
        if len(domain) != 3 or domain[1:] != ["itch", "io"] or not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", path):
            return None
        canonical = f"https://{domain[0]}.itch.io/{path}"
        digest = hashlib.sha256(canonical.encode("ascii")).hexdigest()[:12]
        game = {
            "id": f"itch-{path[:53]}-{digest}",
            "title": title,
            "source_url": canonical,
            "provider": "itch",
            "developer": domain[0],
            "age_rating": "unrated",
            "price_kind": "listed-free",
        }
        if categories:
            game["categories"] = list(dict.fromkeys(categories))[:8]
        published = _timestamp(item.findtext("pubDate"), parsedate_to_datetime)
        if published:
            game["published_at"] = published
        return game


def _unique_object(pairs: list[tuple]) -> dict:
    result = {}
    for key, value in pairs:
        if key in result:
            raise CatalogError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _manifest_game(entry: object) -> dict:
    if not isinstance(entry, dict) or not REQUIRED_GAME_FIELDS <= entry.keys():
        raise CatalogError("A game is missing required manifest fields")
    if entry.keys() - REQUIRED_GAME_FIELDS - OPTIONAL_GAME_FIELDS:
        raise CatalogError("A game has unsupported manifest fields")

    game = {field: _text(entry[field], field, 160) for field in ("title", "license")}
    game_id = _text(entry["id"], "game ID", 80)
    if game_id != entry["id"] or not GAME_ID.fullmatch(game_id):
        raise CatalogError("Invalid game ID")
    game["id"] = game_id
    game["version"] = _text(entry["version"], "version", 80)
    if entry["runtime"] != "html5" or entry["architecture"] != "any":
        raise CatalogError("Only html5/any game manifests are supported")

    entrypoint = _text(entry["entrypoint"], "entry point", 512)
    if (
        entrypoint != entry["entrypoint"]
        or "\\" in entrypoint
        or any(ord(character) < 32 for character in entrypoint)
        or any(
            segment in ("", ".", "..") or any(character in '<>:"|?*' for character in segment)
            or _reserved_windows_name(segment)
            for segment in entrypoint.split("/")
        )
        or not entrypoint.lower().endswith((".htm", ".html"))
    ):
        raise CatalogError("Entry point must be a relative HTML path")
    game["entrypoint"] = entrypoint
    game["archive_url"] = _https_url(entry["archive_url"])
    digest = _text(entry["sha256"], "SHA-256", 64)
    if not SHA256.fullmatch(digest):
        raise CatalogError("Invalid archive SHA-256")
    game["sha256"] = digest.lower()
    game["runtime"] = "html5"
    game["architecture"] = "any"
    rating = entry.get("age_rating", "unrated")
    if rating not in ("all", "teen", "unrated", "restricted"):
        raise CatalogError("Unknown content rating")
    game["age_rating"] = rating
    for field, limit in (("description", 2000), ("genre", 60), ("developer", 160)):
        if field in entry:
            game[field] = _text(entry[field], field, limit)
    if "source_url" in entry:
        game["source_url"] = _https_url(entry["source_url"])
    media = {}
    if "cover_url" in entry:
        media["cover_url"] = _https_url(entry["cover_url"])
    if "screenshots" in entry:
        screenshots = entry["screenshots"]
        if not isinstance(screenshots, list) or len(screenshots) > 6:
            raise CatalogError("A game may include up to six screenshots")
        media["screenshots"] = [_https_url(screenshot) for screenshot in screenshots]
    if rating in ("all", "teen"):
        game.update(media)
    game["provider"] = "manifest"
    return game


def _json(body: bytes) -> object:
    try:
        return json.loads(body, object_pairs_hook=_unique_object)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as error:
        raise CatalogError("Invalid JSON catalog") from error


def _manifest_games(body: bytes) -> list[dict]:
    manifest = _json(body)
    if (
        not isinstance(manifest, dict)
        or manifest.keys() != {"schema_version", "games"}
        or type(manifest["schema_version"]) is not int
        or manifest["schema_version"] != 1
    ):
        raise CatalogError("Expected a version 1 game catalog")
    entries = manifest["games"]
    if not isinstance(entries, list) or len(entries) > MAX_GAMES:
        raise CatalogError(f"Catalog must contain up to {MAX_GAMES} games")
    games = [_manifest_game(entry) for entry in entries]
    if len({game["id"] for game in games}) != len(games):
        raise CatalogError("Duplicate game ID")
    return [game for game in games if game["age_rating"] != "restricted"]


class ManifestProvider:
    """Read an operator-vetted local manifest or an operator-configured HTTPS URL."""

    def __init__(self, path: Path):
        self.path: Path | None = Path(path)
        self.url: str | None = None

    @classmethod
    def from_url(cls, url: str) -> "ManifestProvider":
        """Configure a trusted remote host. Do not expose this method to web clients."""
        provider = cls(Path("."))
        provider.path = None
        provider.url = _https_url(url)
        return provider

    def list_games(self) -> list[dict]:
        if self.url is not None:
            host = urlsplit(self.url).hostname
            try:
                addresses = socket.getaddrinfo(host, 443, type=socket.SOCK_STREAM)
            except OSError as error:
                raise CatalogError("Cannot resolve the configured catalog host") from error
            public_addresses = [ipaddress.ip_address(address[4][0]) for address in addresses]
            if not public_addresses or any(
                not address.is_global or address.is_multicast or address.is_reserved
                for address in public_addresses
            ):
                raise CatalogError("Catalog host must resolve only to public addresses")
            body = _fetch(self.url, "application/json")
        else:
            assert self.path is not None
            try:
                with self.path.open("rb") as catalog:
                    body = _bounded_read(catalog)
            except OSError as error:
                raise CatalogError("Cannot read the configured local catalog") from error
        return _manifest_games(body)
