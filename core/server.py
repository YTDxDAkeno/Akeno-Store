"""Local HTTP frontend and JSON API for the desktop development build."""

import argparse
import json
import mimetypes
import os
import shutil
import sys
from http import HTTPStatus
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, unquote, urlsplit

from core.bundled import PROJECT
from core.config import add_catalog_arguments, providers_for
from core.service import Hub
from runtimes.html5 import RUNTIME, game_policy, player

WEB = PROJECT / "apps" / "ps5-hub" / "web"
HUB_POLICY = (
    "default-src 'self'; object-src 'none'; base-uri 'none'; "
    "frame-ancestors 'none'; form-action 'none'; "
    "img-src 'self' https://raw.githubusercontent.com"
)
PLAYER_POLICY = HUB_POLICY + "; style-src 'unsafe-inline'; frame-src 'self'"
GAME_TYPES = {
    ".html", ".htm", ".js", ".mjs", ".css", ".json", ".txt", ".wasm", ".data", ".bin",
    ".png", ".gif", ".jpg", ".jpeg", ".webp", ".avif", ".svg", ".ico",
    ".ogg", ".oga", ".opus", ".wav", ".mp3", ".mp4", ".webm",
    ".woff", ".woff2", ".ttf",
}


class HubServer(ThreadingHTTPServer):
    def __init__(self, port: int, hub: Hub):
        super().__init__(("127.0.0.1", port), Handler)
        self.hub = hub
        actual_port = self.server_address[1]
        self.hosts = {f"127.0.0.1:{actual_port}", f"localhost:{actual_port}"}


