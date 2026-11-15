"""Bounded owned metadata utilities; no game-specific byte signatures or addresses."""

from pathlib import Path
import csv, json, struct, hashlib


def number(value):
    return int(value, 16) if isinstance(value, str) else int(value)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


class Elf:
    def __init__(self, path):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        d = self.data
        if len(d) < 64 or d[:7] != b"\x7fELF\x02\x01\x01":
            raise ValueError("Expected ELF64 little endian")
        h = struct.unpack_from("<16sHHIQQQIHHHHHH", d)
        if (
            h[2] != 62
            or h[9] != 56
            or not 0 < h[10] <= 256
            or h[5] > len(d) - h[9] * h[10]
        ):
            raise ValueError("Invalid x86-64 program header table")
        self.loads = []
        for i in range(h[10]):
            kind, flags, off, va, pa, fs, ms, align = struct.unpack_from(
                "<IIQQQQQQ", d, h[5] + i * h[9]
            )
            if off > len(d) or fs > len(d) - off or va + ms > 2**64:
                raise ValueError("Invalid ELF load bounds")
            if kind in (1, 0x61000010):
                if ms < fs:
                    raise ValueError("ELF load memory span shorter than file bytes")
                self.loads.append((va, va + fs, off, flags))
        executable = [s for s in self.loads if s[3] & 1]
        if len(executable) != 1 or executable[0][0] != 0:
            raise ValueError(
                "Current catalog requires one executable file mapping beginning at image offset0"
            )
        self.limit = executable[0][1]

    def file_match(self, va, data, readonly=False):
        n = len(data)
        matches = [
            s
            for s in self.loads
            if s[0] <= va <= s[1] - n and (not readonly or not s[3] & 2)
        ]
        if len(matches) != 1:
            raise ValueError("Unmapped, ambiguous, or writable table span")
        start, end, off, flags = matches[0]
        off += va - start
        if self.data[off : off + n] != data:
            raise ValueError("Prepared image bytes differ from ELF file span")
        return off, flags


def load_elf(path):
    return Elf(path)


def load_atlas(path, module, base, limit):
    path = Path(path)
    if path.suffix == ".json":
        rows = json.loads(path.read_text())
        if not isinstance(rows, list):
            raise ValueError("Atlas JSON must be a list of entries")
    else:
        rows = list(csv.DictReader(path.open(), delimiter="\t"))
    rows = [r for r in rows if r.get("module") == module]
    out = []
    for row in rows:
        r = dict(row)
        lo = number(r["canonical_start"])
        hi = number(r["canonical_end"])
        if not base <= lo < hi <= base + limit or hi >= 2**64:
            raise ValueError("Atlas owner outside executable mapping")
        r["canonical_start"] = f"{lo:x}"
        r["canonical_end"] = f"{hi:x}"
        r.setdefault("fde_va", None)
        out.append(r)
    out.sort(key=lambda r: number(r["canonical_start"]))
    if not out:
        raise ValueError("No atlas roots selected")
    for a, b in zip(out, out[1:]):
        if number(a["canonical_end"]) > number(b["canonical_start"]):
            raise ValueError("Atlas owners overlap")
    return out
