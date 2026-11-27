"""Supervise trusted local JARs and Linux ELF applications on the development host."""

import argparse
import json
import math
import os
import platform
import re
import selectors
import shutil
import signal
import struct
import subprocess
import sys
import threading
import time
import zipfile
from collections import deque
from dataclasses import asdict, dataclass
from pathlib import Path

from compatibility.inspection import RuntimeCapability, inspect_path, resolve_runtime

JAVA_MAJORS = {17, 21, 25}


def java_runtimes(homes=()):
    candidates = [Path(home) / "bin" / "java" for home in homes]
    if java := shutil.which("java"):
        candidates.append(Path(java))
    candidates.extend(sorted(Path("/usr/lib/jvm").glob("*/bin/java")))
    runtimes = {}
    for binary in dict.fromkeys(path.resolve() for path in candidates):
        if not binary.is_file() or not os.access(binary, os.X_OK):
            continue
        try:
            probe = subprocess.run(
                [str(binary), "-version"], stdin=subprocess.DEVNULL, capture_output=True,
                timeout=5, env={"PATH": os.defpath, "LC_ALL": "C"},
            )
        except (OSError, subprocess.TimeoutExpired):
            continue
        match = re.search(rb'version "(\d+)', probe.stderr)
        if probe.returncode == 0 and match and int(match[1]) in JAVA_MAJORS:
            runtimes.setdefault(f"java-{int(match[1])}", binary)
    return runtimes


def _elf(path):
    with path.open("rb") as stream:
        header = stream.read(64)
    machines = {"x86_64": 62, "aarch64": 183}
    if len(header) != 64 or header[:7] != b"\x7fELF\x02\x01\x01":
        return False
    kind, machine = struct.unpack_from("<HH", header, 16)
    return kind in {2, 3} and machines.get(platform.machine()) == machine and os.access(path, os.X_OK)


@dataclass(frozen=True)
class Outcome:
    state: str
    returncode: int
    peak_resident_kib_sampled: int
    output_tail: str
    backend: str


