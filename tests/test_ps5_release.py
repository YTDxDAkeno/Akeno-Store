import hashlib
import json
import tempfile
import unittest
from pathlib import Path
from zipfile import ZipFile

from build import ps5_release as release


class PS5ReleaseTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.path = Path(directory.name)
        self.app = self.path / "app"
        self.app.mkdir()
        self.catalog = self.path / "catalog"
        self.catalog.mkdir()
        (self.catalog / "catalog.tsv").write_text("signal\tSignal Drift\tAkeno\t1.0.0\n")
        (self.catalog / "library.tsv").touch()
        release.stage_assets(self.app, self.catalog)
        (self.app / "sce_sys" / "param.json").write_text('{"titleId":"TEST00001"}')

    def test_rejects_missing_eboot_and_generic_elf(self):
        with self.assertRaisesRegex(ValueError, "eboot.bin"):
            release.validate_app(self.app, "TEST00001")
        (self.app / "eboot.bin").write_bytes(b"\x7fELF" + bytes(256))
        with self.assertRaisesRegex(ValueError, "SELF-Header"):
            release.validate_app(self.app, "TEST00001")

    def test_rejects_symlinks(self):
        (self.app / "redirect").symlink_to(self.catalog)
        with self.assertRaisesRegex(ValueError, "Ungültiger Dateipfad"):
            release.application_files(self.app)

    def test_archive_is_reproducible_and_manifest_verifies(self):
        # Test fixture only. The packager checks structure, not a console signature.
        (self.app / "eboot.bin").write_bytes(release.SELF_MAGIC + bytes(256))
        output = self.path / "result.zip"
        release.package(self.app, "TEST00001", output)
        release.validate_archive(output)
        first = output.read_bytes()
        release.package(self.app, "TEST00001", output)
        self.assertEqual(output.read_bytes(), first)
        self.assertTrue(output.with_suffix(".zip.sha256").read_text().startswith(hashlib.sha256(first).hexdigest()))
        with ZipFile(output) as archive:
            self.assertIsNone(archive.testzip())
            manifest = json.loads(archive.read("TEST00001/build-manifest.json"))
            for name, entry in manifest["files"].items():
                data = archive.read(f"TEST00001/{name}")
                self.assertEqual({"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}, entry)

    def test_archive_rejects_file_changed_after_packaging(self):
        (self.app / "eboot.bin").write_bytes(release.SELF_MAGIC + bytes(256))
        output = self.path / "result.zip"
        release.package(self.app, "TEST00001", output)
        changed = self.path / "changed.zip"
        with ZipFile(output) as original, ZipFile(changed, "w") as tampered:
            for name in original.namelist():
                data = b"updated" if name.endswith("catalog.tsv") else original.read(name)
                tampered.writestr(name, data)
        with self.assertRaisesRegex(ValueError, "SHA-256"):
            release.validate_archive(changed)

    def test_wrong_title_id_prevents_packaging(self):
        with self.assertRaisesRegex(ValueError, "Title-ID"):
            release.validate_app(self.app, "bad/id")
        (self.app / "eboot.bin").write_bytes(release.SELF_MAGIC + bytes(256))
        with self.assertRaisesRegex(ValueError, "sce_sys/param.json"):
            release.validate_app(self.app, "DIFF00001")
