#!/usr/bin/env python3
"""GRIMLIGHT solver: mirrors the C game rules exactly, A*-searches a solution per level,
and emits src/qa_script.h (scripted inputs for the QA build) + qa/expected.json."""
import re, json, heapq, sys, time

MW, MH = 15, 10
DX = [0, 0, -1, 1]
DY = [1, -1, 0, 0]
KEY = {"D": 0x0080, "U": 0x0040, "L": 0x0020, "R": 0x0010, "W": 0x0001}
ACTS = ["R", "L", "D", "U", "W"]
DIR_OF = {"D": 0, "U": 1, "L": 2, "R": 3}

src = open("src/main.c").read()
block = src[src.index("static const Level LEVELS"):src.index("#define NUM_LEVELS")]
headers = re.findall(r'\{\s*"([^"]+)",\s*"([^"]+)",\s*(\d+),\s*(\d+),\s*(\d+),\s*([\w |]+),', block)
rows = re.findall(r'"([ #IH T.,xDGgKcCAELBRrSowkvsM1234]{15})"', block)
FLAGMAP = {"0": 0, "LF_CANDLES": 1, "LF_RUNE": 2, "LF_CANDLES | LF_RUNE": 3, "3": 3}
assert len(rows) == 10 * len(headers) and len(headers) == 14, (len(headers), len(rows))
LEVELS = []
for i, (name, chap, ch, rad, oil, flg) in enumerate(headers):
    LEVELS.append({"name": name, "chapter": int(ch), "radius": int(rad), "oil": int(oil),
                   "flags": FLAGMAP[flg.strip()], "map": rows[i * 10:(i + 1) * 10]})

