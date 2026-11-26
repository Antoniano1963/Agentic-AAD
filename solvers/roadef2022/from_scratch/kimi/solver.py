#!/usr/bin/env python3
"""ROADEF2022 truck loading solver - from scratch (KIMI run01).

Design (v003):
  - Every unit has a last-feasible arrival LF (latest truck arrival inside its
    time window that serves its supplier/product/supplier dock/plant dock
    combination, taken from the truck file rows). Loading at LF gives zero
    inventory cost, so the construction never pushes items back in time:
    process arrivals descending, fill all planned trucks, then call extra
    trucks (1.2x cost) for the units whose deadline is this arrival.
  - Stacks: grouped by (supplier, supplier dock, plant dock, stackability
    code, forced orientation); NFD by effective height with limits
    (max stackability, truck height, weight above bottom, density).
  - Packing: stacks sequenced by P4 keys (supplier order, supplier dock
    order, plant dock order); greedy columns along X; P3 enforced via the
    previous-column coverage rule.
  - Axle weights (W2) checked per supplier prefix; violations repaired by
    dropping the heaviest stack of the worst prefix back to the pool.
  - Post-pass: truck elimination moves all stacks of the least valuable
    trucks (extras first) into other used trucks or unused planned trucks,
    respecting item windows, compatibilities, stack height, density, bottom
    weight and axle limits; a move is committed only when its cost gain
    (truck cost saved minus inventory added) is positive.
"""
import csv
import os
import random
import sys
import time
from bisect import bisect_left, bisect_right
from collections import defaultdict
from datetime import date


def fnum(s):
    return float(s.replace(" ", "").replace(",", "."))


def inum(s):
    return int(s.replace(" ", ""))


def dayof(ts):
    return date(int(ts[0:4]), int(ts[4:6]), int(ts[6:8]))


# ---------------------------------------------------------------- model
class Unit:
    __slots__ = ("uid", "line_id", "supplier", "sdock", "pdock", "product",
                 "scode", "forc", "L", "W", "H", "nest", "weight", "ism",
                 "icost", "ide", "idl", "gkey")

    def __init__(self, **kw):
        for k, v in kw.items():
            setattr(self, k, v)


class Truck:
    __slots__ = ("tid", "plant", "arrival", "aday", "L", "W", "H", "maxw",
                 "cost", "multi", "maxdens", "sup_order", "sdock_order",
                 "pdock_order", "products", "tmm", "sup_sdocks",
                 "emm", "emr", "cm", "cjfm", "cjfc", "cjfh", "em", "ejhr",
                 "ejcr", "ejeh", "extra", "src")

    def __init__(self, tid):
        self.tid = tid
        self.sup_order = {}
        self.sdock_order = {}
        self.pdock_order = {}
        self.products = set()
        self.tmm = {}
        self.sup_sdocks = defaultdict(set)
        self.extra = False
        self.src = None


class Stack:
    __slots__ = ("units", "L", "W", "orient", "x_ext", "y_ext", "height",
                 "weight", "x0", "y0", "x1", "y1", "sup", "sdock", "pdock",
                 "scode", "minlf")

    def __init__(self):
        self.units = []
        self.height = 0
        self.weight = 0.0
        self.x0 = self.y0 = self.x1 = self.y1 = 0
        self.x_ext = self.y_ext = None
        self.minlf = None


# ---------------------------------------------------------------- parsing
def read_instance(indir):
    with open(os.path.join(indir, "input_parameters.csv"), encoding="latin1") as f:
        rows = list(csv.reader(f, delimiter=";"))
    v = rows[1]
    params = dict(coeff_inv=fnum(v[0]), coeff_trans=fnum(v[1]),
                  coeff_extra=fnum(v[2]), timelimit=fnum(v[3]))

    units = []
    with open(os.path.join(indir, "input_items.csv"), encoding="latin1") as f:
        rd = csv.DictReader(f, delimiter=";")
        uid = 0
        for r in rd:
            n = inum(r["Number of items"])
            base = dict(
                line_id=r["Item ident"].strip(),
                supplier=r["Supplier code"].strip(),
                sdock=r["Supplier dock"].strip(),
                pdock=r["Plant dock"].strip(),
                product=r["Product code"].strip(),
                scode=r["Stackability code"].strip(),
                forc=r["Forced orientation"].strip().lower(),
                L=inum(r["Length"]), W=inum(r["Width"]), H=inum(r["Height"]),
                nest=inum(r["Nesting height"]),
                weight=fnum(r["Weight"]),
                ism=inum(r["Max stackability"]),
                icost=fnum(r["Inventory cost"]),
                ide=r["Earliest arrival time"].strip(),
                idl=r["Latest arrival time"].strip(),
            )
            base["gkey"] = (base["supplier"], base["sdock"], base["pdock"],
                            base["scode"], base["forc"])
            for _ in range(n):
                u = Unit(uid=uid, **base)
                units.append(u)
                uid += 1

    trucks = {}
    trows = []  # (arrival, supplier, product, supplier dock, plant dock)
    with open(os.path.join(indir, "input_trucks.csv"), encoding="latin1") as f:
        rd = csv.DictReader(f, delimiter=";")
        for r in rd:
            tid = r["Id truck"].strip()
            t = trucks.get(tid)
            if t is None:
                t = Truck(tid)
                t.plant = r["Plant code"].strip()
                t.arrival = r["Arrival time"].strip()
                t.aday = t.arrival[:8]
                t.L = inum(r["Length"]); t.W = inum(r["Width"]); t.H = inum(r["Height"])
                t.maxw = inum(r["Max weight"])
                t.cost = inum(r["Cost"])
                t.multi = r["Stack with multiple docks"].strip() == "1"
                t.maxdens = inum(r["Max density"])
                t.emm = fnum(r["EMmm"]); t.emr = fnum(r["EMmr"])
                t.cm = fnum(r["CM"])
                t.cjfm = inum(r["CJfm"]); t.cjfc = inum(r["CJfc"]); t.cjfh = inum(r["CJfh"])
                t.em = fnum(r["EM"])
                t.ejhr = inum(r["EJhr"]); t.ejcr = inum(r["EJcr"]); t.ejeh = inum(r["EJeh"])
                trucks[tid] = t
            sup = r["Supplier code"].strip()
            t.sup_order[sup] = inum(r["Supplier loading order"])
            sd = r["Supplier dock"].strip()
            t.sdock_order[sd] = inum(r["Supplier dock loading order"])
            t.sup_sdocks[sup].add(sd)
            pd = r["Plant dock"].strip()
            t.pdock_order[pd] = inum(r["Plant dock loading order"])
            prod = r["Product code"].strip()
            t.products.add(prod)
            t.tmm[prod] = inum(r["Max weight on the bottom item in stacks"])
            trows.append((t.arrival, sup, prod, sd, pd))

    return params, units, list(trucks.values()), trows


