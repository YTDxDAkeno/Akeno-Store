"""Read-only identification of game files and resolution against runtime capabilities."""

from __future__ import annotations

from dataclasses import dataclass
import os
from pathlib import Path
import re
import stat

from compatibility.binaries import inspect_elf, inspect_pe, read_at
from compatibility.java import inspect_jar


SCAN_LIMIT = 2048
MAX_DEPTH = 16
HEADER_BYTES = 4096
HTML = re.compile(rb"<(?:!doctype\s+html|html|head|body|script|meta)(?:\s|>|/)", re.IGNORECASE)
RPG_JS = re.compile(rb"RPGMAKER_NAME\s*=\s*(['\"])(MV|MZ)\1")
RENPY_START = re.compile(rb"(?m)^[ \t]*label[ \t]+start[ \t]*:")
RGSS_LIBRARY = re.compile(rb"(?im)^[ \t]*Library[ \t]*=[ \t]*RGSS([123])\d\d[A-Z]?\.dll[ \t]*\r?$")
RGSS_SCRIPTS = re.compile(rb"(?im)^[ \t]*Scripts[ \t]*=[ \t]*([^\r\n]+)")
RGSS_FAMILY = {b"1": "RPG Maker XP", b"2": "RPG Maker VX", b"3": "RPG Maker VX Ace"}
RPG_MARSHAL = {".rxdata": "RPG Maker XP", ".rvdata": "RPG Maker VX", ".rvdata2": "RPG Maker VX Ace"}
RPG_ARCHIVE = {".rgssad": "RPG Maker XP", ".rgss2a": "RPG Maker VX", ".rgss3a": "RPG Maker VX Ace"}
PRIORITY = (
    "RPG Maker MZ", "RPG Maker MV", "RPG Maker 2000/2003", "RPG Maker XP",
    "RPG Maker VX", "RPG Maker VX Ace", "Unity", "Godot", "Ren'Py", "Java",
    "Linux", "Windows", "ELF", "ROM/ISO", "HTML5",
)
CANDIDATES = {
    "Java": ("java",), "Windows": ("windows",), "Linux": ("linux",),
    "ELF": ("elf",), "Unity": ("windows", "linux", "html5"),
    "Godot": ("godot",), "Ren'Py": ("renpy",), "HTML5": ("html5",),
    "RPG Maker 2000/2003": ("easyrpg",), "RPG Maker XP": ("mkxp-z",),
    "RPG Maker VX": ("mkxp-z",), "RPG Maker VX Ace": ("mkxp-z",),
    "RPG Maker MV": ("html5",), "RPG Maker MZ": ("html5",),
    "ROM/ISO": ("recompilation",),
}


@dataclass(frozen=True)
class Finding:
    engine: str
    format: str
    path: str
    evidence: tuple[str, ...]
    architecture: str = "any"
    dependencies: tuple[str, ...] = ()
    minimum_java: int | None = None
    engine_major: int | None = None
    preview_java: bool = False
    entrypoint_detected: bool = True


@dataclass(frozen=True)
class RuntimeCapability:
    id: str
    platform: str
    engines: tuple[str, ...]
    architectures: tuple[str, ...] = ("any",)
    dependencies: tuple[str, ...] = ()
    version: int | None = None


@dataclass(frozen=True)
class RuntimeResolution:
    candidates: tuple[str, ...]
    selected: str | None
    version: int | None
    reason: str


@dataclass(frozen=True)
class Inspection:
    findings: tuple[Finding, ...]
    scanned_files: int
    truncated: bool
    unreadable: int

    @property
    def primary(self) -> Finding | None:
        return self.findings[0] if self.findings else None