class Sim:
    def __init__(self, lv):
        m = lv["map"]
        self.walls = set(); self.candles = set(); self.plates = set(); self.gates = set()
        self.kgates = set(); self.lgates = {}; self.levers = set()
        self.seqlevers = {}; self.seqgates = set()
        self.door = None; self.rune = None
        relics = set(); oils = set(); lit = set()
        self.braziers0 = []; enemies = []
        self.start = None
        for y in range(MH):
            for x in range(MW):
                c = m[y][x]
                if c in "#IHT ": self.walls.add((x, y))
                elif c == "C": self.candles.add((x, y))
                elif c == "c": self.candles.add((x, y)); lit.add((x, y))
                elif c == "g": self.plates.add((x, y))
                elif c == "G": self.gates.add((x, y))
                elif c == "K": self.kgates.add((x, y))
                elif c in "AE": self.lgates[(x, y)] = c
                elif c == "L": self.levers.add((x, y))
                elif c in "1234": self.seqlevers[(x, y)] = int(c)
                elif c == "M": self.seqgates.add((x, y))
                elif c == "D": self.door = (x, y)
                elif c == "R": self.rune = (x, y)
                elif c == "r": relics.add((x, y))
                elif c == "o": oils.add((x, y))
                elif c == "S": self.start = (x, y)
                elif c == "B": self.braziers0.append((x, y))
                elif c in "wkvs":
                    t = 0 if c == "w" else (2 if c == "s" else 1)
                    enemies.append((t, x, y, 1 if c == "v" else 3, 1))
        self.base_radius = lv["radius"]; self.budget = lv["oil"]; self.flags = lv["flags"]
        self.seq_total = len(self.seqlevers)
        self.seq_by_digit = {d: p for p, d in self.seqlevers.items()}
        self._light_cache = {}
        # Sokoban dead squares: cells from which a brazier can never be pushed
        # onto any plate (static reverse-push BFS; gates/lever/door count as
        # walls). A stage that ends with a brazier stranded here has quietly
        # soft-locked the level, so stage goals reject such end states.
        self.dead = set()
        if self.plates:
            blocked = set(self.walls) | self.gates | self.kgates | set(self.lgates) | self.levers | self.seqgates | set(self.seqlevers)
            if self.door: blocked.add(self.door)
            def occrable(x, y):
                return (x, y) not in blocked and 0 <= x < MW and 0 <= y < MH
            live = set(self.plates); work = list(self.plates)
            while work:
                yx, yy = work.pop()
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    bx, by = yx - dx, yy - dy
                    sx, sy = yx - 2 * dx, yy - 2 * dy
                    if (bx, by) not in live and occrable(bx, by) and occrable(sx, sy):
                        live.add((bx, by)); work.append((bx, by))
            for y in range(MH):
                for x in range(MW):
                    if occrable(x, y) and (x, y) not in live: self.dead.add((x, y))
        en0 = tuple(enemies)
        st = (self.start[0], self.start[1], tuple(sorted(self.braziers0)), en0,
              frozenset(relics), frozenset(oils), frozenset(lit), frozenset(),
              self.base_radius, self.budget, 0, 0, False, False, False, 0)
        self.init = self._gates(st)

    # state: (px,py,braz,en,relics,oils,lit,scored,radius,oil_left,freeze,turnpar,gate,rune_used,lever,seq_prog)
    def _char(self, st, x, y):
        if (x, y) in st[4]: return "r"
        if (x, y) in st[5]: return "o"
        if (x, y) in self.walls: return "#"
        if (x, y) in self.candles: return "c" if (x, y) in st[6] else "C"
        if (x, y) in self.plates: return "g"
        if (x, y) in self.gates: return "G"
        if (x, y) in self.kgates: return "K"
        if (x, y) in self.lgates: return self.lgates[(x, y)]
        if (x, y) in self.levers: return "L"
        if (x, y) in self.seqlevers: return str(self.seqlevers[(x, y)])
        if (x, y) in self.seqgates: return "M"
        if (x, y) == self.door: return "D"
        if (x, y) == self.rune: return "R"
        return "."

    def _occ(self, st):
        return {(st[0], st[1])} | set(st[2]) | {(e[1], e[2]) for e in st[3] if e[4]}

    def _pressed(self, st):
        occ = self._occ(st)
        return sum(1 for p in self.plates if p in occ)

    def _solid(self, st, x, y, pressed=None, lever=None, seqp=None):
        if x < 0 or y < 0 or x >= MW or y >= MH: return True
        c = self._char(st, x, y)
        if c in "#CcL1234": return True
        if c in "GKAEM":
            if (x, y) in self._occ(st): return False
            if pressed is None: pressed = self._pressed(st)
            if lever is None: lever = st[14]
            if seqp is None: seqp = st[15]
            if c == "G": return pressed < 1
            if c == "K": return pressed < 2
            if c == "A": return not lever
            if c == "M": return seqp < self.seq_total
            return bool(lever)
        return False

    def door_ready(self, relics, lit, rune_used):
        if relics: return False
        if (self.flags & 1) and not self.candles <= lit: return False
        if (self.flags & 2) and not rune_used: return False
        return True

    def _gates(self, st):
        gate = self._pressed(st) >= 1 or any(g in self._occ(st) for g in self.gates)
        return st[:12] + (gate, st[13], st[14], st[15])

    def _light(self, st):
        key = (st[0], st[1], st[2], st[6], st[8])
        hit = self._light_cache.get(key)
        if hit is not None: return hit
        px, py, braz = st[0], st[1], st[2]
        sources = [(px, py, st[8] + 2)]
        for (x, y) in st[6]: sources.append((x, y, 3))
        for (x, y) in braz: sources.append((x, y, 3))
        L = [[0] * MW for _ in range(MH)]
        for y in range(MH):
            for x in range(MW):
                best = 0
                for sx, sy, s in sources:
                    d = max(abs(x - sx), abs(y - sy))
                    v = s - d
                    if v > best: best = v
                L[y][x] = min(3, best)
        if len(self._light_cache) < 300000: self._light_cache[key] = L
        return L

    def _blocked(self, st, x, y, braz, en):
        tmp = st[:3] + (tuple(tuple(e) for e in en),) + st[4:]
        if self._solid(tmp, x, y): return True
        if self._char(tmp, x, y) == "D": return True
        if (x, y) in braz: return True
        if any(e[4] and e[1] == x and e[2] == y for e in en): return True
        return False

    def step(self, st, act):
        """Returns (new_state, score_gain, solved) or None if the action is a no-op or fatal."""
        px, py, braz, en, relics, oils, lit, scored, radius, oil_left, freeze, turnpar, gate, rune_used, lever, seqp = st
        braz = list(braz); en = [list(e) for e in en]
        score = 0; oil_got = False; solved = False
        if act != "W":
            d = DIR_OF[act]; dx, dy = DX[d], DY[d]
            nx, ny = px + dx, py + dy
            bi = next((i for i, b in enumerate(braz) if b == (nx, ny)), -1)
            if bi >= 0:
                bx, by = nx + dx, ny + dy
                bc = self._char(st, bx, by) if 0 <= bx < MW and 0 <= by < MH else "#"
                if (self._solid(st, bx, by) or (bx, by) in braz
                        or any(e[4] and e[1] == bx and e[2] == by for e in en)
                        or bc not in ".,xg"):
                    return None
                braz[bi] = (bx, by); px, py = nx, ny
            elif (nx < 0 or ny < 0 or nx >= MW or ny >= MH or self._solid(st, nx, ny)
                  or (self._char(st, nx, ny) == "D" and not self.door_ready(relics, lit, rune_used))):
                tc = self._char(st, nx, ny) if 0 <= nx < MW and 0 <= ny < MH else "#"
                if tc in "Cc":
                    cell = (nx, ny)
                    if tc == "C":
                        lit = lit | {cell}
                        if cell not in scored: score += 25; scored = scored | {cell}
                    else:
                        lit = lit - {cell}
                elif tc == "L":
                    lever = not lever
                elif tc in "1234":
                    d = int(tc)
                    if d == seqp + 1: seqp += 1
                    elif d > seqp + 1: seqp = 0
                else:
                    return None
            elif any(e[4] and e[1] == nx and e[2] == ny for e in en):
                return None  # walked into a monster
            else:
                px, py = nx, ny
            c = self._char((px, py, tuple(braz), tuple(tuple(e) for e in en), relics, oils, lit, scored,
                            radius, oil_left, freeze, turnpar, gate, rune_used, lever, seqp), px, py)
            if c == "r":
                relics = relics - {(px, py)}; score += 150
            elif c == "o":
                oils = oils - {(px, py)}; oil_left = self.budget; radius = self.base_radius; oil_got = True
            elif c == "R" and not rune_used:
                rune_used = True; freeze = 3
            elif c == "D" and self.door_ready(relics, lit, rune_used):
                solved = True
        newturn = turnpar + 1  # kept for state-shape stability; no rule reads it now
        if self.budget > 0 and not oil_got:
            oil_left -= 1
            if oil_left <= 0:
                return None  # the lantern dies; death beats even a same-turn clear (C order)
            radius = self.base_radius if oil_left > self.budget // 3 else max(1, self.base_radius - 1)
        st2 = (px, py, tuple(sorted(braz)), tuple(tuple(e) for e in en), relics, oils, lit, scored,
               radius, oil_left, freeze, newturn & 1, gate, rune_used, lever, seqp)
        st2 = self._gates(st2)
        gate = st2[12]
        light = self._light(st2)
        if freeze > 0:
            freeze -= 1
            st2 = st2[:10] + (freeze, st2[11], st2[12], st2[13], st2[14], st2[15])
            st2 = self._gates(st2)
            return st2, score, solved
        en = [list(e) for e in st2[3]]
        braz_t = st2[2]
        died = False
        for i, e in enumerate(en):
            if not e[4]: continue
            t, ex, ey = e[0], e[1], e[2]
            if t == 0:  # wraith
                md = abs(ex - px) + abs(ey - py)
                if light[ey][ex] >= 2:
                    # repelled by light: flee to a strictly better cell (darker, or
                    # as dark and farther off); if none improves, it cowers in place
                    cl = light[ey][ex]
                    best, bl, bd = -1, 99, -1
                    for dd in range(4):
                        wx, wy = ex + DX[dd], ey + DY[dd]
                        if not (0 <= wx < MW and 0 <= wy < MH) or self._blocked(st2, wx, wy, braz_t, en): continue
                        if wx == px and wy == py: continue
                        l = light[wy][wx]; dist = abs(wx - px) + abs(wy - py)
                        if l < bl or (l == bl and dist > bd): best, bl, bd = dd, l, dist
                    if best >= 0 and (bl < cl or (bl == cl and bd > md)):
                        e[1] += DX[best]; e[2] += DY[best]
                elif md == 1:
                    e[1], e[2] = px, py; died = True
                else:
                    # BFS through darkness
                    from collections import deque
                    seen = [[False] * MW for _ in range(MH)]; stepd = [[-1] * MW for _ in range(MH)]
                    q = deque([(ex, ey)]); seen[ey][ex] = True; found = False
                    while q and not found:
                        cx, cy = q.popleft()
                        for dd in range(4):
                            wx, wy = cx + DX[dd], cy + DY[dd]
                            if not (0 <= wx < MW and 0 <= wy < MH) or seen[wy][wx]: continue
                            goal = (wx == px and wy == py)
                            if not goal and (light[wy][wx] >= 2 or self._blocked(st2, wx, wy, braz_t, en)): continue
                            seen[wy][wx] = True
                            stepd[wy][wx] = dd if (cx == ex and cy == ey) else stepd[cy][cx]
                            if goal: found = True; break
                            q.append((wx, wy))
                    if found:
                        dd = stepd[py][px]
                        if dd >= 0: e[1] += DX[dd]; e[2] += DY[dd]
                    else:
                        best, bl, bd = -1, 99, 999
                        for dd in range(4):
                            wx, wy = ex + DX[dd], ey + DY[dd]
                            if not (0 <= wx < MW and 0 <= wy < MH) or self._blocked(st2, wx, wy, braz_t, en): continue
                            l = light[wy][wx]; dist = abs(wx - px) + abs(wy - py)
                            if l < 2 and (l < bl or (l == bl and dist < bd)): best, bl, bd = dd, l, dist
                        if best >= 0: e[1] += DX[best]; e[2] += DY[best]
            elif t == 1:  # skeleton
                for _ in range(2):
                    wx, wy = e[1] + DX[e[3]], e[2] + DY[e[3]]
                    if wx == px and wy == py:
                        e[1], e[2] = wx, wy; died = True; break
                    if not (0 <= wx < MW and 0 <= wy < MH) or self._blocked(st2, wx, wy, braz_t, en):
                        e[3] = (1 if e[3] == 0 else 0) if e[3] < 2 else (3 if e[3] == 2 else 2)
                        continue
                    e[1], e[2] = wx, wy; break
            else:  # spider
                if light[ey][ex] < 3:
                    sx = (px > ex) - (px < ex); sy = (py > ey) - (py < ey)
                    for cx, cy in ((sx, sy), (sx, 0), (0, sy)):
                        if cx == 0 and cy == 0: continue
                        wx, wy = ex + cx, ey + cy
                        if wx == px and wy == py:
                            e[1], e[2] = wx, wy; died = True; break
                        if not (0 <= wx < MW and 0 <= wy < MH) or self._blocked(st2, wx, wy, braz_t, en): continue
                        e[1], e[2] = wx, wy; break
            if e[1] == px and e[2] == py: died = True
            if died: return None
        st3 = (px, py, braz_t, tuple(tuple(e) for e in en), relics, oils, lit, scored,
               radius, oil_left, freeze, newturn & 1, gate, rune_used, lever, seqp)
        st3 = self._gates(st3)
        return st3, score, solved

    def reachable(self, st, pressed=None, lever=None, seqp=None):
        """Cells reachable with braziers as obstacles, enemies ignored. Optionally
        overrides the plate count / lever state / seal progress to ask
        'what if the gates changed?'"""
        from collections import deque
        seen = {(st[0], st[1])}; q = deque([(st[0], st[1])])
        while q:
            cx, cy = q.popleft()
            for dd in range(4):
                nx, ny = cx + DX[dd], cy + DY[dd]
                if (nx, ny) in seen or not (0 <= nx < MW and 0 <= ny < MH): continue
                if self._solid(st, nx, ny, pressed=pressed, lever=lever, seqp=seqp) or (nx, ny) in st[2]: continue
                seen.add((nx, ny)); q.append((nx, ny))
        return seen

    def heuristic(self, st):
        px, py = st[0], st[1]
        rel = st[4]
        if rel:
            d = min(abs(px - x) + abs(py - y) for x, y in rel)
            return 40 * len(rel) + d
        return abs(px - self.door[0]) + abs(py - self.door[1])