# ---------------------------------------------------------------- stacking
def build_stacks(cands, truck, lf_of=None):
    """cands: eligible units. Returns list of Stack (unpositioned)."""
    groups = defaultdict(list)
    for u in cands:
        groups[u.gkey].append(u)
    stacks = []
    for gkey, us in groups.items():
        fl = [u for u in us if u.forc == "lengthwise"]
        fw = [u for u in us if u.forc == "widthwise"]
        fn = [u for u in us if u.forc not in ("lengthwise", "widthwise")]
        if fl:
            _stack_part(fl, "lengthwise", truck, stacks)
        if fw:
            _stack_part(fw, "widthwise", truck, stacks)
        if fn:
            _stack_part(fn, None, truck, stacks)
    if lf_of is not None:
        for s in stacks:
            s.minlf = min((lf_of.get(u.uid) or "99999999999999")
                          for u in s.units)
    return stacks


def _stack_part(us, forc, truck, out):
    if STACK_MODE == 1:
        return _stack_part_bfd(us, forc, truck, out)
    TH = truck.H
    us.sort(key=lambda u: -(u.H - u.nest))
    cur = None
    cur_min_ism = 0
    cur_above_w = 0.0
    cur_tmm = float("inf")
    dens_lim = 0.0

    def close():
        nonlocal cur
        if cur is not None and cur.units:
            out.append(cur)
        cur = None

    def open_new(u):
        nonlocal cur, cur_min_ism, cur_above_w, cur_tmm, dens_lim
        cur = Stack()
        cur.L, cur.W = u.L, u.W
        cur.sup, cur.sdock, cur.pdock, cur.scode = u.supplier, u.sdock, u.pdock, u.scode
        cur.orient = forc
        cur.units.append(u)
        cur.height = u.H
        cur.weight = u.weight
        cur_min_ism = u.ism
        cur_above_w = 0.0
        area_m2 = (u.L * u.W) / 1e6
        dens_lim = (truck.maxdens + 1) * area_m2 - 0.01
        tmm = truck.tmm.get(u.product)
        cur_tmm = tmm if tmm is not None else float("inf")

    for u in us:
        if cur is None:
            open_new(u)
            continue
        if (len(cur.units) + 1 <= min(cur_min_ism, u.ism)
                and cur.height + (u.H - u.nest) <= TH
                and cur_above_w + u.weight <= cur_tmm
                and cur.weight + u.weight <= dens_lim):
            cur.units.append(u)
            cur.height += u.H - u.nest
            cur.weight += u.weight
            cur_above_w += u.weight
            if u.ism < cur_min_ism:
                cur_min_ism = u.ism
        else:
            close()
            open_new(u)
    close()


def _stack_part_bfd(us, forc, truck, out, max_open=14):
    """Best-fit-decreasing stacking: keep up to max_open stacks open per
    group; each unit joins the feasible open stack with the greatest
    current height (least vertical slack). Same limits as NFD (ism, truck
    height, weight above bottom, density). Produces taller stacks, hence
    fewer floor positions and shorter loads - valuable for length-bound
    instances where the axle window has slack."""
    TH = truck.H
    us.sort(key=lambda u: -(u.H - u.nest))
    open_st = []  # [stack, min_ism, above_w, tmm, dens_lim]
    for u in us:
        best = None
        best_h = -1
        for st in open_st:
            s, min_ism, above_w, tmm, dens_lim = st
            if (len(s.units) + 1 <= min(min_ism, u.ism)
                    and s.height + (u.H - u.nest) <= TH
                    and above_w + u.weight <= tmm
                    and s.weight + u.weight <= dens_lim
                    and s.height > best_h):
                best_h = s.height
                best = st
        if best is None:
            cur = Stack()
            cur.L, cur.W = u.L, u.W
            cur.sup, cur.sdock, cur.pdock, cur.scode = (u.supplier, u.sdock,
                                                        u.pdock, u.scode)
            cur.orient = forc
            cur.units.append(u)
            cur.height = u.H
            cur.weight = u.weight
            area_m2 = (u.L * u.W) / 1e6
            dens_lim = (truck.maxdens + 1) * area_m2 - 0.01
            tmm = truck.tmm.get(u.product)
            st = [cur, u.ism, 0.0,
                  tmm if tmm is not None else float("inf"), dens_lim]
            if len(open_st) >= max_open:
                fullest = max(range(len(open_st)),
                              key=lambda i: open_st[i][0].height)
                out.append(open_st.pop(fullest)[0])
            open_st.append(st)
        else:
            s = best[0]
            s.units.append(u)
            s.height += u.H - u.nest
            s.weight += u.weight
            best[1] = min(best[1], u.ism)
            best[2] += u.weight
    for st in open_st:
        out.append(st[0])


# ---------------------------------------------------------------- packing
ORIENT_MODE = 1
RETRY_ORIENT = True
STACK_MODE = 0


def choose_orientation(s, truck):
    """Set x_ext/y_ext for stack s given truck width. False if no fit.
    Free stacks prefer the LONG side along X: trucks are usually
    demand-limited, not length-limited, and stretching the load along X
    moves the center of gravity rearward, which relaxes the binding emm
    axle limit (weight ahead of E=ejeh+ejhr raises emm)."""
    L, W = s.L, s.W
    if s.orient == "lengthwise":
        if W <= truck.W:
            s.x_ext, s.y_ext = L, W
            return True
        return False
    if s.orient == "widthwise":
        if L <= truck.W:
            s.x_ext, s.y_ext = W, L
            return True
        return False
    # free: ORIENT_MODE 1 prefers the long side along X (stretches the
    # load, moves CG rearward, relaxes emm); mode 0 prefers short side
    if L <= truck.W and W <= truck.W:
        if (W <= L) == (ORIENT_MODE == 1):
            s.x_ext, s.y_ext = L, W
            s.orient = "lengthwise"
        else:
            s.x_ext, s.y_ext = W, L
            s.orient = "widthwise"
        return True
    if L <= truck.W:
        s.x_ext, s.y_ext = W, L
        s.orient = "widthwise"
        return True
    if W <= truck.W:
        s.x_ext, s.y_ext = L, W
        s.orient = "lengthwise"
        return True
    return False


def seq_key(s, truck):
    # P4 keys, then stackability code (clusters identical footprints),
    # then weight ascending: heavy stacks go to the rear (helps W2 axle CG)
    return (truck.sup_order.get(s.sup, 0),
            truck.sdock_order.get(s.sdock, 0),
            truck.pdock_order.get(s.pdock, 0),
            s.scode, s.weight)


def pack_truck(stacks, truck):
    """Sequence stacks and cut columns (uniform X-extent per column).
    Sequence: P4 runs (supplier, supplier dock, plant dock), stackability
    code clusters, weight ascending (heavy to the rear, helps W2 axle CG).
    Returns (packed, leftover). Positions are only written to stacks that
    end up packed; leftover stacks keep their previous coordinates."""
    fittable = []
    for s in stacks:
        if s.height <= truck.H and choose_orientation(s, truck):
            fittable.append(s)
    fittable.sort(key=lambda s: seq_key(s, truck))

    packed = []
    coords = []
    x_cursor = 0
    cov_prev = truck.W  # first column unconstrained
    cur_col = []
    h_cur = 0
    x_w = 0
    tot_weight = 0.0
    i = 0
    n = len(fittable)

    def close_col():
        nonlocal x_cursor, cov_prev, cur_col, h_cur, x_w
        if not cur_col:
            return True
        if x_cursor + x_w > truck.L:
            return False
        y = 0
        for s in cur_col:
            coords.append((s, x_cursor, y, x_cursor + s.x_ext, y + s.y_ext))
            y += s.y_ext
            packed.append(s)
        x_cursor += x_w
        cov_prev = h_cur
        cur_col = []
        h_cur = 0
        x_w = 0
        return True

    while i < n:
        s = fittable[i]
        if tot_weight + s.weight > truck.maxw + 0.1:
            break  # weight limit: tail unassigned
        if cur_col and s.x_ext != x_w:
            if not close_col():
                break
            continue
        if h_cur + s.y_ext <= truck.W and h_cur < cov_prev:
            cur_col.append(s)
            h_cur += s.y_ext
            if s.x_ext > x_w:
                x_w = s.x_ext
            tot_weight += s.weight
            i += 1
        else:
            if not close_col():
                break
    if cur_col:
        close_col()
    for s, x0, y0, x1, y1 in coords:
        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
    leftover = fittable[len(packed):]
    return packed, leftover


