"""Reuse guarded trace preparation within one immutable discovery process.

The cache stores small metadata only, not IR text or executable objects. Every
hit hashes both the raw input and the generated file; a changed/missing output
is freshly instrumented. It never persists metadata across tool versions or
processes. The producer still validates its source snapshot before publishing.
"""
from dataclasses import dataclass
import hashlib
from pathlib import Path
import re
from guard_ir import instrument

_DEFINITIONS=re.compile(r"^define ptr @sub_([0-9a-f]+)\(",re.M)
_DECLARATIONS=re.compile(r"^declare ptr @sub_([0-9a-f]+)\(",re.M)
_EXTENDED=re.compile(r"\b(?:fcmp|fpext|fptrunc|fadd|fsub|fmul|fdiv|load|store|call)\b[^\n]*\bx86_fp80\b")
_REMILL_ERROR=re.compile(r"\bcall\b[^\n]*@__remill_error\b")

@dataclass(frozen=True)
class TraceAnalysis:
    raw_sha256: str
    extended_precision: bool
    remill_error: bool
    definitions: tuple
    declarations: tuple

@dataclass(frozen=True)
class PreparedTrace:
    raw_sha256: str
    guarded_sha256: str
    guards_inserted: int
    definitions: tuple
    declarations: tuple

class TracePreparationCache:
    def __init__(self, *, libc_helpers=False, image_sha256=None):
        self.libc_helpers=libc_helpers
        self.image_sha256=image_sha256
        self._entries={}
        self._analysis={}
        self.hits=0
        self.misses=0
        self.analysis_hits=0
        self.analysis_misses=0

    def analyze(self, raw: str) -> TraceAnalysis:
        raw_sha=hashlib.sha256(raw.encode()).hexdigest()
        previous=self._analysis.get(raw_sha)
        if previous:
            self.analysis_hits+=1
            return previous
        analysis=TraceAnalysis(raw_sha,bool(_EXTENDED.search(raw)),bool(_REMILL_ERROR.search(raw)),
                               tuple(_DEFINITIONS.findall(raw)),tuple(_DECLARATIONS.findall(raw)))
        self._analysis[raw_sha]=analysis
        self.analysis_misses+=1
        return analysis

    def prepare(self, raw: str, output: Path) -> PreparedTrace:
        analysis=self.analyze(raw)
        raw_sha=analysis.raw_sha256
        key=(raw_sha,self.libc_helpers,self.image_sha256)
        previous=self._entries.get(output)
        if previous and previous[0]==key and output.is_file():
            actual=hashlib.sha256(output.read_bytes()).hexdigest()
            if actual==previous[1].guarded_sha256:
                self.hits+=1
                return previous[1]
        guarded,count=instrument(raw,libc_helpers=self.libc_helpers,image_sha256=self.image_sha256)
        encoded=guarded.encode();guarded_sha=hashlib.sha256(encoded).hexdigest()
        prepared=PreparedTrace(raw_sha,guarded_sha,count,analysis.definitions,analysis.declarations)
        # Avoid touching a correct file even when this process has no metadata
        # yet. Compilation still uses the exact guarded-content object stamp.
        if not output.is_file() or hashlib.sha256(output.read_bytes()).hexdigest()!=guarded_sha:
            output.write_bytes(encoded)
        self._entries[output]=(key,prepared)
        self.misses+=1
        return prepared
