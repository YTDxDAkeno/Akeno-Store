"""Bounded parsing for executable signatures and declared native dependencies."""

import struct


PE_MACHINES = {0x14C: "x86", 0x8664: "x86_64", 0x1C4: "arm", 0xAA64: "arm64"}
ELF_MACHINES = {3: "x86", 62: "x86_64", 40: "arm", 183: "arm64", 243: "riscv64"}


def read_at(source, offset, count):
    if offset < 0:
        return b""
    source.seek(offset)
    return source.read(count)


def asciiz(raw):
    end = raw.find(b"\0")
    value = raw[:end] if end > 0 else b""
    if not value or any(character < 33 or character > 126 for character in value):
        return None
    return value.decode("ascii")


def inspect_pe(source, header):
    if not header.startswith(b"MZ") or len(header) < 64:
        return None
    offset, = struct.unpack_from("<I", header, 60)
    if not 64 <= offset <= 1024 * 1024:
        return None
    coff = read_at(source, offset, 24)
    if len(coff) != 24 or not coff.startswith(b"PE\0\0"):
        return None
    machine, sections = struct.unpack_from("<HH", coff, 4)
    optional_size, flags = struct.unpack_from("<HH", coff, 20)
    if not 1 <= sections <= 96 or not 2 <= optional_size <= 512:
        return None
    optional = read_at(source, offset + 24, optional_size)
    if len(optional) != optional_size:
        return None
    magic, = struct.unpack_from("<H", optional)
    directory_at = {0x10B: 96, 0x20B: 112}.get(magic)
    if directory_at is None or optional_size < directory_at:
        return None
    imports = set()
    if optional_size >= directory_at + 16:
        count, = struct.unpack_from("<I", optional, directory_at - 4)
        if count >= 2:
            imports_rva, imports_size = struct.unpack_from("<II", optional, directory_at + 8)
            if imports_rva and imports_size:
                base = offset + 24 + optional_size
                imports = _pe_imports(source, base, sections, imports_rva, imports_size)
    return PE_MACHINES.get(machine, f"pe-{machine:#x}"), bool(flags & 0x2000), sorted(imports)


def _pe_imports(source, offset, sections, imports_rva, imports_size):
    mappings = []
    table = read_at(source, offset, sections * 40)
    for position in range(0, len(table) - 39, 40):
        rva, size, start = struct.unpack_from("<III", table, position + 12)
        mappings.append((rva, size, start))

    def resolve(address):
        return next(((begin + address - rva, size - (address - rva))
                     for rva, size, begin in mappings if rva <= address < rva + size), None)

    imports = set()
    for index in range(min(imports_size // 20, 64)):
        location = resolve(imports_rva + 20 * index)
        if location is None or location[1] < 20:
            break
        entry = read_at(source, location[0], 20)
        if len(entry) != 20 or not any(entry):
            break
        address, = struct.unpack_from("<I", entry, 12)
        location = resolve(address)
        if location is not None:
            name = asciiz(read_at(source, location[0], min(location[1], 128)))
            if name:
                imports.add(name.lower())
    return imports


def inspect_elf(source, header):
    if not header.startswith(b"\x7fELF") or len(header) < 52:
        return None
    bits, order, version, os_abi = header[4:8]
    if bits not in {1, 2} or order not in {1, 2} or version != 1:
        return None
    wide = bits == 2
    endian = "<" if order == 1 else ">"
    if wide and len(header) < 64:
        return None
    object_type, machine, elf_version = struct.unpack_from(endian + "HHI", header, 16)
    if elf_version != 1 or object_type not in {2, 3}:
        return None
    header_size, = struct.unpack_from(endian + "H", header, 52 if wide else 40)
    if header_size != (64 if wide else 52):
        return None
    program_offset, = struct.unpack_from(endian + ("Q" if wide else "I"), header, 32 if wide else 28)
    entry_size, count = struct.unpack_from(endian + "HH", header, 54 if wide else 42)
    elf_abi = {0: "system-v", 3: "linux", 9: "freebsd"}.get(os_abi, f"elf-osabi-{os_abi}")
    dependencies, interpreter = _elf_dependencies(source, endian, wide, program_offset, entry_size, count)
    if elf_abi == "system-v" and interpreter and (
        interpreter.startswith("/lib/") or interpreter.startswith("/lib64/")
    ) and ("ld-linux" in interpreter or "ld-musl" in interpreter):
        elf_abi = "linux"
    architecture = ELF_MACHINES.get(machine, f"elf-{machine:#x}")
    if machine == 243 and not wide:
        architecture = "riscv32"
    return architecture, elf_abi, sorted(dependencies), interpreter


def _elf_dependencies(source, endian, wide, offset, entry_size, count):
    if entry_size != (56 if wide else 32) or not 0 < count <= 128:
        return set(), None
    table = read_at(source, offset, entry_size * count)
    loads = []
    dynamic = None
    interpreter = None
    for position in range(0, len(table) - entry_size + 1, entry_size):
        if wide:
            kind, _, file_offset, address, _, file_size = struct.unpack_from(endian + "IIQQQQ", table, position)
        else:
            kind, file_offset, address, _, file_size = struct.unpack_from(endian + "IIIII", table, position)
        if kind == 1:
            loads.append((address, file_offset, file_size))
        elif kind == 2:
            dynamic = (file_offset, file_size)
        elif kind == 3 and 0 < file_size <= 256:
            interpreter = asciiz(read_at(source, file_offset, file_size))
    if dynamic is None:
        return set(), interpreter
    tags = struct.Struct(endian + ("QQ" if wide else "II"))
    data = read_at(source, dynamic[0], min(dynamic[1], 16 * 1024))
    names = []
    strings = None
    string_size = None
    for position in range(0, len(data) - tags.size + 1, tags.size):
        tag, value = tags.unpack_from(data, position)
        if tag == 0:
            break
        if tag == 1 and len(names) < 64:
            names.append(value)
        elif tag == 5:
            strings = value
        elif tag == 10:
            string_size = value
    dependencies = set()
    if strings is not None and string_size is not None:
        for name in names:
            if name >= string_size:
                continue
            location = strings + name
            for address, file_offset, size in loads:
                if address <= location < address + size:
                    available = min(128, string_size - name, address + size - location)
                    dependency = asciiz(read_at(source, file_offset + location - address, available))
                    if dependency:
                        dependencies.add(dependency)
                    break
    return dependencies, interpreter
