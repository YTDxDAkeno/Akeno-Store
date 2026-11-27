from concurrent.futures import ThreadPoolExecutor
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import io
from pathlib import Path
import stat
import tempfile
from threading import Thread
import unittest
from unittest.mock import patch
from urllib.parse import urlsplit
import zipfile

from security.installer import Installer, InstallError


def make_zip(contents):
    data = io.BytesIO()
    with zipfile.ZipFile(data, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for name, content in contents:
            archive.writestr(name, content)
    return data.getvalue()


class FixtureServer(ThreadingHTTPServer):
    daemon_threads = True

    def __init__(self, package):
        self.package = package
        super().__init__(("127.0.0.1", 0), FixtureHandler)

    @property
    def base_url(self):
        return f"http://127.0.0.1:{self.server_port}"


class FixtureHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        pass

    def do_GET(self):
        path = urlsplit(self.path).path
        if path in {"/redirect", "/blocked"}:
            self.send_response(302)
            location = "/package" if path == "/redirect" else "http://127.0.0.2:1234/private"
            self.send_header("Location", location)
            self.end_headers()
            return

        self.send_response(200)
        package = self.server.package
        self.send_header("Content-Length", str(len(package)))
        self.end_headers()
        self.wfile.write(package[:20] if path == "/truncated" else package)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        workspace = tempfile.TemporaryDirectory()
        self.addCleanup(workspace.cleanup)
        self.root = Path(workspace.name) / "games"
        self.installer = Installer(self.root, allow_loopback_http=True)
        self.package = make_zip([
            ("public/index.html", "<h1>Test game</h1>"),
            ("public/assets/main.js", "console.log('ready');"),
        ])
        server = FixtureServer(self.package)
        self.addCleanup(server.server_close)
        worker = Thread(target=server.serve_forever, kwargs={"poll_interval": 0.02}, daemon=True)
        worker.start()
        self.addCleanup(worker.join)
        self.addCleanup(server.shutdown)
        self.server = server

    def manifest(self, data=None, **overrides):
        return {
            "id": "test-game",
            "title": "Test game",
            "version": "1",
            "license": "MIT",
            "runtime": "html5",
            "architecture": "any",
            "entrypoint": "public/index.html",
            "archive_url": self.server.base_url + "/package",
            "sha256": hashlib.sha256(self.package if data is None else data).hexdigest(),
            **overrides,
        }

    def local(self, data, **overrides):
        archive = self.root.parent / "local.zip"
        archive.write_bytes(data)
        return self.installer.install(self.manifest(data, **overrides), local_archive=archive)

    def test_download_redirect_persistence_progress_and_uninstall(self):
        manifest = self.manifest(archive_url=self.server.base_url + "/redirect")
        progress = []
        self.assertEqual(self.installer.install(manifest, lambda *values: progress.append(values)), manifest)
        self.assertEqual(progress[0], (0, len(self.package)))
        self.assertEqual(progress[-1], (len(self.package), len(self.package)))
        restored = Installer(self.root)
        self.assertEqual(restored.list_installed(), [manifest])
        self.assertEqual(restored.entrypoint("test-game").read_text(), "<h1>Test game</h1>")
        self.assertEqual(
            restored.installed_file("test-game", "public/assets/main.js").read_text(),
            "console.log('ready');",
        )
        with self.assertRaises(InstallError):
            restored.installed_file("test-game", "../sibling.txt")
        self.assertTrue(restored.uninstall("test-game"))
        self.assertFalse(restored.uninstall("test-game"))
        self.assertEqual(restored.list_installed(), [])

    def test_failed_updates_preserve_current_version_and_success_can_repair(self):
        self.installer.install(self.manifest())
        for changes in [
            {"sha256": "0" * 64},
            {"entrypoint": "absent.html"},
            {"archive_url": self.server.base_url + "/truncated"},
            {"archive_url": self.server.base_url + "/blocked"},
        ]:
            with self.subTest(changes=changes), self.assertRaises(InstallError):
                self.installer.install(self.manifest(version="2", **changes))
            self.assertEqual(self.installer.list_installed()[0]["version"], "1")
            self.assertEqual(self.installer.entrypoint("test-game").read_text(), "<h1>Test game</h1>")

        updated = make_zip([("public/index.html", "<h1>Updated</h1>")])
        self.local(updated, version="2")
        active = self.installer.entrypoint("test-game")
        self.assertEqual(active.read_text(), "<h1>Updated</h1>")
        active.unlink()
        self.local(updated, version="2")
        self.assertEqual(self.installer.entrypoint("test-game").read_text(), "<h1>Updated</h1>")

    def test_publication_failure_preserves_active_files(self):
        previous = self.installer.install(self.manifest())
        before = self.installer.entrypoint("test-game")
        with patch("pathlib.Path.replace", side_effect=OSError("Simulated write failure")):
            with self.assertRaises(InstallError):
                self.installer.install(self.manifest(version="2"))
        self.assertEqual(self.installer.list_installed(), [previous])
        self.assertEqual(self.installer.entrypoint("test-game"), before)
        self.assertEqual(sorted(before.parent.parent.parent.iterdir()), [
            before.parent.parent.parent / "active.json",
            before.parent.parent,
        ])

    def test_concurrent_installs_publish_one_complete_version(self):
        manifests = [self.manifest(version=str(version)) for version in range(3)]
        with ThreadPoolExecutor(max_workers=len(manifests)) as workers:
            results = list(workers.map(self.installer.install, manifests))
        self.assertEqual(results, manifests)
        installed = self.installer.list_installed()
        self.assertEqual(len(installed), 1)
        self.assertIn(installed[0], manifests)
        self.assertEqual(self.installer.entrypoint("test-game").read_text(), "<h1>Test game</h1>")

    def test_rejects_untrusted_paths_and_link_members(self):
        link = zipfile.ZipInfo("public/link")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        for malicious in ["../../escape", "/absolute", "C:/device", "public\\asset", link]:
            data = make_zip([("public/index.html", "game"), (malicious, "target")])
            with self.subTest(member=malicious), self.assertRaises(InstallError):
                self.local(data)
        self.assertEqual(self.installer.list_installed(), [])
        self.assertFalse((self.root.parent / "escape").exists())

    def test_rejects_invalid_zip_and_oversize_expansion(self):
        with self.assertRaises(InstallError):
            self.local(b"this file is not a ZIP")
        with patch("security.installer.MAX_INSTALLED_BYTES", 8), self.assertRaises(InstallError):
            self.local(self.package)
        self.assertEqual(self.installer.list_installed(), [])

    def test_production_rejects_loopback_http(self):
        with self.assertRaisesRegex(InstallError, "HTTPS"):
            Installer(self.root).install(self.manifest())

    def test_rejects_private_dns_resolution(self):
        with patch(
            "security.installer.socket.getaddrinfo",
            return_value=[(0, 0, 0, "", ("10.0.0.1", 443))],
        ), self.assertRaisesRegex(InstallError, "public network"):
            self.installer.install(self.manifest(archive_url="https://game.example/package"))
