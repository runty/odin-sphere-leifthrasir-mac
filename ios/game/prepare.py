#!/usr/bin/env python3
"""Validate OSLBOOT1 metadata and prepare an explicit-address private AOT image."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--image", required=True, type=Path)
p.add_argument("--output", required=True, type=Path)
a = p.parse_args()
out = a.output.resolve()
if len(out.parts) < 3 or out.parts[1] != "Volumes" or not Path("/Volumes", out.parts[2]).is_mount():
    p.error("Output requires a mounted external disk")
out.mkdir(parents=True, exist_ok=True)
boot = (a.image / "boot.bin").read_bytes()
report = json.loads((a.image / "image.json").read_text())
if report["eboot_sha256"] != "7c517194aba4e5eea55d9197efae47d1567bd785b8c0dced7d1b0f4028d3eed6":
    p.error("Unsupported game profile")
at = 0


def take(fmt):
    global at
    size = struct.calcsize(fmt)
    if at + size > len(boot):
        raise ValueError("Truncated boot image")
    result = struct.unpack_from(fmt, boot, at)
    at += size
    return result


magic, size, entry, ns, nr, ni, init_ret = take("<8s6Q")
if magic != b"OSLBOOT1" or size != report["image_bytes"] or not (0 < ns < 64 and nr < 1000000 and ni < 4096):
    p.error("Invalid boot-image header")
procparam, = take("<Q")
main_tls = take("<4Q")
nm, = take("<Q")
if not 0 < nm < 8:
    p.error("Invalid linked-module count")
modules = [take("<7Q") for _ in range(nm)]
nb, = take("<Q")
if nb > ni:
    p.error("Invalid native-binding count")
bindings = {}
for _ in range(nb):
    index, address, kind = take("<3Q")
    if index >= ni or address >= size or kind not in (1, 2) or index in bindings:
        p.error("Invalid native binding")
    bindings[index] = (address, kind)
segments = [take("<3Q") for _ in range(ns)]
for address, length, flags in segments:
    if address > size or length > size - address or flags > 7:
        p.error("Invalid mapped segment")
names = []
for _ in range(ni):
    raw, = take("<128s")
    if b"\0" not in raw:
        p.error("Invalid import name")
    names.append(raw.split(b"\0")[0].decode("ascii"))
relocs = [take("<4Q") for _ in range(nr)]
if len(boot) - at != size:
    p.error("Incorrect boot-image payload bounds")
image = bytearray(boot[at:])
base, hle = 0x10000000, 0x60000000
data_imports = set()
image_pointers = set()
for target, kind, value, addend in relocs:
    if target > size - 8 or kind > 2 or (kind and value >= ni) or addend >= 4096:
        p.error("Invalid relocation")
    if kind == 0:
        if addend:
            p.error("Unexpected relative addend")
        pointer = (base + value) & ((1 << 64) - 1)
        image_pointers.add(target)
    elif value in bindings:
        address, binding_kind = bindings[value]
        if binding_kind != kind:
            p.error("Binding kind mismatch")
        pointer = base + address + addend
        image_pointers.add(target)
    else:
        pointer = hle + value * 0x1000 + (0x100 if kind == 2 else 0) + addend
        if kind == 2:
            data_imports.add(value)
    struct.pack_into("<Q", image, target, pointer)
mapping = dict(re.findall(r'\{"([^"\n]+)","([^"\n]+)"\}',
    (Path(__file__).resolve().parents[2] / "src/import_names.inc").read_text()))
imports = [{"nid": name, "symbol": mapping.get(name, name), "data": i in data_imports,
            "native": i in bindings} for i, name in enumerate(names)]
manifest = {"base": base, "hle_base": hle, "size": size, "entry": entry, "procparam": procparam,
            "main_tls": main_tls, "modules": modules, "segments": segments, "imports": imports,
            "boot_sha256": hashlib.sha256(boot).hexdigest(), "image_sha256": hashlib.sha256(image).hexdigest(),
            "image_pointer_count": len(image_pointers)}
(out / "image.bin").write_bytes(image)
pointer_bytes = b"".join(struct.pack("<Q", offset) for offset in sorted(image_pointers))
(out / "image-pointers.bin").write_bytes(pointer_bytes)
manifest["image_pointer_sha256"] = hashlib.sha256(pointer_bytes).hexdigest()
(out / "image.json").write_text(json.dumps(manifest, indent=2) + "\n")
print(json.dumps({"bytes": size, "segments": ns, "relocations": nr, "imports": ni, "native": nb,
                  "data_imports": [imports[i]["symbol"] for i in data_imports]}, indent=2))
