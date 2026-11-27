"""Verified ZIP installs and version switching for the Linux development host."""

from __future__ import annotations

from contextlib import closing, contextmanager
import fcntl
import hashlib
import hmac
import http.client
import ipaddress
import json
import os
from pathlib import Path
import re
import shutil
import socket
import ssl
import stat
import struct
import tempfile
import time
from urllib.error import HTTPError, URLError
from urllib.parse import urljoin, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener, getproxies
from uuid import uuid4
import zipfile
import zlib


MAX_ARCHIVE_BYTES = 256 * 1024 * 1024
MAX_INSTALLED_BYTES = 512 * 1024 * 1024
MAX_FILE_BYTES = 128 * 1024 * 1024
MAX_MEMBERS = 10_000
MAX_ZIP_DIRECTORY_BYTES = 4 * 1024 * 1024
BUFFER_SIZE = 64 * 1024
SOCKET_SECONDS = 15
TRANSFER_SECONDS = 300
DEFAULT_PROXY_HOSTS = frozenset({
    "github.com",
    "codeload.github.com",
    "release-assets.githubusercontent.com",
    "github-cloud.githubusercontent.com",
})
GAME_ID = re.compile(r"[a-z0-9][a-z0-9._-]{0,79}\Z")
VERSION_DIR = re.compile(r"version-[0-9a-f]{32}\Z")
SHA256 = re.compile(r"[a-fA-F0-9]{64}\Z")
HOSTNAME = re.compile(
    r"(?=.{1,253}\Z)(?:[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\.)+"
    r"[a-z0-9](?:[a-z0-9-]{0,61}[a-z0-9])?\Z"
)


class InstallError(ValueError):
    """An install could not be prepared or its local state could not be read."""


def _game_id(value):
    if not isinstance(value, str) or not GAME_ID.fullmatch(value):
        raise InstallError("Invalid game ID")
    return value


def _relative_path(value):
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 512
        or any(ord(character) < 32 for character in value)
        or "\\" in value
        or ":" in value
        or any(part in {"", ".", ".."} for part in value.split("/"))
    ):
        raise InstallError("Unsafe package path")
    return Path(*value.split("/"))


def _manifest(game, *, source_required):
    if not isinstance(game, dict):
        raise InstallError("Package metadata must be an object")
    _game_id(game.get("id"))
    for field, maximum in {"title": 250, "version": 80, "license": 250}.items():
        value = game.get(field)
        if not isinstance(value, str) or not value.strip() or len(value) > maximum:
            raise InstallError(f"Invalid package {field}")
    if game.get("runtime") != "html5" or game.get("architecture") != "any":
        raise InstallError("Only platform-independent HTML5 packages are supported")
    if _relative_path(game.get("entrypoint")).suffix.lower() not in {".html", ".htm"}:
        raise InstallError("The package entry point must be an HTML file")
    digest = game.get("sha256")
    if not isinstance(digest, str) or not SHA256.fullmatch(digest):
        raise InstallError("A valid SHA-256 hash is required")
    if source_required and not isinstance(game.get("archive_url"), str):
        raise InstallError("A package download URL is required")


class _NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, request, fp, code, message, headers, newurl):
        return None


class _DirectHTTPS(http.client.HTTPSConnection):
    def __init__(self, host, port, address):
        super().__init__(host, port, timeout=SOCKET_SECONDS, context=ssl.create_default_context())
        self.address = address

    def connect(self):
        # Bind the verified DNS result while retaining hostname and certificate checks.
        connection = socket.create_connection((self.address, self.port), self.timeout)
        try:
            self.sock = self._context.wrap_socket(connection, server_hostname=self.host)
        except BaseException:
            connection.close()
            raise


