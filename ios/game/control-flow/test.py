#!/usr/bin/env python3
"""Owned x86-64 synthetic fixtures for conservative switch-atlas acceptance/rejection."""

from pathlib import Path
import argparse, subprocess, json, struct, hashlib, sys

p = argparse.ArgumentParser(description=__doc__)
p.add_argument("--output", type=Path, required=True)
p.add_argument("--xed-root", type=Path, required=True)
p.add_argument("--llvm-bin", type=Path, required=True)
p.add_argument("--cxx", default="clang++")
a = p.parse_args()
out = a.output.resolve()
if (
    len(out.parts) < 3
    or out.parts[1] != "Volumes"
    or not Path("/Volumes", out.parts[2]).is_mount()
):
    p.error("Test output must be on a mounted external disk")
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).resolve().parent


def run(cmd, name):
    with (out / name).open("w") as f:
        subprocess.run(
            list(map(str, cmd)), stdout=f, stderr=subprocess.STDOUT, check=True
        )


def function(
    name, pre, body="", badcase="", tableoverride=None, before_add="", post_add=""
):
    start = f"fixture_{name}"
    labels = [start + "_case" + str(i) for i in range(3)]
    default = start + "_default"
    table = start + "_table"
    lines = ([".Lnormal_target:"] if name == "normal" else []) + [
        f".global {start}",
        f".global {start}_end",
        f"{start}:",
    ]
    pre = (
        pre.replace("@DEFAULT", default)
        .replace("@TABLE", table)
        .replace("@LOAD", start + "_load")
    )
    lines.extend(pre.splitlines())
    lines.extend(
        [
            f"{start}_load:",
            "movsxd rdx, dword ptr [rcx+rax*4]",
            *before_add.splitlines(),
            "add rdx, rcx",
            f"{start}_post:",
            *post_add.replace("@DEFAULT", default).splitlines(),
            f"{start}_dispatch:",
            "jmp rdx",
        ]
    )
    for i, label in enumerate(labels):
        lines.extend(
            [
                label + ":",
                badcase if i == 0 and badcase else f"mov eax, {0x11223344 + i}",
                "ret",
            ]
        )
    lines.extend([default + ":", "xor eax, eax", "ret", start + "_end:"])
    entries = tableoverride or labels
    data = [".balign 4", table + ":"] + [f".long {entry}-{table}" for entry in entries]
    return lines, data


