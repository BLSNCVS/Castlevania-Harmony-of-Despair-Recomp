#!/usr/bin/env python3
"""Finds code the rexglue analyzer never registered as a function. (Python 3.11+)

MSVC emits no .pdata entry for tiny leaf functions, this-adjustor thunks
('addi r3,r3,N; b target') or vcall thunks ('lwz r12,0(r3); lwz r11,N(r12);
mtctr r11; bctr'), and nothing calls them directly: their addresses only
appear in vtables, other data, or lis/addi pairs. When the game calls one
through a pointer the runtime aborts with "Call to invalid or unregistered
function". This script lists them so they can be declared up front.

Inputs:
  --image      patched image from 'hodtool dump-image <game> out.bin'
               (add --module <dll> for a DLL)
  --layout     its section layout ('hodtool dump-image ... --layout x.json');
               without it the default.xex ranges below are used
  --generated  codegen output directory (generated/default, generated/<dll>)
Writes a TOML [functions] table to --out.

The module's entry point (from --layout) is always reported if unregistered.
Otherwise a candidate is any code address that is referenced from .rdata/.data or built
with lis+addi/ori, that looks like an entry (previous word is blr/bctr/b or
padding), and that is neither a registered function, inside a .pdata function,
an internal label of a generated function, nor obviously data.
"""

import argparse
import bisect
import json
import re
import struct
from pathlib import Path

def ranges_from_layout(path):
    """code, data and .pdata ranges as the CLI's START-END strings."""
    layout = json.loads(Path(path).read_text())
    fmt = lambda s: f"{s['start']:08X}-{s['end']:08X}"
    sections = layout["sections"]
    code = [s for s in sections if s["executable"]]
    if len(code) != 1:
        raise SystemExit(f"{path}: expected one executable section, found {len(code)}")
    data = [s for s in sections
            if not s["executable"] and s["name"] in (".rdata", ".data", ".sdata", ".tls")]
    pdata = [s for s in sections if s["name"] == ".pdata"]
    return (layout["base"], fmt(code[0]), ",".join(fmt(s) for s in data),
            fmt(pdata[0]) if pdata else None, layout.get("entry", 0))


def main():
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--image", required=True)
    p.add_argument("--generated", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--code", default="82220000-828CC8C8", help="code range, hex START-END")
    p.add_argument("--data", default="82000600-821FA084,828D0000-8293D9B8",
                   help="data ranges to search for pointers")
    p.add_argument("--pdata", default="821FA200-82210C98")
    p.add_argument("--layout", help="section layout JSON from hodtool dump-image --layout")
    p.add_argument("--quiet", action="store_true", help="don't write --out when nothing is found")
    args = p.parse_args()

    base, entry = 0x82000000, 0
    if args.layout:
        base, args.code, args.data, args.pdata, entry = ranges_from_layout(args.layout)
    img = Path(args.image).read_bytes()
    def word(a):
        # Outside the image (a bad range) reads as 0 rather than wrapping
        # around to the other end of the file.
        return struct.unpack_from(">I", img, a - base)[0] if 0 <= a - base <= len(img) - 4 else 0
    rng = lambda s: tuple(int(x, 16) for x in s.split("-"))
    code = rng(args.code)
    in_code = lambda v: code[0] <= v < code[1] and v % 4 == 0

    gen = Path(args.generated)
    registered = set()
    for f in gen.glob("*_init.cpp"):
        registered |= {int(m, 16) for m in re.findall(r"\{\s*0x([0-9A-Fa-f]{8})\s*,", f.read_text())}
    labels = set()
    for f in gen.glob("*_recomp.*.cpp"):
        labels |= {int(m, 16) for m in re.findall(r"^loc_([0-9A-F]{8}):", f.read_text(), re.M)}

    pdata = []
    if args.pdata:
        pstart, pend = rng(args.pdata)
        pdata = sorted((b, b + ((d >> 8) & 0x3FFFFF) * 4)
                       for b, d in (struct.unpack_from(">II", img, o - base) for o in range(pstart, pend, 8)) if b)
    starts = [s for s, _ in pdata]

    def inside_pdata_function(a):
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and pdata[i][0] < a < pdata[i][1]

    refs = set()
    for lo, hi in (rng(r) for r in args.data.split(",") if r):
        refs |= {v for v in (word(a) for a in range(lo, hi - 3, 4)) if in_code(v)}
    pending = {}
    for a in range(code[0], code[1], 4):
        i = word(a)
        op = i >> 26
        if op == 15 and ((i >> 16) & 31) == 0:  # lis rD,imm
            pending[(i >> 21) & 31] = (i & 0xFFFF) << 16
        elif op == 14 and ((i >> 16) & 31) in pending:  # addi rD,rA,simm
            lo16 = i & 0xFFFF
            v = (pending[(i >> 16) & 31] + (lo16 - 0x10000 if lo16 & 0x8000 else lo16)) & 0xFFFFFFFF
            if in_code(v):
                refs.add(v)
        elif op == 24 and ((i >> 21) & 31) in pending:  # ori rA,rS,imm
            v = pending[(i >> 21) & 31] | (i & 0xFFFF)
            if in_code(v):
                refs.add(v)
        elif i == 0x4E800020 or op == 18:
            pending.clear()

    def looks_like_entry(a):
        prev = word(a - 4)
        return prev in (0x4E800020, 0x4E800420, 0) or (prev >> 26) == 18

    def looks_like_data(a):
        words = [word(a + 4 * k) for k in range(4)]
        # Repeated words, or a table of pointers into game images (0x82-0x8F).
        return len(set(words)) == 1 or all(0x82 <= (x >> 24) <= 0x8F for x in words[:2])

    found = {a for a in refs
             if a not in registered and a not in labels and looks_like_entry(a)
             and not inside_pdata_function(a) and not looks_like_data(a)}
    # The loader calls a DLL's entry point when the game loads it, and codegen
    # doesn't register it unless something else made it a function.
    if in_code(entry) and entry not in registered:
        found.add(entry)
    found = sorted(found)
    if not found and args.quiet:
        # Nothing to declare: drop a list left from an earlier run.
        Path(args.out).unlink(missing_ok=True)
        return
    lines = [
        "# Generated by tools/analysis/find_missing_functions.py: code reached only",
        "# through pointers (no .pdata entry, no direct calls). Regenerate after",
        "# changing the supported dump; review before committing.",
        "",
        "[functions]",
    ] + [f"0x{a:08X} = {{}}" for a in found]
    Path(args.out).write_text("\n".join(lines) + "\n")
    print(f"{len(found)} unregistered functions written to {args.out}")


if __name__ == "__main__":
    main()
