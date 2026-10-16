#!/usr/bin/env python3
"""Replay cached solutions through the mirror; report per-level oil pressure:
minimum oil_left reached, longest stretch between refills, flask usage."""
import json, sys
sys.path.insert(0, ".")
import solve

cache = json.load(open("qa/solutions.json"))
for i in range(9):
    lv = solve.LEVELS[i]
    sim = solve.Sim(lv)
    path = cache[str(i)]["path"]
    st = sim.init
    min_oil = sim.budget if sim.budget else None
    stretch = 0; max_stretch = 0; flasks = 0; ok = True
    for a in path:
        r = sim.step(st, a)
        if r is None: ok = False; break
        nst = r[0]
        if sim.budget:
            if nst[5] > st[5]:  # oils set shrank? no - flask pickup empties oils entry
                pass
            if len(nst[5]) < len(st[5]): flasks += 1; stretch = 0
            else: stretch += 1
            max_stretch = max(max_stretch, stretch)
            min_oil = min(min_oil, nst[9])
        st = nst
    print(f"L{i+1} {lv['name'][:22]:22} budget={sim.budget:3} len={len(path):3} "
          f"min_oil={min_oil} longest_stretch={max_stretch} flasks_used={flasks} replay_ok={ok}")
