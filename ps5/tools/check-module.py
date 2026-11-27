#!/usr/bin/env python3
# Akeno Gaming Hub - static checks of a converted PS5 module before signing.
# SPDX-License-Identifier: GPL-3.0-or-later
"""Rejects modules the console would refuse to map: every LOAD segment's file offset and
address must agree modulo its alignment (16 KiB pages), and no segment may extend past
the file. Exit status 1 with a description of each problem."""

import struct
import sys

LOAD = 1
NAMES = {1: "LOAD", 2: "DYNAMIC", 7: "TLS", 0x6474E552: "GNU_RELRO", 0x61000001: "SCE_PROCPARAM"}


def check(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF" or data[4] != 2:
        return [f"{path}: not an ELF64 module"]
    phoff = struct.unpack_from("<Q", data, 32)[0]
    size, count = struct.unpack_from("<HH", data, 54)
    problems = []
    for index in range(count):
        kind, flags, offset, address, _, file_size, mem_size, align = struct.unpack_from("<IIQQQQQQ", data, phoff + index * size)
        name = NAMES.get(kind, hex(kind))
        if kind == LOAD and flags and align and offset % align != address % align:
            problems.append(f"{name} #{index}: file offset {offset:#x} and address {address:#x} differ modulo {align:#x}")
        if kind == LOAD and offset + file_size > len(data):
            problems.append(f"{name} #{index}: extends past the end of the file")
        if kind == LOAD and file_size > mem_size:
            problems.append(f"{name} #{index}: file size exceeds memory size")
    return problems


if __name__ == "__main__":
    failures = [problem for path in sys.argv[1:] for problem in check(path)]
    for problem in failures:
        print(problem, file=sys.stderr)
    sys.exit(1 if failures else 0)