def _download_location(url, *, allow_loopback_http):
    if not isinstance(url, str) or not 0 < len(url) <= 4096:
        raise InstallError("Invalid package download URL")
    if not url.isascii() or any(ord(character) <= 32 or ord(character) == 127 for character in url):
        raise InstallError("Invalid package download URL")
    if "\\" in url:
        raise InstallError("Invalid package download URL")
    try:
        parts = urlsplit(url)
        host = parts.hostname
        port = parts.port
    except ValueError as error:
        raise InstallError("Invalid package download URL") from error
    if not host or parts.username is not None or parts.password is not None or parts.fragment:
        raise InstallError("Invalid package download URL")
    if allow_loopback_http and parts.scheme == "http" and host == "127.0.0.1" and port:
        return parts, port
    if parts.scheme != "https" or port not in {None, 443} or not HOSTNAME.fullmatch(host):
        raise InstallError("Package URLs require HTTPS on a public DNS hostname")
    return parts, 443


def _public_address(host, port):
    addresses = [
        item[4][0]
        for item in socket.getaddrinfo(host, port, type=socket.SOCK_STREAM)
    ]
    if not addresses:
        raise InstallError("Package host has no network addresses")
    for address in addresses:
        ip = ipaddress.ip_address(address)
        if ip.version == 6 and ip.ipv4_mapped is not None:
            ip = ip.ipv4_mapped
        if not ip.is_global or ip.is_multicast or ip.is_reserved or ip.is_loopback:
            raise InstallError("Package downloads require public network addresses")
    return addresses[0]


def _content_length(response):
    value = response.headers.get("Content-Length")
    if value is None:
        return None
    if not re.fullmatch(r"[0-9]{1,12}", value):
        raise InstallError("Invalid download length")
    return int(value)


def _copy_package(source, destination, expected_size, progress):
    if expected_size is not None and not 0 < expected_size <= MAX_ARCHIVE_BYTES:
        raise InstallError("Package download is empty or exceeds the size limit")
    if expected_size and shutil.disk_usage(destination.parent).free < expected_size:
        raise InstallError("Insufficient space to download the package")
    digest = hashlib.sha256()
    deadline = time.monotonic() + TRANSFER_SECONDS
    downloaded = 0
    if progress:
        progress(downloaded, expected_size)
    with destination.open("xb") as output:
        while block := source.read1(BUFFER_SIZE):
            downloaded += len(block)
            if downloaded > MAX_ARCHIVE_BYTES or time.monotonic() > deadline:
                raise InstallError("Package transfer exceeded the size or time limit")
            output.write(block)
            digest.update(block)
            if progress:
                progress(downloaded, expected_size)
    if downloaded == 0 or (expected_size is not None and downloaded != expected_size):
        raise InstallError("Package transfer was incomplete")
    return digest.hexdigest()


def check_zip_directory(archive, *, max_members=MAX_MEMBERS):
    # Bound the allocations ZipFile makes while reading the central directory.
    # Within these package size limits, single-disk archives need no ZIP64 EOCD.
    footer = struct.Struct("<4s4H2IH")
    archive_size = archive.stat().st_size
    tail_size = min(archive_size, footer.size + 65535)
    with archive.open("rb") as source:
        source.seek(-tail_size, os.SEEK_END)
        tail = source.read()
    footer_at = tail.rfind(b"PK\x05\x06")
    if footer_at < 0 or len(tail) - footer_at < footer.size:
        raise InstallError("Invalid ZIP central directory")
    _, disk, directory_disk, disk_entries, entries, size, start, comment = footer.unpack_from(tail, footer_at)
    if (
        disk != 0
        or directory_disk != 0
        or entries != disk_entries
        or not 0 < entries <= max_members
        or not 0 < size <= MAX_ZIP_DIRECTORY_BYTES
        or start + size != archive_size - tail_size + footer_at
        or footer_at + footer.size + comment != len(tail)
    ):
        raise InstallError("Unsupported ZIP central directory")


