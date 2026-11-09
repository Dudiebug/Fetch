#!/usr/bin/env python3
"""Build a .hsmclone from a player's .hsmrec recordings: the data a style clone (Fetch.ini clone=<name>) plays from.

Each row is what the player saw (shared/clone/hsm_clone.h Features) and the input stream that followed it. The clone
looks up the row closest to its own fight and plays those inputs, so what it does is the player's own recorded input.
The file holds one person's play: keep it out of the repository.
  hsmclone.py build NAME OUT.hsmclone RECORDING_OR_DIR...
  hsmclone.py info FILE
"""
import array, glob, math, multiprocessing, os, re, struct, sys, zlib
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hsmrec

MAGIC = b"HSMCLONE"
VERSION = 3
FEATURES = 127
PLAY, NAME, KIT, EVENTS, MORE = 1, 2, 3, 4, 5
# shared/clone/hsm_clone_events.h Ev: one 16-byte FightEvent each (u32 sample, u8 kind, who, hand, part, f32 a, b). An Events
# block has the kinds up to Death; the More block after it (Fetch pre11 on) the kinds added since.
EV_NAMES = ["?", "Hit", "Clash", "Damage", "Sever", "ParryOn", "ParryOff", "Thrust", "Kick", "GrabOn", "GrabOff",
            "GrabbedOn", "GrabbedOff", "PickUp", "Drop", "StuckOn", "StuckOff", "Fall", "GetUp", "Death",
            "Strike", "Armour", "Stage"]
EV_LAST, EV_FIRST_MORE = 19, 20
STRIKE, STAGE = 20, 22
STRIKE_HOW = ["bare", "gap", "beat", "held"]   # hsm_clone_armor.h StrikeClass
EVENT = struct.Struct("<I4B2f")
STEP = 3            # one row every third sample (~18 a second); the stream keeps every sample
MAX_RANGE = 1500.0  # uu: further than this nobody is fighting
PREV_MS = 150.0
MAX_DT = 100.0
TALK, KEYS, RUN = 1 << 16, 12, 9
GIVE_UP = 1 << 7                                         # VitalsMsg.state: the fight is over for this fighter
PLAYABLE = (1 << 19) - 1 & ~(1 << 11 | TALK | 1 << 18)   # not the camera lock, Talk or the middle button
SAMPLE = 60
PELVIS, HEAD, HAND_L, HAND_R, FOOT_L, FOOT_R = 0, 8, 12, 16, 19, 22
UPPER_ARM_L, UPPER_ARM_R = 10, 14
PARTS = 7
FOE_HIT, MY_HIT = 113, 120
OFFHAND, ALT_GRIP, MORDHAU = 1 << 10, 1 << 11, 1 << 12    # FighterFrame grip (sync/hsm_pose_codec.h)
HEALTHY = {"health": 100, "consciousness": 100, "stamina": 100, "bleeding": 0, "parts": [100] * PARTS, "sever": 0, "disloc": 0, "state": 0, "flags": 0}


def class_hash(name):
    """sync::WeaponClassHash: FNV-1a of the name in lower case."""
    h = 2166136261
    for c in name.lower().encode():
        h = (h ^ c) * 16777619 & 0xffffffff
    return h


def weapon_types():
    """{class hash: the game's weapon type}, from the list the mod uses (shared/clone/hsm_weapon_types.h)."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "shared", "clone", "hsm_weapon_types.h")
    text = open(path, encoding="utf-8").read()
    order = [t.strip() for t in re.search(r"enum class WeaponType : uint8_t \{(.*?)\}", text).group(1).replace(" = 0", "").split(",")]
    return {class_hash(n): order.index(t) for n, t in re.findall(r'\{"(\w+)", WeaponType::(\w+)\}', text)}


TYPES = weapon_types()


def type_of(w):
    """The game's type of a held weapon, or 0: a forged one's parts were never recorded."""
    return float(TYPES.get(w.get("cls", 0), 0)) if w else 0.0


def lost(before, now):
    """What each body part lost since the row before: shared/clone/hsm_clone.h Hurt."""
    return [max(0.0, a - b) / 100 for a, b in zip(before, now)] if before else [0.0] * PARTS


def tip_of(f):
    """-> (tip, velocity, armed): the first held weapon's tip, or the right hand."""
    for w in f["weapons"]:
        if w:
            return w["tip"], w["vel"], 1.0
    b = f["bones"][HAND_R]
    return b["pos"], b.get("vel", (0, 0, 0)), 0.0


def height(f):
    return (f["bones"][PELVIS]["pos"][2] - min(f["bones"][FOOT_L]["pos"][2], f["bones"][FOOT_R]["pos"][2])) / 100


