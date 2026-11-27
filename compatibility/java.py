"""Bounded metadata inspection for JAR archives."""

import struct
import zipfile
import zlib


def _bounded_central_directory(source):
    footer = struct.Struct("<4s4H2IH")
    source.seek(0, 2)
    size = source.tell()
    tail_size = min(size, 65535 + footer.size)
    source.seek(-tail_size, 2)
    tail = source.read()
    position = tail.rfind(b"PK\x05\x06")
    if position < 0 or position + footer.size > len(tail):
        return False
    _, disk, directory_disk, disk_entries, entries, length, offset, comment = footer.unpack_from(tail, position)
    return (
        disk == directory_disk == 0
        and 0 < entries == disk_entries <= 20000
        and 0 < length <= 4 * 1024 * 1024
        and offset + length == size - tail_size + position
        and position + footer.size + comment == len(tail)
    )


def _attributes(raw):
    attributes = {}
    name = None
    for line in raw.replace(b"\r\n", b"\n").split(b"\n"):
        if not line:
            break
        if line.startswith(b" ") and name:
            attributes[name] += line[1:]
        elif b": " in line:
            name, value = line.split(b": ", 1)
            name = name.lower()
            attributes[name] = value
    return attributes


def inspect_jar(source):
    if not _bounded_central_directory(source):
        return None
    try:
        with zipfile.ZipFile(source) as archive:
            members = archive.infolist()
            if len(members) > 20000:
                return None
            classes = [
                item for item in members
                if item.filename.endswith(".class") and not item.filename.startswith("META-INF/versions/")
            ]
            if not classes:
                return None
            attributes = {}
            try:
                manifest = archive.getinfo("META-INF/MANIFEST.MF")
            except KeyError:
                pass
            else:
                if manifest.file_size <= 64 * 1024:
                    with archive.open(manifest) as data:
                        attributes = _attributes(data.read())
            main_class = attributes.get(b"main-class", b"").decode("utf-8", "replace").strip()
            main_path = main_class.replace(".", "/") + ".class"
            main_found = main_class and sum(item.filename == main_path for item in classes) == 1
            dependencies = attributes.get(b"class-path", b"").decode("utf-8", "replace").split()[:64]
            if len(classes) > 512 and main_found:
                main = archive.getinfo(main_path)
                classes = [main, *(item for item in classes if item != main)]
            versions = set()
            preview = False
            valid_main = False
            for item in classes[:512]:
                with archive.open(item) as data:
                    prefix = data.read(8)
                if len(prefix) == 8 and prefix[:4] == b"\xca\xfe\xba\xbe":
                    minor, major = struct.unpack(">HH", prefix[4:])
                    if 45 <= major < 256:
                        versions.add(major - 44)
                        preview |= minor == 65535
                        valid_main |= item.filename == main_path
            if not versions:
                return None
            return {
                "java": max(versions),
                "main_class": main_class if main_found and valid_main else None,
                "sampled_classes": min(len(classes), 512),
                "total_classes": len(classes),
                "preview": preview,
                "dependencies": dependencies,
            }
    except (zipfile.BadZipFile, OSError, RuntimeError, UnicodeError, ValueError, zlib.error):
        return None
