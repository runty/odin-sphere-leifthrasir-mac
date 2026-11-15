#!/usr/bin/env python3
"""Exercise bounded unwind decoding using owned synthetic sectionless ELF/CFI."""

import argparse
import json
from pathlib import Path
import struct
from unwind_reader import Elf

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
root = args.output.resolve()
if (
    len(root.parts) < 3
    or root.parts[1] != "Volumes"
    or not Path("/Volumes", root.parts[2]).is_mount()
):
    parser.error("Output must be on a mounted external disk")
root.mkdir(parents=True, exist_ok=True)
checks = 0


def require(okay):
    global checks
    checks += 1
    if not okay:
        raise AssertionError(f"Unwind check {checks} failed")


def fixture():
    # File mapping starts at 0x1000; valid code begins at virtual address zero.
    # This distinguishes a decoded RVA zero from a raw encoded null pointer.
    data = bytearray(0x141C)
    ident = b"\x7fELF\x02\x01\x01" + bytes(9)
    struct.pack_into(
        "<16sHHIQQQIHHHHHH",
        data,
        0,
        ident,
        0xFE10,
        62,
        1,
        0,
        64,
        0,
        0,
        64,
        56,
        2,
        0,
        0,
        0,
    )
    struct.pack_into("<IIQQQQQQ", data, 64, 1, 5, 0x1000, 0, 0, 0x41C, 0x1000, 0x1000)
    struct.pack_into("<IIQQQQQQ", data, 120, 0x6474E550, 4, 0x1400, 0x400, 0, 28, 28, 4)
    data[0x1000] = data[0x1040] = 0xC3  # Owned one-instruction RET functions.
    payload = (
        bytes([1]) + b"zR\0" + bytes([1, 0x78, 16, 1, 0x1B, 0x0C, 7, 8, 0x90, 1, 0, 0])
    )
    require(len(payload) == 16)
    struct.pack_into("<II", data, 0x1300, 20, 0)
    data[0x1308:0x1318] = payload
    for offset, start in [(0x318, 0), (0x32C, 0x40)]:
        struct.pack_into(
            "<IIii",
            data,
            0x1000 + offset,
            16,
            offset + 4 - 0x300,
            start - (offset + 8),
            1,
        )
    data[0x1400:0x1404] = bytes([1, 0x1B, 3, 0x3B])
    struct.pack_into(
        "<iIiiii",
        data,
        0x1404,
        0x300 - 0x404,
        2,
        -0x400,
        0x318 - 0x400,
        0x40 - 0x400,
        0x32C - 0x400,
    )
    return data


base = fixture()
path = root / "owned.elf"
path.write_bytes(base)
entries, report = Elf(path, 0x20000000, "owned").decode()
require([row["pc"] for row in entries] == [0x20000000, 0x20000040])
require([row["size"] for row in entries] == [1, 1])
require(report["fde_count"] == 2 and report["null_initial_fde_count"] == 0)
# Scalar data-relative zero is base+0; a nullable raw pointer zero stays null.
zero = bytearray(base)
struct.pack_into("i", zero, 0x140C, 0)
path.write_bytes(zero)
elf = Elf(path, 0)
require(elf.encoded(0x140C, 0x141C, 0x3B, 0x400)[0] == 0x400)
require(elf.encoded(0x140C, 0x141C, 0x3B, 0x400, True)[0] is None)
# A discarded, unindexed FDE is still framed and augmentation-bounds checked.
discarded = bytearray(base)
struct.pack_into("i", discarded, 0x1334, 0)
struct.pack_into("I", discarded, 0x1408, 1)
discarded[0x1414:0x141C] = bytes(8)
path.write_bytes(discarded)
entries, report = Elf(path, 0x20000000).decode()
require(len(entries) == 1 and entries[0]["start"] == 0)
require(report["null_initial_fde_count"] == 1 and report["header_padding_bytes"] == 8)


def reject(label, edits):
    value = bytearray(base)
    edits(value)
    path.write_bytes(value)
    try:
        Elf(path, 0x20000000).decode()
    except ValueError:
        require(True)
    else:
        raise AssertionError("Accepted invalid fixture: " + label)


reject("version", lambda b: b.__setitem__(0x1400, 2))
reject("omitted frame", lambda b: b.__setitem__(0x1401, 255))
reject("excess count", lambda b: struct.pack_into("I", b, 0x1408, 3))
reject("nonzero padding", lambda b: struct.pack_into("I", b, 0x1408, 1))
reject("unsorted keys", lambda b: struct.pack_into("i", b, 0x140C, 0x80 - 0x400))
reject("duplicate FDE", lambda b: struct.pack_into("i", b, 0x1418, 0x318 - 0x400))
reject("lookup/FDE mismatch", lambda b: struct.pack_into("i", b, 0x140C, 1 - 0x400))
reject("DWARF64 record", lambda b: struct.pack_into("I", b, 0x1300, 0xFFFFFFFF))
reject("bad CIE reference", lambda b: struct.pack_into("I", b, 0x131C, 0x100000))
reject("nonzero terminator padding", lambda b: b.__setitem__(0x1348, 1))
reject("zero range", lambda b: struct.pack_into("i", b, 0x1324, 0))
reject("overlapping functions", lambda b: struct.pack_into("i", b, 0x1324, 0x80))
reject("indirect function pointer", lambda b: b.__setitem__(0x1310, 0x9B))
reject("program table cap", lambda b: struct.pack_into("H", b, 56, 65535))
reject("function outside mapping", lambda b: struct.pack_into("i", b, 0x1338, 0x10000))
reject("FDE augmentation bounds", lambda b: b.__setitem__(0x1328, 127))
reject("unsupported CIE augmentation", lambda b: b.__setitem__(0x130A, ord("X")))
path.write_bytes(base)
try:
    Elf(path, 2**64).decode()
except ValueError:
    require(True)

result = {"checks": checks, "failed": 0, "owned_fixture_only": True}
(root / "results.json").write_text(json.dumps(result, indent=2) + "\n")
print(json.dumps(result))