def _extract(archive, directory):
    check_zip_directory(archive)
    with zipfile.ZipFile(archive) as package:
        members = package.infolist()
        if not members or len(members) > MAX_MEMBERS:
            raise InstallError("ZIP member count exceeds the package limit")
        expanded_size = sum(member.file_size for member in members)
        if expanded_size > MAX_INSTALLED_BYTES:
            raise InstallError("ZIP contents exceed the extracted size limit")
        if shutil.disk_usage(directory).free < expanded_size:
            raise InstallError("Insufficient space to extract the package")
        seen = set()
        for member in members:
            raw_name = member.orig_filename.removesuffix("/") if member.is_dir() else member.orig_filename
            relative = _relative_path(raw_name)
            portable_name = relative.as_posix().casefold()
            if portable_name in seen:
                raise InstallError("ZIP contains duplicate paths")
            seen.add(portable_name)
            mode = stat.S_IFMT(member.external_attr >> 16)
            if mode not in {0, stat.S_IFDIR if member.is_dir() else stat.S_IFREG}:
                raise InstallError("ZIP links and special files are unsupported")
            if member.flag_bits & 1 or member.compress_type not in {zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED}:
                raise InstallError("Unsupported ZIP encryption or compression")
            if member.file_size > MAX_FILE_BYTES:
                raise InstallError("ZIP member exceeds the file size limit")
            output = directory / relative
            if member.is_dir():
                output.mkdir(parents=True, exist_ok=True)
                continue
            output.parent.mkdir(parents=True, exist_ok=True)
            with package.open(member) as source, output.open("xb") as target:
                shutil.copyfileobj(source, target, BUFFER_SIZE)