class Handler(BaseHTTPRequestHandler):
    server: HubServer

    def log_message(self, format, *args):
        # Keep local diagnostics free of requested paths and query strings.
        pass

    def _origin_allowed(self, *, game_asset=False) -> bool:
        host = self.headers.get("Host")
        origin = self.headers.get("Origin")
        if host not in self.server.hosts:
            return False
        return origin in (None, f"http://{host}") or (game_asset and origin == "null")

    def _headers(self, status, mime, length, policy=HUB_POLICY):
        self.send_response(status)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(length))
        self.send_header("Content-Security-Policy", policy)
        self.send_header("X-Content-Type-Options", "nosniff")
        self.send_header("Referrer-Policy", "no-referrer")
        self.send_header("Cache-Control", "no-store")
        self.end_headers()

    def _json(self, value, status=HTTPStatus.OK):
        data = json.dumps(value, ensure_ascii=False).encode()
        self._headers(status, "application/json; charset=utf-8", len(data))
        self.wfile.write(data)

    def _error(self, status, message):
        self._json({"error": message}, status)

    def _file(self, path: Path, policy=HUB_POLICY):
        mime = mimetypes.guess_type(path)[0] or "application/octet-stream"
        try:
            stream = path.open("rb")
        except OSError:
            self._error(HTTPStatus.NOT_FOUND, "Datei nicht gefunden.")
            return
        with stream:
            self._headers(HTTPStatus.OK, mime, os.fstat(stream.fileno()).st_size, policy)
            shutil.copyfileobj(stream, self.wfile)

    def do_GET(self):
        url = urlsplit(self.path)
        path = unquote(url.path)
        if not self._origin_allowed(game_asset=path.startswith("/games/")):
            self._error(HTTPStatus.FORBIDDEN, "Nur lokale Anfragen sind zulässig.")
            return
        hub = self.server.hub
        read_routes = {
            "/api/catalog": hub.catalog,
            "/api/library": hub.library,
            "/api/status": hub.status,
            "/api/compatibility": hub.compatibility,
        }
        if path in read_routes:
            self._json(read_routes[path]())
        elif path == "/api/launch":
            self._launch(parse_qs(url.query).get("id", [""])[0])
        elif path == "/runtime/player.js":
            self._file(PROJECT / "runtimes" / "player.js")
        elif path.startswith("/games/"):
            self._game_file(path)
        else:
            self._static_file(path)

    def _launch(self, game_id):
        hub = self.server.hub
        game = next((value for value in hub.library()["games"] if value["id"] == game_id), None)
        if game is None:
            self._error(HTTPStatus.NOT_FOUND, "Spiel ist nicht installiert.")
            return
        try:
            hub.installer.entrypoint(game_id)
        except (KeyError, OSError, ValueError):
            self._error(HTTPStatus.CONFLICT, "Startdatei fehlt. Installiere das Spiel erneut.")
            return
        data = player(game["title"], game_id, game["entrypoint"])
        self._headers(HTTPStatus.OK, "text/html; charset=utf-8", len(data), PLAYER_POLICY)
        self.wfile.write(data)
        hub.observations.observe(game, RUNTIME["version"], served=True)

    def _game_file(self, path):
        try:
            game_id, relative = path.removeprefix("/games/").split("/", 1)
            file = self.server.hub.installer.installed_file(game_id, relative)
        except (KeyError, OSError, ValueError):
            self._error(HTTPStatus.NOT_FOUND, "Spieldatei nicht gefunden.")
            return
        if file.suffix.lower() not in GAME_TYPES:
            self._error(HTTPStatus.NOT_FOUND, "Dateiformat wird nicht ausgeliefert.")
            return
        self._file(file, game_policy(self.headers["Host"], game_id))

    def _static_file(self, path):
        file = (WEB / (path.lstrip("/") or "index.html")).resolve()
        if not file.is_relative_to(WEB) or not file.is_file():
            self._error(HTTPStatus.NOT_FOUND, "Seite nicht gefunden.")
            return
        self._file(file)

    def do_POST(self):
        if not self._origin_allowed():
            self._error(HTTPStatus.FORBIDDEN, "Nur lokale Anfragen sind zulässig.")
            return
        if self.headers.get_content_type() != "application/json":
            self._error(HTTPStatus.UNSUPPORTED_MEDIA_TYPE, "Erwartet wird application/json.")
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length <= 4096:
                raise ValueError()
            body = json.loads(self.rfile.read(length))
            if not isinstance(body, dict):
                raise ValueError()
        except ValueError:
            self._error(HTTPStatus.BAD_REQUEST, "Ungültige JSON-Anfrage.")
            return
        hub = self.server.hub
        if self.path == "/api/sync":
            self._json(hub.sync(), HTTPStatus.ACCEPTED)
            return
        actions = {"/api/install": hub.install, "/api/uninstall": hub.uninstall}
        action = actions.get(self.path)
        if action is None:
            self._error(HTTPStatus.NOT_FOUND, "Aktion nicht gefunden.")
            return
        game_id = body.get("id")
        if not isinstance(game_id, str) or not game_id:
            self._error(HTTPStatus.BAD_REQUEST, "Eine Spiel-ID ist erforderlich.")
            return
        try:
            result = action(game_id)
        except KeyError:
            self._error(HTTPStatus.NOT_FOUND, "Spiel nicht gefunden.")
        except ValueError as exc:
            self._error(HTTPStatus.CONFLICT, str(exc))
        else:
            self._json(result, HTTPStatus.ACCEPTED)


def main(argv=None):
    parser = argparse.ArgumentParser(description="Akeno Gaming Hub · Host-Entwicklungsbuild")
    parser.add_argument("--port", type=int, default=8765)
    add_catalog_arguments(parser)
    args = parser.parse_args(argv)
    hub = Hub(args.data_dir.expanduser(), providers_for(args))
    try:
        with HubServer(args.port, hub) as server:
            print(f"Akeno Gaming Hub: http://127.0.0.1:{server.server_address[1]}", flush=True)
            try:
                server.serve_forever()
            except KeyboardInterrupt:
                pass
    except OSError as exc:
        print(f"Lokaler Server konnte nicht gestartet werden: {exc}", file=sys.stderr)
        return 1
    finally:
        hub.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
