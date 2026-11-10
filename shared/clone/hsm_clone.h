// Style clones: fight anyone's way of fighting. Fetch.ini record=1 writes Clones/<name>.hsmclone as a player plays: rows of what
// they saw and the input stream that followed, with their name and kit. Anyone given that file sets [mod] clone=<name>, and
// a fighter wearing that kit joins their offline fights. A few times a second its Brain finds the row closest to its own
// fight and plays on from there, so every swing and step is the player's own, chosen by the situation. It imitates habits; it
// cannot be them: the physics never repeats, and it does not plan. A file comes from someone else: Db::Load trusts none of it.
// Pure logic: client/Fetch writes the file and plays it. docs/CLONE.md.
#pragma once
#include "hsm_clone_events.h"
#include "hsm_clone_input.h"
#include "hsm_input.h"
#include "hsm_owner.h"
#include "hsm_protocol.h"
#include "hsm_weapon_types.h"
#include "sync/hsm_pose.h"
#include "util/hsm_gzip.h"
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace hsm::clone {

constexpr int kFeatures = 127;
constexpr uint32_t kVersion = 3;
// Where a row keeps what a fighter can tell past the body positions and the input. Features has the whole list.
constexpr int kMyReach = 42, kFoeReach = 44, kLoose = 50, kFoeHealth = 54, kFoeFacing = 76, kMyHealth = 79, kFoeArmor = 94, kMyArmor = 101, kArmorKnown = 108, kMyType = 109, kFoeType = 111, kFoeHit = 113, kMyHit = 120;
constexpr int kMyOffhand = 46;       // the left hand on the shaft of a two handed grip
constexpr int kParts = 7;            // VitalsMsg::partHealth: head, upper, lower, arm R, arm L, leg R, leg L
constexpr int kUpperArmL = 10, kUpperArmR = 14;
constexpr float kQueryMs = 200.f;    // how often the clone looks again; a swing longer than this goes on by the stay bonus
constexpr float kStay = 0.75f;       // the row ahead of the one playing is taken at this share of its cost: no needless cuts
constexpr uint32_t kStayReach = 15;  // samples ahead a row still counts as the same clip
constexpr float kPrevMs = 150.f;     // the recent mouse movement a row is matched on
constexpr float kMaxTurnFixDeg = 30.f;
constexpr int kPelvis = 0, kHead = 8, kHandL = 12, kHandR = 16, kFootL = 19, kFootR = 22;
constexpr int kKeyBit = 12;          // Action::KeyForward; Back, Left, Right follow
constexpr int kRunBit = int(Action::Run);
constexpr uint32_t kRowEvery = 3;    // one row every third sample (~18 a second); the stream keeps every sample
constexpr float kMaxRange = 1500.f;  // uu: further than this nobody is fighting
constexpr float kMaxDtMs = 100.f, kFirstDtMs = 16.f, kMaxTurnDeg = 45.f;
constexpr uint32_t kMaxRows = 3000000, kMaxSamples = 9000000, kMaxBlock = 64u << 20, kMaxStepSamples = 32;
constexpr size_t kMaxFile = size_t(1) << 30, kMaxName = 32;
constexpr size_t kMaxWrite = kMaxFile / 4 * 3;   // where recording stops: a file past kMaxFile would load for nobody
constexpr uint32_t kScanRows = 500000;           // rows one Look searches, about 8 h of play and 4 ms: a longer file takes turns
// What the clone copies (Db::LeftOut). The file keeps everything; the clone plays the fighting, not what came after it.
constexpr float kWalkOffM = 2.5f;      // a beaten foe further than this: the player was walking off, not finishing it
constexpr float kIdleOutMs = 3000.f;   // no input at all for this long: a pause, a menu or a wait, not fighting
constexpr float kDownMismatch = 1e3f;  // a row where someone else is on the ground: taken only when no row agrees
constexpr int kFoeDowned = 66, kFoeFallen = 74, kFoeOut = 75, kMyFallen = 90, kMyDowned = 91;   // Features: the flags of who is down
// What the clone minds in a fight, besides what the row says. Pain: the health its parts lost in the last kPainMs, split by
// where the foe stood (within kSideDeg of straight ahead, or not), full at kPainFull (a solid hit; a light one is half).
constexpr float kPainMs = 1000.f, kPainFull = 0.1f, kSideDeg = 60.f, kPainWeight = 4.f;
// Recall: a move picked again within kRepeatMs costs kRepeatCost times as much; one it lost kHurtMin or more of a part's health
// playing costs kFailCost times for kFailMs. Rows within kSameRows of a move's are the same move.
constexpr float kRepeatMs = 3000.f, kRepeatCost = 2.f, kFailMs = 10000.f, kFailCost = 4.f, kHurtMin = 0.01f;
constexpr uint32_t kSameRows = 9, kRecallMoves = 64;
// Facing: a foe further than kFaceDeg off the camera is turned to first, at kFaceTurnDegS until within kFacedDeg, when it hurts
// from there or the move picked had it in front (kMisfitDeg off this one); one behind (kBehindDeg) within kFaceNearM, always.
// After kFaceGiveUpMs the clone fights as it stands, and does not turn so again for kFaceRestMs.
constexpr float kFaceDeg = 75.f, kBehindDeg = 120.f, kFaceNearM = 3.f, kMisfitDeg = 60.f, kFacedDeg = 20.f, kFaceTurnDegS = 360.f, kFaceGiveUpMs = 1500.f, kFaceRestMs = 1000.f;
// Events (hsm_clone_events.h) belong to the Play block just before them. An older Fetch skips a kind it does not know.
// More holds the event kinds added since (hsm_clone_events.h): it follows the Events block. Input (hsm_clone_input.h): the
// sensitivity and the keys of things other than the fighter, after More. Later (pre12 on): the event kinds added since pre11, and
// every kind added after (hsm_clone_events.h LaterKind), after Input. A new kind of event goes in Later, never in a new block.
enum class Block : uint8_t { Play = 1, Name = 2, Kit = 3, Events = 4, More = 5, Input = 6, Later = 7 };
// Never played: the camera lock flips the player's own setting, the middle button is a debug trace, Talk opens nothing here.
constexpr uint32_t kPlayable = ((1u << kActionCount) - 1) & ~(1u << uint32_t(Action::ToggleCameraLock) | 1u << uint32_t(Action::Talk) | 1u << uint32_t(Action::MiddleMouse));
// How much each feature counts, tuned on held-out recordings (the cost squares the difference, so these are squared).
constexpr float kWeights[kFeatures] = {0.64f, 0.64f, 0.64f, 1, 1, 1, 2.56f, 2.56f, 2.56f, 2.56f, 2.56f, 2.56f, 2.56f, 1, 1, 0.0625f, 0.0625f,
    0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 0.25f, 1, 1, 1, 1, 1, 0.0625f, 1, 1, 1, 1, 1, 1, 4, 4, 4};   // the rest: 0, replay never looks
// The foe's armour columns, counted only when both rows know it (Db::Cost): a fighter fights a knight in plate otherwise than a
// peasant. Tuned on held-out recordings like the rest (docs/CLONE.md); 0 turns it off.
constexpr float kArmourWeight = .5f;

// One frame of the player's input: every axis (a gamepad's too), the held buttons, how far the camera turned.
struct Sample { float dtMs, dyaw, dpitch; float axes[kAxisCount]; uint32_t buttons, end; };
static_assert(sizeof(Sample) == 60);
constexpr size_t kTurn = size_t(Axis::TurnMouse), kLook = size_t(Axis::LookMouse), kGuardR = size_t(Axis::RightGuard), kGuardL = size_t(Axis::LeftGuard);

// How a weapon swings: no weapon, then the game's seven types (WeaponType, same numbers), then the two that stand for a weapon of no
// known type: forged before recordings kept what it was made of, or a shield or a stool. Those go by length, and anything at least
// kLongReach long swings as a polearm whatever it is. A clip is only played with the group it was made with.
constexpr int kWeaponGroups = 10;
constexpr uint8_t kUntypedShort = 8, kUntypedMiddling = 9;
constexpr float kShortReach = .65f, kLongReach = 1.3f;
inline uint8_t WeaponGroup(float type, float metres) {
    if (!(metres > 0)) return 0;
    if (type >= 1 && type < float(kWeaponTypes)) return uint8_t(type);
    return metres < kShortReach ? kUntypedShort : metres < kLongReach ? kUntypedMiddling : uint8_t(WeaponType::Polearms);
}
// Two weapons: the longer one says how the fighter fights.
inline uint8_t WeaponGroupOf(const float* row) {
    const int hand = row[kMyReach + 1] > row[kMyReach] ? 1 : 0;
    return WeaponGroup(row[kMyType + hand], row[kMyReach + hand]);
}

// Where a fighter was hit since the row before and how hard: what each body part lost, health 100 being 1. Parts only fall to a hit
// (178 of 178 damage events in the recordings showed, and 179 of 181 falls had one); they heal slowly, which is no hit.
class Hurt {
public:
    void Forget() { have_ = false; }
    void Row(const VitalsMsg& v, float* out) {
        for (int i = 0; i < kParts; ++i) {
            out[i] = have_ ? std::max(0.f, last_[size_t(i)] - v.partHealth[size_t(i)]) / 100.f : 0.f;
            last_[size_t(i)] = v.partHealth[size_t(i)];
        }
        have_ = true;
    }
private:
    std::array<float, kParts> last_{};
    bool have_ = false;
};

// armor: how well each body part is covered, kParts values (ArmorCover), or null where nobody read it.
// forged: the WeaponType of what each hand holds, from its parts (ForgedWeaponType), or null. The class says the rest.
struct Seen { const sync::PoseFrame* pose; const VitalsMsg* vitals; const float* armor; const uint8_t* forged = nullptr; };

namespace detail {
struct Tip { sync::V3 at, vel; float armed; };
inline Tip TipOf(const sync::PoseFrame& p) {
    for (const auto& w : p.weapons) if (w.present) return {w.tip, w.vel, 1.f};
    return {p.bones[kHandR].pos, p.bones[kHandR].vel, 0.f};
}
inline float Height(const sync::PoseFrame& p) {
    return float((p.bones[kPelvis].pos.z - std::min(p.bones[kFootL].pos.z, p.bones[kFootR].pos.z)) / 100.0);
}
inline float Wrap(float deg) { deg = std::fmod(deg + 180.f, 360.f); return (deg < 0 ? deg + 360.f : deg) - 180.f; }
inline float Reach(const sync::WeaponSample& w) {   // blade base to tip: the same for a class however it is held
    if (!w.present) return 0;
    return std::min(float(std::sqrt((w.tip.x - w.base.x) * (w.tip.x - w.base.x) + (w.tip.y - w.base.y) * (w.tip.y - w.base.y) + (w.tip.z - w.base.z) * (w.tip.z - w.base.z)) / 100), 3.f);
}
inline float Share(float v, float full) { return std::isfinite(v) ? std::clamp(v / full, 0.f, 2.f) : 0.f; }
// Which way the chest points, from the line of the shoulders: a unit vector in the world, flat.
inline void Forward(const sync::PoseFrame& p, double& x, double& y) {
    const sync::V3 l = p.bones[kUpperArmL].pos, r = p.bones[kUpperArmR].pos;
    x = r.y - l.y; y = l.x - r.x;
    const double d = std::hypot(x, y);
    if (d < 1e-6) { x = 1; y = 0; return; }
    x /= d; y /= d;
}
}  // namespace detail

// What a fighter sees, in its own view: metres-ish, x ahead of its camera. tools/hsmclone.py features() is the same.
// loose: the weapon lying nearest to the fighter, or null. The order past feature 41: reach of both hands and the foe's; how the
// hands hold; the loose weapon; the foe's wounds, what it is doing and which way it faces; the fighter's own; the armor; the game's type of what each hand of both holds; the hits.
inline void Features(const Seen& me, const Seen& foe, const sync::V3* loose, float yawDeg, float pitchDeg, float prevTurn, float prevLook, uint32_t buttons, float guardR, float guardL, float* out) {
    const double r = double(yawDeg) * 3.14159265358979323846 / 180.0, c = std::cos(r), s = std::sin(r);
    const sync::V3 o = me.pose->bones[kPelvis].pos;
    int k = 0;
    auto at = [&](const sync::V3& p) {
        out[k++] = float(((p.x - o.x) * c + (p.y - o.y) * s) / 100); out[k++] = float((-(p.x - o.x) * s + (p.y - o.y) * c) / 100);
        out[k++] = float((p.z - o.z) / 100);
    };
    auto flat = [&](const sync::V3& v) { out[k++] = float((v.x * c + v.y * s) / 400); out[k++] = float((-v.x * s + v.y * c) / 400); };
    auto vel = [&](const sync::V3& v) { flat(v); out[k++] = float(v.z / 400); };
    const detail::Tip mine = detail::TipOf(*me.pose), theirs = detail::TipOf(*foe.pose);
    at(mine.at); vel(mine.vel);
    at(me.pose->bones[kHandL].pos);
    flat(me.pose->bones[kPelvis].vel);
    out[k++] = detail::Height(*me.pose);
    out[k++] = (pitchDeg > 180.f ? pitchDeg - 360.f : pitchDeg) / 60.f;
    out[k++] = mine.armed;
    out[k++] = me.pose->weapons[1].present ? 1.f : 0.f;
    const VitalsMsg& mv = *me.vitals; const VitalsMsg& fv = *foe.vitals;
    out[k++] = mv.stamina / 100.f;
    out[k++] = mv.consciousness / 100.f;
    const sync::V3 fp = foe.pose->bones[kPelvis].pos;
    const double fx = ((fp.x - o.x) * c + (fp.y - o.y) * s) / 100, fy = (-(fp.x - o.x) * s + (fp.y - o.y) * c) / 100;
    const double d = std::hypot(fx, fy) + 1e-3, reach = std::min(d, 3.0) + 0.25 * std::max(0.0, std::min(d - 3, 8.0));
    out[k++] = float(fx / d * reach); out[k++] = float(fy / d * reach);
    at(foe.pose->bones[kHead].pos);
    flat(foe.pose->bones[kPelvis].vel);
    at(theirs.at); vel(theirs.vel);
    out[k++] = theirs.armed;
    out[k++] = detail::Height(*foe.pose);
    out[k++] = fv.consciousness / 100.f;
    out[k++] = std::tanh(prevTurn / 60.f);
    out[k++] = std::tanh(prevLook / 30.f);
    for (int b = 0; b < 4; ++b) out[k++] = float(buttons >> (kKeyBit + b) & 1u);
    out[k++] = guardR; out[k++] = guardL; out[k++] = float(buttons >> kRunBit & 1u);
    for (const sync::PoseFrame* p : {me.pose, foe.pose}) for (const auto& w : p->weapons) out[k++] = detail::Reach(w);
    out[k++] = me.pose->grip.offhand; out[k++] = me.pose->grip.altGrip; out[k++] = me.pose->grip.mordhau; out[k++] = foe.pose->grip.offhand;
    if (loose) {
        const double lx = ((loose->x - o.x) * c + (loose->y - o.y) * s) / 100, ly = (-(loose->x - o.x) * s + (loose->y - o.y) * c) / 100;
        const double ld = std::hypot(lx, ly) + 1e-3, seen = std::min(ld, 3.0) + 0.25 * std::max(0.0, std::min(ld - 3, 8.0));
        out[k++] = float(lx / ld * seen); out[k++] = float(ly / ld * seen); out[k++] = std::clamp(float((loose->z - o.z) / 100), -5.f, 5.f); out[k++] = 1;
    } else for (int i = 0; i < 4; ++i) out[k++] = 0;
    auto wounds = [&](const VitalsMsg& v) {
        out[k++] = detail::Share(v.health, 100);
        for (int i = 0; i < kParts; ++i) out[k++] = detail::Share(v.partHealth[i], 100);
        out[k++] = detail::Share(v.bleeding, 100);
    };
    auto hurt = [&](const VitalsMsg& v) { out[k++] = float(std::popcount(v.severed)) / 4; out[k++] = float(std::popcount(v.dislocated)) / 4; };
    wounds(fv);
    out[k++] = detail::Share(fv.stamina, 100);
    hurt(fv);
    for (int b = 0; b < 8; ++b) out[k++] = float(fv.state >> b & 1u);
    out[k++] = float(fv.flags >> 1 & 1u); out[k++] = float(fv.flags >> 2 & 1u);   // fallen, knocked out
    double ax, ay;
    detail::Forward(*foe.pose, ax, ay);
    out[k++] = float(ax * c + ay * s); out[k++] = float(-ax * s + ay * c);
    const double tx = o.x - fp.x, ty = o.y - fp.y, td = std::hypot(tx, ty) + 1e-3;
    out[k++] = float((ax * tx + ay * ty) / td);   // 1: it faces the fighter, -1: its back is turned
    wounds(mv);
    hurt(mv);
    out[k++] = float(mv.flags >> 1 & 1u); out[k++] = float(mv.state & 1u);   // fallen, downed
    detail::Forward(*me.pose, ax, ay);
    out[k++] = float(ax * c + ay * s); out[k++] = float(-ax * s + ay * c);
    for (const Seen* w : {&foe, &me}) for (int i = 0; i < kParts; ++i) out[k++] = w->armor ? w->armor[i] : 0.f;
    out[k++] = foe.armor && me.armor ? 1.f : 0.f;
    auto type = [&](const Seen& w, int hand) {
        const sync::WeaponSample& held = w.pose->weapons[hand];
        const WeaponType t = held.present ? WeaponTypeOfClass(held.classHash) : WeaponType::Unknown;
        out[k++] = held.present ? float(t != WeaponType::Unknown ? uint8_t(t) : w.forged ? w.forged[hand] : 0) : 0.f;
    };
    type(me, 0); type(me, 1); type(foe, 0); type(foe, 1);
    while (k < kFeatures) out[k++] = 0;   // the hits: only Collector::Row knows the row before
}

// Who is on the ground in a row of Features: fallen, knocked out or downed.
inline bool FoeDown(const float* row) { return row[kFoeFallen] > .5f || row[kFoeOut] > .5f || row[kFoeDowned] > .5f; }
inline bool MeDown(const float* row) { return row[kMyFallen] > .5f || row[kMyDowned] > .5f; }
// How far the foe is, in metres: Features squeezes it past 3 m (features 17 and 18).
inline float FoeMetres(const float* row) { const float r = std::hypot(row[17], row[18]); return r <= 3.f ? r : 3.f + (r - 3.f) / .25f; }
// Which way the foe is, in degrees off straight ahead: + to the right.
inline float FoeBearingDeg(const float* row) { return std::atan2(row[18], row[17]) * 57.29578f; }
// Why a row is not played. Played: it is.
enum class Out : uint8_t { Played, WalkOff, Idle };

// What hurt the fighter lately (kPainMs), from the rows it saw: [0] with the foe in front, [1] behind or beside it. Db works it
// out for each recorded row and Brain for what the clone sees, the same way, so the two can be matched.
class Pain {
public:
    void Forget() { seen_.clear(); lost_ = 0; }
    // ms: a clock that only goes on.
    void Add(float ms, const float* row) {
        float health = 0;
        for (int i = 1; i <= kParts; ++i) health += row[kMyHealth + i];
        const bool behind = std::fabs(FoeBearingDeg(row)) > kSideDeg;
        lost_ = seen_.empty() ? 0.f : std::max(0.f, seen_.back().health - health);
        seen_.push_back({ms, health, behind});
        while (seen_.size() > 1 && seen_[1].ms <= ms - kPainMs) seen_.pop_front();   // the first one left is the window's start
    }
    // What the last Add lost: the move playing then was hurt.
    float lost() const { return lost_; }
    std::array<float, 2> Now() const {
        std::array<float, 2> p{};
        for (size_t i = 1; i < seen_.size(); ++i) p[seen_[i].behind] += std::max(0.f, seen_[i - 1].health - seen_[i].health);
        for (float& v : p) v = std::min(1.f, v / kPainFull);
        return p;
    }
private:
    struct Seen { float ms, health; bool behind; };
    std::deque<Seen> seen_;
    float lost_ = 0;
};

// The moves the clone tried this fight, as the rows they started at and ran to: one picked again soon, or one it was hurt
// playing, costs more, so it does something else unless nothing else comes near.
class Recall {
public:
    void Forget() { tried_.clear(); }
    // A move picked at this row; on: the one playing goes on to it (Brain's stay), not a new one.
    void Picked(uint32_t row, float ms, bool on) {
        if (on && !tried_.empty()) {
            Tried& t = tried_.back();
            t.last = std::max(t.last, row);
            if (!t.failed) t.until = ms + kRepeatMs;
            return;
        }
        std::erase_if(tried_, [&](const Tried& t) { return t.until <= ms; });
        if (tried_.size() >= kRecallMoves) tried_.erase(tried_.begin());
        tried_.push_back({row, row, ms + kRepeatMs, false});
    }
    // The move playing lost health.
    void Hurt(float ms) {
        if (tried_.empty()) return;
        tried_.back().failed = true;
        tried_.back().until = ms + kFailMs;
    }
    // What a row's cost is multiplied by. The move playing, going on forward, is no repeat (stay: the row ahead of it).
    float Cost(uint32_t row, float ms, bool stay) const {
        float c = 1.f;
        for (size_t i = 0; i < tried_.size(); ++i) {
            const Tried& t = tried_[i];
            if (t.until <= ms || row + kSameRows < t.first || row > t.last + kSameRows) continue;
            if (t.failed) c = std::max(c, kFailCost);
            else if (!stay && !(i + 1 == tried_.size() && row >= t.last)) c = std::max(c, kRepeatCost);
        }
        return c;
    }
    size_t moves() const { return tried_.size(); }
private:
    struct Tried { uint32_t first, last; float until; bool failed; };
    std::vector<Tried> tried_;
};
// What one Db::Nearest weighed, for the overlay that shows the clone thinking: the cheapest rows it found, cheapest first, and the
// row ahead of the one playing (its stay). Of a file over kScanRows rows, the cheapest are of the rows that Look scanned.
constexpr int kTraceTop = 8;
struct NearTrace {
    struct Top { uint32_t row = 0; float raw = 0, recall = 1, cost = 0; };   // cost: raw * recall, as Nearest weighs the row
    std::array<Top, kTraceTop> top{};
    int n = 0;
    uint32_t stay = 0;   // Db::rows(): none
    float stayRaw = 0, stayRecall = 1, stayCost = 0;   // stayCost: raw * kStay * recall
    void Clear(uint32_t none) { n = 0; stay = none; stayRaw = stayCost = 0; stayRecall = 1; }
    // A row dearer than this cannot get in.
    float limit() const { return n == kTraceTop ? top[kTraceTop - 1].cost : 3.4e38f; }
    void Keep(const Top& t) {
        if (!(t.cost < limit())) return;
        int i = n < kTraceTop ? n++ : kTraceTop - 1;
        for (; i > 0 && t.cost < top[size_t(i - 1)].cost; --i) top[size_t(i)] = top[size_t(i - 1)];
        top[size_t(i)] = t;
    }
};
// What the clone minds when it picks a row (Db::Nearest): none of it, by default. trace: what it weighed goes there.
struct Mind { const std::array<float, 2>* pain = nullptr; const Recall* recall = nullptr; float ms = 0; NearTrace* trace = nullptr; };

// A file name from a player's name or a clone= value: letters, digits, - and _ only, so it cannot leave Clones/.
inline std::string FileStem(const std::string& name) {
    std::string o;
    for (char c : name) {
        if (o.size() >= kMaxName) break;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_') o.push_back(c);
        else if (c == ' ') o.push_back('_');
    }
    return o.empty() ? "Player" : o;
}

inline void PutU32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
inline void PutBytes(std::vector<uint8_t>& o, const void* p, size_t n) { const uint8_t* b = static_cast<const uint8_t*>(p); o.insert(o.end(), b, b + n); }
inline std::vector<uint8_t> FileHeader() {
    std::vector<uint8_t> o;
    PutBytes(o, "HSMCLONE", 8); PutU32(o, kVersion); PutU32(o, uint32_t(kFeatures));
    return o;
}
inline bool IsFileHeader(const uint8_t* d, size_t n) { const std::vector<uint8_t> h = FileHeader(); return n >= h.size() && std::memcmp(d, h.data(), h.size()) == 0; }
// A block: u8 kind, u32 length, payload, u32 crc32(payload). Blocks only ever go on the end, so a file grows session by
// session, and one cut short by a crash loses its last block alone.
inline std::vector<uint8_t> EncodeBlock(Block kind, const uint8_t* p, size_t n) {
    std::vector<uint8_t> o;
    o.reserve(n + 9);
    o.push_back(uint8_t(kind)); PutU32(o, uint32_t(n)); PutBytes(o, p, n); PutU32(o, util::Crc32(p, n));
    return o;
}

// A recording from before pre12 keeps no mouse sensitivity, but its samples show it. Off guard, Willie_BP turns the camera by
// exactly TurnMouse x sensitivity x kTurnPerCount degrees (x 0.75, x the 0.5 of its Look Sensiticity, x UE's yaw scale of 2.5),
// and while guarding by 1/kGuardTurn of that (x 0.005 x 10 x a Look Sensiticity eased to 0.25). Its turns come out at those two
// ratios, and at others while the guard eases in and out or the turn meets the pelvis limit. The guess is the ratio most turns
// share, each guarding turn counting at kGuardTurn times its own; pitch on those turns gives Invert Mouse by its sign. Too few
// turns agree (no mouse turning, or not recorded by Fetch): not known, and the mouse plays at the player's, as before.
class LookGuess {
public:
    static constexpr double kTurnPerCount = .9375, kGuardTurn = 80, kBin = .002;   // ratios within 0.2 % share a bin
    static constexpr uint32_t kMinVotes = 32;
    static constexpr double kMinShare = .03;   // of all turns
    void Add(const Sample& s) {
        if (s.end || !(std::fabs(s.axes[kTurn]) >= 1) || !(std::fabs(s.dyaw) > 1e-3f)) return;
        const double q = double(s.dyaw) / s.axes[kTurn] / kTurnPerCount;
        if (!(q > 1e-4 && q < 1e4)) return;
        const bool pitched = std::fabs(s.axes[kLook]) >= 1 && std::fabs(s.dpitch) > 1e-3f;
        votes_.push_back({int32_t(std::lround(std::log(q) / kBin)), float(q), pitched ? float(s.dpitch / s.axes[kLook] / kTurnPerCount) : 0.f});
    }
    LookScale Guess() const {
        std::map<int32_t, uint32_t> bins;
        for (const Vote& v : votes_) ++bins[v.bin];
        auto at = [&](int32_t b) { const auto i = bins.find(b); return i == bins.end() ? 0u : i->second; };
        auto around = [&](int32_t b) { return at(b - 1) + at(b) + at(b + 1); };
        const int32_t fold = int32_t(std::lround(std::log(kGuardTurn) / kBin));
        uint32_t best = 0;
        int32_t bin = 0;
        for (const auto& [b, n] : bins) {   // lowest first: a tie goes to the lower, so never to kGuardTurn x the answer
            if (around(b) < kMinVotes) continue;
            const uint32_t score = around(b) + around(b - fold);
            if (score > best) { best = score; bin = b; }
        }
        if (best < kMinVotes || best < kMinShare * double(votes_.size())) return {};
        std::vector<float> q;
        std::vector<std::pair<float, float>> pitch;   // its pitch ratio, and its scale
        for (const Vote& v : votes_) {
            const float f = std::abs(v.bin - bin) <= 1 ? 1.f : std::abs(v.bin - (bin - fold)) <= 1 ? float(kGuardTurn) : 0.f;
            if (f > 0) { q.push_back(v.q * f); pitch.push_back({v.pitch, f}); }
        }
        std::nth_element(q.begin(), q.begin() + std::ptrdiff_t(q.size() / 2), q.end());
        const float sens = q[q.size() / 2];
        uint32_t up = 0, down = 0;   // the game pitches against the mouse unless inverted
        for (const auto& [p, f] : pitch)
            if (std::fabs(std::fabs(p) * f / sens - 1) < .01f) ++(p > 0 ? up : down);
        LookScale l = MakeLook(sens, up > down);
        l.guessed = l.known();
        return l;
    }
    size_t turns() const { return votes_.size(); }
private:
    struct Vote { int32_t bin; float q, pitch; };
    std::vector<Vote> votes_;
};

class Db {
public:
    // false: not a clone file of this version, or no play in it. A bad or cut block ends the reading; what came before stands.
    // Everything played is forced within what a player's input can be.
    bool Load(const uint8_t* data, size_t size) {
        rows_ = 0; feats_.clear(); starts_.clear(); samples_.clear(); name_.clear(); kit_.clear(); events_.clear(); blockStart_.clear(); blockKnown_.clear(); out_.clear(); leftOut_.fill(0); pain_.clear();
        blockLook_.clear(); keys_.clear(); presses_.clear(); sources_.clear(); blockSource_.clear();
        // Whose play each Play block is: the Name in effect when it loads. A merged file has each source's Name before its play
        // (hsm_clone_merge.h); a Name with no play after it (the merged name, a rename) starts no source.
        std::string nameNow;
        bool named = false;      // a Name since the last Play block
        uint32_t playBase = 0, playSamples = 0;
        bool playOpen = false;   // the last block was a Play block that loaded: an Events block may follow it
        bool inputOpen = false;  // no block but its Events and More since that Play block: its Input block may follow
        bool moreOpen = false;   // the last block was its Events block: a More block may follow
        bool laterOpen = false;  // no block but its Events, More and Input since that Play block: its Later block may follow
        size_t blockEvents = 0;  // where that play's events start in events_
        // A play no More block followed is from before pre11, whose armour columns came from a slot table that was wrong (it read
        // greaves as a helmet): its rows say the armour is not known.
        size_t playRow = 0;
        bool playMore = true;
        auto closePlay = [&] {
            if (!playMore)
                for (size_t r = playRow; r < starts_.size(); ++r) std::fill_n(&feats_[r * kFeatures + kFoeArmor], kArmorKnown + 1 - kFoeArmor, 0.f);
            playMore = true;
        };
        if (size > kMaxFile || !IsFileHeader(data, size)) return false;
        for (size_t p = FileHeader().size(); size - p >= 9;) {
            uint32_t len, crc;
            const uint8_t kind = data[p];
            std::memcpy(&len, data + p + 1, 4);
            if (len > kMaxBlock || size - p - 9 < len) break;
            const uint8_t* d = data + p + 5;
            std::memcpy(&crc, d + len, 4);
            if (crc != util::Crc32(d, len)) break;
            if (kind == uint8_t(Block::Play)) {
                closePlay();
                playBase = uint32_t(samples_.size());
                playRow = starts_.size(); playMore = false;
                if (!AddPlay(d, len)) break;
                playSamples = uint32_t(samples_.size()) - playBase; playOpen = true; inputOpen = true; laterOpen = true;
                blockStart_.push_back(playBase); blockKnown_.push_back(0); blockLook_.push_back({});
                if (sources_.empty() || named) sources_.push_back(nameNow);
                named = false;
                blockSource_.push_back(uint32_t(sources_.size() - 1));
            } else if (kind == uint8_t(Block::Events)) {
                std::vector<FightEvent> got;   // a bad Events block is left out alone: the play it goes with stands
                blockEvents = events_.size();
                if (playOpen && events_.size() < kMaxEvents && ReadEventsPayload(d, len, playSamples, got) && events_.size() + got.size() <= kMaxEvents) {
                    for (FightEvent& e : got) { e.sample += playBase; events_.push_back(e); }
                    blockKnown_.back() = 1;
                    moreOpen = true;
                }
                playOpen = false;
                p += size_t(len) + 9;
                continue;
            } else if (kind == uint8_t(Block::More)) {
                std::vector<FightEvent> got;   // like an Events block, and merged with it in time order
                if (moreOpen && ReadEventsPayload(d, len, playSamples, got, kInMore) && events_.size() + got.size() <= kMaxEvents) {
                    playMore = true;
                    const size_t mid = events_.size();
                    for (FightEvent& e : got) { e.sample += playBase; events_.push_back(e); }
                    std::inplace_merge(events_.begin() + std::ptrdiff_t(blockEvents), events_.begin() + std::ptrdiff_t(mid), events_.end(),
                                       [](const FightEvent& a, const FightEvent& b) { return a.sample < b.sample; });
                }
                moreOpen = false;
                p += size_t(len) + 9;
                continue;
            } else if (kind == uint8_t(Block::Input)) {
                InputLog got;   // a bad one is left out alone, like an Events block
                if (inputOpen && ReadInputPayload(d, len, playSamples, got)) AddInput(got, playBase);
                playOpen = moreOpen = inputOpen = false;
                p += size_t(len) + 9;
                continue;
            } else if (kind == uint8_t(Block::Later)) {
                std::vector<FightEvent> got;   // like a More block, for a play whose events are known
                if (laterOpen && !blockKnown_.empty() && blockKnown_.back() && ReadEventsPayload(d, len, playSamples, got, kInLater) &&
                    events_.size() + got.size() <= kMaxEvents) {
                    const size_t mid = events_.size();
                    for (FightEvent& e : got) { e.sample += playBase; events_.push_back(e); }
                    std::inplace_merge(events_.begin() + std::ptrdiff_t(blockEvents), events_.begin() + std::ptrdiff_t(mid), events_.end(),
                                       [](const FightEvent& a, const FightEvent& b) { return a.sample < b.sample; });
                }
                playOpen = moreOpen = inputOpen = laterOpen = false;
                p += size_t(len) + 9;
                continue;
            }
            if (kind != uint8_t(Block::Play)) playOpen = inputOpen = laterOpen = false;
            moreOpen = false;
            if (kind == uint8_t(Block::Name) && len <= kMaxName * 4) { name_.assign(reinterpret_cast<const char*>(d), len); nameNow = name_; named = true; }
            if (kind == uint8_t(Block::Kit)) kit_.assign(d, d + len);
            p += size_t(len) + 9;
        }
        closePlay();
        rows_ = uint32_t(starts_.size());
        if (sources_.size() == 1) sources_[0] = name_;   // one player's: the name it has now (a rename goes on the end)
        PairKeys();
        GuessLooks();
        MarkLeftOut();
        MarkPain();
        return rows_ > 0;
    }
    // Why the clone does not play this row (Out::Played: it does), and how many rows each reason left out.
    Out LeftOut(uint32_t row) const { return Out(out_[row]); }
    uint32_t LeftOutCount(Out why) const { return leftOut_[size_t(why)]; }
    const std::string& name() const { return name_; }
    const std::vector<uint8_t>& kit() const { return kit_; }   // the player's passport save: not checked here (CheckPassportSave)
    // Whose play the file holds: one name for a recording of one player, each one merged for a merged file (hsm_clone_merge.h),
    // in file order. A recording renamed and then recorded into further has the play before the rename under its old name.
    const std::vector<std::string>& sources() const { return sources_; }
    // The source a row's play came from: an index into sources().
    uint32_t SourceOf(uint32_t row) const {
        const size_t b = size_t(std::upper_bound(blockStart_.begin(), blockStart_.end(), starts_[row]) - blockStart_.begin());
        return b ? blockSource_[b - 1] : 0;
    }
    const std::string& source(uint32_t row) const { return sources_[SourceOf(row)]; }
    // Whether a Play block's payload is one Load takes (the rows within it, in order, finite; the samples finite), with its row
    // and sample counts. Not whether it fits after what is loaded already.
    static bool ValidPlay(const uint8_t* d, uint32_t len, uint32_t* rowsOut = nullptr, uint32_t* samplesOut = nullptr) {
        constexpr size_t rowBytes = kFeatures * 4 + 4;
        if (len < 8) return false;
        uint32_t rows, samples;
        std::memcpy(&rows, d, 4); std::memcpy(&samples, d + 4, 4);
        if (!samples || rows > kMaxRows || samples > kMaxSamples || size_t(len) != 8 + rows * rowBytes + samples * sizeof(Sample)) return false;
        const uint8_t* p = d + 8;
        uint32_t last = 0;
        for (uint32_t i = 0; i < rows; ++i, p += rowBytes) {
            float f[kFeatures];
            std::memcpy(f, p, kFeatures * 4);
            uint32_t at;
            std::memcpy(&at, p + kFeatures * 4, 4);
            if (at >= samples || (i && at < last)) return false;
            last = at;
            for (int k = 0; k < kFeatures; ++k) if (!std::isfinite(f[k])) return false;
        }
        for (uint32_t i = 0; i < samples; ++i, p += sizeof(Sample)) {
            Sample s;
            std::memcpy(&s, p, sizeof s);
            if (!std::isfinite(s.dtMs) || !std::isfinite(s.dyaw) || !std::isfinite(s.dpitch)) return false;
            for (size_t a = 0; a < kAxisCount; ++a) if (!std::isfinite(s.axes[a])) return false;
        }
        if (rowsOut) *rowsOut = rows;
        if (samplesOut) *samplesOut = samples;
        return true;
    }
    double PlayedSeconds() const { double ms = 0; for (const Sample& s : samples_) ms += s.dtMs; return ms / 1000; }
    uint32_t rows() const { return rows_; }
    // The fight's events in time order, sample counted over the whole file (sample(): the one each came before).
    const std::vector<FightEvent>& events() const { return events_; }
    // Whether the play at this sample has its events: a Play block an Events block followed. Not in a file from before events,
    // nor, in one from pre6, in a stretch where none happened (pre6 wrote no block then).
    bool EventsKnown(uint32_t sample) const {
        const size_t b = size_t(std::upper_bound(blockStart_.begin(), blockStart_.end(), sample) - blockStart_.begin());
        return b && blockKnown_[b - 1];
    }
    const Sample& sample(uint32_t i) const { return samples_[i]; }
    uint32_t samples() const { return uint32_t(samples_.size()); }
    // The sensitivity the play at this sample was recorded at (not known: a file from before pre12, or a bad Input block).
    LookScale Look(uint32_t sample) const {
        const size_t b = size_t(std::upper_bound(blockStart_.begin(), blockStart_.end(), sample) - blockStart_.begin());
        return b ? blockLook_[b - 1] : LookScale{};
    }
    // The keys of things other than the fighter that the recording pressed (hsm_clone_input.h). release: the released end of
    // the key pressed by `partner`; a press with a partner is held until its release plays. partner -1: a key with one end only.
    struct Key { std::string cls, fn; int32_t partner = -1; bool release = false; };
    struct KeyAt { uint32_t sample; uint16_t key; };   // the sample it came before, counted over the whole file
    const std::vector<Key>& keys() const { return keys_; }
    const std::vector<KeyAt>& presses() const { return presses_; }
    // The first press at or after this sample.
    size_t PressFrom(uint32_t sample) const {
        return size_t(std::lower_bound(presses_.begin(), presses_.end(), sample, [](const KeyAt& k, uint32_t v) { return k.sample < v; }) - presses_.begin());
    }
    uint32_t start(uint32_t row) const { return starts_[row]; }
    const float* row(uint32_t i) const { return &feats_[size_t(i) * kFeatures]; }
    // What hurt the player at this row (Pain::Now), as the clone's own pain is matched with it.
    std::array<float, 2> pain(uint32_t i) const { return pain_[i]; }
    // A row where a different one of the two is on the ground costs kDownMismatch more: a standing foe is never struck as if
    // it lay there, nor does a clone on its feet play a player getting up. With pain, the row's is matched with it.
    float Cost(uint32_t i, const float* q, float limit = 3.4e38f, const std::array<float, 2>* pain = nullptr) const {
        const float* f = row(i);
        float c = FoeDown(f) != FoeDown(q) || MeDown(f) != MeDown(q) ? kDownMismatch : 0.f;
        if (pain) for (int k = 0; k < 2; ++k) { const float e = pain_[i][k] - (*pain)[k]; c += kPainWeight * e * e; }
        for (int k = 0; k < kFeatures && c < limit; ++k) { const float e = f[k] - q[k]; c += kWeights[k] * e * e; }
        // What the foe wears, when both know it: a recording from before armour was read, or a moment it was not, says nothing.
        if (kArmourWeight > 0 && f[kArmorKnown] > .5f && q[kArmorKnown] > .5f)
            for (int k = kFoeArmor; k < kFoeArmor + kParts && c < limit; ++k) { const float e = f[k] - q[k]; c += kArmourWeight * e * e; }
        return c;
    }
    // The first row of the clip playing at sample `at`, within reach and before its stretch of play ends. rows(): none, or
    // that row is left out.
    uint32_t RowAhead(uint32_t at) const {
        const uint32_t i = uint32_t(std::lower_bound(starts_.begin(), starts_.end(), at) - starts_.begin());
        if (i >= rows_ || starts_[i] - at > kStayReach || !Plays(i)) return rows_;
        for (uint32_t s = at; s < starts_[i]; ++s) if (samples_[s].end) return rows_;
        return i;
    }
    // The closest row the clone plays; `stay` (a row, or rows()) gets the stay bonus and wins a tie. What it minds: its pain is
    // matched, and what it recalls multiplies a row's cost (never below it, so a row already too dear is not looked up).
    // With a trace, a row's cost is worked out on past the best so far, up to the trace's limit, so the cheapest kTraceTop are
    // known; the row picked is the same, as a cost cut short is never under the best so far and the rest only adds to it.
    uint32_t Nearest(const float* q, uint32_t stay, uint32_t from = 0, uint32_t count = kScanRows, const Mind& mind = {}) const {
        if (stay < rows_ && !Plays(stay)) stay = rows_;
        uint32_t best = stay;
        float cost = stay < rows_ ? Cost(stay, q, 3.4e38f, mind.pain) * kStay * (mind.recall ? mind.recall->Cost(stay, mind.ms, true) : 1.f) : 3.4e38f;
        NearTrace* const t = mind.trace;
        if (t) {
            t->Clear(rows_);
            if (stay < rows_) {
                t->stay = stay;
                t->stayRaw = Cost(stay, q, 3.4e38f, mind.pain);
                t->stayRecall = mind.recall ? mind.recall->Cost(stay, mind.ms, true) : 1.f;
                t->stayCost = cost;
            }
        }
        count = std::min(count, rows_);
        for (uint32_t n = 0, i = from % std::max(rows_, 1u); n < count; ++n, i = i + 1 == rows_ ? 0 : i + 1) {
            if (!Plays(i)) continue;
            const float limit = t ? std::max(cost, t->limit()) : cost;
            float c = Cost(i, q, limit, mind.pain);
            if (t && c < limit) {
                const float r = mind.recall ? mind.recall->Cost(i, mind.ms, false) : 1.f;
                t->Keep({i, c, r, c * r});
            }
            if (c < cost && mind.recall) c *= mind.recall->Cost(i, mind.ms, false);
            if (c < cost) { cost = c; best = i; }
        }
        return best;
    }
private:
    static constexpr size_t kMaxKeys = 4096;
    // Every play that keeps no sensitivity gets the one all of that player's show together (the guess wants many turns, more
    // than a block has, and a block alone cannot tell guarding turns from the rest). One player's to a source: a merged file's
    // players are guessed each on their own.
    void GuessLooks() {
        for (uint32_t src = 0; src < uint32_t(sources_.size()); ++src) {
            LookGuess g;
            for (size_t b = 0; b < blockStart_.size(); ++b) {
                if (blockSource_[b] != src || blockLook_[b].known()) continue;
                const uint32_t to = b + 1 < blockStart_.size() ? blockStart_[b + 1] : uint32_t(samples_.size());
                for (uint32_t s = blockStart_[b]; s < to; ++s) g.Add(samples_[s]);
            }
            const LookScale l = g.Guess();
            if (l.known())
                for (size_t b = 0; b < blockLook_.size(); ++b)
                    if (blockSource_[b] == src && !blockLook_[b].known()) blockLook_[b] = l;
        }
    }
    void AddInput(const InputLog& got, uint32_t base) {
        blockLook_.back() = got.look;
        for (const KeyPress& k : got.presses) {
            if (presses_.size() >= size_t(kMaxKeyPresses) * 4) return;
            const std::string& cls = got.names[k.cls];
            const std::string& fn = got.names[k.fn];
            size_t id = 0;
            while (id < keys_.size() && !(keys_[id].cls == cls && keys_[id].fn == fn)) ++id;
            if (id == keys_.size()) { if (keys_.size() >= kMaxKeys) continue; keys_.push_back({cls, fn}); }
            presses_.push_back({k.sample + base, uint16_t(id)});
        }
    }
    // A key seen with exactly two ends (KeyOf): the one the file pressed first is the press, the other its release.
    void PairKeys() {
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (keys_[i].partner >= 0) continue;
            int32_t other = -1, ends = 1;
            for (size_t j = i + 1; j < keys_.size(); ++j)
                if (keys_[j].cls == keys_[i].cls && KeyOf(keys_[j].fn) == KeyOf(keys_[i].fn)) { other = int32_t(j); ++ends; }
            if (ends != 2) continue;
            keys_[i].partner = other; keys_[size_t(other)].partner = int32_t(i); keys_[size_t(other)].release = true;
        }
    }
    bool Plays(uint32_t i) const { return out_[i] == uint8_t(Out::Played) || leftOut_[size_t(Out::Played)] == 0; }   // none left: all play
    static bool Quiet(const Sample& s) {
        if (s.buttons) return false;
        for (float a : s.axes) if (std::fabs(a) > 1e-3f) return false;
        return true;
    }
    // Two things a player does that are not fighting. A won fight's end: the foe on the ground from some row until the stretch
    // of play ends (the foe died, or play stopped); there, a row further than kWalkOffM from it is walking off. And a stillness:
    // kIdleOutMs or more of samples with no input at all; every row whose clip starts in it.
    void MarkLeftOut() {
        out_.assign(rows_, uint8_t(Out::Played));
        for (uint32_t first = 0, r = 0; r < rows_; ++r) {
            const uint32_t to = r + 1 < rows_ ? starts_[r + 1] : uint32_t(samples_.size());
            bool ends = r + 1 == rows_;
            for (uint32_t s = starts_[r]; s < to && !ends; ++s) ends = samples_[s].end != 0;
            if (!ends) continue;
            uint32_t down = r + 1;
            while (down > first && FoeDown(row(down - 1))) --down;
            for (uint32_t q = down; q <= r; ++q) if (FoeMetres(row(q)) > kWalkOffM) out_[q] = uint8_t(Out::WalkOff);
            first = r + 1;
        }
        for (uint32_t i = 0, r = 0; i < samples_.size();) {
            if (!Quiet(samples_[i])) { ++i; continue; }
            uint32_t j = i;
            float ms = 0;
            while (j < samples_.size() && Quiet(samples_[j])) { ms += samples_[j].dtMs; if (samples_[j++].end) break; }
            if (ms >= kIdleOutMs) {
                while (r < rows_ && starts_[r] < i) ++r;
                for (uint32_t q = r; q < rows_ && starts_[q] < j; ++q) if (out_[q] == uint8_t(Out::Played)) out_[q] = uint8_t(Out::Idle);
            }
            i = j;
        }
        leftOut_.fill(0);
        for (uint8_t o : out_) ++leftOut_[o];
    }
    // Each row's pain, over its stretch of play only: what came before a stretch is not known.
    void MarkPain() {
        pain_.assign(rows_, {});
        Pain p;
        float ms = 0;
        for (uint32_t r = 0, s = 0; r < rows_; ++r) {
            for (; s < starts_[r]; ++s) { ms += samples_[s].dtMs; if (samples_[s].end) p.Forget(); }
            p.Add(ms, row(r));
            pain_[r] = p.Now();
        }
    }
    bool AddPlay(const uint8_t* d, uint32_t len) {
        constexpr size_t rowBytes = kFeatures * 4 + 4;
        uint32_t rows, samples;
        if (!ValidPlay(d, len, &rows, &samples)) return false;
        if (starts_.size() + rows > kMaxRows || samples_.size() + samples > kMaxSamples) return false;
        const uint32_t base = uint32_t(samples_.size());
        const size_t oldRows = starts_.size();
        const uint8_t* p = d + 8;
        feats_.resize((oldRows + rows) * kFeatures); starts_.resize(oldRows + rows); samples_.resize(base + size_t(samples));
        for (uint32_t i = 0; i < rows; ++i, p += rowBytes) {
            std::memcpy(&feats_[(oldRows + i) * kFeatures], p, kFeatures * 4);
            uint32_t at;
            std::memcpy(&at, p + kFeatures * 4, 4);
            starts_[oldRows + i] = base + at;
        }
        std::memcpy(&samples_[base], p, samples * sizeof(Sample));
        for (uint32_t i = base; i < samples_.size(); ++i) {
            Sample& s = samples_[i];
            for (size_t a = 0; a < kAxisCount; ++a) {
                const float limit = IsDeltaAxis(Axis(a)) ? kAxisLimit : 1.f;
                s.axes[a] = std::clamp(s.axes[a], -limit, limit);
            }
            s.dtMs = std::clamp(s.dtMs, 1.f, kMaxDtMs);
            s.dyaw = std::clamp(s.dyaw, -kMaxTurnDeg, kMaxTurnDeg); s.dpitch = std::clamp(s.dpitch, -kMaxTurnDeg, kMaxTurnDeg);
            s.buttons &= kPlayable;
        }
        samples_.back().end = 1;   // play never runs on into the next block
        return true;
    }
    uint32_t rows_ = 0;
    std::string name_;
    std::vector<uint8_t> kit_;
    std::vector<float> feats_;
    std::vector<uint32_t> starts_;
    std::vector<Sample> samples_;
    std::vector<FightEvent> events_;
    std::vector<uint32_t> blockStart_;   // each Play block's first sample
    std::vector<uint8_t> blockKnown_;    // and whether its events came with it
    std::vector<LookScale> blockLook_;   // and its sensitivity
    std::vector<uint32_t> blockSource_;  // and whose play it is (sources_)
    std::vector<std::string> sources_;
    std::vector<Key> keys_;
    std::vector<KeyAt> presses_;
    std::vector<uint8_t> out_;           // each row's Out
    std::vector<std::array<float, 2>> pain_;   // each row's Pain::Now
    std::array<uint32_t, 3> leftOut_{};  // rows by Out: [Played] is the rows that play
};

