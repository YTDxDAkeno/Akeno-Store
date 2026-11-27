import tempfile
import unittest
from pathlib import Path

from core.native_catalog import main, write_catalog
from core.service import Hub


class NativeCatalogTest(unittest.TestCase):
    def test_export_reads_installed_library(self):
        with tempfile.TemporaryDirectory() as directory:
            data = Path(directory) / "data"
            output = Path(directory) / "export"
            hub = Hub(data, [])
            try:
                hub.install("signal-drift")
            finally:
                hub.close()

            self.assertEqual(main(["--offline", "--data-dir", str(data), "--output", str(output)]), 0)
            for file in ("catalog.tsv", "library.tsv"):
                self.assertEqual(
                    (output / file).read_text(),
                    "signal-drift\tSignal Drift\tAkeno\t1.0.0\n",
                )

    def test_export_escapes_delimiters_and_bounds_utf8(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "catalog.tsv"
            write_catalog(path, [{"id": "game", "title": "ä" * 180, "version": "release\t1\n"}])
            key, title, provider, version = path.read_bytes().removesuffix(b"\n").split(b"\t")
            self.assertEqual(key, b"game")
            self.assertEqual(title.decode(), "ä" * 127)
            self.assertEqual(provider, b"Lokal")
            self.assertEqual(version, b"release 1 ")