# ---------------------------------------------------------------- axles
AXLE_STATS = {}


def axle_bad_prefixes(truck, packed):
    orders = sorted(set(truck.sup_order.get(s.sup, 0) for s in packed))
    bad = []
    for k in orders:
        sel = [s for s in packed if truck.sup_order.get(s.sup, 0) <= k]
        tm = sum(s.weight for s in sel)
        if tm <= 0:
            continue
        cg = sum((s.x0 + (s.x1 - s.x0) / 2.0) * s.weight for s in sel) / tm
        ejr = truck.ejeh + truck.ejhr - cg
        emh = (tm * ejr + truck.em * truck.ejcr) / truck.ejhr
        emr = tm + truck.em - emh
        emm = (truck.cm * truck.cjfc + emh * truck.cjfh) / truck.cjfm
        if emm > truck.emm + 0.1:
            AXLE_STATS["emm"] = AXLE_STATS.get("emm", 0) + 1
            bad.append(k)
        elif emr > truck.emr + 0.1:
            AXLE_STATS["emr"] = AXLE_STATS.get("emr", 0) + 1
            bad.append(k)
    return bad


def repair_axles(truck, stacks, packed, leftover):
    """Check W2 prefixes; while a prefix violates, drop one stack of the
    worst prefix and repack the remaining stacks. Victim selection: the
    lightest stack whose removal brings both axle loads within limits
    (removing weight at low X raises the remaining CG, which lowers emm);
    if none fixes it, the stack whose removal reduces emm the most.
    Returns (packed, leftover) with positions recomputed."""
    def axle_of(sel):
        tw = sum(s.weight for s in sel)
        if tw <= 0:
            return 0.0, 0.0
        cg = sum((s.x0 + (s.x1 - s.x0) / 2.0) * s.weight for s in sel) / tw
        ejr = truck.ejeh + truck.ejhr - cg
        emh = (tw * ejr + truck.em * truck.ejcr) / truck.ejhr
        emr = tw + truck.em - emh
        emm = (truck.cm * truck.cjfc + emh * truck.cjfh) / truck.cjfm
        return emm, emr

    for _ in range(len(stacks) + 1):
        bad = axle_bad_prefixes(truck, packed)
        if not bad:
            return packed, leftover
        k = bad[0]
        prefix = [s for s in packed if truck.sup_order.get(s.sup, 0) <= k]
        base_emm, _ = axle_of(prefix)
        victim = None
        for v in sorted(prefix, key=lambda s: s.weight):
            emm, emr = axle_of([s for s in prefix if s is not v])
            if emm <= truck.emm + 0.1 and emr <= truck.emr + 0.1:
                victim = v
                break
        if victim is None:
            best_red = -1.0
            for v in prefix:
                emm, _ = axle_of([s for s in prefix if s is not v])
                if base_emm - emm > best_red:
                    best_red = base_emm - emm
                    victim = v
        remaining = [s for s in stacks if s is not victim]
        stacks = remaining
        packed, leftover = pack_truck(stacks, truck)
    return [], stacks


# ---------------------------------------------------------------- solver
def clone_truck(src, n):
    et = Truck("Q" + src.tid[1:] + "_" + str(n))
    et.plant = src.plant
    et.arrival = src.arrival
    et.aday = src.aday
    et.L, et.W, et.H = src.L, src.W, src.H
    et.maxw = src.maxw
    et.cost = src.cost
    et.multi = src.multi
    et.maxdens = src.maxdens
    et.sup_order = src.sup_order
    et.sdock_order = src.sdock_order
    et.pdock_order = src.pdock_order
    et.products = src.products
    et.tmm = src.tmm
    et.sup_sdocks = src.sup_sdocks
    et.emm, et.emr = src.emm, src.emr
    et.cm, et.cjfm, et.cjfc, et.cjfh = src.cm, src.cjfm, src.cjfc, src.cjfh
    et.em, et.ejhr, et.ejcr, et.ejeh = src.em, src.ejhr, src.ejcr, src.ejeh
    et.extra = True
    et.src = src
    return et