def reach(w):
    return min(math.dist(w["tip"], w["base"]) / 100, 3) if w else 0.0


def share(v, full):
    return min(max(v / full, 0), 2)


def forward(f):
    """Which way the chest points, from the line of the shoulders: a flat unit vector."""
    l, r = f["bones"][UPPER_ARM_L]["pos"], f["bones"][UPPER_ARM_R]["pos"]
    x, y = r[1] - l[1], l[0] - r[0]
    d = math.hypot(x, y)
    return (x / d, y / d) if d >= 1e-6 else (1.0, 0.0)


def features(me, mv, foe, fv, loose, yaw, pitch, prev_turn, prev_look, buttons, guard_r, guard_l):
    """hsm::clone::Features; tests/test_hsmclone.py and test_hsm_clone.cpp pin one case on both sides. Armor is not in a recording."""
    c, s = math.cos(math.radians(yaw)), math.sin(math.radians(yaw))
    px, py, pz = me["bones"][PELVIS]["pos"]

    def at(p):
        return [((p[0] - px) * c + (p[1] - py) * s) / 100, (-(p[0] - px) * s + (p[1] - py) * c) / 100, (p[2] - pz) / 100]

    def vel(v):
        return [(v[0] * c + v[1] * s) / 400, (-v[0] * s + v[1] * c) / 400, v[2] / 400]

    tip, tip_v, armed = tip_of(me)
    foe_tip, foe_tip_v, foe_armed = tip_of(foe)
    fp = at(foe["bones"][PELVIS]["pos"])
    d = math.hypot(fp[0], fp[1]) + 1e-3
    near = min(d, 3) + 0.25 * max(0, min(d - 3, 8))
    p = pitch - 360 if pitch > 180 else pitch
    if loose:
        lx, ly, lz = at(loose)
        ld = math.hypot(lx, ly) + 1e-3
        seen = min(ld, 3) + 0.25 * max(0, min(ld - 3, 8))
        lying = [lx / ld * seen, ly / ld * seen, min(max(lz, -5), 5), 1.0]
    else:
        lying = [0.0] * 4

    def wounds(v):
        return [share(v["health"], 100)] + [share(x, 100) for x in v["parts"]] + [share(v["bleeding"], 100)]

    def hurt(v):
        return [bin(v["sever"]).count("1") / 4, bin(v["disloc"]).count("1") / 4]

    def flat(v):
        return [v[0] * c + v[1] * s, -v[0] * s + v[1] * c]

    ff, mf = forward(foe), forward(me)
    q = foe["bones"][PELVIS]["pos"]
    tx, ty = px - q[0], py - q[1]
    extra = ([reach(me["weapons"][0]), reach(me["weapons"][1]), reach(foe["weapons"][0]), reach(foe["weapons"][1])]
             + [float(bool(me.get("grip", 0) & OFFHAND)), float(bool(me.get("grip", 0) & ALT_GRIP)), float(bool(me.get("grip", 0) & MORDHAU)),
                float(bool(foe.get("grip", 0) & OFFHAND))] + lying
             + wounds(fv) + [share(fv["stamina"], 100)] + hurt(fv) + [float(fv["state"] >> b & 1) for b in range(8)]
             + [float(fv["flags"] >> 1 & 1), float(fv["flags"] >> 2 & 1)]
             + flat(ff) + [(ff[0] * tx + ff[1] * ty) / (math.hypot(tx, ty) + 1e-3)]
             + wounds(mv) + hurt(mv) + [float(mv["flags"] >> 1 & 1), float(mv["state"] & 1)] + flat(mf) + [0.0] * (2 * PARTS + 1)
             + [type_of(me["weapons"][0]), type_of(me["weapons"][1]), type_of(foe["weapons"][0]), type_of(foe["weapons"][1])] + [0.0] * (2 * PARTS))
    return (at(tip) + vel(tip_v) + at(me["bones"][HAND_L]["pos"]) + vel(me["bones"][PELVIS].get("vel", (0, 0, 0)))[:2]
            + [height(me), p / 60, armed, 1.0 if me["weapons"][1] else 0.0, mv["stamina"] / 100, mv["consciousness"] / 100,
               fp[0] / d * near, fp[1] / d * near] + at(foe["bones"][HEAD]["pos"]) + vel(foe["bones"][PELVIS].get("vel", (0, 0, 0)))[:2]
            + at(foe_tip) + vel(foe_tip_v) + [foe_armed, height(foe), fv["consciousness"] / 100, math.tanh(prev_turn / 60), math.tanh(prev_look / 30)]
            + [float(buttons >> (KEYS + k) & 1) for k in range(4)] + [guard_r, guard_l, float(buttons >> RUN & 1)] + extra)


