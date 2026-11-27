"""Validate and archive the output of an authorized PS5 application SDK."""

import argparse
import hashlib
import json
import re
import shutil
import stat
import struct
import sys
from pathlib import Path
from tempfile import NamedTemporaryFile
from zipfile import BadZipFile, ZIP_DEFLATED, ZipFile, ZipInfo

from security.installer import check_zip_directory

VERSION = "0.1.0"
PROJECT_ID = "org.akenogaming.hub"
TITLE_ID = re.compile(r"[A-Z]{4}[0-9]{5}\Z")
SELF_MAGIC = bytes.fromhex("4f153d1d")
PNG_MAGIC = b"\x89PNG\r\n\x1a\n"
MAX_FILES = 4096
MAX_BYTES = 2 * 1024**3
REQUIRED = {
    "eboot.bin", "sce_sys/param.json", "sce_sys/icon0.png",
    "assets/catalog.tsv", "assets/library.tsv",
}
PNG_HEADER = PNG_MAGIC + b"\x00\x00\x00\x0dIHDR" + struct.pack(">II", 512, 512)


def validate_module(path):
    with path.open("rb") as file:
        header = file.read(64)
        if len(header) != 64 or header[:8] != b"\x7fELF\x02\x01\x01\x09":
            raise ValueError("Modul muss ein FreeBSD-ELF64 sein.")
        kind, architecture = struct.unpack_from("<HH", header, 16)
        entry, offset, _, _, _, item_size, count = struct.unpack_from("<QQQIHHH", header, 24)
        if kind != 3 or architecture != 62 or entry or offset < 64 or item_size != 56 or not 0 < count <= 64:
            raise ValueError("Erwartet wird ein gelinktes x86-64-Shared-Modul ohne Startadresse.")
        file.seek(offset)
        table = file.read(item_size * count)
        if len(table) != item_size * count:
            raise ValueError("ELF-Programmheader sind unvollständig.")
        types = {struct.unpack_from("<I", table, index * item_size)[0] for index in range(count)}
        if not {1, 2} <= types or 3 in types:
            raise ValueError("Erwartet wird ein dynamisches Modul ohne eigenen Interpreter.")


def application_files(root):
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Anwendungsverzeichnis fehlt oder ist ein Symlink.")
    files = []
    occupied = set()
    total = 0
    for path in sorted(root.rglob("*")):
        parts = path.relative_to(root).parts
        if (path.is_symlink() or any("\\" in part or ":" in part or any(ord(c) < 32 for c in part)
                                     for part in parts)):
            raise ValueError(f"Ungültiger Dateipfad: {path}")
        key = "/".join(parts).casefold()
        if key in occupied:
            raise ValueError(f"Mehrdeutiger Dateipfad: {path}")
        occupied.add(key)
        if path.is_dir():
            continue
        if not path.is_file() or len(files) >= MAX_FILES:
            raise ValueError("Ungültiger Dateityp oder zu viele Dateien im Anwendungspaket.")
        files.append(path)
        total += path.stat().st_size
        if total > MAX_BYTES:
            raise ValueError("Das Anwendungspaket überschreitet 2 GiB.")
    return files


def read_prefix(path, size):
    with path.open("rb") as file:
        return file.read(size)


def validate_app(root, title_id):
    if not TITLE_ID.fullmatch(title_id):
        raise ValueError("Title-ID muss aus vier Großbuchstaben und fünf Ziffern bestehen.")
    files = application_files(root)
    relative = {path.relative_to(root).as_posix(): path for path in files}
    if missing := REQUIRED - relative.keys():
        raise ValueError("Anwendungsdateien fehlen: " + ", ".join(sorted(missing)))
    if any(name.casefold() == "build-manifest.json" for name in relative):
        raise ValueError("build-manifest.json ist für die Paketpipeline reserviert.")
    if relative["sce_sys/param.json"].stat().st_size > 64 * 1024:
        raise ValueError("sce_sys/param.json ist zu groß.")
    metadata = json.loads(relative["sce_sys/param.json"].read_text(encoding="utf-8"))
    if not isinstance(metadata, dict) or metadata.get("titleId") != title_id:
        raise ValueError("sce_sys/param.json muss die angegebene Title-ID enthalten.")
    eboot = relative["eboot.bin"]
    if eboot.stat().st_size < 128 or read_prefix(eboot, 4) != SELF_MAGIC:
        raise ValueError("eboot.bin hat keinen erwarteten SELF-Header. Verwende die autorisierte SDK-Ausgabe.")
    icon_header = read_prefix(relative["sce_sys/icon0.png"], 24)
    if icon_header != PNG_HEADER:
        raise ValueError("sce_sys/icon0.png muss ein PNG mit 512 × 512 Pixeln sein.")
    return relative


