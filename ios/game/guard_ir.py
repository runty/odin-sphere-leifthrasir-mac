"""Insert a bounded-runtime checkpoint at trace entries and every CFG backedge."""
import re


def instrument(text):
    if '@__osl_guest_tick' in text:
        raise ValueError('Input IR is already guarded')
    count = 0
    def function(match):
        nonlocal count
        header, body = match.group(1), match.group(2)
        if not re.fullmatch(r'define ptr @sub_[0-9a-f]+\(ptr noalias %state, i64 %program_counter, ptr noalias %memory\) \{', header):
            raise ValueError('Unexpected lifted function ABI')
        labels = {m.group(1): i + 1 for i, m in enumerate(re.finditer(r'^([A-Za-z0-9_.]+):', body, re.M))}
        labels.setdefault('0', 0)
        block = 0
        lines = body.splitlines(keepends=True)
        out = ['\n  call void @__osl_guest_tick(ptr %memory, ptr %state)\n']
        count += 1
        i = 0
        while i < len(lines):
            line = lines[i]
            label = re.match(r'^([A-Za-z0-9_.]+):', line)
            if label:
                block = labels[label.group(1)]
            if re.match(r'\s*(?:br|switch|indirectbr)\b', line):
                end = i + 1
                if re.match(r'\s*(?:switch|indirectbr)\b', line):
                    while ']' not in ''.join(lines[i:end]):
                        if end == len(lines):
                            raise ValueError('Unterminated branch table')
                        end += 1
                terminator = ''.join(lines[i:end])
                targets = re.findall(r'label %([A-Za-z0-9_.]+)', terminator)
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
    result, functions = re.subn(r'^(define [^\n]+)\n(.*?)^}', function, text, flags=re.M | re.S)
    if not functions:
        raise ValueError('No lifted function')
    return result + '\ndeclare void @__osl_guest_tick(ptr, ptr)\n', count