def solve(params, units, trucks, trows, seed, log, t0, elim_deadline,
          hard_deadline):
    global ORIENT_MODE, RETRY_ORIENT, STACK_MODE
    trucks_by_arr = defaultdict(list)
    for t in trucks:
        trucks_by_arr[t.arrival].append(t)
    arrs = sorted(trucks_by_arr.keys(), reverse=True)

    # feasible arrivals per (supplier, product, supplier dock, plant dock) row
    key_arrs = defaultdict(set)
    for (arr, sup, prod, sd, pd) in trows:
        key_arrs[(sup, prod, sd, pd)].add(arr)
    key_list = {k: sorted(v) for k, v in key_arrs.items()}

    def key_of(u):
        return (u.supplier, u.product, u.sdock, u.pdock)

    LF = {}
    by_LF = defaultdict(list)
    n_infeasible = 0
    for u in units:
        lst = key_list.get(key_of(u))
        lf = None
        if lst:
            i = bisect_right(lst, u.idl) - 1
            if i >= 0 and lst[i] >= u.ide:
                lf = lst[i]
        LF[u.uid] = lf
        if lf is None:
            n_infeasible += 1
        else:
            by_LF[lf].append(u)
    if n_infeasible:
        log(f"ERROR: {n_infeasible} units have no feasible truck arrival")

    # EARLIER[u]: True when u has at least one feasible arrival before LF,
    # i.e. it may be pushed back (paying inventory) instead of forcing an
    # extra truck at its deadline.
    EARLIER = {}
    for u in units:
        lf = LF[u.uid]
        e = False
        if lf is not None:
            lst = key_list.get(key_of(u), ())
            i = bisect_left(lst, lf)
            if i > 0 and lst[i - 1] >= u.ide:
                e = True
        EARLIER[u.uid] = e

    pool = defaultdict(list)
    for u in units:
        pool[u.supplier].append(u)
    alive = None
    constructs = []
    e_half = 0.5 * (elim_deadline - t0)
    # A and B run with their own elimination slices (v011 lesson: sharing
    # starves the second variant). C is built adaptively: only when the
    # orient-1 NFD path leads, give BFD a chance on that same path, with a
    # deadline carved from the (usually mostly idle) ILS window. When the
    # orient-0 BFD path leads, C is skipped entirely, so VO/FC keep their
    # full ILS budget.
    plan = [(True, 1, 0, t0 + e_half), (False, 0, 1, elim_deadline)]
    c_built = False
    vi = 0
    while vi < len(plan):
        retry_on, mode, smode, edl = plan[vi]
        vi += 1
        RETRY_ORIENT = retry_on
        ORIENT_MODE = mode
        STACK_MODE = smode
        alive = set(u.uid for u in units)

        used = []  # [truck, packed_stacks, is_extra, source_truck]
        extra_count = defaultdict(int)
        retry = []

        def eligible(t, arr):
            out = []
            for sup in t.sup_order:
                sdocks = t.sup_sdocks.get(sup)
                for u in pool[sup]:
                    if u.uid not in alive:
                        continue
                    if u.product not in t.products:
                        continue
                    if u.pdock not in t.pdock_order:
                        continue
                    if u.sdock not in sdocks:
                        continue
                    if not (u.ide <= arr <= u.idl):
                        continue
                    out.append(u)
            return out

        def fill_into(t, arr, is_extra, srct):
            """Pack all eligible alive units into t. Returns (n, kept)."""
            global ORIENT_MODE
            stacks = build_stacks(eligible(t, arr), t)
            if not stacks:
                return 0, False
            free_stacks = [s for s in stacks
                           if s.orient not in ("lengthwise", "widthwise")]
            packed, leftover = pack_truck(stacks, t)
            packed, leftover = repair_axles(t, stacks, packed, leftover)
            n_packed = sum(len(s.units) for s in packed)
            n_total = sum(len(s.units) for s in stacks)
            if n_packed < n_total and free_stacks and RETRY_ORIENT:
                # axle repair dropped load: retry with the opposite
                # orientation preference (X-stretch moves the CG rearward
                # and relaxes emm), keep whichever packs more units
                saved = [(s, s.x0, s.y0, s.x1, s.y1, s.orient)
                         for s in packed]
                saved_mode = ORIENT_MODE
                ORIENT_MODE = 1 - ORIENT_MODE
                for s in free_stacks:
                    s.orient = None
                p2, l2 = pack_truck(stacks, t)
                p2, l2 = repair_axles(t, stacks, p2, l2)
                n2 = sum(len(s.units) for s in p2)
                ORIENT_MODE = saved_mode
                if n2 > n_packed:
                    packed = p2
                else:
                    for s, x0, y0, x1, y1, o in saved:
                        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
                        s.orient = o
            if not packed:
                return 0, False
            n = 0
            for s in packed:
                for u in s.units:
                    alive.discard(u.uid)
                    n += 1
            used.append([t, packed, is_extra, srct])
            return n, True

        for arr in arrs:
            for t in sorted(trucks_by_arr[arr], key=lambda t: -(t.L * t.W)):
                fill_into(t, arr, False, None)
            # deadline units (LF <= this arrival) must leave now or earlier
            dl = [u for u in by_LF.get(arr, ()) if u.uid in alive]
            if retry:
                dl += [u for u in retry
                       if u.uid in alive and u.ide <= arr <= u.idl
                       and arr in key_arrs.get(key_of(u), ())]
            retry = []
            failed_keys = set()
            while True:
                counts = defaultdict(int)
                for u in dl:
                    if u.uid in alive:
                        counts[key_of(u)] += 1
                if not counts:
                    dl = []
                    break
                progress = False
                for key in sorted(counts, key=lambda k: -counts[k]):
                    if key in failed_keys:
                        continue
                    sup, prod, sd, pd = key
                    src = None
                    for t in trucks_by_arr[arr]:
                        if sup not in t.sup_order:
                            continue
                        if prod not in t.products:
                            continue
                        if pd not in t.pdock_order:
                            continue
                        if sd not in t.sup_sdocks.get(sup, ()):
                            continue
                        if src is None or t.L * t.W > src.L * src.W:
                            src = t
                    if src is None:
                        failed_keys.add(key)
                        continue
                    extra_count[src.tid] += 1
                    et = clone_truck(src, extra_count[src.tid])
                    n, kept = fill_into(et, arr, True, src)
                    if n > 0:
                        progress = True
                    else:
                        extra_count[src.tid] -= 1
                        failed_keys.add(key)
                dl = [u for u in dl if u.uid in alive]
                if not progress:
                    break
            retry = [u for u in dl if u.uid in alive]

        # emergency: units still alive -> extra trucks at latest feasible arrival
        leftovers = [u for u in units if u.uid in alive]
        if leftovers:
            log(f"WARNING: {len(leftovers)} units unassigned after main loop; emergency assignment")
            em_groups = defaultdict(list)
            for u in leftovers:
                lst = [a for a in key_list.get(key_of(u), []) if u.ide <= a <= u.idl]
                if not lst:
                    log(f"ERROR: no truck arrival in window for unit {u.uid} line {u.line_id}")
                    continue
                em_groups[(lst[-1], u.supplier, u.product)].append(u)
            for (a, sup, prod), us in em_groups.items():
                while True:
                    remaining = [u for u in us if u.uid in alive]
                    if not remaining:
                        break
                    src = None
                    for t in trucks_by_arr[a]:
                        if sup in t.sup_order and prod in t.products:
                            if src is None or t.L * t.W > src.L * src.W:
                                src = t
                    if src is None:
                        log(f"ERROR: no source truck for {sup}/{prod}@{a}")
                        break
                    extra_count[src.tid] += 1
                    et = clone_truck(src, extra_count[src.tid])
                    n, kept = fill_into(et, a, True, src)
                    if n == 0:
                        break

        obj_m, _, _ = objective(used, params)
        log(f"construction retry={retry_on} orient={mode}: obj~{obj_m:.0f}, "
            f"{len(used)} trucks ({sum(1 for x in used if x[2])} extra)")
        used = eliminate_trucks(used, trucks, params, log, edl)
        used = merge_pairs(used, params, log, edl)
        obj_m, _, _ = objective(used, params)
        log(f"eliminated retry={retry_on} orient={mode}: obj~{obj_m:.0f}, "
            f"{len(used)} trucks ({sum(1 for x in used if x[2])} extra)")
        constructs.append((obj_m, mode, retry_on, smode, used, alive))
        if vi == 2 and not c_built:
            c_built = True
            lead = min(constructs, key=lambda c: c[0])
            if lead[3] == 0:
                # orient-1 NFD leads: try BFD on the same path, eliminated
                # inside a slice carved from the ILS window
                c_dl = elim_deadline + 0.5 * (hard_deadline - elim_deadline)
                log(f"variant C: retry={lead[2]} orient={lead[1]} "
                    f"stack=BFD (deadline +{c_dl - elim_deadline:.0f}s)")
                plan.append((lead[2], lead[1], 1, c_dl))

    constructs.sort(key=lambda c: c[0])
    _, ORIENT_MODE, RETRY_ORIENT, STACK_MODE, used, alive = constructs[0]
    log(f"selected retry={RETRY_ORIENT} orient_mode={ORIENT_MODE} "
        f"stack_mode={STACK_MODE}")

    # ---------------- iterated local search until hard deadline
    used, best_obj = ils(used, trucks, params, log, seed, hard_deadline)

    return used, alive


