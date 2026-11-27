import os
import shutil
import subprocess
import tempfile
import threading
import time
import unittest
import zipfile
from pathlib import Path

from runtimes.managed import ManagedRuntime


class ManagedTest(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory()
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        homes = list(filter(None, os.environ.get("AKENO_TEST_JAVA_HOMES", "").split(os.pathsep)))
        self.runtime = ManagedRuntime(self.root, java_homes=homes)

    def compile_c(self, name, body):
        if "linux-elf" not in self.runtime.available or not shutil.which("cc"):
            self.skipTest("Requires host ELF runtime and C compiler")
        source = self.root / f"{name}.c"
        source.write_text('#include <stdio.h>\n#include <unistd.h>\nint main(void){' + body + '}\n')
        subprocess.run(["cc", source, "-o", self.root / name], check=True)

    def test_elf_outputs_and_exit(self):
        for name, code in (("alpha", 0), ("beta", 7)):
            self.compile_c(name, f'puts("{name}");return {code};')
            result = self.runtime.run("linux-elf", name)
            self.assertEqual(result.returncode, code)
            self.assertEqual(result.output_tail.strip(), name)
            if shutil.which("ldconfig", path="/usr/sbin:/sbin"):
                automatic = self.runtime.run("auto", name)
                self.assertEqual(automatic.returncode, code)
                self.assertEqual(automatic.backend, "linux-elf")

    def test_java_jar_execution(self):
        if not self.runtime.java:
            self.skipTest("Requires Java 17, 21 or 25")
        for backend, java in self.runtime.java.items():
            modules = subprocess.check_output([java, "--list-modules"], text=True)
            if "jdk.compiler@" not in modules:
                self.skipTest("Building Java fixtures requires jdk.compiler")
            for name in ("Amber", "Cobalt"):
                java_src = self.root / f"{name}.java"
                java_src.write_text(f'public class {name} {{ public static void main(String[] args) {{ System.out.println("{name}"); }} }}')
                subprocess.run([java, "-m", "jdk.compiler/com.sun.tools.javac.Main", java_src], check=True)
                with zipfile.ZipFile(self.root / f"{name}.jar", "w") as jar:
                    jar.writestr("META-INF/MANIFEST.MF", f"Manifest-Version: 1.0\r\nMain-Class: {name}\r\n\r\n")
                    jar.write(self.root / f"{name}.class", f"{name}.class")
                result = self.runtime.run(backend, f"{name}.jar")
                self.assertEqual(result.state, "exited")
                self.assertEqual(result.output_tail.strip(), name)
                automatic = self.runtime.run("auto", f"{name}.jar")
                self.assertEqual(automatic.output_tail.strip(), name)
                self.assertEqual(automatic.backend, backend)

    def test_watchdog_stop_and_single_instance(self):
        self.compile_c("sleeper", "sleep(30);return 0;")
        result = self.runtime.run("linux-elf", "sleeper", timeout=.12)
        self.assertEqual(result.state, "timeout")
        results = []
        thread = threading.Thread(target=lambda: results.append(self.runtime.run("linux-elf", "sleeper")))
        thread.start()
        try:
            deadline = time.monotonic() + 3
            while not self.runtime.status["pid"] and time.monotonic() < deadline:
                time.sleep(.01)
            with self.assertRaisesRegex(ValueError, "already running"):
                self.runtime.run("linux-elf", "sleeper")
        finally:
            self.runtime.stop()
            thread.join(timeout=3)
        self.assertEqual(results[0].state, "stopped")
        self.assertIsNone(self.runtime.status["pid"])

    def test_safe_mode_and_path_restrictions(self):
        safe = ManagedRuntime(self.root, safe_mode=True)
        self.assertEqual(safe.available, [])
        with self.assertRaises(ValueError):
            safe.run("linux-elf", "unknown")
        (self.root / "external").symlink_to("/bin/true")
        with self.assertRaises(ValueError):
            self.runtime.run("linux-elf", "external")
