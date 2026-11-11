#!/usr/bin/env python3
"""Read an HSMMod recording (.hsmrec, written with [mod] record=1): fighters, props and events at 60 Hz.

The format is HSMMod's shared/record/hsm_record.h (github.com/Dudiebug/halfsword-multiplayer-mod). A short or bad last chunk is a crash's
cut-off tail: it is reported and the rest still reads. Unknown chunk kinds are skipped. Stdlib only.
  hsmrec.py summary FILE                         header, fighters (bones, bodies, rate), props, event counts
  hsmrec.py events FILE [--kind K] [--fighter N] one line per event, seconds since the recording started
  hsmrec.py csv FILE --fighter N [--bone pelvis]  one bone's position, velocity and rotation per sample
  hsmrec.py csv FILE --prop ID                    one prop's bodies per sample
  hsmrec.py csv FILE --input                      the local player's axes, held buttons and camera per sample
  hsmrec.py counts FILE                          chunks and events by kind (tools/check.sh compares the C++ test's)
  hsmrec.py --self-test
Exit codes: 0 done, 2 unreadable file (not a recording, or a corrupt chunk before the end).
"""
import argparse, collections, io, struct, sys, zlib

MAGIC = b"HSMREC"
VERSION = 1
CHUNKS = {1: "FighterTable", 2: "FighterFrame", 3: "Vitals", 4: "PropTable", 5: "PropFrame", 6: "Event", 7: "Input"}
EVENTS = ["?", "GrabOn", "GrabOff", "BeingGrabbedOn", "BeingGrabbedOff", "GripChange", "PickUp", "Drop", "Draw", "Sheathe",
          "Thrust", "Kick", "ParryOn", "ParryOff", "Hit", "Damage", "Sever", "StuckOn", "StuckOff", "Fall", "GetUp", "Death",
          "Input", "LevelStart", "LevelEnd", "Marker", "Break"]
FIGHTER_FLAGS = {1: "local", 2: "bot", 4: "stand-in"}
# hsm::Axis and hsm::Action order (shared/protocol/hsm_protocol.h).
AXES = ["move_fwd", "move_right", "turn_mouse", "look_mouse", "turn_pad", "look_pad", "arm_r", "arm_l", "guard_r", "guard_l"]
ACTIONS = ["GrabLeft", "GrabRight", "GuardLeft", "GuardRight", "Thrust", "ThrustGamepad", "Jump", "CrouchKey", "CrouchHold",
           "Run", "SwapHands", "ToggleCameraLock", "KeyForward", "KeyBack", "KeyLeft", "KeyRight", "Talk", "RightMouse",
           "MiddleMouse", "ChainAttack"]
NO_HAND = 0xFF
MAX_CHUNK = 1 << 20


class BadFile(Exception):
    pass


class R:
    """Little-endian cursor; raises BadFile past the end."""
    def __init__(self, b):
        self.b, self.p = b, 0

    def take(self, fmt):
        n = struct.calcsize(fmt)
        if self.p + n > len(self.b):
            raise BadFile("short payload")
        v = struct.unpack_from("<" + fmt, self.b, self.p)
        self.p += n
        return v if len(v) > 1 else v[0]

    def s(self):
        n = self.take("B")
        if self.p + n > len(self.b):
            raise BadFile("short string")
        v = self.b[self.p:self.p + n].decode("utf-8", "replace")
        self.p += n
        return v

    def f3(self):
        return self.take("3f")

    def q(self):
        return tuple(v / 32767.0 for v in self.take("4h"))

    def end(self):
        if self.p != len(self.b):
            raise BadFile("trailing bytes")


def dec_fighter_table(r):
    t = {"id": r.take("H"), "flags": r.take("B"), "name": r.s(), "cls": r.s(), "bones": []}
    for _ in range(r.take("H")):
        t["bones"].append({"name": r.s(), "parent": r.take("H"), "body": r.take("B") == 1})
    return t


def dec_weapon(r):
    if r.take("B") == 0:
        return None
    return {"cls": r.take("I"), "pos": r.f3(), "rot": r.q(), "vel": r.f3(), "ang": r.f3(), "base": r.f3(), "tip": r.f3()}