class ManagedRuntime:
    def __init__(self, trusted_root, *, safe_mode=False, disabled=(), java_homes=()):
        self.root = Path(trusted_root).resolve(strict=True)
        if not self.root.is_dir():
            raise ValueError("Trusted root must be a directory")
        self.safe_mode = safe_mode
        self.disabled = frozenset(disabled)
        self.java = {} if safe_mode else java_runtimes(java_homes)
        self._stop = threading.Event()
        self._exclusive = threading.Lock()
        self._pid = None

    @property
    def status(self):
        return {"pid": self._pid, "safe_mode": self.safe_mode, "available": self.available}

    @property
    def available(self):
        backends = set(self.java)
        if sys.platform.startswith("linux") and platform.machine() in {"aarch64", "x86_64"}:
            backends.add("linux-elf")
        return [] if self.safe_mode else sorted(backends - self.disabled)

    def _trusted_entry(self, entry):
        entry = Path(entry)
        if entry.is_absolute():
            raise ValueError("Select a relative path inside the trusted directory")
        path = (self.root / entry).resolve(strict=True)
        if not path.is_relative_to(self.root) or not path.is_file():
            raise ValueError("Entry must be a regular file inside the trusted directory")
        return path

    def _choose_backend(self, path):
        finding = inspect_path(path).primary
        capabilities = [
            RuntimeCapability("java", "host", ("Java",), version=int(backend.split("-")[1]))
            for backend in self.available if backend.startswith("java-")
        ]
        if finding and finding.engine == "Linux" and "linux-elf" in self.available and _elf(path):
            cache = shutil.which("ldconfig", path="/usr/sbin:/sbin")
            if cache:
                probe = subprocess.run([cache, "-p"], capture_output=True, check=False, timeout=5)
                architecture = {"x86_64": "x86-64", "aarch64": "AArch64"}[platform.machine()]
                libraries = tuple(
                    match.group(1).decode("ascii") for match in re.finditer(
                        rb"^\s*(\S+) \([^)]*" + architecture.encode() + rb"[^)]*\) => ",
                        probe.stdout, re.MULTILINE | re.IGNORECASE,
                    )
                ) if probe.returncode == 0 else ()
            else:
                libraries = ()
            machine = {"x86_64": "x86_64", "aarch64": "arm64"}[platform.machine()]
            capabilities.append(RuntimeCapability("linux", "host", ("Linux",), (machine,), libraries))
        resolution = resolve_runtime(finding, tuple(capabilities), platform="host")
        if resolution.selected == "java":
            return f"java-{resolution.version}"
        if resolution.selected == "linux":
            return "linux-elf"
        raise ValueError(f"No automatic runtime available ({resolution.reason})")

    def _command(self, backend, entry):
        if self.safe_mode or (backend != "auto" and backend not in self.available):
            raise ValueError("Runtime unavailable or disabled")
        path = self._trusted_entry(entry)
        if backend == "auto":
            backend = self._choose_backend(path)
        if backend == "linux-elf":
            if not _elf(path):
                raise ValueError("Incompatible ELF header or missing execute permission")
            return [str(path)], path.parent, backend
        if path.suffix.lower() != ".jar" or not zipfile.is_zipfile(path):
            raise ValueError("Expected a local JAR")
        return [str(self.java[backend]), "-jar", str(path)], path.parent, backend

    def stop(self):
        self._stop.set()

    def run(self, backend, entry, *, timeout=120.0):
        if not math.isfinite(timeout) or not 0 < timeout <= 86400:
            raise ValueError("Timeout must be in (0, 86400] seconds")
        if not self._exclusive.acquire(blocking=False):
            raise ValueError("A program is already running")
        try:
            self._stop.clear()
            command, cwd, selected = self._command(backend, entry)
            env = {
                name: os.environ[name] for name in (
                    "HOME", "LANG", "DISPLAY", "XAUTHORITY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR",
                    "PULSE_SERVER", "DBUS_SESSION_BUS_ADDRESS",
                ) if name in os.environ
            }
            env["PATH"] = os.defpath
            with subprocess.Popen(command, cwd=cwd, env=env, stdin=subprocess.DEVNULL,
                                  stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                  start_new_session=True) as process:
                self._pid = process.pid
                try:
                    return self._watch(process, timeout, selected)
                finally:
                    if process.poll() is None:
                        self._signal(process, signal.SIGKILL)
                        process.wait(timeout=3)
        finally:
            self._pid = None
            self._exclusive.release()

    @staticmethod
    def _signal(process, sig):
        try:
            os.killpg(process.pid, sig)
        except ProcessLookupError:
            pass

    def _watch(self, process, timeout, backend):
        deadline = time.monotonic() + timeout
        tail = deque(maxlen=8192)
        peak = 0
        reason = None
        status_file = Path(f"/proc/{process.pid}/status")
        with selectors.DefaultSelector() as selector:
            os.set_blocking(process.stdout.fileno(), False)
            selector.register(process.stdout, selectors.EVENT_READ)
            while process.poll() is None:
                now = time.monotonic()
                if reason is None and (self._stop.is_set() or now >= deadline):
                    reason = "stopped" if self._stop.is_set() else "timeout"
                    self._signal(process, signal.SIGTERM)
                    deadline = now + 0.5
                elif reason and now >= deadline:
                    self._signal(process, signal.SIGKILL)
                try:
                    memory = re.search(r"^VmRSS:\s*(\d+) kB", status_file.read_text(), re.MULTILINE)
                    if memory:
                        peak = max(peak, int(memory[1]))
                except (OSError, UnicodeError):
                    pass
                if selector.select(.05):
                    data = os.read(process.stdout.fileno(), 8192)
                    if data:
                        tail.extend(data)
                    else:
                        selector.unregister(process.stdout)
            # Release children left in the application's process group on every exit.
            self._signal(process, signal.SIGKILL)
            while block := process.stdout.read(8192):
                tail.extend(block)
        state = reason or ("exited" if process.returncode == 0 else "failed")
        return Outcome(state, process.returncode, peak, bytes(tail).decode("utf-8", errors="replace"), backend)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--trusted-root", type=Path, required=True)
    parser.add_argument("--java-home", action="append", default=[], type=Path)
    parser.add_argument("--disable", action="append", default=[])
    parser.add_argument("--safe-mode", action="store_true")
    parser.add_argument("--timeout", type=float, default=120)
    parser.add_argument("backend", nargs="?")
    parser.add_argument("entry", nargs="?")
    args = parser.parse_args()
    if bool(args.backend) != bool(args.entry):
        parser.error("Supply both backend and relative entry, or neither for status")
    try:
        manager = ManagedRuntime(args.trusted_root, java_homes=args.java_home,
                                 safe_mode=args.safe_mode, disabled=args.disable)
        if args.backend is None:
            print(json.dumps(manager.status))
            return 0
        result = manager.run(args.backend, args.entry, timeout=args.timeout)
        print(json.dumps(asdict(result)))
        return 0 if result.state == "exited" else 1
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    raise SystemExit(main())