def zip_info(path):
    info = ZipInfo(path)
    info.compress_type = ZIP_DEFLATED
    info.external_attr = 0o100644 << 16
    return info


def digest(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def package(root, title_id, output):
    files = validate_app(root, title_id)
    if output.suffix.lower() != ".zip":
        raise ValueError("Das Ausgabeformat ist ZIP.")
    if output.resolve().is_relative_to(root.resolve()):
        raise ValueError("Das Archiv muss außerhalb des Anwendungsverzeichnisses liegen.")
    output.parent.mkdir(parents=True, exist_ok=True)
    with NamedTemporaryFile(dir=output.parent, delete=False) as temporary:
        staged = Path(temporary.name)
    try:
        entries = {}
        with ZipFile(staged, "w", compression=ZIP_DEFLATED, strict_timestamps=True) as archive:
            for name, source in files.items():
                sha256 = hashlib.sha256()
                length = 0
                with archive.open(zip_info(f"{title_id}/{name}"), "w", force_zip64=True) as writer, source.open("rb") as reader:
                    while chunk := reader.read(256 * 1024):
                        length += len(chunk)
                        sha256.update(chunk)
                        writer.write(chunk)
                entries[name] = {"sha256": sha256.hexdigest(), "bytes": length}
            manifest = {"project": PROJECT_ID, "title_id": title_id, "akeno_version": VERSION, "files": entries}
            archive.writestr(zip_info(f"{title_id}/build-manifest.json"), json.dumps(manifest, sort_keys=True, indent=2) + "\n")
        validate_archive(staged)
        staged.replace(output)
    finally:
        staged.unlink(missing_ok=True)
    output.with_suffix(output.suffix + ".sha256").write_text(f"{digest(output)}  {output.name}\n", encoding="utf-8")



def validate_archive(path):
    check_zip_directory(path, max_members=MAX_FILES + 1)
    with ZipFile(path) as archive:
        entries = archive.infolist()
        if len(entries) > MAX_FILES + 1 or sum(entry.file_size for entry in entries) > MAX_BYTES + 1024**2:
            raise ValueError("Archiv überschreitet Datei- oder Größenlimit.")
        by_name = {}
        title_id = None
        casefolded = set()
        for entry in entries:
            parts = entry.filename.split("/")
            namespace, *rest = parts
            if title_id is None:
                title_id = namespace
            member = "/".join(rest)
            mode = stat.S_IFMT(entry.external_attr >> 16)
            if (
                namespace != title_id or not TITLE_ID.fullmatch(namespace) or not rest
                or any(part in {"", ".", ".."} or "\\" in part or ":" in part or any(ord(c) < 32 for c in part)
                       for part in rest)
                or member.casefold() in casefolded or mode not in (0, stat.S_IFREG)
                or entry.flag_bits & 1
            ):
                raise ValueError("Archiv enthält einen ungültigen Pfad oder Dateityp.")
            casefolded.add(member.casefold())
            by_name[member] = entry
        for member in casefolded:
            folders = member.split("/")
            if any("/".join(folders[:length]) in casefolded for length in range(1, len(folders))):
                raise ValueError("Eine Datei kollidiert mit einem Verzeichnis im Archiv.")
        manifest_entry = by_name.pop("build-manifest.json", None)
        if manifest_entry is None or manifest_entry.file_size > 1024**2:
            raise ValueError("Build-Manifest fehlt oder ist zu groß.")
        manifest = json.loads(archive.read(manifest_entry))
        if (not isinstance(manifest, dict) or manifest.get("project") != PROJECT_ID or manifest.get("title_id") != title_id
                or not isinstance(manifest.get("files"), dict) or manifest["files"].keys() != by_name.keys()
                or not REQUIRED <= by_name.keys()):
            raise ValueError("Build-Manifest oder erforderliche App-Dateien passen nicht zum Archiv.")
        for filename, entry in by_name.items():
            with archive.open(entry) as stream:
                checksum = hashlib.file_digest(stream, "sha256").hexdigest()
            if manifest["files"][filename] != {"sha256": checksum, "bytes": entry.file_size}:
                raise ValueError(f"SHA-256 oder Dateigröße stimmt nicht: {filename}")
        param = by_name["sce_sys/param.json"]
        if param.file_size > 64 * 1024:
            raise ValueError("sce_sys/param.json ist zu groß.")
        metadata = json.loads(archive.read(param))
        if not isinstance(metadata, dict) or metadata.get("titleId") != title_id:
            raise ValueError("Die Title-ID in sce_sys/param.json passt nicht zur App.")
        with archive.open(by_name["eboot.bin"]) as stream:
            if by_name["eboot.bin"].file_size < 128 or stream.read(4) != SELF_MAGIC:
                raise ValueError("eboot.bin hat keinen erwarteten SELF-Header.")
        with archive.open(by_name["sce_sys/icon0.png"]) as stream:
            if stream.read(24) != PNG_HEADER:
                raise ValueError("sce_sys/icon0.png muss ein PNG mit 512 × 512 Pixeln sein.")

def stage_assets(root, catalog):
    if root.is_symlink() or not root.is_dir():
        raise ValueError("Anwendungsverzeichnis fehlt oder ist ein Symlink.")
    icon = Path(__file__).resolve().parents[1] / "platform/ps5/assets/icon0.png"
    inputs = [
        (icon, root / "sce_sys/icon0.png"),
        (catalog / "catalog.tsv", root / "assets/catalog.tsv"),
        (catalog / "library.tsv", root / "assets/library.tsv"),
    ]
    for source, target in inputs:
        if not source.is_file() or target.parent.is_symlink() or target.is_symlink():
            raise ValueError(f"Quelle fehlt oder Ziel ist ein Symlink: {source} → {target}")
    for source, target in inputs:
        target.parent.mkdir(exist_ok=True)
        if target.exists() and source.samefile(target):
            continue
        with NamedTemporaryFile(dir=target.parent, delete=False) as temporary:
            staged = Path(temporary.name)
        try:
            shutil.copyfile(source, staged)
            staged.replace(target)
        finally:
            staged.unlink(missing_ok=True)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    validate = commands.add_parser("validate")
    targets = validate.add_subparsers(dest="kind", required=True)
    targets.add_parser("module").add_argument("path", type=Path)
    targets.add_parser("archive").add_argument("path", type=Path)
    application = targets.add_parser("app")
    application.add_argument("path", type=Path)
    application.add_argument("--title-id", required=True)
    stage = commands.add_parser("assets")
    stage.add_argument("path", type=Path)
    stage.add_argument("catalog", type=Path)
    pack = commands.add_parser("package")
    pack.add_argument("path", type=Path)
    pack.add_argument("output", type=Path)
    pack.add_argument("--title-id", required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == "assets":
            stage_assets(args.path, args.catalog)
            print(f"Akeno-Assets kopiert: {args.path}")
        elif args.command == "package":
            package(args.path, args.title_id, args.output)
            print(f"Archiv erzeugt: {args.output}")
        elif args.kind == "archive":
            validate_archive(args.path)
            print("Archivstruktur, Build-Manifest und SHA-256 der Einträge geprüft.")
        elif args.kind == "module":
            validate_module(args.path)
            print("Gelinktes x86-64-FreeBSD-Modul geprüft.")
        else:
            files = validate_app(args.path, args.title_id)
            print(f"Anwendungsstruktur geprüft ({len(files)} Dateien).")
    except (OSError, ValueError, BadZipFile, NotImplementedError, RuntimeError) as error:
        print(str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