def resolve_runtime(
    finding: Finding | None, capabilities: tuple[RuntimeCapability, ...], *, platform: str,
) -> RuntimeResolution:
    if finding is None:
        return RuntimeResolution((), None, None, "unknown-format")
    candidates = {
        "Unity PE": ("windows",),
        "Unity ELF": ("linux",),
        "Unity WebGL data": ("html5",),
        "Godot PE with PCK": ("windows",),
        "Godot ELF with PCK": ("linux",),
    }.get(finding.format, CANDIDATES.get(finding.engine, ()))
    if not finding.entrypoint_detected:
        return RuntimeResolution(candidates, None, None, "launch-entrypoint-unconfirmed")
    matches = []
    for runtime in capabilities:
        if runtime.id not in candidates or runtime.platform != platform or finding.engine not in runtime.engines:
            continue
        if finding.architecture != "any" and finding.architecture not in runtime.architectures:
            continue
        if not set(finding.dependencies).issubset(runtime.dependencies):
            continue
        if finding.minimum_java is not None and (runtime.version or 0) < finding.minimum_java:
            continue
        if runtime.id == "godot" and finding.engine_major is not None and runtime.version != finding.engine_major:
            continue
        if finding.preview_java and (
            runtime.version != finding.minimum_java or "java-preview" not in runtime.dependencies
        ):
            continue
        matches.append(runtime)
    if not matches:
        return RuntimeResolution(candidates, None, None, "no-matching-runtime-capability")
    selected = min(matches, key=lambda runtime: (candidates.index(runtime.id), runtime.version or 0))
    return RuntimeResolution(candidates, selected.id, selected.version, "matching-runtime-capability")


def _crc16(dump):
    crc = 0xFFFF
    for byte in dump:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def _rom_format(source, header, suffix):
    if suffix in {".n64", ".z64", ".v64"} and header[:4] in {b"\x80\x37\x12\x40", b"\x37\x80\x40\x12", b"\x40\x12\x37\x80"}:
        return "Nintendo 64 ROM", "Nintendo 64 cartridge byte-order signature"
    if suffix in {".gb", ".gbc"} and len(header) >= 0x150:
        if (-(sum(header[0x134:0x14D]) + 25)) & 0xFF == header[0x14D] and any(header[0x104:0x134]):
            format_name = "Game Boy Color ROM" if header[0x143] & 0x80 else "Game Boy ROM"
            return format_name, "Game Boy cartridge header checksum"
    if suffix == ".gba" and len(header) >= 0xC0 and header[0xB2] == 0x96:
        if (-sum(header[0xA0:0xBD]) - 0x19) & 0xFF == header[0xBD]:
            return "Game Boy Advance ROM", "GBA header fixed value and checksum"
    if suffix == ".nds" and len(header) >= 0x160:
        if (
            header[0x15C:0x15E] == b"\x56\xcf"
            and _crc16(header[0xC0:0x15C]) == 0xCF56
            and _crc16(header[:0x15E]) == int.from_bytes(header[0x15E:0x160], "little")
        ):
            return "Nintendo DS ROM", "Nintendo DS cartridge logo and header checksums"
    if suffix in {".3ds", ".cci", ".cxi"} and header[0x100:0x104] in {b"NCSD", b"NCCH"}:
        return "Nintendo 3DS image", "Nintendo 3DS NCSD/NCCH header"
    if suffix in {".iso", ".gcm"} and header[0x18:0x1C] == b"\x5d\x1c\x9e\xa3":
        return "Wii disc image", "Wii disc header"
    if suffix in {".iso", ".gcm"} and header[0x1C:0x20] == b"\xc2\x33\x9f\x3d":
        return "GameCube disc image", "GameCube disc header"
    if suffix == ".wbfs" and header.startswith(b"WBFS"):
        return "Wii Backup File System image", "WBFS header"
    if suffix == ".xci" and header[0x100:0x104] == b"HEAD":
        return "Switch cartridge container", "XCI header"
    if suffix == ".nsp" and header.startswith(b"PFS0"):
        return "Switch package container", "PFS0 header"
    if suffix == ".iso":
        if read_at(source, 32 * 2048, 20) == b"MICROSOFT*XBOX*MEDIA":
            return "Xbox XDVDFS disc image", "XDVDFS volume descriptor"
        descriptor = read_at(source, 16 * 2048, 7)
        if len(descriptor) == 7 and descriptor[1:] == b"CD001\x01":
            return "ISO 9660 image", "ISO 9660 volume descriptor"
    return None


