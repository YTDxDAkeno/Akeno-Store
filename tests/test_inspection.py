from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
import unittest
from unittest.mock import patch
import zipfile

from compatibility.inspection import RuntimeCapability, inspect_path, resolve_runtime


def minimal_pe(machine=0x8664, *, dll=False, dependency=None):
    data = bytearray(1024)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 60, 64)
    data[64:68] = b"PE\0\0"
    struct.pack_into("<HH", data, 68, machine, 1)
    struct.pack_into("<HH", data, 84, 240, 0x2000 if dll else 2)
    struct.pack_into("<H", data, 88, 0x20B)
    if dependency:
        struct.pack_into("<I", data, 88 + 108, 16)
        struct.pack_into("<II", data, 88 + 120, 0x1000, 40)
        struct.pack_into("<III", data, 88 + 240 + 12, 0x1000, 0x200, 0x200)
        struct.pack_into("<I", data, 0x200 + 12, 0x1050)
        data[0x250:0x250 + len(dependency)] = dependency
    return data


class InspectionTest(unittest.TestCase):
    def setUp(self):
        directory = TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def resolution(self, target, *capabilities, platform="host"):
        return resolve_runtime(inspect_path(target).primary, capabilities, platform=platform)

    def test_java_class_versions_and_declared_dependencies(self):
        archive = self.root / "sample.jar"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as jar:
            jar.writestr(
                "META-INF/MANIFEST.MF",
                "Manifest-Version: 1.0\r\nMain-Class: game.Start\r\nClass-Path: extras.jar\r\n\r\n",
            )
            jar.writestr("game/Start.class", b"\xca\xfe\xba\xbe" + struct.pack(">HH", 0, 65))
            jar.writestr("game/Sound.class", b"\xca\xfe\xba\xbe" + struct.pack(">HH", 0, 61))
            jar.writestr("META-INF/versions/25/game/Sound.class", b"\xca\xfe\xba\xbe" + struct.pack(">HH", 0, 69))
        finding = inspect_path(archive).primary
        self.assertEqual(finding.engine, "Java")
        self.assertEqual(finding.minimum_java, 21)
        self.assertEqual(finding.dependencies, ("extras.jar",))
        self.assertTrue(finding.entrypoint_detected)
        capabilities = tuple(RuntimeCapability(
            "java", "host", ("Java",), dependencies=("extras.jar",), version=version,
        ) for version in (25, 17, 21))
        self.assertEqual(self.resolution(archive, *capabilities).version, 21)
        self.assertIsNone(self.resolution(archive, capabilities[1]).selected)
        self.assertIsNone(self.resolution(archive, *capabilities, platform="ps5").selected)
        self.assertIsNone(self.resolution(archive, RuntimeCapability("java", "host", ("Java",), version=25)).selected)

    def test_java_missing_main_and_non_java_zip(self):
        jar_path = self.root / "library.jar"
        with zipfile.ZipFile(jar_path, "w") as jar:
            jar.writestr("game/Start.class", b"\xca\xfe\xba\xbe" + struct.pack(">HH", 0, 61))
        self.assertEqual(self.resolution(jar_path).reason, "launch-entrypoint-unconfirmed")
        with zipfile.ZipFile(jar_path, "w") as jar:
            jar.writestr("README.md", "plain ZIP")
        self.assertIsNone(inspect_path(jar_path).primary)

    def test_windows_architecture_and_actual_import_table(self):
        target = self.write("game.exe", minimal_pe(dependency=b"SDL2.dll"))
        finding = inspect_path(target).primary
        self.assertEqual((finding.engine, finding.architecture, finding.dependencies), (
            "Windows", "x86_64", ("sdl2.dll",),
        ))
        win64 = RuntimeCapability(
            "windows", "host", ("Windows",), ("x86_64",), ("sdl2.dll",), version=1,
        )
        win32 = RuntimeCapability(
            "windows", "host", ("Windows",), ("x86",), ("sdl2.dll",), version=1,
        )
        self.assertEqual(self.resolution(target, win64).selected, "windows")
        self.assertIsNone(self.resolution(target, win32).selected)
        self.assertIsNone(inspect_path(self.write("bad.exe", b"MZ truncated")).primary)

    @unittest.skipUnless(sys.platform.startswith("linux"), "requires the Linux interpreter executable")
    def test_real_linux_interpreter_elf(self):
        target = Path(sys.executable).resolve()
        finding = inspect_path(target).primary
        self.assertEqual((finding.engine, finding.format), ("Linux", "ELF"))
        self.assertTrue(any(dependency.startswith("libc.") for dependency in finding.dependencies))
        self.assertTrue(any("Interpreter: " in evidence for evidence in finding.evidence))

    def test_rpg_maker_suites_use_content_evidence(self):
        self.write("web/index.html", b"<!doctype html><script src='js/rmmz_core.js'></script>")
        self.write("web/js/rmmz_core.js", b'Utils.RPGMAKER_NAME = "MZ";')
        result = inspect_path(self.root / "web", entrypoint="index.html")
        self.assertEqual(result.primary.engine, "RPG Maker MZ")
        self.assertTrue(result.primary.entrypoint_detected)
        self.assertEqual(result.scanned_files, 2)

        self.write("lcf/RPG_RT.ldb", b"\x0bLcfDataBase")
        self.write("lcf/RPG_RT.lmt", b"\x0aLcfMapTree")
        self.write("lcf/Map001.lmu", b"\x0aLcfMapUnit")
        lcf = self.root / "lcf"
        self.assertEqual(inspect_path(lcf).primary.format, "LCF game directory")
        engine = RuntimeCapability("easyrpg", "host", ("RPG Maker 2000/2003",))
        self.assertEqual(self.resolution(lcf, engine).selected, "easyrpg")

        self.write("legacy/Game.ini", b"[Game]\nLibrary=RGSS301.dll\nScripts=Data\\Scripts.rvdata2\n")
        self.write("legacy/Data/Scripts.rvdata2", b"\x04\x08payload")
        self.assertEqual(inspect_path(self.root / "legacy").primary.format, "RGSS game directory")

    def test_godot_version_and_renpy_entrypoint(self):
        pack_header = b"GDPC" + struct.pack("<IIII", 2, 4, 3, 0)
        pack = self.write("quest.pck", pack_header)
        capabilities = tuple(RuntimeCapability("godot", "host", ("Godot",), version=x) for x in (3, 4))
        self.assertEqual(self.resolution(pack, *capabilities).version, 4)
        self.assertIsNone(self.resolution(pack, capabilities[0]).selected)
        self.assertIsNone(inspect_path(self.write("bogus.pck", b"extension only")).primary)
        binary = self.write("quest.exe", minimal_pe() + pack_header + struct.pack("<Q", len(pack_header)) + b"GDPC")
        self.assertEqual(inspect_path(binary).primary.format, "Godot PE with PCK")
        self.assertEqual(self.resolution(binary, *capabilities).candidates, ("windows",))

        archive = self.write("script.rpa", b"RPA-3.0 sample")
        source = self.write("plot/game/script.rpy", b"label start:\n    return\n")
        renpy = RuntimeCapability("renpy", "host", ("Ren'Py",))
        self.assertIsNone(self.resolution(archive, renpy).selected)
        self.assertEqual(self.resolution(source, renpy).selected, "renpy")

    def test_unity_assets_and_matching_native_player_signature(self):
        self.write("Racer.exe", minimal_pe())
        self.write("UnityPlayer.dll", minimal_pe(dll=True))
        payload = struct.pack(">4I", 8, 24, 17, 20) + b"\0" * 8
        self.write("Racer_Data/globalgamemanagers", payload)
        finding = inspect_path(self.root).primary
        self.assertEqual(finding.engine, "Unity")
        self.assertEqual(finding.format, "Unity PE")
        self.assertEqual(finding.architecture, "x86_64")
        runtime = RuntimeCapability("windows", "host", ("Unity",), ("x86_64",))
        self.assertEqual(self.resolution(self.root, runtime).selected, "windows")
        self.assertFalse(inspect_path(self.write("file.data", b"UnityWebData1.0\0")).primary.entrypoint_detected)

    def test_rom_and_disc_signatures_never_authorize_execution(self):
        iso = bytearray(16 * 2048 + 7)
        iso[-7:] = b"\x01CD001\x01"
        fixtures = [
            ("fixture.iso", iso, "ISO 9660 image"),
            ("fixture.z64", b"\x80\x37\x12\x40" + b"\0" * 96, "Nintendo 64 ROM"),
            ("fixture.gcm", b"\0" * 28 + b"\xc2\x33\x9f\x3d", "GameCube disc image"),
        ]
        runtime = RuntimeCapability("recompilation", "host", ("ROM/ISO",))
        for name, data, expected in fixtures:
            with self.subTest(name=name):
                path = self.write(name, data)
                self.assertEqual(inspect_path(path).primary.format, expected)
                self.assertIsNone(self.resolution(path, runtime).selected)

    def test_explicit_html_entry_and_bounded_walk_without_symlinks(self):
        index = self.write("page/index.html", b"<!DOCTYPE HTML><html></html>")
        self.write("page/subdir/ignore.txt", b"No game")
        self.write("other/external.exe", minimal_pe())
        link = self.root / "page" / "external.exe"
        link.symlink_to(self.root / "other" / "external.exe")
        inspected = inspect_path(index.parent, entrypoint="index.html")
        self.assertEqual([finding.engine for finding in inspected.findings], ["HTML5"])
        self.assertIsNone(inspect_path(link).primary)
        with patch("compatibility.inspection.SCAN_LIMIT", 1):
            self.assertTrue(inspect_path(index.parent, entrypoint="index.html").truncated)
            self.assertEqual(inspect_path(index.parent, entrypoint="index.html").primary.engine, "HTML5")

    def test_cli_reports_detection_but_no_ps5_backend(self):
        target = self.write("index.html", b"<!doctype html>")
        result = subprocess.run(
            (sys.executable, "-m", "compatibility", str(target), "--platform", "ps5"),
            check=True, capture_output=True, text=True, timeout=10,
        )
        self.assertIn('"selected": null', result.stdout)
        self.assertIn('"engine": "HTML5"', result.stdout)