def wrap(deg):
    return (deg + 180) % 360 - 180


def one(path):
    """-> (feature floats, row sample indices, sample bytes, sample count), indices local to this file."""
    _, chunks, _ = hsmrec.load(path)
    flags, frame, vitals, dead = {}, {}, {}, set()
    weapons, lying, held = set(), {}, set()   # the props that are weapons; where those not in a hand last lay
    feats, index, samples = array.array("f"), array.array("I"), bytearray()
    count, last, history, step = 0, None, [], 0
    foe_parts, my_parts, foe_was = None, None, None

    def end_segment():
        nonlocal last, history, foe_parts, my_parts
        if count and last is not None:
            samples[-4:] = struct.pack("<I", 1)
        last, history, foe_parts, my_parts = None, [], None, None

    for kind, r in chunks:
        if r is None:
            continue
        if kind == 1:
            flags[r["id"]] = r["flags"]
        elif kind == 2:
            if len(r["bones"]) >= 23:
                frame[r["id"]] = r
        elif kind == 3:
            vitals[r["id"]] = r
            if r["health"] <= 0:
                dead.add(r["id"])
        elif kind == 4:
            if "Weapon" in r["cls"]:
                weapons.add(r["id"])
        elif kind == 5:
            if r["id"] in weapons and r["id"] not in held and r["body"] == 0:
                lying[r["id"]] = r["pos"]
        elif kind == 6:
            event = hsmrec.EVENTS[r["kind"]]
            if event == "Death":
                dead.add(r["subject"])
            elif event == "PickUp":
                held.add(r["prop"])
                lying.pop(r["prop"], None)
            elif event == "Drop":
                held.discard(r["prop"])
            elif event in ("LevelStart", "LevelEnd"):
                lying.clear()
                held.clear()
        elif kind == 7:
            me = frame.get(r["id"])
            foe, best = None, MAX_RANGE ** 2
            if (me is not None and r["ts"] - me["ts"] <= 100 and r["id"] not in dead and not r["buttons"] & TALK
                    and not vitals.get(r["id"], {}).get("state", 0) & GIVE_UP):
                mp = me["bones"][PELVIS]["pos"]
                for i, f in frame.items():
                    if i == r["id"] or i in dead or flags.get(i, 0) & 1 or r["ts"] - f["ts"] > 200:
                        continue
                    q = f["bones"][PELVIS]["pos"]
                    d = (q[0] - mp[0]) ** 2 + (q[1] - mp[1]) ** 2 + (q[2] - mp[2]) ** 2
                    if d < best:
                        foe, best = i, d
            if foe is None or (last and last["id"] != r["id"]):
                end_segment()
                if foe is None:
                    continue
            a = r["axes"] + [0.0] * (10 - len(r["axes"]))
            dt = min(r["ts"] - last["ts"], MAX_DT) if last else 16.0
            dyaw, dpitch = (wrap(r["yaw"] - last["yaw"]), wrap(r["pitch"] - last["pitch"])) if last else (0.0, 0.0)
            samples += struct.pack("<13f2I", dt, dyaw, dpitch, *a[:10], r["buttons"] & PLAYABLE, 0)
            count += 1
            last = r
            history = [h for h in history if r["ts"] - h[0] <= PREV_MS] + [(r["ts"], a[2], a[3])]
            step += 1
            if step % STEP == 0:
                mp = me["bones"][PELVIS]["pos"]
                loose = min(lying.values(), key=lambda p: math.dist(p, mp), default=None)
                mv, fv = vitals.get(r["id"], HEALTHY), vitals.get(foe, HEALTHY)
                row = features(me, mv, frame[foe], fv, loose, r["yaw"], r["pitch"],
                               sum(h[1] for h in history), sum(h[2] for h in history), r["buttons"], a[8], a[9])
                if foe != foe_was:
                    foe_was, foe_parts = foe, None
                row[FOE_HIT:FOE_HIT + PARTS], row[MY_HIT:MY_HIT + PARTS] = lost(foe_parts, fv["parts"]), lost(my_parts, mv["parts"])
                foe_parts, my_parts = fv["parts"], mv["parts"]
                feats.extend(row)
                index.append(count)   # the row's clip starts with the next sample
    end_segment()
    return feats.tobytes(), index.tobytes(), bytes(samples), count


def block(kind, payload):
    return struct.pack("<BI", kind, len(payload)) + payload + struct.pack("<I", zlib.crc32(payload))


def header():
    return MAGIC + struct.pack("<2I", VERSION, FEATURES)