def solve_level(idx):
    lv = LEVELS[idx]; sim = Sim(lv)
    start = sim.init
    t0 = time.time()
    pq = [(sim.heuristic(start) * 2, 0, 0, start, 0)]  # f, g, tiebreak, state, score
    best = {start: (0, 0)}  # state -> (g, score)
    parent = {start: None}
    counter = 0; expansions = 0
    while pq:
        f, g, _, st, sc = heapq.heappop(pq)
        if best.get(st, (10 ** 9, 0))[0] < g: continue
        expansions += 1
        if expansions > 1500000: return None, f"expansion cap ({time.time()-t0:.0f}s)"
        for act in ACTS:
            r = sim.step(st, act)
            if r is None: continue
            nst, gain, solved = r
            ng, nsc = g + 1, sc + gain
            if solved:
                moves = ng
                bonus = max(0, 600 - 15 * moves)
                path = [act]; cur = st
                while parent[cur] is not None:
                    cur, a = parent[cur]; path.append(a)
                path.reverse()
                return (path, nsc + bonus), f"{expansions} expansions, {time.time()-t0:.1f}s"
            if nst not in best or ng < best[nst][0]:
                best[nst] = (ng, nsc)
                parent[nst] = (st, act)
                counter += 1
                heapq.heappush(pq, (ng + sim.heuristic(nst) * 2, ng, counter, nst, nsc))
    return None, "no solution"