def dec_fighter_frame(r):
    f = {"ts": r.take("d"), "dt": r.take("f"), "id": r.take("H"), "bones": []}
    for _ in range(r.take("H")):
        b = {"pos": r.f3(), "rot": r.q(), "body": r.take("B") == 1}
        if b["body"]:
            b["vel"], b["ang"] = r.f3(), r.f3()
        f["bones"].append(b)
    f["weapons"] = [dec_weapon(r), dec_weapon(r)]
    f["grip"] = r.take("H")
    return f


def dec_vitals(r):
    v = {"ts": r.take("d"), "id": r.take("H")}
    v["health"], v["consciousness"], v["stamina"], v["bleeding"] = r.take("4f")
    v["parts"] = [r.take("f") for _ in range(r.take("B"))]
    v["flags"], v["state"], v["sever"], v["disloc"] = r.take("B"), r.take("I"), r.take("I"), r.take("I")
    return v


def dec_prop_table(r):
    t = {"id": r.take("Q"), "name": r.s(), "cls": r.s()}
    t["bodies"] = [r.s() for _ in range(r.take("B"))]
    return t


def dec_prop_frame(r):
    return {"ts": r.take("d"), "id": r.take("Q"), "body": r.take("B"), "flags": r.take("B"), "pos": r.f3(), "rot": r.q(),
            "vel": r.f3(), "ang": r.f3()}


def dec_event(r):
    e = {"ts": r.take("d"), "kind": r.take("B"), "subject": r.take("H"), "other": r.take("H"), "prop": r.take("Q"),
         "hand": r.take("B"), "text": r.s(), "x": r.take("4f")}
    if not 1 <= e["kind"] < len(EVENTS):
        raise BadFile(f"event kind {e['kind']}")
    return e


def dec_input(r):
    i = {"ts": r.take("d"), "id": r.take("H")}
    n = r.take("B")
    if n > 32:
        raise BadFile(f"input with {n} axes")
    i["axes"] = [r.take("f") for _ in range(n)]
    i["buttons"], i["yaw"], i["pitch"] = r.take("I"), r.take("f"), r.take("f")
    return i


DECODERS = {1: dec_fighter_table, 2: dec_fighter_frame, 3: dec_vitals, 4: dec_prop_table, 5: dec_prop_frame, 6: dec_event,
            7: dec_input}


def read(data):
    """-> (header, [(kind, record or None)], tail) where tail is None, or what cut the file short."""
    if data[:6] != MAGIC:
        raise BadFile("not an .hsmrec file")
    r = R(data)
    r.p = 6
    version, _ = r.take("2B")
    if version != VERSION:
        raise BadFile(f"version {version}, this reader knows {VERSION}")
    h = {"build": r.take("Q"), "mod": r.s(), "level": r.s(), "wall_ms": r.take("Q"), "steady_ms": r.take("d")}
    out, p, tail = [], r.p, None
    while p < len(data):
        if len(data) - p < 5:
            tail = f"{len(data) - p} byte(s) of a chunk header"
            break
        kind, n = struct.unpack_from("<BI", data, p)
        if n > MAX_CHUNK:
            raise BadFile(f"chunk at byte {p}: length {n}")
        if len(data) - p - 5 < n + 4:
            tail = f"a {CHUNKS.get(kind, kind)} chunk cut at {len(data) - p} of {n + 9} bytes"
            break
        payload = data[p + 5:p + 5 + n]
        (crc,) = struct.unpack_from("<I", data, p + 5 + n)
        if crc != zlib.crc32(payload):
            raise BadFile(f"chunk at byte {p}: checksum")
        p += 9 + n
        dec = DECODERS.get(kind)
        if dec is None:
            out.append((kind, None))
            continue
        cr = R(payload)
        rec = dec(cr)
        cr.end()
        out.append((kind, rec))
    return h, out, tail


def load(path):
    with open(path, "rb") as f:
        return read(f.read())


def counts(chunks):
    c = collections.Counter()
    for kind, rec in chunks:
        if kind not in CHUNKS:
            c["Unknown"] += 1
        else:
            c[CHUNKS[kind]] += 1
            if kind == 6:
                c["Event." + EVENTS[rec["kind"]]] += 1
    return c


