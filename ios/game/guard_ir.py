"""Insert a bounded-runtime checkpoint at trace entries and every CFG backedge."""
import re

_ABI = re.compile('define ptr @sub_[0-9a-f]+\\(ptr noalias %state, i64 %program_counter, ptr noalias %memory\\) \\{')
_SYMBOL = re.compile('@sub_([0-9a-f]+)\\(')
_LABELS = re.compile('^([A-Za-z0-9_.]+):', re.M)
_BRANCH = re.compile('\\s*(?:br|switch|indirectbr)\\b')
_TABLE_BRANCH = re.compile('\\s*(?:switch|indirectbr)\\b')
_TARGETS = re.compile('label %([A-Za-z0-9_.]+)')
_FUNCTIONS = re.compile('^(define [^\\n]+)\\n(.*?)^}', re.M | re.S)


def instrument(text, libc_helpers=False, image_sha256=None):
    if libc_helpers and image_sha256 != '51a38d7dc7476f0163c27273eccd0c37b4861e53b17a5e48b7e1234749602f22':
        raise ValueError('Native libc helper requires the exact verified image')
    if '@__osl_guest_tick' in text:
        raise ValueError('Input IR is already guarded')
    count = 0
    used_helpers = set()
    def function(match):
        nonlocal count
        header, body = match.group(1), match.group(2)
        if not _ABI.fullmatch(header):
            raise ValueError('Unexpected lifted function ABI')
        helpers={'10cd0ba0':'memcpy','10cd0c40':'memmove','10cd0ce0':'memset'}
        helper=helpers.get(_SYMBOL.search(header)[1]) if libc_helpers else None
        if helper:
            used_helpers.add(helper)
            # Public libc exports in the hash-checked CUSA05083 base image.
            # This is a C ABI substitution, not an instruction/flags shortcut.
            body = f'\n  %native_result = call ptr @__osl_libc_{helper}(ptr %state, i64 %program_counter, ptr %memory)\n  ret ptr %native_result\n'
        labels = {m.group(1): i + 1 for i, m in enumerate(_LABELS.finditer(body))}
        labels.setdefault('0', 0)
        block = 0
        lines = body.splitlines(keepends=True)
        out = ['\n  call void @__osl_guest_tick(ptr %memory, ptr %state)\n']
        count += 1
        i = 0
        while i < len(lines):
            line = lines[i]
            label = _LABELS.match(line) if not line.startswith((' ', '\t')) else None
            if label:
                block = labels[label.group(1)]
            if ('br' in line or 'switch' in line) and _BRANCH.match(line):
                end = i + 1
                if _TABLE_BRANCH.match(line):
                    while ']' not in ''.join(lines[i:end]):
                        if end == len(lines):
                            raise ValueError('Unterminated branch table')
                        end += 1
                terminator = ''.join(lines[i:end])
                targets = _TARGETS.findall(terminator)
                if not targets or any(target not in labels for target in targets):
                    raise ValueError('Unresolved LLVM branch destination')
                if any(labels[target] <= block for target in targets):
                    out.append('  call void @__osl_guest_tick(ptr %memory, ptr %state)\n')
                    count += 1
                out.extend(lines[i:end])
                i = end
            else:
                out.append(line)
                i += 1
        return header + ''.join(out) + '}'
    result, functions = _FUNCTIONS.subn(function, text)
    if not functions:
        raise ValueError('No lifted function')
    declarations = ['memset'] + [name for name in ('memcpy','memmove') if name in used_helpers]
    return result + '\ndeclare void @__osl_guest_tick(ptr, ptr)\n' + (''.join(f'declare ptr @__osl_libc_{name}(ptr, i64, ptr)\n' for name in declarations) if libc_helpers else ''), count