def ils(used, trucks, params, log, seed, hard_deadline):
    """inv_relax + eliminate rounds with snapshot/best tracking and
    stagnation-triggered perturbation. Returns (used, best_obj)."""
    rng = random.Random(seed)
    best_used = snapshot(used)
    best_obj, _, _ = objective(used, params)
    stagn = 0
    rb_fails = 0
    while time.time() < hard_deadline:
        # inventory-heavy instances (coeff_inv >= 3, the X2 family): every
        # day of inventory saved is worth 5x, so search the inventory-relax
        # neighbourhood deeper than the default (2 passes / 25 targets).
        if params["coeff_inv"] >= 3.0:
            used = inv_relax(used, params, log, hard_deadline,
                             max_passes=3, max_targets=40)
        else:
            used = inv_relax(used, params, log, hard_deadline)
        # transport-heavy instances (coeff_trans >= 3, the X3 family): a
        # dropped truck is worth 5x, so elimination and pair-merging get a
        # deeper slice of each ILS round (mirror of the X2 inv_relax
        # deepening above).
        x3 = params["coeff_trans"] >= 3.0
        used = eliminate_trucks(used, trucks, params, log, hard_deadline,
                                max_passes=2 if x3 else 1,
                                max_targets=240 if x3 else 120)
        obj, _, _ = objective(used, params)
        log(f"local search round: {obj:.0f} (best {best_obj:.0f}, "
            f"{len(used)} trucks, {sum(1 for x in used if x[2])} extra)")
        if obj < best_obj - 0.5:
            best_obj = obj
            best_used = snapshot(used)
            stagn = 0
            continue
        stagn += 1
        if time.time() > hard_deadline - 2:
            break
        used = restore(best_used)
        if stagn >= 2:
            used = merge_pairs(used, params, log, hard_deadline,
                               max_attempts=2500 if x3 else 1500)
        n_pert = perturb(used, rng, min(2 + stagn, 16), log)
        # rebuild fires (a) when the state is saturated for relocate/swap
        # (v032) and (b) as an escalated kick on odd high stagnation counts
        # even when small perturbations still succeed - the late ILS tail
        # otherwise churns 3-4 stack moves around the same objective.
        if n_pert == 0 or (stagn >= 5 and stagn % 2 == 1):
            if n_pert == 0 and stagn > 3 and rb_fails >= 5:
                break
            snap2 = snapshot(used)
            n_rb = rebuild(used, rng, min(4 + rb_fails * 2, 14), log)
            if n_rb < 0:
                used = restore(snap2)
                rb_fails += 1
            else:
                rb_fails = 0
    return restore(best_used), best_obj


def snapshot(used):
    return ([[t, list(pk), ex, sr] for t, pk, ex, sr in used],
            [(s, s.x0, s.y0, s.x1, s.y1, s.orient)
             for _, pk, _, _ in used for s in pk])


def restore(snap):
    used, coords = snap
    for s, x0, y0, x1, y1, o in coords:
        s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
        s.orient = o
    return [[t, list(pk), ex, sr] for t, pk, ex, sr in used]


def perturb(used, rng, k, log):
    """Randomly relocate all stacks of k random trucks into random
    compatible trucks (feasibility only, gain ignored). Stacks that find
    no feasible target stay. Returns number of relocated stacks."""
    cands = [i for i, u in enumerate(used) if u[1]]
    if len(cands) < 3:
        return 0
    rng.shuffle(cands)
    moved = 0
    swapped = 0
    for idx in cands[:k]:
        tA, packedA, exA, srcA = used[idx]
        if not packedA:
            continue
        for s in list(packedA):
            order = [j for j, u in enumerate(used) if u[1] and j != idx]
            rng.shuffle(order)
            placed = False
            swap_tried = 0
            for j in order[:25]:
                tB = used[j][0]
                if not move_compat(s, tB, tB.arrival):
                    continue
                packedB = used[j][1]
                savedB = [(x, x.x0, x.y0, x.x1, x.y1) for x in packedB]
                saved_s = (s.x0, s.y0, s.x1, s.y1, s.orient)
                newB = try_append(tB, packedB, s)
                if newB is None:
                    newB = try_accept_pack(tB, packedB + [s])
                if newB is not None:
                    newA = try_accept_pack(tA, [x for x in packedA
                                                if x is not s])
                    if newA is None:
                        for x, a0, b0, c0, d0 in savedB:
                            x.x0, x.y0, x.x1, x.y1 = a0, b0, c0, d0
                        s.x0, s.y0, s.x1, s.y1 = saved_s[:4]
                        s.orient = saved_s[4]
                        continue
                    used[j][1] = newB
                    used[idx][1] = newA
                    packedA = newA
                    moved += 1
                    placed = True
                    break
                # relocate failed (target full): try swapping s with a
                # stack of the target truck - needs no spare room
                if swap_tried >= 6:
                    continue
                pool = [x for x in packedB
                        if move_compat(x, tA, tA.arrival)]
                if not pool:
                    continue
                swap_tried += 1
                s2 = rng.choice(pool)
                savedB2 = [(x, x.x0, x.y0, x.x1, x.y1, x.orient)
                           for x in packedB] + [(s, s.x0, s.y0, s.x1, s.y1,
                                                 s.orient)]
                newB = try_accept_pack(
                    tB, [x for x in packedB if x is not s2] + [s])
                if newB is None:
                    continue
                newA = try_accept_pack(
                    tA, [x for x in packedA if x is not s] + [s2])
                if newA is None:
                    for x, a0, b0, c0, d0, oo in savedB2:
                        x.x0, x.y0, x.x1, x.y1 = a0, b0, c0, d0
                        x.orient = oo
                    continue
                used[j][1] = newB
                used[idx][1] = newA
                packedA = newA
                swapped += 1
                placed = True
                break
    used[:] = [u for u in used if u[1]]
    log(f"perturbation: {moved} relocated, {swapped} swapped from {k} trucks")
    return moved + swapped


