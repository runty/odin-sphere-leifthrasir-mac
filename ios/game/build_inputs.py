"""Ordered LLVM response-file inputs, following OpenCrown's mobile archiver."""
from pathlib import Path
import shlex


def response_inputs(path: Path, objects) -> str:
    # LLVM's GNU command-line tokenizer accepts shell-style quoting; no shell
    # executes this file. Preserve input order and literal special characters.
    path.write_text('\n'.join(shlex.quote(str(obj)) for obj in objects)+'\n')
    return '@'+str(path)
