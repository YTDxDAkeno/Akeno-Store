import json
import tempfile
import threading
import time
import unittest
from http.client import HTTPConnection
from pathlib import Path

from compatibility.profiles import detect_engine
from core.server import HubServer
from core.service import Hub


class FixtureProvider:
    def __init__(self):
        self.fail = False

    def list_games(self):
        if self.fail:
            raise ValueError("Fixture unavailable")
        return [{"id": "discovery", "title": "Public release", "provider": "fixture"}]


class HostTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.provider = FixtureProvider()
        hub = Hub(Path(temporary.name), [("Fixture", self.provider)])
        self.addCleanup(hub.close)
        self.server = HubServer(0, hub)
        thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.addCleanup(self.server.server_close)
        self.addCleanup(thread.join)
        self.addCleanup(self.server.shutdown)
        thread.start()

    def request(self, method, path, body=None, **headers):
        client = HTTPConnection(*self.server.server_address, timeout=3)
        try:
            client.request(method, path, body, headers)
            result = client.getresponse()
            return result.status, result.headers, result.read()
        finally:
            client.close()

    def get_json(self, path):
        status, _, data = self.request("GET", path)
        self.assertEqual(status, 200)
        return json.loads(data)

    def post(self, path, **data):
        return self.request("POST", path, json.dumps(data), **{"Content-Type": "application/json"})

    def poll(self, path, predicate):
        deadline = time.monotonic() + 4
        while time.monotonic() < deadline:
            result = self.get_json(path)
            if predicate(result):
                return result
            time.sleep(.02)
        self.fail(f"No matching state for {path}: {result}")

    def wait_job(self):
        status = self.poll("/api/status", lambda data: data["jobs"][0]["state"] not in {"queued", "running"})
        self.assertEqual(status["jobs"][0]["state"], "completed", status)

    def test_install_repair_launch_uninstall(self):
        self.assertEqual(self.request("GET", "/api/launch?id=signal-drift")[0], 404)
        self.assertEqual(self.post("/api/install", id="signal-drift")[0], 202)
        self.wait_job()
        self.assertEqual(self.get_json("/api/library")["games"][0]["title"], "Signal Drift")

        status, headers, document = self.request("GET", "/api/launch?id=signal-drift")
        self.assertEqual(status, 200)
        self.assertIn("frame-ancestors 'none'", headers["Content-Security-Policy"])
        self.assertIn(b'sandbox="allow-scripts"', document)
        status, headers, script = self.request("GET", "/games/signal-drift/game.js")
        self.assertEqual(status, 200)
        self.assertIn("sandbox allow-scripts", headers["Content-Security-Policy"])
        self.assertIn(b"function move", script)
        status, _, _ = self.request("GET", "/games/signal-drift/game.js", Origin="null")
        self.assertEqual(status, 200)
        record = self.poll(
            "/api/compatibility",
            lambda data: data["records"] and data["records"][0]["player_served"],
        )["records"][0]
        self.assertEqual(record["engine"], "HTML5")
        self.assertEqual(record["gameplay"], "unconfirmed")
        self.assertEqual(self.request("GET", "/games/signal-drift/%2e%2e/index.html")[0], 404)

        entry = self.server.hub.installer.entrypoint("signal-drift")
        entry.unlink()
        self.assertEqual(self.post("/api/install", id="signal-drift")[0], 202)
        self.wait_job()
        self.assertEqual(self.request("GET", "/api/launch?id=signal-drift")[0], 200)
        self.assertEqual(self.post("/api/uninstall", id="signal-drift")[0], 202)
        self.wait_job()
        self.assertEqual(self.get_json("/api/library")["games"], [])
        self.assertEqual(self.request("GET", "/api/launch?id=signal-drift")[0], 404)

    def test_sync_preserves_last_good_result_and_discovery_is_not_installable(self):
        self.assertEqual(self.post("/api/sync")[0], 202)
        catalog = self.poll("/api/catalog", lambda data: len(data["games"]) == 2)
        self.assertFalse(catalog["games"][1]["installable"])
        self.assertEqual(self.post("/api/install", id="discovery")[0], 409)
        self.provider.fail = True
        self.assertEqual(self.post("/api/sync")[0], 202)
        catalog = self.poll("/api/catalog", lambda data: bool(data["errors"]))
        self.assertEqual(catalog["errors"][0]["source"], "Fixture")
        self.assertEqual(len(catalog["games"]), 2)

    def test_rejects_foreign_origin_and_host(self):
        status, _, _ = self.request("GET", "/api/library", Host="foreign.invalid")
        self.assertEqual(status, 403)
        status, _, _ = self.request(
            "POST", "/api/install", '{"id":"signal-drift"}',
            Origin="null", **{"Content-Type": "application/json"}
        )
        self.assertEqual(status, 403)

    def test_engine_hints(self):
        self.assertEqual(detect_engine(["index.html", "js/rmmz_core.js"]), "RPG Maker MZ")
        self.assertEqual(detect_engine(["Story.RPA"]), "Ren'Py")
        self.assertEqual(detect_engine(["target.html"]), "HTML5")
        self.assertIsNone(detect_engine(["notes.txt"]))