def rebuild(used, rng, k, log):
    """Destroy-and-repair shake for saturated states where no relocate or
    swap exists: unload the k lightest trucks (sampled from the lightest
    third), reinsert their stacks in random order into any compatible
    truck - freed trucks first - ignoring gain (acceptance is left to the
    ILS best-tracking). Emptied trucks are dropped, so a successful repair
    may also remove trucks outright. Returns the number of reinserted
    stacks, or -1 when some stack found no feasible target (the caller
    restores the pre-rebuild snapshot)."""
    cands = [i for i, u in enumerate(used) if u[1]]
    if len(cands) < 2:
        return -1
    by_w = sorted(cands, key=lambda i: sum(s.weight for s in used[i][1]))
    light = by_w[:max(2, len(by_w) // 3)]
    rng.shuffle(light)
    victims = light[:k]
    vset = set(victims)
    pool = []
    for i in victims:
        pool.extend(used[i][1])
        used[i][1] = []
    others = [j for j in range(len(used)) if j not in vset and used[j][1]]
    rng.shuffle(pool)
    rng.shuffle(victims)
    rng.shuffle(others)
    order = victims + others
    moved = 0
    for s in pool:
        placed = False
        tried = 0
        for j in order:
            if tried >= 40:
                break
            tB = used[j][0]
            if not move_compat(s, tB, tB.arrival):
                continue
            tried += 1
            cur = used[j][1]
            newp = None
            if cur:
                newp = try_append(tB, cur, s)
            if newp is None:
                newp = try_accept_pack(tB, cur + [s])
            if newp is None:
                continue
            used[j][1] = newp
            moved += 1
            placed = True
            break
        if not placed:
            return -1
    used[:] = [u for u in used if u[1]]
    log(f"rebuild: {moved} stacks reinserted, {len(used)} trucks remain")
    return moved


APPEND_STATS = {}


def try_append(t, packed, s):
    """Append stack s as a new final column after an existing packing,
    without moving existing stacks. Validity: no X overlap (appended beyond
    max x1), P4 kept (s's loading orders must be >= all existing), P3 kept
    (leans on the stack attaining max x1, placed at its y0). Axle weights
    are rechecked. Returns new packed list or None."""
    rs = APPEND_STATS

    def rej(r):
        rs[r] = rs.get(r, 0) + 1
        return None

    if not packed:
        return rej("empty")
    if not move_compat(s, t, t.arrival):
        return rej("compat")
    tot = 0.0
    sup_o = t.sup_order.get(s.sup, 0)
    sd_o = t.sdock_order.get(s.sdock, 0)
    pd_o = t.pdock_order.get(s.pdock, 0)
    x0 = 0
    lean = None
    for o in packed:
        tot += o.weight
        if t.sup_order.get(o.sup, 0) > sup_o:
            return rej("p4_sup")
        if t.sdock_order.get(o.sdock, 0) > sd_o:
            return rej("p4_sdock")
        if t.pdock_order.get(o.pdock, 0) > pd_o:
            return rej("p4_pdock")
        if o.x1 > x0:
            x0 = o.x1
            lean = o
    if tot + s.weight > t.maxw + 0.1:
        return rej("weight")
    saved_coords = (s.x0, s.y0, s.x1, s.y1)
    saved_orient = s.orient
    if not choose_orientation(s, t):
        return rej("orient")
    if x0 + s.x_ext > t.L:
        s.orient = saved_orient
        return rej("xlen")
    y0 = lean.y0
    if y0 + s.y_ext > t.W:
        y0 = 0
        if s.y_ext > t.W:
            s.orient = saved_orient
            return rej("ylen")
        # lean on any stack with x1 == x0 overlapping [0, s.y_ext)
        ok = False
        for o in packed:
            if o.x1 == x0 and o.y0 < s.y_ext:
                ok = True
                break
        if not ok:
            s.orient = saved_orient
            return rej("lean")
    s.x0, s.y0, s.x1, s.y1 = x0, y0, x0 + s.x_ext, y0 + s.y_ext
    newp = packed + [s]
    if axle_bad_prefixes(t, newp):
        s.x0, s.y0, s.x1, s.y1 = saved_coords
        s.orient = saved_orient
        return rej("axle")
    rs["ok"] = rs.get("ok", 0) + 1
    return newp


def try_accept_pack(t, stacks_plus):
    """Pack stacks_plus into t; accept only if everything fits and axles ok.
    Returns packed list or None. Restores original coordinates on failure."""
    saved = [(s, s.x0, s.y0, s.x1, s.y1) for s in stacks_plus]
    packed, leftover = pack_truck(list(stacks_plus), t)
    if leftover or axle_bad_prefixes(t, packed):
        for s, x0, y0, x1, y1 in saved:
            s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
        return None
    return packed


def merge_pairs(used, params, log, deadline, max_attempts=3000):
    """Joint two-truck merge: pool all units of a lightly-filled truck A
    and a partner B, rebuild stacks (NFD/BFD per STACK_MODE) for the
    surviving truck and repack. Succeeds where per-stack elimination is
    stuck: recombined stacks can fill one truck even when A's individual
    stacks fit nowhere. The survivor is the later-arrival truck when it
    can serve all pooled units (later arrival also reduces inventory).
    Committed only when the cost gain is positive."""
    dayc = {}

    def day(a):
        d = dayc.get(a)
        if d is None:
            d = dayof(a)
            dayc[a] = d
        return d

    def truck_info(i):
        t, packed, ex, src = used[i]
        w = sum(s.weight for s in packed)
        max_ide = None
        min_idl = None
        sups = set()
        for s in packed:
            sups.add((s.sup, s.sdock, s.pdock))
            for u in s.units:
                if max_ide is None or u.ide > max_ide:
                    max_ide = u.ide
                if min_idl is None or u.idl < min_idl:
                    min_idl = u.idl
        return w, max_ide, min_idl, sups

    attempts = 0
    merged_total = 0
    pas = 0
    while time.time() < deadline:
        pas += 1
        order = sorted((i for i in range(len(used)) if used[i][1]),
                       key=lambda i: sum(s.weight for s in used[i][1]))
        infos = {}
        for i in order:
            infos[i] = truck_info(i)
        dropped = set()
        changed = False
        for ai in order:
            if attempts >= max_attempts or time.time() > deadline:
                break
            if ai in dropped:
                continue
            tA, packedA, exA, srcA = used[ai]
            wA, ideA, idlA, supsA = infos[ai]
            for bi in order:
                if bi == ai or bi in dropped:
                    continue
                tB, packedB, exB, srcB = used[bi]
                wB, ideB, idlB, supsB = infos[bi]
                # survivor candidates: prefer later arrival (less inventory)
                pair = ((bi, ai, tB, wB), (ai, bi, tA, wA)) \
                    if tB.arrival >= tA.arrival else \
                    ((ai, bi, tA, wA), (bi, ai, tB, wB))
                for si, di, tS, wS in pair:
                    if wA + wB > tS.maxw + 0.1:
                        continue
                    tD = used[di][0]
                    # feasibility prechecks for the dropped truck's units
                    ideD, idlD = (ideA, idlA) if di == ai else (ideB, idlB)
                    if idlD < tS.arrival or ideD > tS.arrival:
                        continue
                    ok = True
                    for (sup, sd, pd) in (supsA if di == ai else supsB):
                        if sup not in tS.sup_order \
                                or sd not in tS.sup_sdocks.get(sup, ()) \
                                or pd not in tS.pdock_order:
                            ok = False
                            break
                    if not ok:
                        continue
                    prods = set()
                    for s in used[di][1]:
                        for u in s.units:
                            if u.product not in tS.products:
                                ok = False
                                break
                            prods.add(u.product)
                        if not ok:
                            break
                    if not ok:
                        continue
                    attempts += 1
                    pool = [u for s in used[di][1] for u in s.units] + \
                        [u for s in used[si][1] for u in s.units]
                    new_stacks = build_stacks(pool, tS)
                    packed2, leftover = pack_truck(new_stacks, tS)
                    if leftover or axle_bad_prefixes(tS, packed2):
                        continue
                    cost_drop = (tD.cost * (1.0 + params["coeff_extra"])
                                 if used[di][2] else tD.cost)
                    dinv = 0.0
                    for s in used[di][1]:
                        for u in s.units:
                            dinv += u.icost * (day(tD.arrival)
                                               - day(tS.arrival)).days
                    gain = (params["coeff_trans"] * cost_drop
                            - params["coeff_inv"] * dinv)
                    if gain <= 0:
                        continue
                    used[si][1] = packed2
                    used[di][1] = []
                    dropped.add(di)
                    merged_total += 1
                    changed = True
                    break
                if ai in dropped:
                    break
        used = [u for u in used if u[1]]
        log(f"pair-merge pass {pas}: {merged_total} merged, "
            f"{len(used)} trucks remain, {attempts} attempts")
        if not changed:
            break
    return used


def move_compat(s, tB, arr):
    """Stack-level compatibility of s with truck tB at its arrival."""
    if s.height > tB.H:
        return False
    if s.sup not in tB.sup_order:
        return False
    if s.sdock not in tB.sup_sdocks.get(s.sup, ()):
        return False
    if s.pdock not in tB.pdock_order:
        return False
    maxIDE = None
    minIDL = None
    for u in s.units:
        if u.product not in tB.products:
            return False
        if maxIDE is None or u.ide > maxIDE:
            maxIDE = u.ide
        if minIDL is None or u.idl < minIDL:
            minIDL = u.idl
    if not (maxIDE <= arr <= minIDL):
        return False
    area = (s.L * s.W) / 1e6
    if s.weight > (tB.maxdens + 1) * area - 0.01 + 0.1:
        return False
    above = s.weight - s.units[0].weight
    tmm = tB.tmm.get(s.units[0].product)
    if tmm is not None and above > tmm + 0.1:
        return False
    return True


def inv_relax(used, params, log, deadline, max_passes=2, max_targets=25):
    """Inventory optimization: move stacks to LATER used trucks when the
    inventory gain is positive. Source truck is repacked without the stack;
    target receives it by append or full repack. Strictly improving moves
    only. Empty trucks are dropped afterwards."""
    dayc = {}

    def day(a):
        d = dayc.get(a)
        if d is None:
            d = dayof(a)
            dayc[a] = d
        return d

    for pas in range(max_passes):
        if time.time() > deadline:
            break
        by_sup = defaultdict(list)
        for j, u in enumerate(used):
            if not u[1]:
                continue
            for sup in u[0].sup_order:
                by_sup[sup].append(j)
        improved = False
        n_moves = 0
        for i in range(len(used)):
            if time.time() > deadline:
                break
            if i >= len(used):
                break
            tA, packedA, exA, srcA = used[i]
            if not packedA:
                continue
            for s in list(packedA):
                ic = sum(u.icost for u in s.units)
                if ic <= 0:
                    continue
                cands = []
                for j in by_sup.get(s.sup, ()):
                    if j == i or not used[j][1]:
                        continue
                    tB = used[j][0]
                    if tB.arrival <= tA.arrival:
                        continue
                    if not move_compat(s, tB, tB.arrival):
                        continue
                    g = ic * (day(tB.arrival) - day(tA.arrival)).days
                    if g > 0:
                        cands.append((g, j))
                if not cands:
                    continue
                cands.sort(reverse=True)
                tried = 0
                moved = False
                for g, j in cands:
                    if tried >= max_targets:
                        break
                    tried += 1
                    tB = used[j][0]
                    packedB = used[j][1]
                    savedB = [(x, x.x0, x.y0, x.x1, x.y1) for x in packedB]
                    saved_s = (s.x0, s.y0, s.x1, s.y1)
                    newB = try_append(tB, packedB, s)
                    if newB is None:
                        newB = try_accept_pack(tB, packedB + [s])
                    if newB is None:
                        continue
                    savedA = [(x, x.x0, x.y0, x.x1, x.y1) for x in packedA]
                    newA = try_accept_pack(tA, [x for x in packedA if x is not s])
                    if newA is None:
                        for x, a0, b0, c0, d0 in savedB:
                            x.x0, x.y0, x.x1, x.y1 = a0, b0, c0, d0
                        s.x0, s.y0, s.x1, s.y1 = saved_s
                        continue
                    used[j][1] = newB
                    used[i][1] = newA
                    packedA = newA
                    improved = True
                    moved = True
                    n_moves += 1
                    break
        used = [u for u in used if u[1]]
        log(f"inv_relax pass {pas + 1}: {n_moves} stacks moved later")
        if not improved:
            break
    return used


def eliminate_trucks(used, all_trucks, params, log, deadline, max_passes=6,
                     max_targets=60):
    """Move all stacks of the least valuable trucks (extras first, then by
    fill ascending) into other trucks - used ones first, then unused planned
    trucks. A source truck is dropped only when the whole move has positive
    cost gain (truck cost saved minus extra inventory incurred)."""
    dayc = {}

    def day(a):
        d = dayc.get(a)
        if d is None:
            d = dayof(a)
            dayc[a] = d
        return d

    used_tids = set(u[0].tid for u in used)
    unused = [t for t in all_trucks if t.tid not in used_tids]
    MAX_TARGETS = max_targets
    from collections import Counter
    failstat = Counter()

    for pas in range(max_passes):
        if time.time() > deadline:
            log(f"elimination stopped at pass {pas + 1} (time budget)")
            break
        changed = False
        by_sup_used = defaultdict(list)
        for j, u in enumerate(used):
            if not u[1]:
                continue
            for sup in u[0].sup_order:
                by_sup_used[sup].append(j)
        by_sup_unused = defaultdict(list)
        for k, t in enumerate(unused):
            for sup in t.sup_order:
                by_sup_unused[sup].append(k)
        order = sorted(range(len(used)),
                       key=lambda i: (not used[i][2],
                                      sum(s.weight for s in used[i][1])))
        dropped = set()
        opened = set()
        for idx in order:
            if idx in dropped or idx >= len(used):
                continue
            tA, packedA, exA, srcA = used[idx]
            if not packedA:
                continue
            snap = {}

            def touch(stacks):
                for s in stacks:
                    k = id(s)
                    if k not in snap:
                        snap[k] = (s, s.x0, s.y0, s.x1, s.y1)

            sim_used = {}    # j -> new packed list (used trucks)
            sim_open = {}    # k -> [truck, packed list] (unused planned)
            ok_all = True
            moved_inv = 0.0  # sum icost*(day(dst)-day(src)) over moved units
            for s in list(packedA):
                placed = False
                cands = []
                for j in by_sup_used.get(s.sup, ()):
                    if j == idx or j in dropped or not used[j][1]:
                        continue
                    tB = used[j][0]
                    if move_compat(s, tB, tB.arrival):
                        cands.append((0, tB.arrival, j, None))
                for k in by_sup_unused.get(s.sup, ()):
                    if k in opened:
                        continue
                    tB = unused[k]
                    if move_compat(s, tB, tB.arrival):
                        cands.append((1, tB.arrival, None, k))
                if not cands:
                    failstat["no_compat"] += 1
                cands.sort(key=lambda c: c[1], reverse=True)
                cands.sort(key=lambda c: c[0])
                tried = 0
                for c0, arrB, j, k in cands:
                    if tried >= MAX_TARGETS:
                        break
                    tried += 1
                    if j is not None:
                        tB = used[j][0]
                        cur = sim_used.get(j, used[j][1])
                        touch([s])
                        newp = try_append(tB, cur, s)
                        if newp is None:
                            failstat["append_fail"] += 1
                            touch(list(cur) + [s])
                            newp = try_accept_pack(tB, cur + [s])
                            if newp is None:
                                failstat["repack_fail"] += 1
                        if newp is None:
                            continue
                        sim_used[j] = newp
                        dst_arr = tB.arrival
                    else:
                        tB = unused[k]
                        cur = sim_open.get(k, [tB, []])[1]
                        touch(list(cur) + [s])
                        newp = try_accept_pack(tB, cur + [s])
                        if newp is None:
                            continue
                        sim_open[k] = [tB, newp]
                        dst_arr = tB.arrival
                    placed = True
                    dd = (day(dst_arr) - day(tA.arrival)).days
                    for u in s.units:
                        moved_inv += u.icost * dd
                    break
                if not placed:
                    failstat["pack_fail" if cands else "no_compat2"] += 1
                    ok_all = False
                    break
            if ok_all:
                src_cost = tA.cost * (1.0 + params["coeff_extra"]) if exA else tA.cost
                open_cost = sum(unused[k].cost for k in sim_open)
                gain = (params["coeff_trans"] * (src_cost - open_cost)
                        + params["coeff_inv"] * moved_inv)
                if gain > 0:
                    for j, newp in sim_used.items():
                        used[j][1] = newp
                    for k in sorted(sim_open.keys(), reverse=True):
                        tB, plist = sim_open[k]
                        used.append([tB, plist, False, None])
                        opened.add(k)
                    used[idx][1] = []
                    dropped.add(idx)
                    changed = True
                    continue
                else:
                    failstat["gain_nonpos"] += 1
            # rollback coordinates
            for s, x0, y0, x1, y1 in snap.values():
                s.x0, s.y0, s.x1, s.y1 = x0, y0, x1, y1
        used = [u for u in used if u[1]]
        if opened:
            unused = [t for k, t in enumerate(unused) if k not in opened]
        log(f"elimination pass {pas + 1}: dropped {len(dropped)} trucks, {len(used)} remain; fails {dict(failstat)}; append {APPEND_STATS}; axle {AXLE_STATS}")
        failstat.clear()
        APPEND_STATS.clear()
        AXLE_STATS.clear()
        if not changed:
            break
    return used


# ---------------------------------------------------------------- output
def stack_code(i):
    s = ""
    i += 1
    while i > 0:
        i, r = divmod(i - 1, 26)
        s = chr(65 + r) + s
    return s


def f3(v):
    return f"{v:.3f}".replace(".", ",")


def write_outputs(used, params, outdir):
    os.makedirs(outdir, exist_ok=True)
    truck_rows = []
    stack_rows = []
    item_rows = []
    for t, packed, is_extra, src in used:
        tot_w = sum(s.weight for s in packed)
        vol = 0.0
        for s in packed:
            first = True
            for u in s.units:
                h = u.H if first else (u.H - u.nest)
                vol += (u.L * u.W * h) / 1e9
                first = False
        loaded_len = max((s.x1 for s in packed), default=0)
        cg = (sum((s.x0 + (s.x1 - s.x0) / 2.0) * s.weight for s in packed) / tot_w) if tot_w > 0 else 0.0
        ejr = t.ejeh + t.ejhr - cg
        emh = (tot_w * ejr + t.em * t.ejcr) / t.ejhr
        emr = tot_w + t.em - emh
        emm = (t.cm * t.cjfc + emh * t.cjfh) / t.cjfm
        truck_rows.append(";".join([t.tid, str(loaded_len), f3(tot_w), f3(vol), f3(emm), f3(emr)]))
        for si, s in enumerate(packed):
            sid = f"{t.tid}_{si + 1}"
            sc = stack_code(si)
            stack_rows.append(";".join([t.tid, sid, sc, str(s.x0), str(s.y0), "0",
                                        str(s.x1), str(s.y1), str(s.height)]))
            z = 0
            for ii, u in enumerate(s.units):
                z0 = z
                ze = z0 + u.H if ii == 0 else z0 + u.H - u.nest
                z = ze
                item_rows.append(";".join([u.line_id, t.tid, sid, f"{sc}{ii + 1}",
                                           str(s.x0), str(s.y0), str(z0),
                                           str(s.x1), str(s.y1), str(ze)]))
    with open(os.path.join(outdir, "output_trucks.csv"), "w", encoding="latin1") as f:
        f.write("Id truck;Loaded length;Weight of loaded items;Volume of loaded items;EMm;EMr\n")
        f.write("\n".join(truck_rows) + ("\n" if truck_rows else ""))
    with open(os.path.join(outdir, "output_stacks.csv"), "w", encoding="latin1") as f:
        f.write("Id truck;Id stack;Stack code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n")
        f.write("\n".join(stack_rows) + ("\n" if stack_rows else ""))
    with open(os.path.join(outdir, "output_items.csv"), "w", encoding="latin1") as f:
        f.write("Item ident;Id truck;Id stack;Item code;X origin;Y origin;Z origin;X extremity;Y extremity;Z extremity\n")
        f.write("\n".join(item_rows) + ("\n" if item_rows else ""))


def overlaps_check(used):
    """Detect P2 violations (same-truck stack rectangle overlap)."""
    bad = 0
    for t, packed, _, _ in used:
        n = len(packed)
        for i in range(n):
            a = packed[i]
            for j in range(i + 1, n):
                b = packed[j]
                if (a.x0 < b.x1 and b.x0 < a.x1
                        and a.y0 < b.y1 and b.y0 < a.y1):
                    bad += 1
    return bad


def objective(used, params):
    trans = 0.0
    for t, packed, is_extra, src in used:
        c = t.cost * (1.0 + params["coeff_extra"]) if is_extra else t.cost
        trans += c
    inv = 0.0
    for t, packed, is_extra, src in used:
        da = dayof(t.arrival)
        for s in packed:
            for u in s.units:
                inv += u.icost * max(0, (dayof(u.idl) - da).days)
    return params["coeff_trans"] * trans + params["coeff_inv"] * inv, trans, inv


# ---------------------------------------------------------------- main
def main():
    indir, outdir = sys.argv[1], sys.argv[2]
    internal = float(sys.argv[3]) if len(sys.argv) > 3 else 3600
    seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1111
    t0 = time.time()

    def log(msg):
        print(f"[{time.time() - t0:8.1f}] {msg}", flush=True)

    elim_deadline = t0 + max(20.0, internal * 0.5)
    hard_deadline = t0 + max(30.0, internal - 25.0)
    params, units, trucks, trows = read_instance(indir)
    log(f"parsed: {len(units)} units, {len(trucks)} trucks")
    used, alive = solve(params, units, trucks, trows, seed, log,
                        t0, elim_deadline, hard_deadline)
    obj, trans, inv = objective(used, params)
    npack = sum(len(s.units) for _, pk, _, _ in used for s in pk)
    log(f"used trucks: {len(used)} ({sum(1 for x in used if x[2])} extra), "
        f"packed units: {npack}/{len(units)}")
    log(f"objective ~ {obj:.0f} (trans {trans:.0f} + inv {inv:.0f})")
    nbad = overlaps_check(used)
    if nbad:
        log(f"ERROR: {nbad} overlapping stack pairs detected before output")
    write_outputs(used, params, outdir)
    log(f"outputs written to {outdir}")


if __name__ == "__main__":
    main()