// Why the clone plays what it does (Brain::lastLook). BestMatch: the row that fit best. KeptPlaying: the move playing fit best,
// so it went on. Facing: the foe is off to the side or behind, and the clone turns to it before it plays anything.
enum class LookWhy : uint8_t { None, BestMatch, KeptPlaying, Facing };
// The last Look, with Brain::SetTrace on: what it saw, what it weighed and what it picked. seq goes up by one a Look.
struct LookTrace {
    NearTrace weighed;
    uint32_t row = 0, stay = 0, seq = 0;   // row: the row picked, or the one it would have played (Facing); Db::rows(): none
    LookWhy why = LookWhy::None;
    std::array<float, 2> pain{};
    float foeMetres = 0, foeDeg = 0;
};

// look: the sensitivity the mouse axes were recorded at (not known: play them at the sensitivity the game is set to).
struct Output { std::array<float, kAxisCount> axes{}; uint32_t buttons = 0; float yaw = 0, pitch = 0; LookScale look; };

class Brain {
public:
    explicit Brain(const Db& db) : db_(db) {}
    void Reset(float yawDeg, float pitchDeg) {
        out_ = Output{}; out_.yaw = yawDeg; out_.pitch = pitchDeg;
        playing_ = false; cursor_ = 0; budgetMs_ = 0; sinceQueryMs_ = 0; fixDeg_ = 0; recent_.clear();
        pain_.Forget(); recall_.Forget(); facing_ = false; faceSinceMs_ = 0; noFaceUntilMs_ = 0;   // a new fight: nothing minded
        turns_ = hurtMoves_ = 0;
        held_.clear(); keys_.clear(); keyAt_ = 0;   // a new fighter: nothing of it is held
        moveFrom_ = 0;
    }
    // Keeps what each Look weighed (lastLook): only the overlay wants it, and it costs a little time a Look.
    void SetTrace(bool on) { trace_ = on; }
    bool tracing() const { return trace_; }
    const LookTrace& lastLook() const { return look_; }
    // How far into the move playing the clone is: from the row its move started at (a move that goes on counts on).
    float ClipMs() const {
        float ms = 0;
        for (uint32_t s = moveFrom_, n = 0; s < cursor_ && s < db_.samples() && n < kClipMsSamples; ++s, ++n) ms += db_.sample(s).dtMs;
        return ms;
    }
    // The keys (Db::keys) to play since the last call, in order: what the clip pressed, and the release of every key it held when
    // the clip stops (a new move, the clip's end, turning to the foe, ReleaseKeys).
    std::vector<uint16_t> TakeKeys() { std::vector<uint16_t> k; k.swap(keys_); return k; }
    // Lets go of every key held: the fighter is down or leaves.
    void ReleaseKeys() {
        for (uint16_t k : held_) keys_.push_back(uint16_t(db_.keys()[k].partner));
        held_.clear();
    }
    bool NeedsLook() const { return !playing_ || sinceQueryMs_ >= kQueryMs; }
    // The mouse movement of the last kPrevMs, for Features.
    float PrevTurn() const { float v = 0; for (const Moved& m : recent_) v += m.turn; return v; }
    float PrevLook() const { float v = 0; for (const Moved& m : recent_) v += m.look; return v; }
    uint32_t buttons() const { return out_.buttons; }
    float guardR() const { return out_.axes[kGuardR]; }
    float guardL() const { return out_.axes[kGuardL]; }
    float yaw() const { return out_.yaw; }
    float pitch() const { return out_.pitch; }
    bool playing() const { return playing_; }
    bool facing() const { return facing_; }
    uint32_t cursor() const { return cursor_; }
    const Recall& recall() const { return recall_; }
    // This fight so far: how often it turned to face the foe first, and how many moves it was hurt playing (for the log).
    uint32_t turns() const { return turns_; }
    uint32_t hurtMoves() const { return hurtMoves_; }
    // Picks what to play from what the fighter sees now, minding what hurts and what it tried. The camera is eased towards
    // the row's bearing to the foe: the row's input then lands where it did for the player. A foe well off to the side or
    // behind, where the recording has nothing that fits or it hurts from, is turned to first (Step), the move picked after.
    void Look(const float* seen) {
        pain_.Add(clockMs_, seen);
        if (pain_.lost() >= kHurtMin && playing_) { recall_.Hurt(clockMs_); ++hurtMoves_; }
        const std::array<float, 2> pain = pain_.Now();
        const bool apart = std::hypot(seen[kFoeX], seen[kFoeX + 1]) > 0.5;
        const float now = FoeBearingDeg(seen);
        sinceQueryMs_ = 0;
        if (facing_) {
            if (apart && std::fabs(now) > kFacedDeg && clockMs_ - faceSinceMs_ < kFaceGiveUpMs) {
                fixDeg_ = now;
                if (trace_) { look_.weighed.Clear(db_.rows()); Noted(LookWhy::Facing, db_.rows(), db_.rows(), pain, seen); }
                return;
            }
            facing_ = false;
            noFaceUntilMs_ = clockMs_ + kFaceRestMs;
        }
        const uint32_t stay = playing_ ? db_.RowAhead(cursor_) : db_.rows();
        if (stay < db_.rows()) recall_.Picked(stay, clockMs_, true);   // how far the move playing got: going back is a repeat
        const uint32_t row = db_.Nearest(seen, stay, scan_, kScanRows, Mind{&pain, &recall_, clockMs_, trace_ ? &look_.weighed : nullptr});
        scan_ = uint32_t((uint64_t(scan_) + kScanRows) % std::max(db_.rows(), 1u));
        const float then = FoeBearingDeg(db_.row(row));
        const bool misfit = std::fabs(detail::Wrap(now - then)) > kMisfitDeg, hurtThere = pain[1] > 0.f;
        if (apart && std::fabs(now) > kFaceDeg && (misfit || hurtThere || (std::fabs(now) > kBehindDeg && FoeMetres(seen) <= kFaceNearM)) && clockMs_ >= noFaceUntilMs_) {
            facing_ = true; faceSinceMs_ = clockMs_; playing_ = false; fixDeg_ = now; ++turns_;
            ReleaseKeys();
            if (trace_) Noted(LookWhy::Facing, row, stay, pain, seen);
            return;
        }
        const bool on = playing_ && row == stay;
        if (trace_) Noted(on ? LookWhy::KeptPlaying : LookWhy::BestMatch, row, stay, pain, seen);
        recall_.Picked(row, clockMs_, on);
        fixDeg_ = apart ? std::clamp(detail::Wrap(now - then), -kMaxTurnFixDeg, kMaxTurnFixDeg) : 0.f;
        cursor_ = db_.start(row); playing_ = true; budgetMs_ = 0;
        // The move playing goes on: the presses of the few samples stepped over still play, in the next Step.
        if (!on) { ReleaseKeys(); keyAt_ = db_.PressFrom(cursor_); moveFrom_ = cursor_; }
    }
    // One game frame: the samples its time covers, mouse summed, the rest as of the last one.
    Output Step(float dtMs) {
        Output o = out_;
        o.axes[kTurn] = o.axes[kLook] = 0;
        sinceQueryMs_ += dtMs;
        clockMs_ += dtMs;
        if (facing_) {   // turning to the foe: the guard as it was, everything else let go
            for (size_t a = 0; a < kAxisCount; ++a) if (a != kGuardR && a != kGuardL) o.axes[a] = 0;
            o.buttons = 0;
            const float turn = std::clamp(fixDeg_, -kFaceTurnDegS * dtMs / 1000, kFaceTurnDegS * dtMs / 1000);
            o.yaw += turn;
            fixDeg_ -= turn;
        }
        const float share = facing_ ? 0.f : std::min(1.f, dtMs / kQueryMs);
        o.yaw += fixDeg_ * share;
        fixDeg_ -= fixDeg_ * share;
        budgetMs_ += dtMs;
        uint32_t last = db_.samples();
        for (uint32_t n = 0; playing_ && n < kMaxStepSamples && budgetMs_ >= db_.sample(cursor_).dtMs; ++n) {
            const Sample& s = db_.sample(cursor_);
            budgetMs_ -= s.dtMs;
            for (const auto& p = db_.presses(); keyAt_ < p.size() && p[keyAt_].sample <= cursor_; ++keyAt_) Press(p[keyAt_].key);
            for (size_t a = 0; a < kAxisCount; ++a) o.axes[a] = IsDeltaAxis(Axis(a)) ? o.axes[a] + s.axes[a] : s.axes[a];
            o.yaw += s.dyaw; o.pitch += s.dpitch; o.buttons = s.buttons;
            last = cursor_;
            if (s.end || ++cursor_ >= db_.samples()) { playing_ = false; ReleaseKeys(); }
        }
        if (last < db_.samples()) o.look = db_.Look(last);
        budgetMs_ = std::min(budgetMs_, kMaxDtMs);
        o.yaw = detail::Wrap(o.yaw);
        o.pitch = std::clamp(o.pitch, -80.f, 80.f);
        recent_.push_back({dtMs, o.axes[kTurn], o.axes[kLook]});
        float held = 0;
        for (const Moved& m : recent_) held += m.ms;
        while (held > kPrevMs && recent_.size() > 1) { held -= recent_.front().ms; recent_.pop_front(); }
        out_ = o;
        return o;
    }
private:
    static constexpr int kFoeX = 17;   // Features: the foe's pelvis, x then y
    static constexpr uint32_t kClipMsSamples = 1u << 14;   // ClipMs adds up no more than this (about 4 minutes)
    void Noted(LookWhy why, uint32_t row, uint32_t stay, const std::array<float, 2>& pain, const float* seen) {
        look_.why = why; look_.row = row; look_.stay = stay; look_.pain = pain;
        look_.foeMetres = FoeMetres(seen); look_.foeDeg = FoeBearingDeg(seen);
        ++look_.seq;
    }
    // A key of the clip: a release only if its press is held, a press held already not again.
    void Press(uint16_t k) {
        const Db::Key& key = db_.keys()[k];
        if (key.release) {
            const auto h = std::find(held_.begin(), held_.end(), uint16_t(key.partner));
            if (h == held_.end()) return;
            held_.erase(h);
        } else if (key.partner >= 0) {
            if (std::find(held_.begin(), held_.end(), k) != held_.end()) return;
            held_.push_back(k);
        }
        keys_.push_back(k);
    }
    struct Moved { float ms, turn, look; };
    const Db& db_;
    Output out_;
    bool playing_ = false, facing_ = false;
    uint32_t cursor_ = 0, scan_ = 0, moveFrom_ = 0;   // moveFrom_: the sample the move playing started at
    float budgetMs_ = 0, sinceQueryMs_ = 0, fixDeg_ = 0;
    float clockMs_ = 0, faceSinceMs_ = 0, noFaceUntilMs_ = 0;   // the fight's time, by Step
    bool trace_ = false;
    LookTrace look_;
    uint32_t turns_ = 0, hurtMoves_ = 0;
    std::deque<Moved> recent_;
    Pain pain_;
    Recall recall_;
    std::vector<uint16_t> held_, keys_;   // presses held; keys to play
    size_t keyAt_ = 0;                    // the clip's next press (Db::presses)
};