def summary(h, chunks, tail, size):
    t0 = h["steady_ms"]
    fighters, frames, props, pframes = {}, collections.defaultdict(list), {}, collections.Counter()
    inputs, input_ids, pressed = 0, set(), 0
    t_end = t0
    for kind, rec in chunks:
        if rec is None:
            continue
        if "ts" in rec:
            t_end = max(t_end, rec["ts"])
        if kind == 1:
            fighters[rec["id"]] = rec
        elif kind == 2:
            frames[rec["id"]].append(rec["ts"])
        elif kind == 4:
            props[rec["id"]] = rec
        elif kind == 5:
            pframes[rec["id"]] += 1
        elif kind == 7:
            inputs += 1
            input_ids.add(rec["id"])
            pressed |= rec["buttons"]
    dur = (t_end - t0) / 1000.0
    lines = [f"build {h['build']}  mod {h['mod']}  level {h['level']}",
             f"duration {dur:.1f} s  size {size / 1e6:.2f} MB" + (f" ({size / 1e6 / dur * 60:.0f} MB/min)" if dur > 0 else "")]
    if tail:
        lines.append(f"cut short: {tail} (the game stopped while writing; everything before it is here)")
    lines.append(f"fighters {len(fighters)}")
    for fid in sorted(fighters):
        t = fighters[fid]
        ts = frames[fid]
        span = (ts[-1] - ts[0]) / 1000.0 if len(ts) > 1 else 0
        hz = (len(ts) - 1) / span if span > 0 else 0
        flags = ",".join(n for b, n in FIGHTER_FLAGS.items() if t["flags"] & b) or "-"
        bodies = sum(b["body"] for b in t["bones"])
        lines.append(f"  #{fid} {t['name']} ({t['cls']}, {flags}): {len(t['bones'])} bones, {bodies} bodies, "
                     f"{len(ts)} frames, {hz:.1f} Hz")
    lines.append(f"props {len(props)}, frames {sum(pframes.values())}")
    for pid in sorted(props, key=lambda k: -pframes[k])[:10]:
        lines.append(f"  {pid:#x} {props[pid]['name']} ({props[pid]['cls']}): {pframes[pid]} frames")
    if inputs:
        held = ", ".join(n for b, n in enumerate(ACTIONS) if pressed >> b & 1) or "none"
        lines.append(f"input {inputs} samples (fighter {', '.join(f'#{i}' for i in sorted(input_ids))}), buttons used: {held}")
    else:
        lines.append("input none")
    ev = counts(chunks)
    names = sorted(k[6:] for k in ev if k.startswith("Event."))
    lines.append("events " + (", ".join(f"{n} {ev['Event.' + n]}" for n in names) if names else "none"))
    if ev["Unknown"]:
        lines.append(f"skipped {ev['Unknown']} chunk(s) of a newer kind")
    return lines


def fmt_event(e, t0, fighters):
    who = lambda i: f"#{i}" + (f" {fighters[i]['name']}" if i in fighters else "") if i else "-"
    hand = {0: "R", 1: "L"}.get(e["hand"], "")
    name = EVENTS[e["kind"]]
    extra = []
    if e["other"]:
        extra.append("other " + who(e["other"]))
    if e["prop"]:
        extra.append(f"prop {e['prop']:#x}")
    if e["text"]:
        extra.append(repr(e["text"]))
    if name == "GripChange":
        extra.append(f"grip {int(e['x'][0]):#06x} -> {int(e['x'][1]):#06x}")
    elif name in ("PickUp", "Drop"):
        extra.append(f"class {int(e['x'][0]) | int(e['x'][1]) << 16:#010x}")
    elif name == "Damage":
        extra.append(f"-{e['x'][0]:g} -> {e['x'][1]:g}")
    elif name == "Sever":
        extra.append(f"bit {int(e['x'][0])}")
    elif name == "Hit":
        extra.append(f"impulse {e['x'][3]:g}")
    elif name == "Input":
        extra.append("press" if e["x"][0] else "release")
    elif any(e["x"]):
        extra.append("x " + " ".join(f"{v:g}" for v in e["x"]))
    return f"{(e['ts'] - t0) / 1000.0:9.3f}  {name}{' ' + hand if hand else ''}  {who(e['subject'])}  " + "  ".join(extra)