def stage_search(sim, st0, target=None, cap=800000, mode="any"):
    """A* from st0 until a progress event: a relic collected, a mechanism advanced
    (plate / twin plates / lever flip / rune / all candles), or the level solved."""
    def braz_on_plate(st):
        return any(b in sim.plates for b in st[2])
    def braz_pressed(st):
        return sum(1 for p in sim.plates if p in st[2])
    need = 2 if sim.kgates else 1
    plate0 = braz_on_plate(st0)
    nrel0 = len(st0[4]); lever0 = st0[14]; rune0 = st0[13]
    unlit0 = len(sim.candles - st0[6])
    parked0 = braz_pressed(st0)
    seqp0 = st0[15]
    def is_goal(st, solved):
        if not solved and any(b in sim.dead for b in st[2]): return False
        if mode not in ("plate", "twin") and braz_pressed(st) < parked0: return False
        if mode == "plate": return braz_on_plate(st)
        if mode == "twin": return braz_pressed(st) >= need
        if mode == "lever": return st[14] != lever0
        if mode == "seq": return st[15] > seqp0
        if mode == "rune": return st[13]
        if mode == "candles": return sim.candles <= st[6]
        if mode == "candles1": return len(sim.candles - st[6]) < unlit0
        if solved: return True
        if target is not None: return target not in st[4]
        if len(st[4]) < nrel0: return True
        if not plate0 and braz_on_plate(st): return True
        if st[13] and not rune0: return True
        if st[15] > seqp0: return True
        if len(sim.candles - st[6]) < unlit0: return True
        return False
    # Wall-aware distance fields (BFS over non-wall cells, gates treated as
    # passable). Manhattan distance floods walled-off chambers whose
    # straight-line distance looks close; fields keep A* focused on the real
    # route, which matters in big enemy-product state spaces (L14).
    def bfs_field(tgt):
        fld = {tgt: 0}
        frontier = [tgt]
        while frontier:
            nxt = []
            for cx, cy in frontier:
                for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    np_ = (cx + dx, cy + dy)
                    if np_ in fld or np_ in sim.walls: continue
                    if not (0 <= np_[0] < MW and 0 <= np_[1] < MH): continue
                    fld[np_] = fld[(cx, cy)] + 1
                    nxt.append(np_)
            frontier = nxt
        return fld
    tfield = bfs_field(target) if target is not None else None
    cfields = [bfs_field(c) for c in (sim.candles - st0[6])] if mode in ("candles", "candles1") else None
    def push_h(st, plates):
        px, py = st[0], st[1]
        best = 999
        for bx, by in st[2]:
            for gx, gy in plates:
                v = abs(px - bx) + abs(py - by) + 2 * (abs(bx - gx) + abs(by - gy))
                if v < best: best = v
        return best
    def h(st):
        px, py = st[0], st[1]
        if mode == "twin":
            open_plates = [p for p in sim.plates if p not in st[2]]
            if not open_plates: return 0
            tot = min(abs(px - bx) + abs(py - by) for bx, by in st[2])
            for gx, gy in open_plates:
                tot += min(2 * (abs(bx - gx) + abs(by - gy)) for bx, by in st[2])
            return tot
        if mode == "plate":
            open_plates = [p for p in sim.plates if p not in st[2]]
            return push_h(st, open_plates or sim.plates)
        if mode == "lever":
            return min(abs(px - x) + abs(py - y) for x, y in sim.levers) if sim.levers else 0
        if mode == "rune":
            return abs(px - sim.rune[0]) + abs(py - sim.rune[1]) if sim.rune else 0
        if mode in ("candles", "candles1"):
            if cfields: return min(f.get((px, py), 80) for f in cfields)
            un = sim.candles - st[6]
            return min((abs(px - x) + abs(py - y) for x, y in un), default=0)
        if target is not None:
            if tfield is not None: return tfield.get((px, py), 80)
            return abs(px - target[0]) + abs(py - target[1])
        if st[4]: return min(abs(px - x) + abs(py - y) for x, y in st[4])
        return abs(px - sim.door[0]) + abs(py - sim.door[1])
    pq = [(h(st0) * 2, 0, 0, st0, 0)]
    best = {st0: 0}; parent = {st0: None}; counter = 0; expansions = 0
    while pq:
        f, g, _, st, sc = heapq.heappop(pq)
        if best.get(st, 10 ** 9) < g: continue
        expansions += 1
        if expansions > cap: return None
        for act in ACTS:
            r = sim.step(st, act)
            if r is None: continue
            nst, gain, solved = r
            if is_goal(nst, solved):
                path = [act]; cur = st
                while parent[cur] is not None:
                    cur, a = parent[cur]; path.append(a)
                path.reverse()
                return path, nst, sc + gain, solved
            ng = g + 1
            if nst not in best or ng < best[nst]:
                best[nst] = ng; parent[nst] = (st, act); counter += 1
                heapq.heappush(pq, (ng + h(nst) * 2, ng, counter, nst, sc + gain))
    return None