// One game frame of the player's input, as the file keeps it.
struct Played { float dtMs; std::array<float, kAxisCount> axes; uint32_t buttons; float yaw, pitch; };

// Builds the play blocks while a player fights: tools/hsmclone.py does the same from a recording.
class Collector {
public:
    // A frame of fighting. true: a row is due; the caller reads both fighters and calls Row.
    bool Add(const Played& p) {
        const float dt = have_ ? std::min(p.dtMs, kMaxDtMs) : kFirstDtMs;
        Sample s{dt, have_ ? detail::Wrap(p.yaw - last_.yaw) : 0.f, have_ ? detail::Wrap(p.pitch - last_.pitch) : 0.f, {}, p.buttons & kPlayable, 0};
        std::copy(p.axes.begin(), p.axes.end(), s.axes);
        samples_.push_back(s);
        last_ = p; have_ = true;
        recent_.push_back({dt, p.axes[kTurn], p.axes[kLook]});
        float held = 0;
        for (const Moved& m : recent_) held += m.ms;
        while (held > kPrevMs && recent_.size() > 1) { held -= recent_.front().ms; recent_.pop_front(); }
        return ++step_ % kRowEvery == 0;
    }
    // An event of the fight now, before the next sample: Take writes it after the play it belongs to.
    void Note(FightEvent e) {
        if (events_.size() >= kMaxEvents) return;
        e.sample = uint32_t(samples_.size());
        events_.push_back(e);
    }
    const std::vector<FightEvent>& pending() const { return events_; }
    // The sensitivity the game is set to. A change belongs to the next block: Take first, then this.
    void SetLook(LookScale l) { input_.look = l; }
    LookScale look() const { return input_.look; }
    // A key of something other than the fighter, pressed now, before the next sample (hsm_clone_input.h). false: not kept.
    bool NoteKey(const std::string& cls, const std::string& fn) { return input_.Press(uint32_t(samples_.size()), cls, fn); }
    // foeId: who the foe is, so that a new one's wounds are not taken for a hit. Damage and cuts since the row before are noted.
    void Row(const Seen& me, const Seen& foe, const sync::V3* loose, uint64_t foeId = 0) {
        float turn = 0, look = 0;
        for (const Moved& m : recent_) { turn += m.turn; look += m.look; }
        const size_t at = rows_.size();
        rows_.resize(at + kFeatures);
        Features(me, foe, loose, last_.yaw, last_.pitch, turn, look, last_.buttons, last_.axes[kGuardR], last_.axes[kGuardL], &rows_[at]);
        if (foeId != foeId_) { foeId_ = foeId; foeHurt_.Forget(); health_[kFoe].known = false; }
        foeHurt_.Row(*foe.vitals, &rows_[at + kFoeHit]); myHurt_.Row(*me.vitals, &rows_[at + kMyHit]);
        for (uint8_t who : {kMe, kFoe}) {
            const VitalsMsg& v = *(who == kMe ? me.vitals : foe.vitals);
            Health& h = health_[who];
            std::vector<FightEvent> got;
            if (h.known) HealthEvents(h.parts, v.partHealth, h.severed, v.severed, who, got);
            for (const FightEvent& e : got) Note(e);
            std::copy(v.partHealth.begin(), v.partHealth.begin() + kParts, h.parts.begin());
            h.severed = v.severed; h.known = true;
        }
        starts_.push_back(uint32_t(samples_.size()));   // its clip starts with the next sample
    }
    // Not fighting: the stretch of play ends here.
    void Break() {
        if (have_ && !samples_.empty()) samples_.back().end = 1;
        have_ = false; recent_.clear(); foeHurt_.Forget(); myHurt_.Forget(); health_[kMe].known = health_[kFoe].known = false;
    }
    uint32_t samples() const { return uint32_t(samples_.size()); }
    // The play so far as one block (empty: none), then its events in a block of their own; the collector starts over. A row with
    // no clip after it is left out, and so are events with no play to belong to.
    std::vector<uint8_t> Take() {
        Break();
        size_t rows = starts_.size();
        while (rows && starts_[rows - 1] >= samples_.size()) --rows;
        std::vector<uint8_t> p;
        if (rows) {
            p.reserve(8 + rows * (kFeatures * 4 + 4) + samples_.size() * sizeof(Sample));
            PutU32(p, uint32_t(rows)); PutU32(p, uint32_t(samples_.size()));
            for (size_t i = 0; i < rows; ++i) { PutBytes(p, &rows_[i * kFeatures], kFeatures * 4); PutU32(p, starts_[i]); }
            PutBytes(p, samples_.data(), samples_.size() * sizeof(Sample));
        }
        std::vector<uint8_t> out = rows ? EncodeBlock(Block::Play, p.data(), p.size()) : std::vector<uint8_t>{};
        if (rows) {   // even with none: then the play is known to have had none (Db::EventsKnown)
            std::vector<FightEvent> old, more, later;
            for (const FightEvent& e : events_) (LaterKind(e.kind) ? later : MoreKind(e.kind) ? more : old).push_back(e);
            for (const auto& [kind, list] : {std::pair{Block::Events, &old}, std::pair{Block::More, &more}}) {
                const std::vector<uint8_t> e = EventsPayload(*list);
                const std::vector<uint8_t> b = EncodeBlock(kind, e.data(), e.size());
                out.insert(out.end(), b.begin(), b.end());
            }
            // Always, so the play's sensitivity is known. A press after the last sample has no play to go with.
            std::erase_if(input_.presses, [&](const KeyPress& k) { return k.sample >= samples_.size(); });
            const std::vector<uint8_t> in = InputPayload(input_);
            const std::vector<uint8_t> b = EncodeBlock(Block::Input, in.data(), in.size());
            out.insert(out.end(), b.begin(), b.end());
            const std::vector<uint8_t> e = EventsPayload(later);   // last: a reader before pre12 passes over it
            const std::vector<uint8_t> l = EncodeBlock(Block::Later, e.data(), e.size());
            out.insert(out.end(), l.begin(), l.end());
        }
        rows_.clear(); starts_.clear(); samples_.clear(); events_.clear(); input_.Clear();
        return out;
    }
private:
    struct Moved { float ms, turn, look; };
    std::vector<float> rows_;
    std::vector<uint32_t> starts_;
    std::vector<Sample> samples_;
    std::deque<Moved> recent_;
    Hurt foeHurt_, myHurt_;
    struct Health { std::array<float, kParts> parts{}; uint32_t severed = 0; bool known = false; };
    std::array<Health, 2> health_{};   // kMe, kFoe: for the Damage and Sever events
    std::vector<FightEvent> events_;
    InputLog input_;
    uint64_t foeId_ = 0;
    Played last_{};
    bool have_ = false;
    uint32_t step_ = 0;
};

}  // namespace hsm::clone