def events(h, chunks, kind=None, fighter=None):
    fighters = {rec["id"]: rec for k, rec in chunks if k == 1}
    out = []
    for k, e in chunks:
        if k != 6 or (kind and EVENTS[e["kind"]].lower() != kind.lower()):
            continue
        if fighter is not None and fighter not in (e["subject"], e["other"]):
            continue
        out.append(fmt_event(e, h["steady_ms"], fighters))
    return out


def csv_rows(h, chunks, fighter=None, bone=None, prop=None, inputs=False):
    t0 = h["steady_ms"]
    out = io.StringIO()
    if inputs:
        out.write("t,fighter," + ",".join(AXES) + ",buttons,yaw,pitch\n")
        for k, i in chunks:
            if k == 7:
                axes = (list(i["axes"]) + [""] * len(AXES))[:len(AXES)]
                out.write(",".join(map(str, [round((i["ts"] - t0) / 1000.0, 4), i["id"], *axes, i["buttons"], i["yaw"],
                                             i["pitch"]])) + "\n")
        return out.getvalue()
    if prop is not None:
        out.write("t,body,at_rest,px,py,pz,vx,vy,vz,ax,ay,az,qx,qy,qz,qw\n")
        for k, f in chunks:
            if k == 5 and f["id"] == prop:
                out.write(",".join(map(str, [round((f["ts"] - t0) / 1000.0, 4), f["body"], f["flags"] & 1, *f["pos"], *f["vel"],
                                             *f["ang"], *f["rot"]])) + "\n")
        return out.getvalue()
    table = next((rec for k, rec in chunks if k == 1 and rec["id"] == fighter), None)
    if table is None:
        raise BadFile(f"no fighter #{fighter}")
    names = [b["name"] for b in table["bones"]]
    bone = bone or "pelvis"
    if bone not in names:
        raise BadFile(f"fighter #{fighter} has no bone {bone!r} (it has {', '.join(names[:12])}...)")
    i = names.index(bone)
    out.write("t,dt,px,py,pz,vx,vy,vz,ax,ay,az,qx,qy,qz,qw,grip\n")
    for k, f in chunks:
        if k != 2 or f["id"] != fighter:
            continue
        if i >= len(f["bones"]):
            continue
        b = f["bones"][i]
        vel, ang = b.get("vel", ("", "", "")), b.get("ang", ("", "", ""))
        out.write(",".join(map(str, [round((f["ts"] - t0) / 1000.0, 4), round(f["dt"], 5), *b["pos"], *vel, *ang, *b["rot"],
                                     f["grip"]])) + "\n")
    return out.getvalue()


# ---------------------------------------------------------------- a writer, for the self-test and tests/test_hsmrec.py
class W:
    def __init__(self):
        self.b = bytearray()

    def put(self, fmt, *v):
        self.b += struct.pack("<" + fmt, *v)
        return self

    def s(self, text):
        raw = text.encode()[:255]
        self.b += bytes([len(raw)]) + raw
        return self


def enc_header(build, mod, level, wall_ms, steady_ms):
    return bytes(W().put("6sBB", MAGIC, VERSION, 0).put("Q", build).s(mod).s(level).put("Qd", wall_ms, steady_ms).b)


def enc_chunk(kind, payload):
    return struct.pack("<BI", kind, len(payload)) + payload + struct.pack("<I", zlib.crc32(payload))


def enc_fighter_table(fid, flags, name, cls, bones):
    w = W().put("HB", fid, flags).s(name).s(cls).put("H", len(bones))
    for bname, parent, body in bones:
        w.s(bname).put("HB", parent, 1 if body else 0)
    return enc_chunk(1, bytes(w.b))


def enc_fighter_frame(ts, dt, fid, bones, grip=0):
    """bones: [(pos3, quat16 x4 ints, vel3 or None, ang3 or None)]; no weapons."""
    w = W().put("dfHH", ts, dt, fid, len(bones))
    for pos, q, vel, ang in bones:
        w.put("3f4hB", *pos, *q, 1 if vel else 0)
        if vel:
            w.put("6f", *vel, *ang)
    w.put("BBH", 0, 0, grip)
    return enc_chunk(2, bytes(w.b))