def solve_staged(idx):
    sim = Sim(LEVELS[idx]); st = sim.init; path = []; total_gain = 0
    seen = set()
    for stage_no in range(40):
        if st in seen: return None, "stage loop"
        seen.add(st)
        # Mechanism check: if targets are sealed, work out WHICH device opens the
        # way (plates, the lever, or both) and run that dedicated stage first.
        targets = set(st[4]) if st[4] else {sim.door}
        reach = sim.reachable(st)
        # Grab first: if any relic is reachable as-is, take it before touching
        # a device. Flipping a switch while loot is in reach just ping-pongs
        # the lever (each flip makes a different relic the reachable one).
        grabbable = [t for t in st[4] if t in reach]
        if grabbable:
            tgt = min(grabbable, key=lambda t: abs(t[0] - st[0]) + abs(t[1] - st[1]))
            res = stage_search(sim, st, target=tgt)
            if res is not None:
                p, st, gain, solved = res
                path += p; total_gain += gain
                if solved:
                    bonus = max(0, 600 - 15 * len(path))
                    return (path, total_gain + bonus), f"staged, {stage_no + 1} stages"
                continue
        if any(t not in reach for t in targets):
            rp = sim.reachable(st, pressed=99)
            rl = sim.reachable(st, lever=not st[14])
            rb = sim.reachable(st, pressed=99, lever=not st[14])
            need = 2 if sim.kgates else 1
            have = sum(1 for p in sim.plates if p in st[2])
            mode = None
            if all(t in rp for t in targets) and have < need: mode = "twin" if need == 2 else "plate"
            elif all(t in rl for t in targets): mode = "lever"
            elif all(t in rb for t in targets): mode = "twin" if have < need else "lever"
            elif have < need and any(t in rp for t in targets): mode = "twin" if need == 2 else "plate"
            elif any(t in rl for t in targets): mode = "lever"
            if mode is None and sim.seq_total and st[15] < sim.seq_total:
                rseq = sim.reachable(st, seqp=sim.seq_total)
                if any(t in rseq for t in targets): mode = "seq"
            if mode == "twin" and have == 0:
                res = stage_search(sim, st, mode="plate")
                if res is None: return None, f"plate pre-stage {stage_no} failed"
                p, st, gain, solved = res
                path += p; total_gain += gain
                if solved:
                    bonus = max(0, 600 - 15 * len(path))
                    return (path, total_gain + bonus), f"staged, {stage_no + 1} stages"
                continue
            if mode:
                tgt = sim.seq_by_digit.get(st[15] + 1) if mode == "seq" else None
                res = stage_search(sim, st, mode=mode, target=tgt)
                if res is None: return None, f"{mode} stage {stage_no} failed"
                p, st, gain, solved = res
                path += p; total_gain += gain
                if solved:
                    bonus = max(0, 600 - 15 * len(path))
                    return (path, total_gain + bonus), f"staged, {stage_no + 1} stages"
                continue
        # Door-seal stages: relics done but the door itself is still sealed.
        if not st[4]:
            if (sim.flags & 2) and not st[13] and sim.rune:
                res = stage_search(sim, st, mode="rune")
                if res is None: return None, f"rune stage {stage_no} failed"
                p, st, gain, solved = res
                path += p; total_gain += gain
                continue
            if (sim.flags & 1) and not sim.candles <= st[6]:
                res = stage_search(sim, st, mode="candles1")
                if res is None: return None, f"candle stage {stage_no} failed"
                p, st, gain, solved = res
                path += p; total_gain += gain
                continue
        res = stage_search(sim, st)
        if res is None:
            for r in sorted(st[4]):
                res = stage_search(sim, st, target=r)
                if res: break
        if res is None: return None, f"stage {stage_no} failed"
        p, st, gain, solved = res
        path += p; total_gain += gain
        if solved:
            bonus = max(0, 600 - 15 * len(path))
            return (path, total_gain + bonus), f"staged, {stage_no + 1} stages"
    return None, "too many stages"