def play_block(feats, index, stream, count):
    """-> a Play block, or None for a recording with no fighting in it. A row at the very end has no clip."""
    idx = array.array("I")
    idx.frombytes(index)
    rows = bytearray()
    for k, at in enumerate(idx):
        if at < count:
            rows += feats[k * FEATURES * 4:(k + 1) * FEATURES * 4] + struct.pack("<I", at)
    n = len(rows) // (FEATURES * 4 + 4)
    return block(PLAY, struct.pack("<2I", n, count) + rows + stream) if n else None


def build(name, out, inputs):
    paths = []
    for i in inputs:
        paths += sorted(glob.glob(os.path.join(i, "HSMRec-*.hsmrec"))) if os.path.isdir(i) else [i]
    with open(out, "wb") as f:
        f.write(header() + block(NAME, name.encode()))
        with multiprocessing.Pool(min(5, os.cpu_count() or 1)) as pool:
            for got in pool.imap(one, paths):
                b = play_block(*got)
                if b:
                    f.write(b)
    info(out)


def read_blocks(data):
    if data[:16] != header():
        raise ValueError("not a clone file of this version")
    p = 16
    while len(data) - p >= 9:
        kind, n = struct.unpack_from("<BI", data, p)
        if len(data) - p - 9 < n or struct.unpack_from("<I", data, p + 5 + n)[0] != zlib.crc32(data[p + 5:p + 5 + n]):
            break
        yield kind, data[p + 5:p + 5 + n]
        p += n + 9


def events_of(d, samples, more=False):
    """An Events (or, more: a More) block's events as (sample, kind, who, hand, part, a, b), or None when the Fetch that reads
    it would refuse it."""
    if len(d) < 4:
        return None
    n = struct.unpack_from("<I", d)[0]
    if len(d) != 4 + n * EVENT.size:
        return None
    got, last = [], 0
    for e in EVENT.iter_unpack(d[4:]):
        sample, kind, who, hand, _, a, b = e
        known = EV_FIRST_MORE <= kind < len(EV_NAMES) if more else 1 <= kind <= EV_LAST
        if not known or who > 1 or sample > samples or sample < last or (hand != 0xFF and hand > 2):
            return None
        if not (math.isfinite(a) and math.isfinite(b)) or abs(a) > 1e9 or abs(b) > 1e9:
            return None
        got.append(e)
        last = sample
    return got


def info(path):
    with open(path, "rb") as f:
        data = f.read()
    name, rows, samples, ms, kit = "?", 0, 0, 0.0, 0
    counts, refused, last_play, more_play = {}, 0, None, None
    strikes, stages = {}, 0
    for kind, d in read_blocks(data):
        if kind in (EVENTS, MORE):
            samples_of = last_play if kind == EVENTS else more_play
            got = events_of(d, samples_of, kind == MORE) if samples_of is not None else None
            if got is None:
                refused += 1
            for e in got or []:
                counts[e[1]] = counts.get(e[1], 0) + 1
                if e[1] == STRIKE and e[2] == 0:
                    how = STRIKE_HOW[int(e[6]) >> 8 & 3 if math.isfinite(e[6]) and 0 < e[6] < 16384 else 0]
                    strikes[how] = strikes.get(how, 0) + 1
                elif e[1] == STAGE and e[4] != 0xFF:
                    stages += 1
            more_play = last_play if kind == EVENTS and got is not None else None
            last_play = None
            continue
        last_play, more_play = None, None
        if kind == NAME:
            name = d.decode("utf-8", "replace")
        elif kind == KIT:
            kit = len(d)
        elif kind == PLAY:
            r, s = struct.unpack_from("<2I", d)
            rows, samples = rows + r, samples + s
            at = 8 + r * (FEATURES * 4 + 4)
            ms += sum(x[0] for x in struct.iter_unpack(f"<f{SAMPLE - 4}x", d[at:]))
            last_play = s
    print(f"{path}: {name}, {ms / 3600000:.2f} h of play, {rows} rows, {samples} samples, {len(data) / 1e6:.1f} MB, kit {kit} bytes")
    if counts or refused:
        listed = ", ".join(f"{EV_NAMES[k]} {counts[k]}" for k in sorted(counts))
        print(f"  events: {sum(counts.values())} ({listed or 'none'}){f', {refused} block(s) refused' if refused else ''}")
    if strikes:
        print("  your strikes: " + ", ".join(f"{h} {strikes[h]}" for h in STRIKE_HOW if h in strikes))
    if stages:
        print(f"  training stages: {stages}")
    return counts


def main(argv):
    if len(argv) >= 4 and argv[0] == "build":
        build(argv[1], argv[2], argv[3:])
        return 0
    if len(argv) == 2 and argv[0] == "info":
        info(argv[1])
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