def enc_prop_frame(ts, pid, body, flags, pos, vel):
    return enc_chunk(5, bytes(W().put("dQBB3f4h3f3f", ts, pid, body, flags, *pos, 0, 0, 0, 32767, *vel, 0, 0, 0).b))


def enc_input(ts, fid, axes, buttons, yaw=0.0, pitch=0.0):
    return enc_chunk(7, bytes(W().put("dHB", ts, fid, len(axes)).put(f"{len(axes)}f", *axes).put("Iff", buttons, yaw, pitch).b))


def enc_event(ts, kind, subject, other=0, prop=0, hand=NO_HAND, text="", x=(0, 0, 0, 0)):
    return enc_chunk(6, bytes(W().put("dBHHQB", ts, EVENTS.index(kind), subject, other, prop, hand).s(text).put("4f", *x).b))


def sample():
    """Two fighters for 1 s at 60 Hz (fighter 2 only every other frame), a falling prop, five events."""
    b = enc_header(24185754, "selftest", "Arena", 1793000000000, 500.0)
    bones = [("root", 0xFFFF, False), ("pelvis", 0, True), ("hand_r", 1, True)]
    b += enc_fighter_table(1, 1, "Willie_BP_C_0", "Willie_BP_C", bones)
    b += enc_fighter_table(2, 2, "Willie_BP_C_1", "Willie_BP_C", bones)
    for i in range(61):
        ts = 500.0 + i * 1000.0 / 60
        pose = [((0.0, 0.0, 0.0), (0, 0, 0, 32767), None, None),
                ((float(i), 0.0, 90.0), (0, 0, 0, 32767), (60.0, 0.0, 0.0), (0.0, 0.0, 0.0)),
                ((float(i), 20.0, 120.0), (0, 0, 0, 32767), (60.0, 0.0, 0.0), (0.0, 0.0, 90.0))]
        b += enc_fighter_frame(ts, 1 / 60, 1, pose, grip=3 if i >= 30 else 0)
        if i % 2 == 0:
            b += enc_fighter_frame(ts, 1 / 60, 2, pose)
        b += enc_input(ts, 1, [1.0] + [0.0] * 9, 2 if 30 <= i < 50 else 0, yaw=float(i))
        if i < 5:
            b += enc_prop_frame(ts, 0x2A, 0, 1 if i == 4 else 0, (0.0, 0.0, 100.0 - i * 10), (0.0, 0.0, -600.0))
    b += enc_chunk(4, bytes(W().put("Q", 0x2A).s("BP_Stool_C_1").s("BP_Stool_C").put("B", 1).s("Mesh").b))
    b += enc_event(600.0, "GrabOn", 1, other=2, hand=0, text="head")
    b += enc_event(1000.0, "GripChange", 1, x=(0, 3, 0, 0))
    b += enc_event(1200.0, "GrabOff", 1, other=2, hand=0, text="head")
    b += enc_event(1300.0, "PickUp", 2, prop=0x2A, hand=1, x=(0x1234, 0xABCD, 0, 0))
    b += enc_event(1000.0, "Input", 1, text="Grab Right", x=(1, 0, 0, 0))
    b += enc_event(1500.0, "Marker", 0, text="end")
    b += enc_chunk(200, b"\x01\x02")
    return b