class Installer:
    """Install verified packages into private versioned directories beneath root.

    A single active record switches versions atomically. Calls that mutate an
    installation are serialized across processes for that game.
    """

    def __init__(self, root: Path, *, allow_loopback_http=False, proxy_hosts=DEFAULT_PROXY_HOSTS):
        self.root = Path(root).resolve()
        self.root.mkdir(parents=True, exist_ok=True, mode=0o700)
        self._locks = self.root / ".locks"
        self._locks.mkdir(exist_ok=True, mode=0o700)
        self._allow_loopback_http = allow_loopback_http
        self._proxy_hosts = frozenset(proxy_hosts)

    @contextmanager
    def _locked(self, game_id):
        with (self._locks / game_id).open("ab") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            yield

    @contextmanager
    def _response(self, url):
        parts, port = _download_location(url, allow_loopback_http=self._allow_loopback_http)
        headers = {"User-Agent": "Akeno-Gaming-Hub/0.1", "Accept-Encoding": "identity"}
        if parts.scheme == "https" and parts.hostname in self._proxy_hosts and getproxies().get("https"):
            # Approved hosts can use the deployment's trusted, TLS-verified egress.
            opener = build_opener(_NoRedirects())
            try:
                response = opener.open(Request(url, headers=headers), timeout=SOCKET_SECONDS)
            except HTTPError as error:
                response = error
            with closing(response):
                yield response
            return

        if parts.scheme == "http":
            connection = http.client.HTTPConnection(parts.hostname, port, timeout=SOCKET_SECONDS)
        else:
            connection = _DirectHTTPS(parts.hostname, port, _public_address(parts.hostname, port))
        target = parts.path or "/"
        if parts.query:
            target += "?" + parts.query
        with closing(connection):
            connection.request("GET", target, headers=headers)
            with closing(connection.getresponse()) as response:
                yield response

    def _download(self, url, archive, progress):
        for _ in range(6):
            with self._response(url) as response:
                if response.status == 200:
                    if response.headers.get("Content-Encoding", "identity").lower() != "identity":
                        raise InstallError("Unsupported package transfer encoding")
                    return _copy_package(response, archive, _content_length(response), progress)
                if response.status not in {301, 302, 303, 307, 308}:
                    raise InstallError(f"Package download returned HTTP {response.status}")
                location = response.headers.get("Location")
                if not location:
                    raise InstallError("Package download redirect has no destination")
                next_url = urljoin(url, location)
                if url.startswith("https:") and not next_url.startswith("https:"):
                    raise InstallError("HTTPS package downloads cannot be downgraded")
                url = next_url
        raise InstallError("Package download exceeded the redirect limit")

    def _read_record(self, game_id):
        record_path = self.root / _game_id(game_id) / "active.json"
        try:
            with record_path.open(encoding="utf-8") as stream:
                record = json.load(stream)
        except FileNotFoundError:
            return None
        except (OSError, ValueError) as error:
            raise InstallError("Cannot read local package metadata") from error
        if (
            not isinstance(record, dict)
            or not isinstance(record.get("directory"), str)
            or not VERSION_DIR.fullmatch(record["directory"])
            or not isinstance(record.get("game"), dict)
            or record["game"].get("id") != game_id
        ):
            raise InstallError("Invalid local package metadata")
        return record

    def install(self, game: dict, progress=None, *, local_archive: Path | None = None) -> dict:
        _manifest(game, source_required=local_archive is None)
        game_id = game["id"]
        game_directory = self.root / game_id
        try:
            with tempfile.TemporaryDirectory(prefix=".install-", dir=self.root) as staging_name:
                staging = Path(staging_name)
                archive = staging / "archive.zip"
                content = staging / "content"
                content.mkdir()
                if local_archive is None:
                    actual_hash = self._download(game["archive_url"], archive, progress)
                else:
                    with Path(local_archive).open("rb") as source:
                        file_status = os.fstat(source.fileno())
                        if not stat.S_ISREG(file_status.st_mode):
                            raise InstallError("Local package must be a regular file")
                        actual_hash = _copy_package(source, archive, file_status.st_size, progress)

                if not hmac.compare_digest(actual_hash, game["sha256"].lower()):
                    raise InstallError("Package SHA-256 does not match")
                _extract(archive, content)
                if not (content / _relative_path(game["entrypoint"])).is_file():
                    raise InstallError("Package entry point is missing")

                installed = dict(game)
                version_directory = "version-" + uuid4().hex
                with self._locked(game_id):
                    game_directory.mkdir(mode=0o700, exist_ok=True)
                    record = self._read_record(game_id)
                    next_record = {"directory": version_directory, "game": installed}
                    descriptor = staging / "active.json"
                    with descriptor.open("x", encoding="utf-8") as stream:
                        json.dump(next_record, stream, ensure_ascii=False)
                        stream.flush()
                        os.fsync(stream.fileno())
                    installed_path = game_directory / version_directory
                    content.rename(installed_path)
                    try:
                        descriptor.replace(game_directory / "active.json")
                    except OSError:
                        shutil.rmtree(installed_path, ignore_errors=True)
                        raise
                    if record is not None:
                        shutil.rmtree(game_directory / record["directory"], ignore_errors=True)
                return installed
        except (
            OSError, URLError, http.client.HTTPException, zipfile.BadZipFile,
            RuntimeError, UnicodeError, zlib.error,
        ) as error:
            raise InstallError("Could not download, unpack or publish the package") from error

    def list_installed(self) -> list[dict]:
        installed = []
        for directory in sorted(self.root.iterdir()):
            if not directory.is_dir() or not GAME_ID.fullmatch(directory.name):
                continue
            record = self._read_record(directory.name)
            if record is not None:
                installed.append(record["game"])
        return installed

    def uninstall(self, game_id: str) -> bool:
        game_directory = self.root / _game_id(game_id)
        with self._locked(game_id):
            if self._read_record(game_id) is None:
                return False
            trash = self.root / (".uninstall-" + uuid4().hex)
            game_directory.rename(trash)
        shutil.rmtree(trash, ignore_errors=True)
        return True

    def _installed_file(self, game_id, record, relative_path):
        if record is None:
            raise InstallError("Game is not installed")
        directory = self.root / game_id / record["directory"]
        path = (directory / _relative_path(relative_path)).resolve()
        if not path.is_relative_to(directory) or not path.is_file():
            raise InstallError("Installed file is missing")
        return path

    def installed_file(self, game_id: str, relative_path: str) -> Path:
        return self._installed_file(game_id, self._read_record(game_id), relative_path)

    def entrypoint(self, game_id: str) -> Path:
        record = self._read_record(game_id)
        if record is None:
            raise InstallError("Game is not installed")
        return self._installed_file(game_id, record, record["game"].get("entrypoint"))