def main():
    stream = [0x0008]  # START at title
    total_actions = 0; total_score = 0; per_level = []
    cache = {}
    try: cache = json.load(open("qa/solutions.json"))
    except Exception: pass
    for i in range(len(LEVELS)):
        if str(i) in cache:
            sol = (cache[str(i)]["path"], cache[str(i)]["score"]); info = "cached"
            print(f"LEVEL {i+1} ({LEVELS[i]['name']}): cached, {len(sol[0])} actions, score +{sol[1]}")
            stream.append(0x0001); stream += [KEY[a] for a in sol[0]]
            total_actions += len(sol[0]); total_score += sol[1]
            per_level.append(dict(level=i + 1, name=LEVELS[i]["name"], actions=len(sol[0]), score=sol[1]))
            continue
        if i in (4, 7, 8, 12, 13):
            sol, info = solve_staged(i)
            if sol is None:
                sol, info2 = solve_level(i); info += "; whole: " + info2
        else:
            sol, info = solve_level(i)
            if sol is None:
                sol, info2 = solve_staged(i); info += "; staged: " + info2
        if sol is None:
            print(f"LEVEL {i+1} ({LEVELS[i]['name']}): FAILED - {info}")
            sys.exit(1)
        path, sc = sol
        print(f"LEVEL {i+1} ({LEVELS[i]['name']}): solved in {len(path)} actions, score +{sc} [{info}]")
        cache[str(i)] = dict(path=path, score=sc)
        json.dump(cache, open("qa/solutions.json", "w"))
        stream.append(0x0001)  # A at intro card
        stream += [KEY[a] for a in path]
        total_actions += len(path); total_score += sc
        per_level.append(dict(level=i + 1, name=LEVELS[i]["name"], actions=len(path), score=sc))
    stream.append(0x0008)  # START at end screen -> back to title
    with open("src/qa_script.h", "w") as fh:
        fh.write("// Generated by solve.py - scripted inputs for the QA build.\n")
        fh.write(f"#define QA_NACT {len(stream)}\n")
        fh.write("static const unsigned short QA_ACTIONS[QA_NACT] = {\n    ")
        fh.write(", ".join(f"0x{v:04X}" for v in stream))
        fh.write("\n};\n")
    with open("qa/expected.json", "w") as fh:
        json.dump(dict(total_play_actions=total_actions, predicted_score=total_score,
                       stream_len=len(stream), per_level=per_level), fh, indent=1)
    print(f"TOTAL: {total_actions} play actions, predicted score {total_score}, stream {len(stream)}")

if __name__ == "__main__":
    main()