def self_test():
    data = sample()
    h, chunks, tail = read(data)
    assert h["level"] == "Arena" and h["build"] == 24185754 and tail is None, (h, tail)
    c = counts(chunks)
    assert c["FighterFrame"] == 61 + 31 and c["PropFrame"] == 5 and c["Event"] == 6 and c["Input"] == 61 and c["Unknown"] == 1, c
    text = "\n".join(summary(h, chunks, tail, len(data)))
    for needle in ("level Arena", "duration 1.0 s", "#1 Willie_BP_C_0 (Willie_BP_C, local): 3 bones, 2 bodies, 61 frames, 60.0 Hz",
                   "#2 Willie_BP_C_1 (Willie_BP_C, bot): 3 bones, 2 bodies, 31 frames, 30.0 Hz", "props 1, frames 5",
                   "0x2a BP_Stool_C_1 (BP_Stool_C): 5 frames",
                   "input 61 samples (fighter #1), buttons used: GrabRight",
                   "events GrabOff 1, GrabOn 1, GripChange 1, Input 1, Marker 1, PickUp 1", "skipped 1 chunk(s)"):
        assert needle in text, (needle, text)
    ev = events(h, chunks)
    assert ev[0] == "    0.100  GrabOn R  #1 Willie_BP_C_0  other #2 Willie_BP_C_1  'head'", ev[0]
    assert "grip 0x0000 -> 0x0003" in ev[1] and "class 0xabcd1234" in ev[3] and "prop 0x2a" in ev[3], ev
    assert len(events(h, chunks, kind="grabon")) == 1 and len(events(h, chunks, fighter=2)) == 3
    rows = csv_rows(h, chunks, fighter=1, bone="hand_r").splitlines()
    assert len(rows) == 62 and rows[1].startswith("0.0,0.01667,0.0,20.0,120.0,60.0,0.0,0.0,0.0,0.0,90.0,0.0,0.0,0.0,1.0,0"), rows[1]
    rows = csv_rows(h, chunks, fighter=1, bone="root").splitlines()
    assert rows[1].split(",")[5:8] == ["", "", ""], rows[1]   # no body: no velocity
    rows = csv_rows(h, chunks, inputs=True).splitlines()
    assert len(rows) == 62 and rows[0].startswith("t,fighter,move_fwd,") and rows[31].split(",")[12:14] == ["2", "30.0"], rows[31]
    assert events(h, chunks, kind="input")[0].endswith("'Grab Right'  press")
    rows = csv_rows(h, chunks, prop=0x2A).splitlines()
    assert len(rows) == 6 and rows[5].split(",")[2] == "1", rows
    # A crash cuts the last chunk: the rest reads and the summary says so.
    h2, chunks2, tail2 = read(data[:-3])
    assert tail2 and "Unknown" not in counts(chunks2) and len(chunks2) == len(chunks) - 1, tail2
    assert any("cut short" in line for line in summary(h2, chunks2, tail2, len(data) - 3))
    for bad in (b"NOTREC" + data[6:], data[:6] + b"\x02" + data[7:]):
        try:
            read(bad)
            raise AssertionError("bad header read")
        except BadFile:
            pass
    flipped = bytearray(data)
    flipped[len(enc_header(24185754, "selftest", "Arena", 0, 0)) + 7] ^= 1
    try:
        read(bytes(flipped))
        raise AssertionError("bad checksum read")
    except BadFile as e:
        assert "checksum" in str(e)
    print("hsmrec: self-test passed")
    return 0


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--self-test", action="store_true")
    sub = ap.add_subparsers(dest="cmd")
    for name in ("summary", "events", "csv", "counts"):
        p = sub.add_parser(name)
        p.add_argument("file")
        if name == "events":
            p.add_argument("--kind")
            p.add_argument("--fighter", type=int)
        if name == "csv":
            p.add_argument("--fighter", type=int)
            p.add_argument("--bone")
            p.add_argument("--prop", type=lambda s: int(s, 0))
            p.add_argument("--input", action="store_true")
    a = ap.parse_args(argv)
    if a.self_test:
        return self_test()
    if not a.cmd:
        ap.error("give a command (summary, events, csv, counts)")
    try:
        with open(a.file, "rb") as f:
            data = f.read()
        h, chunks, tail = read(data)
        if a.cmd == "summary":
            out = summary(h, chunks, tail, len(data))
        elif a.cmd == "events":
            out = events(h, chunks, a.kind, a.fighter)
        elif a.cmd == "counts":
            c = counts(chunks)
            out = [f"{k} {c[k]}" for k in sorted(c)]
        else:
            if a.fighter is None and a.prop is None and not a.input:
                ap.error("csv needs --fighter N, --prop ID or --input")
            sys.stdout.write(csv_rows(h, chunks, a.fighter, a.bone, a.prop, a.input))
            out = []
    except (OSError, BadFile) as e:
        print(f"hsmrec: {a.file}: {e}", file=sys.stderr)
        return 2
    if tail and a.cmd != "summary":
        print(f"hsmrec: {a.file}: cut short ({tail})", file=sys.stderr)
    if out:
        print("\n".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
