#!/usr/bin/env python3
"""Bounded function-entry reader for sectionless x86-64 SCE ELF unwind indexes.

Adapted from runty/OpenCrown-PC, commit d6f8110d272f5d41276d5b30ba0936efe5d63985,
ios/game/unwind_atlas.py (source SHA256
29014dcacc56d4b7ac8dfde6a4a3430160fe46bfdf84151472bc6d783dcf9782).

This validates static metadata, not reachability or executable instruction
semantics. It never interprets CFI programs or LSDA personality behavior.
"""

from pathlib import Path
import hashlib
import json
import struct


def sha(data):
    return hashlib.sha256(data).hexdigest()


def need(value, message):
    if not value:
        raise ValueError(message)


def leb(data, pos, end, signed=False):
    value = 0
    shift = 0
    while pos < end and shift < 70:
        byte = data[pos]
        pos += 1
        value |= (byte & 127) << shift
        shift += 7
        if not byte & 128:
            if signed and byte & 64:
                value -= 1 << shift
            return value, pos
    raise ValueError("Truncated/oversized LEB128")


class Elf:
    def __init__(self, path, canonical, name=None):
        self.path = Path(path)
        self.name = name or self.path.stem
        self.canonical = canonical
        self.data = self.path.read_bytes()
        d = self.data
        need(
            len(d) >= 64 and d[:7] == b"\x7fELF\x02\x01\x01", "Not ELF64 little endian"
        )
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", d)
        need(h[2] == 62 and h[1] in (0xFE10, 0xFE18), "Unexpected SCE x86-64 ELF type")
        need(
            h[8] == 64
            and h[9] == 56
            and 0 < h[10] < 256
            and h[5] + h[9] * h[10] <= len(d),
            "Invalid ELF header/program table",
        )
        need(h[6] == 0 and h[12] == 0, "Expected sectionless SCE ELF")
        need(0 <= canonical < 2**64, "Canonical base exceeds 64-bit address space")
        self.header = {
            "type": hex(h[1]),
            "entry": hex(h[4]),
            "phoff": h[5],
            "phnum": h[10],
            "section_headers": 0,
        }
        self.programs = []
        for i in range(h[10]):
            t, f, o, v, p, fs, ms, a = struct.unpack_from(
                "<IIQQQQQQ", d, h[5] + i * h[9]
            )
            need(o + fs <= len(d), "Program file range exceeds ELF")
            self.programs.append(
                dict(
                    index=i,
                    type=t,
                    flags=f,
                    offset=o,
                    vaddr=v,
                    filesz=fs,
                    memsz=ms,
                    align=a,
                )
            )
        self.loads = [p for p in self.programs if p["type"] in (1, 0x61000010)]
        self.executable = [p for p in self.loads if p["flags"] & 1]
        headers = [p for p in self.programs if p["type"] == 0x6474E550]
        need(len(headers) == 1, "Expected one GNU EH frame header")
        need(self.executable, "No executable file mapping")
        self.eh = headers[0]
        self.base = self.eh["vaddr"]
        self.textbase = min(p["vaddr"] for p in self.executable)

    def file(self, va, size=1):
        choices = [
            p
            for p in self.loads
            if size <= p["filesz"]
            and va >= p["vaddr"]
            and va - p["vaddr"] <= p["filesz"] - size
        ]
        need(len(choices) == 1, "Unmapped or ambiguous file-backed VA span " + hex(va))
        p = choices[0]
        return p["offset"] + va - p["vaddr"]

    def encoded(self, pos, end, encoding, relative_base=None, nullable=False):
        need(encoding != 255, "Omitted required encoded value")
        need(
            not encoding & 0x80, "Indirect value unsupported as function-entry pointer"
        )
        location = pos
        fmt = encoding & 15
        application = encoding & 0x70
        if fmt in (1, 9):
            value, pos = leb(self.data, pos, end, fmt == 9)
        else:
            specs = {
                0: ("<Q", 8),
                2: ("<H", 2),
                3: ("<I", 4),
                4: ("<Q", 8),
                10: ("<h", 2),
                11: ("<i", 4),
                12: ("<q", 8),
            }
            need(fmt in specs, "Unknown pointer format " + hex(fmt))
            spec, size = specs[fmt]
            need(pos + size <= end, "Encoded pointer exceeds enclosing record")
            value = struct.unpack_from(spec, self.data, pos)[0]
            pos += size
        # Nullable encoded pointers preserve a raw zero before base addition.
        # None distinguishes null from a valid decoded virtual address zero.
        # Table offsets/ranges/counts are scalar values, not nullable pointers.
        if value == 0 and nullable:
            return None, pos
        if application == 0:
            pass
        elif application == 0x10:
            candidates = [
                p
                for p in self.loads
                if p["offset"] <= location < p["offset"] + p["filesz"]
            ]
            need(len(candidates) == 1, "Ambiguous pointer field mapping")
            p = candidates[0]
            value += p["vaddr"] + location - p["offset"]
        elif application == 0x20:
            value += self.textbase
        elif application == 0x30:
            need(relative_base is not None, "Missing data-relative base")
            value += relative_base
        else:
            raise ValueError("Unsupported pointer application " + hex(application))
        return value, pos

    def decode(self):
        e = self.eh
        d = self.data
        o = e["offset"]
        end = o + e["filesz"]
        need(self.file(e["vaddr"], e["filesz"]) == o, "EH header file/VA mismatch")
        need(e["filesz"] >= 4, "Truncated EH header")
        version, frame_enc, count_enc, table_enc = struct.unpack_from("4B", d, o)
        need(version == 1, "EH header version unsupported")
        frame, pos = self.encoded(o + 4, end, frame_enc, self.base, True)
        need(frame is not None, "Null EH frame pointer")
        count, pos = self.encoded(pos, end, count_enc, self.base)
        need(0 < count < 1000000, "EH count outside cap")
        table = []
        for index in range(count):
            pc, pos = self.encoded(pos, end, table_enc, self.base)
            fde, pos = self.encoded(pos, end, table_enc, self.base)
            table.append((pc, fde))
        need(
            all(table[i][0] < table[i + 1][0] for i in range(count - 1)),
            "EH lookup keys not strictly increasing",
        )
        need(len({fde for pc, fde in table}) == count, "Duplicate indexed FDE pointer")
        padding = d[pos:end]
        need(not any(padding), "Unexpected nonzero bytes after bounded EH index")
        # Walk framing independently, not by guessing record starts from index values.
        begin = self.file(frame, 1)
        limit = self.file(self.base, 0)
        need(begin < limit, "EH frame does not precede its header")
        need(
            self.file(frame, self.base - frame) == begin
            and begin + self.base - frame == limit,
            "EH region is not one contiguous file-backed span",
        )
        records = {}
        cursor = begin
        terminator = None
        while cursor < limit:
            need(cursor + 4 <= limit, "Truncated CFI record length")
            length = struct.unpack_from("<I", d, cursor)[0]
            if not length:
                terminator = cursor
                cursor += 4
                break
            need(
                length != 0xFFFFFFFF,
                "DWARF64 records unsupported by this pinned-table decoder",
            )
            record_end = cursor + 4 + length
            need(
                length >= 4 and record_end <= limit,
                "CFI record framing exceeds bounded EH region",
            )
            va = frame + cursor - begin
            identifier = struct.unpack_from("<I", d, cursor + 4)[0]
            records[va] = {
                "va": va,
                "file_offset": cursor,
                "length": length,
                "end": record_end,
                "identifier": identifier,
                "payload": cursor + 8,
            }
            cursor = record_end
        need(
            terminator is not None and not any(d[cursor:limit]),
            "Missing CFI terminator or nonzero post-terminator bytes",
        )
        cies = {}
        for va, record in records.items():
            if record["identifier"]:
                continue
            p = record["payload"]
            record_end = record["end"]
            need(p < record_end, "CIE payload absent")
            v = d[p]
            p += 1
            need(v in (1, 3, 4), "CIE version unsupported")
            string_end = d.find(b"\0", p, min(record_end, p + 65))
            need(string_end >= p, "CIE augmentation string malformed")
            augmentation = d[p:string_end].decode("ascii")
            p = string_end + 1
            if v == 4:
                need(d[p : p + 2] == b"\x08\0", "Unexpected CIE address/segment sizes")
                p += 2
            code, p = leb(d, p, record_end)
            data, p = leb(d, p, record_end, True)
            if v == 1:
                need(p < record_end, "Missing CIE return register")
                ret = d[p]
                p += 1
            else:
                ret, p = leb(d, p, record_end)
            need(
                code > 0 and data < 0 and ret == 16,
                "Unexpected x86-64 CIE alignment/return register",
            )
            enc = 0
            lsda = 255
            if augmentation:
                need(augmentation.startswith("z"), "Unsupported non-z CIE augmentation")
                amount, p = leb(d, p, record_end)
                aug_end = p + amount
                need(aug_end <= record_end, "CIE augmentation exceeds record")
                for char in augmentation[1:]:
                    if char == "R":
                        need(p < aug_end, "Missing R encoding")
                        enc = d[p]
                        p += 1
                    elif char == "L":
                        need(p < aug_end, "Missing L encoding")
                        lsda = d[p]
                        p += 1
                    elif char == "P":
                        need(p < aug_end, "Missing personality encoding")
                        personality = d[p]
                        p += 1
                        _, p = self.encoded(
                            p, aug_end, personality & 0x7F, self.base, True
                        )  # validate encoded field; do not dereference an indirect personality.
                    elif char == "S":
                        pass
                    else:
                        raise ValueError("Unknown CIE augmentation " + char)
                need(p == aug_end, "CIE augmentation byte count mismatch")
                p = aug_end
            cies[va] = {
                "version": v,
                "augmentation": augmentation,
                "code_alignment": code,
                "data_alignment": data,
                "return_register": ret,
                "fde_encoding": enc,
                "lsda_encoding": lsda,
                "cfi_bytes": record_end - p,
            }
        fdes = {}
        null_fdes = []
        for va, record in records.items():
            if not record["identifier"]:
                continue
            cie_va = va + 4 - record["identifier"]
            need(cie_va in cies and cie_va < va, "FDE refers outside framed prior CIE")
            cie = cies[cie_va]
            p = record["payload"]
            initial, p = self.encoded(
                p, record["end"], cie["fde_encoding"], self.base, True
            )
            size, p = self.encoded(
                p, record["end"], cie["fde_encoding"] & 15, self.base
            )
            need(
                size > 0
                and (
                    initial is None
                    or (
                        initial >= 0
                        and initial + size < 2**64
                        and self.canonical + initial + size < 2**64
                    )
                ),
                "Invalid FDE function range",
            )
            if cie["augmentation"].startswith("z"):
                aug_size, p = leb(d, p, record["end"])
                need(p + aug_size <= record["end"], "FDE augmentation exceeds frame")
                p += aug_size
            if initial is None:
                need(
                    va not in {fde for pc, fde in table},
                    "Indexed FDE has a null initial PC",
                )
                null_fdes.append(
                    {
                        "fde_va": va,
                        "cie_va": cie_va,
                        "file_offset": record["file_offset"],
                        "size": size,
                        "fde_sha256": sha(d[record["file_offset"] : record["end"]]),
                    }
                )
                continue
            executable = [
                s
                for s in self.executable
                if size <= s["filesz"]
                and initial >= s["vaddr"]
                and initial - s["vaddr"] <= s["filesz"] - size
            ]
            need(
                len(executable) == 1,
                "FDE range outside executable file bytes " + hex(initial),
            )
            fdes[va] = {
                "module": self.name,
                "pc": self.canonical + initial,
                "canonical_start": self.canonical + initial,
                "canonical_end": self.canonical + initial + size,
                "start": initial,
                "end": initial + size,
                "size": size,
                "fde_va": va,
                "fde_file_offset": record["file_offset"],
                "fde_bytes": record["length"] + 4,
                "fde_sha256": sha(d[record["file_offset"] : record["end"]]),
                "cie_va": cie_va,
                "cfi_bytes": record["end"] - p,
            }
        entries = []
        for pc, fde in table:
            need(fde in fdes, "Index FDE pointer is not an independently framed FDE")
            record = dict(fdes[fde])
            need(
                pc == record["start"],
                "EH table start differs from decoded FDE initial location",
            )
            entries.append(record)
        need(
            all(
                entries[i]["end"] <= entries[i + 1]["start"]
                for i in range(len(entries) - 1)
            ),
            "Overlapping FDE function ranges",
        )
        unindexed = set(fdes) - {fde for pc, fde in table}
        need(not unindexed, "Framed FDE/index inventory mismatch")
        report = {
            "module": self.name,
            "canonical": hex(self.canonical),
            "elf_path": str(self.path),
            "elf_sha256": sha(d),
            "elf_bytes": len(d),
            "elf_header": self.header,
            "program_headers": self.programs,
            "eh_header": e,
            "header_version": version,
            "frame_encoding": hex(frame_enc),
            "count_encoding": hex(count_enc),
            "table_encoding": hex(table_enc),
            "entry_count": count,
            "eh_frame_start": hex(frame),
            "eh_frame_end": hex(self.base),
            "frame_sha256": sha(d[begin:limit]),
            "header_sha256": sha(d[o:end]),
            "index_payload_end_file_offset": pos,
            "header_padding_bytes": len(padding),
            "header_padding_all_zero": True,
            "terminator_file_offset": terminator,
            "cie_count": len(cies),
            "cies": {hex(k): v for k, v in cies.items()},
            "fde_count": len(fdes),
            "total_framed_fdes": len(fdes) + len(null_fdes),
            "null_initial_fde_count": len(null_fdes),
            "unindexed_null_fdes": null_fdes,
            "strictly_sorted_unique_starts": True,
            "index_fdes_equal_nonnull_framed_fdes": True,
        }
        return entries, report