fixtures = {
    "narrow8": ("movzx eax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "narrow8_copy": (
        "movzx eax,bl\ncmp bl,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_schedule": (
        "add bl,0x7d\nmovzx eax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nmov r14b,1",
        True,
        "",
    ),
    "narrow16_copy": (
        "movzx eax,si\ncmp si,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_regcopy32": (
        "movzx ecx,bl\nmov eax,ecx\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_regcopy64": (
        "movzx ecx,bl\nmov rax,rcx\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "narrow8_memory": (
        "movzx eax,byte ptr [rdi]\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        True,
        "",
    ),
    "bad_upper8": (
        "mov eax,0x100\nmov al,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "bad_upper16": (
        "mov eax,0x10000\nmov ax,si\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "stale_movzx8": (
        "movzx eax,bl\nadd eax,0x100\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "stale_movzx16": (
        "movzx eax,si\nmov eax,edi\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "clobber_narrow_source": (
        "movzx eax,bl\ninc bl\ncmp bl,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_partial_dest": (
        "movzx ax,bl\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_source_too_wide": (
        "movzx eax,si\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_highbyte": (
        "movzx eax,bh\ncmp ah,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_ambiguous16": ("cmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "narrow_call_clobber": (
        "movzx eax,bl\ncall .Lnormal_target\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "narrow_bypass": (
        "test edi,edi\njnz fixture_narrow_bypass_cmp\nmovzx eax,bl\nfixture_narrow_bypass_cmp:\ncmp al,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "copy_bypass": (
        "mov eax,0x100\ntest edi,edi\njnz fixture_copy_bypass_guard\nmov eax,esi\nfixture_copy_bypass_guard:\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "normal": ("mov eax, edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "copy": ("mov eax, esi\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "intervening": (
        "mov eax, edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nmov rdi,qword ptr [r12+0x78]",
        True,
        "",
    ),
    "wide": ("cmp rax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
    "clobber_index": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nmov eax,99\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "clobber_base": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]\nxor ecx,ecx",
        False,
        "",
    ),
    "ambiguous_upper": ("cmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "clobber_copy": (
        "mov eax,esi\nmov esi,2\ncmp esi,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "alternate_entry": (
        "test edx,edx\njnz @LOAD\nmov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "unresolved_cfg": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "jmp r8",
    ),
    "overlap": ("mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", False, ""),
    "count_cap": (
        "mov eax,edi\ncmp eax,4096\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "",
    ),
    "external_tail": (
        "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
        False,
        "jmp .Lnormal_target",
    ),
    "guard16": ("movzx eax,si\ncmp ax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]", True, ""),
}
# CMP/JA scheduling regressions: original case0..2 domains are unchanged.
fixtures.update(
    {
        "scheduled_mov": (
            "mov eax,edi\ncmp eax,2\nmov r9d,7\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_stack_future_base": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "mov rcx,qword ptr [r13]\n"
            "mov qword ptr [rbp-0x30],rcx\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_movzx": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "movzx r9d,byte ptr [rdi]\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_movsx": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "movsx r9d,word ptr [rdi]\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_movsxd": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "movsxd r9,dword ptr [rdi]\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_lea": (
            "mov eax,edi\ncmp eax,2\nlea r9,[rbx+4]\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_nop": (
            "mov eax,edi\ncmp eax,2\nnop\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_store": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "mov qword ptr [rbp-8],rdi\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_wide": (
            "cmp rax,2\nmov r9d,7\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_copy32": (
            "mov eax,esi\ncmp esi,2\nmov r9d,7\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_narrow_copy": (
            "movzx eax,bl\ncmp bl,2\nmov r9d,7\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_narrow_same": (
            "movzx eax,bl\ncmp al,2\nmov r9d,7\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_copy_source_after_cmp": (
            "mov eax,esi\ncmp esi,2\nmov esi,99\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_eight": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "mov r9d,7\n"
            "movzx r10d,byte ptr [rdi]\n"
            "movsx r11d,word ptr [rdi]\n"
            "movsxd r8,dword ptr [rdi]\n"
            "lea r9,[rbx+4]\n"
            "mov qword ptr [rbp-8],rdi\n"
            "nop\n"
            "nop\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "scheduled_add_flags": (
            "mov eax,edi\ncmp eax,2\nadd r9d,1\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_test_flags": (
            "mov eax,edi\ncmp eax,2\ntest r9d,r9d\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_inc_flags": (
            "mov eax,edi\ncmp eax,2\ninc r9d\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_other_cmp": (
            "mov eax,edi\ncmp eax,2\ncmp r9d,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_index32": (
            "mov eax,edi\ncmp eax,2\nmov eax,99\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_index64": (
            "mov eax,edi\ncmp eax,2\nmov rax,99\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_index_lea": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "lea rax,[rax+1]\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_index_movzx": (
            "mov eax,edi\ncmp eax,2\nmovzx eax,al\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_call": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "call .Lnormal_target\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_alternate_schedule": (
            "test esi,esi\n"
            "jnz fixture_scheduled_alternate_schedule_enter\n"
            "mov eax,edi\n"
            "cmp eax,2\n"
            "fixture_scheduled_alternate_schedule_enter:\n"
            "mov r9d,7\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_alternate_ja": (
            "test esi,esi\n"
            "jnz fixture_scheduled_alternate_ja_enter\n"
            "mov eax,edi\n"
            "cmp eax,2\n"
            "nop\n"
            "fixture_scheduled_alternate_ja_enter:\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_copy_bypass": (
            "mov eax,99\n"
            "test edi,edi\n"
            "jnz fixture_scheduled_copy_bypass_cmp\n"
            "mov eax,esi\n"
            "fixture_scheduled_copy_bypass_cmp:\n"
            "cmp esi,2\n"
            "nop\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_stale_copy": (
            "mov eax,esi\nmov esi,3\ncmp esi,2\nnop\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_nine": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "nop\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "scheduled_table_bypass": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "fixture_scheduled_table_bypass_schedule:\n"
            "mov r9d,7\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
    }
)
# Computed-target lifetime and full64 table-base copy regressions.
fixtures.update(
    {
        "base_copy64": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rdx,[rip+@TABLE]\nmov rcx,rdx",
            True,
            "",
        ),
        "base_copy64_source_reused": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "mov rcx,rdx\n"
            "mov rdx,0x123",
            True,
            "",
        ),
        "base_copy64_chain": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "mov r8,rdx\n"
            "mov rcx,r8",
            True,
            "",
        ),
        "base_copy64_other_source": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea r9,[rip+@TABLE]\nmov rcx,r9",
            True,
            "",
        ),
        "base_copy_post_xor": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rdx,[rip+@TABLE]\nmov rcx,rdx",
            True,
            "",
        ),
        "post_xor_base": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_xor_index": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_xor_other": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_mov_base": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_mov_index": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_read_target": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_movzx": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_movsx": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_movsxd": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_lea": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_nop": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_add": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_sub": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_and": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_or": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_inc": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_dec": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_cmp": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_test": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_eight": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            True,
            "",
        ),
        "post_bad_target64": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_target32": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_target16": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_target8": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_target_lea": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_call": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_branch": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_bad_nine": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "base_bad_clobber": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "mov rcx,rdx\n"
            "mov rcx,99",
            False,
            "",
        ),
        "base_bad_source": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "mov rdx,99\n"
            "mov rcx,rdx",
            False,
            "",
        ),
        "base_bad_copy32": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rdx,[rip+@TABLE]\nmov ecx,edx",
            False,
            "",
        ),
        "base_bad_copy16": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rdx,[rip+@TABLE]\nmov cx,dx",
            False,
            "",
        ),
        "base_bad_index": (
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "mov rcx,rdx\n"
            "mov eax,99",
            False,
            "",
        ),
        "base_bad_source_index": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rax,[rip+@TABLE]\nmov rcx,rax",
            False,
            "",
        ),
        "base_bad_alternate": (
            "test esi,esi\n"
            "jnz fixture_base_bad_alternate_copy\n"
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rdx,[rip+@TABLE]\n"
            "fixture_base_bad_alternate_copy:\n"
            "mov rcx,rdx",
            False,
            "",
        ),
        "post_bad_alternate": (
            "test esi,esi\n"
            "jnz fixture_post_bad_alternate_post\n"
            "mov eax,edi\n"
            "cmp eax,2\n"
            "ja @DEFAULT\n"
            "lea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "post_table_bypass": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "base_bad_before_add": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
        "target_bad_before_add": (
            "mov eax,edi\ncmp eax,2\nja @DEFAULT\nlea rcx,[rip+@TABLE]",
            False,
            "",
        ),
    }
)
post_add_fixtures = {
    "base_copy64": "",
    "base_copy64_source_reused": "",
    "base_copy64_chain": "",
    "base_copy64_other_source": "",
    "base_copy_post_xor": "xor ecx,ecx",
    "post_xor_base": "xor ecx,ecx",
    "post_xor_index": "xor eax,eax",
    "post_xor_other": "xor ecx,edi",
    "post_mov_base": "mov rcx,99",
    "post_mov_index": "mov eax,99",
    "post_read_target": "mov r9,rdx",
    "post_movzx": "movzx r9d,byte ptr [rdi]",
    "post_movsx": "movsx r9d,word ptr [rdi]",
    "post_movsxd": "movsxd r9,dword ptr [rdi]",
    "post_lea": "lea r9,[rbx+7]",
    "post_nop": "nop",
    "post_add": "add r9d,1",
    "post_sub": "sub r9d,1",
    "post_and": "and r9d,7",
    "post_or": "or r9d,7",
    "post_inc": "inc r9d",
    "post_dec": "dec r9d",
    "post_cmp": "cmp edx,2",
    "post_test": "test edx,edx",
    "post_eight": "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop",
    "post_bad_target64": "mov rdx,99",
    "post_bad_target32": "mov edx,99",
    "post_bad_target16": "xor dx,dx",
    "post_bad_target8": "mov dl,1",
    "post_bad_target_lea": "lea rdx,[rdx+1]",
    "post_bad_call": "call .Lnormal_target",
    "post_bad_branch": "jnz @DEFAULT",
    "post_bad_nine": "nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop\nnop",
    "base_bad_clobber": "",
    "base_bad_source": "",
    "base_bad_copy32": "",
    "base_bad_copy16": "",
    "base_bad_index": "",
    "base_bad_source_index": "",
    "base_bad_alternate": "",
    "post_bad_alternate": "xor ecx,ecx",
    "post_table_bypass": "xor ecx,ecx",
    "base_bad_before_add": "",
    "target_bad_before_add": "",
}
before_add_fixtures = {
    "base_copy64": "",
    "base_copy64_source_reused": "",
    "base_copy64_chain": "",
    "base_copy64_other_source": "",
    "base_copy_post_xor": "",
    "post_xor_base": "",
    "post_xor_index": "",
    "post_xor_other": "",
    "post_mov_base": "",
    "post_mov_index": "",
    "post_read_target": "",
    "post_movzx": "",
    "post_movsx": "",
    "post_movsxd": "",
    "post_lea": "",
    "post_nop": "",
    "post_add": "",
    "post_sub": "",
    "post_and": "",
    "post_or": "",
    "post_inc": "",
    "post_dec": "",
    "post_cmp": "",
    "post_test": "",
    "post_eight": "",
    "post_bad_target64": "",
    "post_bad_target32": "",
    "post_bad_target16": "",
    "post_bad_target8": "",
    "post_bad_target_lea": "",
    "post_bad_call": "",
    "post_bad_branch": "",
    "post_bad_nine": "",
    "base_bad_clobber": "",
    "base_bad_source": "",
    "base_bad_copy32": "",
    "base_bad_copy16": "",
    "base_bad_index": "",
    "base_bad_source_index": "",
    "base_bad_alternate": "",
    "post_bad_alternate": "",
    "post_table_bypass": "",
    "base_bad_before_add": "xor ecx,ecx",
    "target_bad_before_add": "mov edx,99",
}
text = [".intel_syntax noprefix", ".text", ".org 0x100"]
tables = []
for name, (pre, accepted, badcase) in fixtures.items():
    override = (
        [f"fixture_{name}_case0", f"fixture_{name}_case0+1", f"fixture_{name}_case2"]
        if name == "overlap"
        else [
            f"fixture_{name}_case0",
            f"fixture_{name}_schedule",
            f"fixture_{name}_case2",
        ]
        if name == "scheduled_table_bypass"
        else [f"fixture_{name}_case0", f"fixture_{name}_post", f"fixture_{name}_case2"]
        if name == "post_table_bypass"
        else None
    )
    code, data = function(
        name,
        pre,
        badcase=badcase,
        tableoverride=override,
        before_add=before_add_fixtures.get(name, ""),
        post_add=post_add_fixtures.get(name, ""),
    )
    text += code
    tables += data
specials = ["table_bypass32", "table_bypass_copy"]
for name in specials:
    fn = "fixture_" + name
    guard = "eax" if name.endswith("32") else "esi"
    initial = "movabs rax,0x100000000" if name.endswith("32") else "mov eax,99"
    text += [
        f".global {fn}",
        f".global {fn}_end",
        fn + ":",
        initial,
        "mov ecx,edi",
        "cmp rcx,1",
        f"ja {fn}_default",
        f"lea rdx,[rip+{fn}_table0]",
        "movsxd r8,dword ptr [rdx+rcx*4]",
        "add r8,rdx",
        "jmp r8",
        fn + "_define:",
        "mov eax,esi",
        fn + "_cmp:",
        f"cmp {guard},2",
        f"ja {fn}_default",
        f"lea rcx,[rip+{fn}_table1]",
        "movsxd rdx,dword ptr [rcx+rax*4]",
        "add rdx,rcx",
        "jmp rdx",
    ]
    for i in range(3):
        text += [fn + f"_case{i}:", f"mov eax,{i + 1}", "ret"]
    text += [fn + "_default:", "xor eax,eax", "ret", fn + "_end:"]
    tables += [
        ".balign 4",
        fn + "_table0:",
        f".long {fn}_cmp-{fn}_table0",
        f".long {fn}_define-{fn}_table0",
        ".balign 4",
        fn + "_table1:",
    ] + [f".long {fn}_case{i}-{fn}_table1" for i in range(3)]

text += [
    ".global fixture_nested",
    ".global fixture_nested_end",
    "fixture_nested:",
    "mov eax,edi",
    "cmp eax,1",
    "ja fixture_nested_default",
    "lea rcx,[rip+fixture_nested_table]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    "fixture_nested_case0:",
    "mov eax,esi",
    "cmp eax,1",
    "ja fixture_nested_default",
    "lea rcx,[rip+fixture_nested_table2]",
    "movsxd rdx,dword ptr [rcx+rax*4]",
    "add rdx,rcx",
    "jmp rdx",
    "fixture_nested_case1:",
    "mov eax,5",
    "ret",
    "fixture_nested_case2:",
    "mov eax,6",
    "ret",
    "fixture_nested_case3:",
    "mov eax,7",
    "ret",
    "fixture_nested_default:",
    "xor eax,eax",
    "ret",
    "fixture_nested_end:",
]
tables += [
    ".balign 4",
    "fixture_nested_table:",
    ".long fixture_nested_case0-fixture_nested_table",
    ".long fixture_nested_case1-fixture_nested_table",
    "fixture_nested_table2:",
    ".long fixture_nested_case2-fixture_nested_table2",
    ".long fixture_nested_case3-fixture_nested_table2",
]
(out / "fixture.s").write_text("\n".join(text + tables) + "\n")
run(
    [
        a.cxx,
        "-target",
        "x86_64-unknown-linux-gnu",
        "-c",
        out / "fixture.s",
        "-o",
        out / "fixture.o",
    ],
    "assemble.log",
)
run(
    [a.llvm_bin / "llvm-readobj", "--relocations", out / "fixture.o"], "relocations.txt"
)
assert "R_X86_64" not in (out / "relocations.txt").read_text()
run(
    [
        a.llvm_bin / "llvm-objcopy",
        "-O",
        "binary",
        "--only-section=.text",
        out / "fixture.o",
        out / "fixture.image",
    ],
    "objcopy.log",
)
run([a.llvm_bin / "llvm-nm", "--format=posix", out / "fixture.o"], "symbols.txt")
symbols = {}
for line in (out / "symbols.txt").read_text().splitlines():
    fields = line.split()
    if len(fields) >= 3:
        symbols[fields[0]] = int(fields[2], 16)
image = (out / "fixture.image").read_bytes()
header = bytearray(4096)
ident = bytes([127]) + b"ELF" + bytes([2, 1, 1]) + bytes(9)
struct.pack_into(
    "<16sHHIQQQIHHHHHH", header, 0, ident, 2, 62, 1, 0, 64, 0, 0, 64, 56, 1, 0, 0, 0
)
struct.pack_into(
    "<IIQQQQQQ", header, 64, 1, 5, 4096, 0, 0, len(image), len(image), 4096
)
(out / "fixture.elf").write_bytes(header + image)
base = 0x400000
rows = []
for name in list(fixtures) + specials + ["nested"]:
    rows.append(
        {
            "module": "fixture",
            "canonical_start": base + symbols["fixture_" + name],
            "canonical_end": base + symbols["fixture_" + name + "_end"],
            "fde_va": None,
        }
    )
(out / "entries.json").write_text(json.dumps(rows, indent=2) + "\n")
run(
    [
        sys.executable,
        source / "catalog.py",
        "--image",
        out / "fixture.image",
        "--elf",
        out / "fixture.elf",
        "--atlas",
        out / "entries.json",
        "--module",
        "fixture",
        "--base",
        hex(base),
        "--passes",
        "3",
        "--xed-root",
        a.xed_root,
        "--cxx",
        a.cxx,
        "--output",
        out / "catalog",
    ],
    "catalog-test.log",
)
accepted = json.loads((out / "catalog/accepted-tables.json").read_text())
actual = {s["owner"] for s in accepted}
expected = {symbols["fixture_" + n] for n, (_, ok, _) in fixtures.items() if ok} | {
    symbols["fixture_nested"]
}
assert actual == expected, {"actual": actual, "expected": expected}
assert len(accepted) == len(expected) + 1
partial = json.loads((out / "catalog/incomplete-owner-tables.json").read_text())
partial_owners = {t["owner"] for t in partial}
assert {
    symbols["fixture_external_tail"],
    symbols["fixture_unresolved_cfg"],
} <= partial_owners
assert all(
    not t["owner_cfg"]["complete"]
    and t["owner_cfg"]["decode_or_reachability_failures"] == 0
    and t["owner_cfg"]["instruction_overlaps"] == 0
    for t in partial
)
assert all(
    len(t["owner_cfg"]["unresolved_indirect_pcs"])
    == t["owner_cfg"]["unresolved_indirect_branches"]
    and len(t["owner_cfg"]["external_direct_edges"])
    == t["owner_cfg"]["external_direct_branches"]
    for t in partial
)
scheduled_rejections = json.loads((out / "catalog/pass-0/rejected.json").read_text())
assert any(
    row["dispatch"] == symbols["fixture_scheduled_table_bypass_dispatch"]
    and row["reason"] == "accepted table creates alternate protected-path entry"
    for row in scheduled_rejections
), "scheduled table-edge bypass must be rejected"
assert any(
    row["dispatch"] == symbols["fixture_post_table_bypass_dispatch"]
    and row["reason"] == "accepted table creates alternate protected-path entry"
    for row in scheduled_rejections
), "table edge bypassing ADD into cleanup must be rejected"
manifest = json.loads((out / "catalog/provenance.json").read_text())
assert len(manifest["passes"]) >= 2

# Read-only artifact boundaries fail explicitly before scanner execution.
cli = [
    sys.executable,
    source / "catalog.py",
    "--image",
    out / "fixture.image",
    "--elf",
    out / "fixture.elf",
    "--atlas",
    out / "entries.json",
    "--module",
    "fixture",
    "--base",
    hex(base),
    "--passes",
    "1",
    "--xed-root",
    a.xed_root,
    "--cxx",
    a.cxx,
]


def rejection(name, replace, expect):
    command = list(map(str, cli))
    for key, value in replace.items():
        command[command.index(key) + 1] = str(value)
    command += ["--output", str(out / name)]
    result = subprocess.run(
        command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )
    (out / (name + ".log")).write_text(result.stdout)
    assert result.returncode and expect in result.stdout, (
        name,
        result.returncode,
        result.stdout,
    )


changed = bytearray(image)
changed[-1] ^= 1
(out / "mismatch.image").write_bytes(changed)
rejection(
    "reject-image-mismatch",
    {"--image": out / "mismatch.image"},
    "Prepared image differs",
)
overlap = list(rows) + [dict(rows[0])]
(out / "overlap-entries.json").write_text(json.dumps(overlap))
rejection(
    "reject-atlas-overlap",
    {"--atlas": out / "overlap-entries.json"},
    "Atlas owners overlap",
)
bad = bytearray(header + image)
struct.pack_into("<Q", bad, 64 + 32, len(image) + 4096)
(out / "bad-bounds.elf").write_bytes(bad)
rejection(
    "reject-elf-bounds", {"--elf": out / "bad-bounds.elf"}, "Invalid ELF load bounds"
)

results = {
    "owned_functions": len(rows),
    "accepted_owners": len(actual),
    "accepted_tables": len(accepted),
    "rejected_owners": len(rows) - len(actual),
    "checks": len(rows) + 9,
    "incomplete_owner_candidates": len(partial_owners),
    "failures": 0,
    "nested_passes": len(manifest["passes"]),
    "accepted_names": sorted(n for n in fixtures if fixtures[n][1]) + ["nested"],
    "rejected_names": sorted(n for n in fixtures if not fixtures[n][1]) + specials,
    "outputs": {
        f.name: hashlib.sha256(f.read_bytes()).hexdigest()
        for f in [
            out / "fixture.s",
            out / "fixture.image",
            out / "fixture.elf",
            out / "entries.json",
        ]
    },
}
(out / "results.json").write_text(json.dumps(results, indent=2) + "\n")
print(json.dumps(results, indent=2))