def _probe(path: Path, relative: str):
    suffix = path.suffix.lower()
    fd = os.open(path, os.O_RDONLY | getattr(os, "O_NOFOLLOW", 0) | getattr(os, "O_NONBLOCK", 0))
    with os.fdopen(fd, "rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            return None
        prefix = source.read(HEADER_BYTES)
        if suffix == ".jar" and prefix.startswith(b"PK\x03\x04"):
            jar = inspect_jar(source)
            if jar:
                evidence = [f"JAR class headers ({jar['sampled_classes']}/{jar['total_classes']})"]
                if jar["main_class"]:
                    evidence.append("Manifest Main-Class: " + jar["main_class"])
                return Finding(
                    "Java", "JAR", relative, tuple(evidence),
                    dependencies=tuple(jar["dependencies"]), minimum_java=jar["java"],
                    preview_java=jar["preview"], entrypoint_detected=bool(jar["main_class"]),
                )
        if prefix.startswith(b"MZ") and (pe := inspect_pe(source, prefix)):
            architecture, library, dependencies = pe
            if not library and (version := _godot_embedded_major(source)):
                return Finding(
                    "Godot", "Godot PE with PCK", relative, ("PE signature", "Embedded Godot pack header"),
                    architecture, tuple(dependencies), engine_major=version,
                )
            return Finding(
                "Windows", "PE DLL" if library else "PE EXE", relative, ("PE signature",),
                architecture, tuple(dependencies), entrypoint_detected=not library,
            )
        if prefix.startswith(b"\x7fELF") and (elf := inspect_elf(source, prefix)):
            architecture, abi, dependencies, linker = elf
            evidence = (f"ELF header and ABI: {abi}",)
            if linker:
                evidence += (f"Interpreter: {linker}",)
            leaf = path.name.casefold()
            shared = leaf.endswith(".so") or ".so." in leaf
            if not shared and abi == "linux" and (version := _godot_embedded_major(source)):
                return Finding(
                    "Godot", "Godot ELF with PCK", relative, evidence + ("Embedded Godot pack header",),
                    architecture, tuple(dependencies), engine_major=version,
                )
            return Finding(
                "Linux" if abi == "linux" else "ELF", "ELF shared object" if shared else "ELF",
                relative, evidence, architecture, tuple(dependencies), entrypoint_detected=not shared,
            )
        if version := _godot_pack_major(prefix):
            return Finding(
                "Godot", "PCK", relative, (f"Godot pack header, major version {version}",),
                engine_major=version,
            )
        if suffix == ".rpy" and RENPY_START.search(prefix):
            return Finding("Ren'Py", "Ren'Py source", relative, ("Ren'Py start label",))
        if prefix.startswith((b"RPA-2.0 ", b"RPA-3.0 ", b"RPA-3.2 ", b"RENPY RPC2")):
            return Finding("Ren'Py", "Ren'Py archive or script", relative, ("Ren'Py file header",), entrypoint_detected=False)
        if prefix.startswith((b"UnityFS\0", b"UnityRaw\0", b"UnityWeb\0", b"UnityWebData1.0\0")):
            format_name = "Unity WebGL data" if prefix.startswith(b"UnityWebData") else "Unity assets"
            return Finding("Unity", format_name, relative, ("Unity asset signature",), entrypoint_detected=False)
        if path.name in {"globalgamemanagers", "globalgamemanagers.assets"} and _unity_serialized(prefix, source):
            return Finding("Unity", "Unity assets", relative, ("Unity serialized game settings",), entrypoint_detected=False)
        if prefix.startswith(b"\x0bLcfDataBase"):
            return Finding("RPG Maker 2000/2003", "LCF database", relative, ("LCF database signature",), entrypoint_detected=False)
        if prefix.startswith(b"\x0aLcfMapTree"):
            return Finding("RPG Maker 2000/2003", "LCF map tree", relative, ("LCF map-tree signature",), entrypoint_detected=False)
        if prefix.startswith(b"\x0aLcfMapUnit"):
            return Finding("RPG Maker 2000/2003", "LCF map", relative, ("LCF map signature",), entrypoint_detected=False)
        if suffix in RPG_MARSHAL and prefix.startswith(b"\x04\x08"):
            return Finding(RPG_MARSHAL[suffix], "RGSS data", relative, ("RGSS data and Ruby Marshal header",), entrypoint_detected=False)
        if suffix in RPG_ARCHIVE and prefix.startswith((b"RGSSAD\0\x01", b"RGSSAD\0\x03")):
            return Finding(RPG_ARCHIVE[suffix], "RGSS archive", relative, ("RGSS archive header",), entrypoint_detected=False)
        if path.name.casefold() == "game.ini" and (library := RGSS_LIBRARY.search(prefix)):
            scripts = RGSS_SCRIPTS.search(prefix)
            if scripts is not None:
                configured = scripts.group(1).decode("ascii", "replace").strip().replace("\\", "/")
                if ":" not in configured and all(part not in {"", ".", ".."} for part in configured.split("/")):
                    return Finding(
                        RGSS_FAMILY[library.group(1)], "RGSS game configuration", relative,
                        ("RGSS library declaration",), dependencies=(configured,), entrypoint_detected=False,
                    )
        if path.name.lower() in {"rpg_core.js", "rmmz_core.js"} and (marker := RPG_JS.search(prefix)):
            engine = "RPG Maker " + marker.group(2).decode("ascii")
            return Finding(engine, "RPG Maker JavaScript", relative, ("Engine identity in JavaScript",), entrypoint_detected=False)
        if suffix in {".html", ".htm"} and HTML.search(prefix):
            return Finding("HTML5", "HTML", relative, ("HTML markup",))
        if rom := _rom_format(source, prefix, suffix):
            return Finding("ROM/ISO", rom[0], relative, (rom[1],), entrypoint_detected=False)
    return None


def _godot_pack_major(header):
    if len(header) >= 20 and header.startswith(b"GDPC"):
        format_version = int.from_bytes(header[4:8], "little")
        version = int.from_bytes(header[8:12], "little")
        if 1 <= format_version <= 3 and 2 <= version < 10:
            return version
    return None


def _godot_embedded_major(source):
    size = os.fstat(source.fileno()).st_size
    footer = read_at(source, size - 12, 12)
    if len(footer) != 12 or footer[8:] != b"GDPC":
        return None
    pack_size = int.from_bytes(footer[:8], "little")
    if not 20 <= pack_size <= size - 12:
        return None
    return _godot_pack_major(read_at(source, size - 12 - pack_size, 20))


def _unity_serialized(header, source):
    if len(header) < 20 or header[16] not in {0, 1}:
        return False
    version = int.from_bytes(header[8:12], "big")
    if 9 <= version < 22:
        length = int.from_bytes(header[4:8], "big")
        offset = int.from_bytes(header[12:16], "big")
    elif 22 <= version <= 30 and len(header) >= 48:
        length = int.from_bytes(header[24:32], "big")
        offset = int.from_bytes(header[32:40], "big")
    else:
        return False
    return 20 <= offset <= length == os.fstat(source.fileno()).st_size


def _find_project_engines(findings):
    by_path = {Path(finding.path): finding for finding in findings}
    path_index = {path.as_posix().casefold(): finding for path, finding in by_path.items()}
    asset_directories = {
        parent for path, finding in by_path.items() if finding.format == "Unity assets"
        for parent in path.parents if parent.name.endswith("_Data")
    }
    lcf_maps = {path.parent for path, finding in by_path.items() if finding.format == "LCF map"}
    combined = []
    for path, finding in by_path.items():
        if finding.format == "RPG Maker JavaScript":
            project = path.parent.parent if path.parent.name.casefold() == "js" else path.parent
            html = by_path.get(project / "index.html")
            if html is not None and html.engine == "HTML5":
                combined.append(Finding(finding.engine, "RPG Maker web game", html.path, finding.evidence + html.evidence))
        if finding.format == "LCF database" and path.parent in lcf_maps:
            map_tree = path_index.get((path.parent / "RPG_RT.lmt").as_posix().casefold())
            if map_tree and map_tree.format == "LCF map tree":
                combined.append(Finding(
                    finding.engine, "LCF game directory", finding.path,
                    finding.evidence + map_tree.evidence + ("LCF map file",),
                ))
        if finding.format == "RGSS game configuration":
            scripts = path_index.get((path.parent / finding.dependencies[0]).as_posix().casefold())
            if scripts and scripts.format == "RGSS data" and scripts.engine == finding.engine:
                combined.append(Finding(
                    finding.engine, "RGSS game directory", finding.path,
                    finding.evidence + scripts.evidence,
                ))
        if finding.format not in {"PE EXE", "ELF"}:
            continue
        player_name = "UnityPlayer.dll" if finding.format == "PE EXE" else "UnityPlayer.so"
        player = path_index.get((path.parent / player_name).as_posix().casefold())
        data = path.parent / (path.stem + "_Data")
        player_format = "PE DLL" if finding.format == "PE EXE" else "ELF shared object"
        if player and player.format == player_format and player.architecture == finding.architecture and data in asset_directories:
            combined.append(Finding(
                "Unity", f"Unity {finding.format.split()[0]}", finding.path,
                finding.evidence + ("UnityPlayer binary and matching Unity assets",),
                finding.architecture, tuple(sorted(set(finding.dependencies) | set(player.dependencies))),
            ))
    return combined


def inspect_path(target: Path | str, *, entrypoint: str | None = None) -> Inspection:
    target = Path(target)
    findings = []
    scanned = 0
    unreadable = 0
    visited = 0
    truncated = False
    probed = set()

    def probe(path, relative):
        nonlocal scanned, unreadable
        if relative in probed:
            return
        probed.add(relative)
        scanned += 1
        try:
            found = _probe(path, relative)
        except (OSError, ValueError, OverflowError):
            unreadable += 1
        else:
            if found is not None:
                findings.append(found)

    def walk(directory, depth):
        nonlocal unreadable, visited, truncated
        try:
            with os.scandir(directory) as entries:
                for item in entries:
                    if visited >= SCAN_LIMIT:
                        truncated = True
                        break
                    visited += 1
                    if item.is_dir(follow_symlinks=False):
                        if depth < MAX_DEPTH:
                            walk(Path(item.path), depth + 1)
                        else:
                            truncated = True
                    elif item.is_file(follow_symlinks=False):
                        path = Path(item.path)
                        probe(path, path.relative_to(target).as_posix())
        except OSError:
            unreadable += 1

    try:
        mode = target.lstat().st_mode
    except OSError:
        unreadable += 1
    else:
        if stat.S_ISREG(mode):
            probe(target, target.name)
        elif stat.S_ISDIR(mode):
            explicit = Path(entrypoint) if entrypoint else None
            if explicit is not None and not explicit.is_absolute() and ".." not in explicit.parts:
                exact = target / explicit
                if all(not parent.is_symlink() for parent in exact.parents if parent.is_relative_to(target)):
                    probe(exact, explicit.as_posix())
            walk(target, 0)

    findings = list(dict.fromkeys(findings))
    findings.extend(_find_project_engines(findings))
    findings.sort(key=lambda item: (
        entrypoint is not None and item.path != entrypoint,
        not item.entrypoint_detected,
        PRIORITY.index(item.engine),
        item.path,
    ))
    return Inspection(tuple(findings), scanned, truncated, unreadable)
