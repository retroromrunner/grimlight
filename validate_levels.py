#!/usr/bin/env python3
"""Validate the level maps embedded in src/main.c."""
import re, sys

src = open("src/main.c").read()
block = src[src.index("static const Level LEVELS"):src.index("#define NUM_LEVELS")]
rows = [s for s in re.findall(r'"([ #IH T.,xDGgKcCAELBRrSowkvsM1234]{15})"', block)]
print(f"found {len(rows)} map rows")
NLV = len(rows) // 10
assert len(rows) == 10 * NLV, "rows not a multiple of 10"
ok = True
for lv in range(NLV):
    m = rows[lv * 10:(lv + 1) * 10]
    flat = "".join(m)
    problems = []
    if flat.count("S") != 1: problems.append(f"S x{flat.count('S')}")
    if flat.count("D") != 1: problems.append(f"D x{flat.count('D')}")
    if flat.count("r") < 1: problems.append("no relics")
    # border sanity: row 0 and row 9 mostly walls
    if set(m[0]) - set("# "): problems.append("top row not solid-ish")
    if set(m[9]) - set("# "): problems.append("bottom row not solid-ish")
    print(f"level {lv + 1}: relics={flat.count('r')} oil={flat.count('o')} wraith={flat.count('w')} "
          f"skel={flat.count('k') + flat.count('v')} spider={flat.count('s')} brazier={flat.count('B')} "
          f"candle={flat.count('C') + flat.count('c')} gate={flat.count('G')} plate={flat.count('g')} rune={flat.count('R')}"
          + (f"  PROBLEMS: {problems}" if problems else ""))
    if problems: ok = False
sys.exit(0 if ok else 1)
