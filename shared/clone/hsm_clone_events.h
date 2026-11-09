// Fight events in a recording: who hit whom, with which hand, where, how hard; damage, cuts, parries, thrusts, kicks, grabs,
// pick-ups, falls and deaths. Each is stamped with the input sample it came before, so it lines up with the input stream and the
// rows exactly. They go in an Events block straight after the Play block they belong to (Collector::Take), so a file of an
// older Fetch, which has no such block, still loads, and an older Fetch skips the block. Kinds added since (Strike, Armour,
// Stage) go in a More block after it: a Fetch that does not know them drops a whole Events block with a kind it does not know in
// it, and skips a block kind it does not know. Pure logic: client/Fetch reads the game, docs/CLONE.md.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "hsm_clone_armor.h"

namespace hsm::clone {

enum class Ev : uint8_t {
    Hit = 1,      // who struck the other. hand: 0 R, 1 L (what that hand holds, fists too), kBody. part: where, or kNoPart. a: impulse
    Clash,        // my weapon met the foe's. hand: mine, part: the foe's hand. a: impulse
    Damage,       // who lost health. part: which (kParts order). a: how much (100 a whole part), b: what is left
    Sever,        // who lost a body part. part: the game's dismemberment bit
    ParryOn, ParryOff, Thrust, Kick,          // who, hand
    GrabOn,       // who, hand. part: the foe's body part held, or kNoPart for anything else. a: 1 the foe, 0 an object
    GrabOff,      // who, hand
    GrabbedOn, GrabbedOff,                    // who is held by the other
    PickUp, Drop, StuckOn, StuckOff,          // who, hand
    Fall, GetUp, Death,                       // who
    // In the More block:
    Strike,       // who struck the other where it hurt or met armour (Deal Complex Damage). hand, part: ArmourPart. a: the game's
                  // damage. b: PackStrike (the surface it met, how it went, how much of it was a stab)
    Armour,       // what who wears now, when it changes: a is ArmourView::Pack
    Stage,        // a training session's stage starts (who: kMe). part: its number, or kNoPart when the session ends. a, b: hsm_clone_drill.h
};
constexpr uint8_t kEvLast = uint8_t(Ev::Death);                                        // the Events block's kinds
constexpr uint8_t kEvFirstMore = uint8_t(Ev::Strike), kEvLastMore = uint8_t(Ev::Stage);  // the More block's
inline bool MoreKind(uint8_t kind) { return kind >= kEvFirstMore && kind <= kEvLastMore; }
constexpr uint8_t kMe = 0, kFoe = 1;
constexpr uint8_t kBody = 2, kNoHand = 0xFF, kNoPart = 0xFF;
constexpr float kDamageMin = 0.5f;           // smaller drops are regeneration noise, not a hit (as shared/record)
constexpr double kHitEveryMs = 100.0;        // one Hit per striker and hand this often at most: a blade resting on a body reports every step
constexpr uint32_t kMaxEvents = 4000000;     // a whole file's worth, far past what kMaxFile of play makes

inline const char* EvName(Ev e) {
    static const char* const names[] = {"?", "Hit", "Clash", "Damage", "Sever", "ParryOn", "ParryOff", "Thrust", "Kick", "GrabOn", "GrabOff",
                                        "GrabbedOn", "GrabbedOff", "PickUp", "Drop", "StuckOn", "StuckOff", "Fall", "GetUp", "Death",
                                        "Strike", "Armour", "Stage"};
    static_assert(sizeof(names) / sizeof(names[0]) == kEvLastMore + 1, "one name per event kind");
    return uint8_t(e) <= kEvLastMore ? names[uint8_t(e)] : "?";
}

// sample: the input sample it came before, counted in its Play block (the block's sample count: after the last one).
struct FightEvent { uint32_t sample = 0; uint8_t kind = 0, who = 0, hand = kNoHand, part = kNoPart; float a = 0, b = 0; };
static_assert(sizeof(FightEvent) == 16);
inline FightEvent MakeEvent(Ev kind, uint8_t who, uint8_t hand = kNoHand, uint8_t part = kNoPart, float a = 0, float b = 0) {
    FightEvent e; e.kind = uint8_t(kind); e.who = who; e.hand = hand; e.part = part; e.a = a; e.b = b;
    return e;
}

// A Strike's b: the surface the blow met (8 bits), how it went (2), its stab share in 16ths (4). A whole number a float holds exactly.
struct StrikeInfo { uint8_t surface = 0; StrikeClass how = StrikeClass::Bare; float stab = 0; };
inline float PackStrike(uint8_t surface, StrikeClass how, float stab) {
    const uint32_t s = uint32_t(std::lround(std::clamp(std::isfinite(stab) ? stab : 0.f, 0.f, 1.f) * 15.f));
    return float(uint32_t(surface) | uint32_t(how) << 8 | s << 10);
}
inline StrikeInfo UnpackStrike(float b) {
    const uint32_t v = std::isfinite(b) && b > 0 && b < 16384.f ? uint32_t(b) : 0;
    return {uint8_t(v & 0xFF), StrikeClass(v >> 8 & 3u), float(v >> 10 & 15u) / 15.f};
}

// A file's event is checked before it is kept: a shared file is untrusted. more: it is in a More block, which has the kinds
// added since (an Events block never has them, so that an older Fetch keeps reading it).
inline bool ValidEvent(const FightEvent& e, uint32_t samples, bool more = false) {
    if (e.kind < 1 || (more ? !MoreKind(e.kind) : e.kind > kEvLast) || e.who > kFoe || e.sample > samples) return false;
    if (!std::isfinite(e.a) || !std::isfinite(e.b) || std::fabs(e.a) > 1e9f || std::fabs(e.b) > 1e9f) return false;
    if (e.hand != kNoHand && e.hand > kBody) return false;
    return true;
}

// The body part (VitalsMsg::partHealth order: head, upper, lower, arm R, arm L, leg R, leg L) a bone of the fighter's skeleton is
// in, or kNoPart. Matched without case, by the start of the name, so fingers and twist bones go with their limb.
inline uint8_t PartOfBone(const std::string& bone) {
    std::string b;
    for (char c : bone) b.push_back(char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c));
    auto starts = [&](const char* p) { return b.rfind(p, 0) == 0; };
    auto side = [&](uint8_t right, uint8_t left) -> uint8_t {
        const size_t n = b.size();
        if (n >= 2 && b[n - 2] == '_' && b[n - 1] == 'r') return right;
        if (n >= 2 && b[n - 2] == '_' && b[n - 1] == 'l') return left;
        if (b.find("_r_") != std::string::npos) return right;
        if (b.find("_l_") != std::string::npos) return left;
        return kNoPart;
    };
    if (starts("head") || starts("neck")) return 0;
    if (starts("spine_03") || starts("spine_04") || starts("spine_05") || starts("clavicle")) return 1;
    if (starts("pelvis") || starts("spine_01") || starts("spine_02") || starts("root")) return 2;
    if (starts("upperarm") || starts("lowerarm") || starts("hand") || starts("thumb") || starts("index") || starts("middle") ||
        starts("ring") || starts("pinky")) return side(3, 4);
    if (starts("thigh") || starts("calf") || starts("foot") || starts("ball")) return side(5, 6);
    return kNoPart;
}

// What each fighter is doing, from the fighter's own flags. The edges between two reads are events.
struct Doing {
    std::array<bool, 2> parry{}, thrust{}, kick{}, stuck{}, grab{}, holding{};
    std::array<bool, 2> grabFoe{};                 // that hand holds the other fighter
    std::array<uint8_t, 2> grabPart{kNoPart, kNoPart};   // which part of them
    bool grabbed = false, fallen = false, dead = false;
};

inline void DoingEvents(const Doing& was, const Doing& now, uint8_t who, std::vector<FightEvent>& out) {
    for (uint8_t h = 0; h < 2; ++h) {
        if (now.parry[h] != was.parry[h]) out.push_back(MakeEvent(now.parry[h] ? Ev::ParryOn : Ev::ParryOff, who, h));
        if (now.thrust[h] && !was.thrust[h]) out.push_back(MakeEvent(Ev::Thrust, who, h));
        if (now.kick[h] && !was.kick[h]) out.push_back(MakeEvent(Ev::Kick, who, h));
        if (now.stuck[h] != was.stuck[h]) out.push_back(MakeEvent(now.stuck[h] ? Ev::StuckOn : Ev::StuckOff, who, h));
        const bool sameGrab = was.grab[h] && now.grab[h] && was.grabFoe[h] == now.grabFoe[h] && was.grabPart[h] == now.grabPart[h];
        if (was.grab[h] && !sameGrab) out.push_back(MakeEvent(Ev::GrabOff, who, h));
        if (now.grab[h] && !sameGrab) out.push_back(MakeEvent(Ev::GrabOn, who, h, now.grabFoe[h] ? now.grabPart[h] : kNoPart, now.grabFoe[h] ? 1.f : 0.f));
        if (was.holding[h] && !now.holding[h]) out.push_back(MakeEvent(Ev::Drop, who, h));
        if (now.holding[h] && !was.holding[h]) out.push_back(MakeEvent(Ev::PickUp, who, h));
    }
    if (now.grabbed != was.grabbed) out.push_back(MakeEvent(now.grabbed ? Ev::GrabbedOn : Ev::GrabbedOff, who));
    if (now.fallen != was.fallen) out.push_back(MakeEvent(now.fallen ? Ev::Fall : Ev::GetUp, who));
    if (now.dead && !was.dead) out.push_back(MakeEvent(Ev::Death, who));
}

// Damage and cuts between two health reads of one fighter: one Damage per part that fell by kDamageMin or more, one Sever per
// newly cut part. A heal, or a cut that went away (a new body), is no event.
template <class Parts>
inline void HealthEvents(const Parts& was, const Parts& now, uint32_t wasSevered, uint32_t nowSevered, uint8_t who, std::vector<FightEvent>& out) {
    for (size_t i = 0; i < was.size() && i < now.size(); ++i) {
        const float drop = float(was[i]) - float(now[i]);
        if (std::isfinite(drop) && drop >= kDamageMin) out.push_back(MakeEvent(Ev::Damage, who, kNoHand, uint8_t(i), drop, float(now[i])));
    }
    const uint32_t added = nowSevered & ~wasSevered;
    for (uint8_t b = 0; b < 32; ++b) if (added >> b & 1u) out.push_back(MakeEvent(Ev::Sever, who, kNoHand, b));
}

// One Hit per striker and hand per kHitEveryMs: a contact reaches both actors' hit events and repeats every physics step.
class HitGate {
public:
    bool Allow(uint8_t who, uint8_t hand, double nowMs) {
        double& last = last_[size_t(who & 1) * 3 + (hand > kBody ? kBody : hand)];
        if (last > 0 && nowMs - last < kHitEveryMs && nowMs >= last) return false;
        last = nowMs;
        return true;
    }
    void Forget() { last_.fill(0); }
private:
    std::array<double, 6> last_{};
};

// A clash is between two weapons, whichever side's event reported it.
class ClashGate {
public:
    bool Allow(uint8_t myHand, uint8_t foeHand, double nowMs) {
        double& last = last_[size_t(myHand > 1 ? 2 : myHand) * 3 + (foeHand > 1 ? 2 : foeHand)];
        if (last > 0 && nowMs - last < kHitEveryMs && nowMs >= last) return false;
        last = nowMs;
        return true;
    }
    void Forget() { last_.fill(0); }
private:
    std::array<double, 9> last_{};
};

// What happened in a stretch of fighting, for the player to see (the Fetch menu).
struct Tally {
    uint32_t landed = 0, taken = 0, clashes = 0, parries = 0, thrusts = 0, kicks = 0, grabs = 0, falls = 0, kills = 0, deaths = 0;
    uint32_t gaps = 0, bare = 0, beat = 0, held = 0, gapsTaken = 0;   // my strikes by how they went; the foe's that found my gaps
    float dealt = 0, suffered = 0;   // health points, 100 a whole part
    void Count(const FightEvent& e) {
        const bool me = e.who == kMe;
        switch (Ev(e.kind)) {
            case Ev::Hit: ++(me ? landed : taken); break;
            case Ev::Clash: ++clashes; break;
            case Ev::Damage: (me ? suffered : dealt) += e.a; break;
            case Ev::ParryOn: if (me) ++parries; break;
            case Ev::Thrust: if (me) ++thrusts; break;
            case Ev::Kick: if (me) ++kicks; break;
            case Ev::GrabOn: if (me && e.a > 0) ++grabs; break;
            case Ev::Fall: if (me) ++falls; break;
            case Ev::Death: ++(me ? deaths : kills); break;
            case Ev::Strike: {
                const StrikeClass how = UnpackStrike(e.b).how;
                if (!me) { gapsTaken += how == StrikeClass::Gap; break; }
                ++(how == StrikeClass::Gap ? gaps : how == StrikeClass::Bare ? bare : how == StrikeClass::Beat ? beat : held);
                break;
            }
            default: break;
        }
    }
};

// What the fight's events tell a fighter that a look at the two bodies does not, as the clone's nets hear it: how long since a
// blow landed either way, a clash, the foe's parry, thrust or kick, and what holds now (held, holding the foe, down, stuck). The
// trainer reads it from a recording's events and the clone from its own fight, the same way: Add each event as it comes, Age
// by the time that passes. Unknown (a recording from before events, kEventsUnknown): everything reads "nothing happened", and
// the last input says it is not known.
constexpr int kEventInputs = 13;
class EventSense {
public:
    explicit EventSense(bool known = false) { Reset(known); }
    void Reset(bool known) {
        for (float& s : since_) s = kLongAgo;
        grabbed_ = false; foeParry_ = {}; holding_ = {}; stuck_ = {}; down_ = {};
        known_ = known;
    }
    bool known() const { return known_; }
    void Age(float ms) { if (std::isfinite(ms) && ms > 0) for (float& s : since_) s = std::min(s + ms, kLongAgo); }
    void Add(const FightEvent& e) {
        if (!known_ || e.who > kFoe) return;
        const bool me = e.who == kMe, hand = e.hand < 2;
        switch (Ev(e.kind)) {
            case Ev::Hit: since_[me ? kLanded : kTaken] = 0; break;
            case Ev::Clash: since_[kClash] = 0; break;
            case Ev::ParryOn: if (!me) { since_[kFoeParry] = 0; if (hand) foeParry_[e.hand] = true; } break;
            case Ev::ParryOff: if (!me && hand) foeParry_[e.hand] = false; break;
            case Ev::Thrust: if (!me) since_[kFoeThrust] = 0; break;
            case Ev::Kick: if (!me) since_[kFoeKick] = 0; break;
            case Ev::GrabOn: if (me && hand) holding_[e.hand] = e.a > 0; break;
            case Ev::GrabOff: if (me && hand) holding_[e.hand] = false; break;
            case Ev::GrabbedOn: if (me) grabbed_ = true; break;
            case Ev::GrabbedOff: if (me) grabbed_ = false; break;
            case Ev::StuckOn: if (me && hand) stuck_[e.hand] = true; break;
            case Ev::StuckOff: if (me && hand) stuck_[e.hand] = false; break;
            case Ev::Fall: down_[e.who] = true; break;
            case Ev::GetUp: down_[e.who] = false; break;
            default: break;
        }
    }
    void Fill(float* o) const {
        for (int i = 0; i < kTimes; ++i) o[i] = std::log1p(since_[i] / 100.f) / kLongAgoLog;   // 0 now, 1 long ago
        o[6] = grabbed_; o[7] = holding_[0] || holding_[1]; o[8] = down_[kMe]; o[9] = down_[kFoe];
        o[10] = stuck_[0] || stuck_[1]; o[11] = foeParry_[0] || foeParry_[1]; o[12] = known_;
    }
private:
    enum { kLanded, kTaken, kClash, kFoeParry, kFoeThrust, kFoeKick, kTimes };
    static constexpr float kLongAgo = 3000.f;
    static inline const float kLongAgoLog = std::log1p(kLongAgo / 100.f);
    float since_[kTimes];
    bool grabbed_ = false, known_ = false;
    std::array<bool, 2> foeParry_{}, holding_{}, stuck_{}, down_{};
};

// How well what a fighter did came out, from the events after it: blows landed, damage dealt and kills count for it, blows
// taken, damage suffered and its own death against it, the sooner the more. Training leans on moves by this (a landed hit is
// good). 0: nothing came of it.
constexpr float kOutcomeAheadMs = 1500.f, kOutcomeHalfMs = 500.f, kGapOutcome = .5f, kHeldOutcome = .25f;
inline float OutcomeOf(const FightEvent& e) {
    const bool me = e.who == kMe;
    switch (Ev(e.kind)) {
        case Ev::Hit: return me ? 1.f : -1.f;
        case Ev::Damage: return std::min(e.a, 100.f) / 40.f * (me ? -1.f : 1.f);   // who lost it: a whole part is 2.5 blows
        case Ev::Death: return me ? -3.f : 3.f;
        // On top of its Hit: a blow that found a gap is what a fight in armour is won by, one that bounced off plate was wasted.
        case Ev::Strike: {
            const StrikeClass how = UnpackStrike(e.b).how;
            if (me) return how == StrikeClass::Gap ? kGapOutcome : how == StrikeClass::Held ? -kHeldOutcome : 0.f;
            return how == StrikeClass::Gap ? -kGapOutcome : 0.f;
        }
        default: return 0.f;
    }
}
// good and bad of one event `ms` after the move, weighed by how soon.
inline void AddOutcome(const FightEvent& e, float ms, float& good, float& bad) {
    if (!(ms >= 0) || ms > kOutcomeAheadMs) return;
    const float v = OutcomeOf(e) * std::exp2(-ms / kOutcomeHalfMs);
    if (v > 0) good += v; else bad -= v;
}
// A move's weight in training from its good and bad: 1 when nothing came of it. strength 0: always 1.
constexpr float kOutcomeGood = .6f, kOutcomeBad = .4f, kOutcomeMin = .4f, kOutcomeMax = 3.f;
inline float OutcomeWeight(float good, float bad, float strength) {
    const float w = 1.f + strength * (kOutcomeGood * good - kOutcomeBad * bad);
    return std::isfinite(w) ? std::clamp(w, kOutcomeMin, kOutcomeMax) : 1.f;
}

// The Events block: u32 count, then the events as they are in memory (16 bytes, little-endian on every machine this runs on).
inline std::vector<uint8_t> EventsPayload(const std::vector<FightEvent>& events) {
    std::vector<uint8_t> o(4 + events.size() * sizeof(FightEvent));
    const uint32_t n = uint32_t(events.size());
    for (int i = 0; i < 4; ++i) o[size_t(i)] = uint8_t(n >> (8 * i));
    if (!events.empty()) std::memcpy(o.data() + 4, events.data(), events.size() * sizeof(FightEvent));
    return o;
}

// false: the block is not well formed, or an event in it is not valid for a Play block of `samples` samples. Then none of it
// is kept: a file is someone else's, and half a block says nothing reliable.
inline bool ReadEventsPayload(const uint8_t* d, size_t len, uint32_t samples, std::vector<FightEvent>& out, bool more = false) {
    if (len < 4) return false;
    uint32_t n;
    std::memcpy(&n, d, 4);
    if (n > kMaxEvents || len != 4 + size_t(n) * sizeof(FightEvent)) return false;
    std::vector<FightEvent> got(n);
    if (n) std::memcpy(got.data(), d + 4, size_t(n) * sizeof(FightEvent));
    uint32_t last = 0;
    for (const FightEvent& e : got) {
        if (!ValidEvent(e, samples, more) || e.sample < last) return false;   // in time order, as the recorder writes them
        last = e.sample;
    }
    out = std::move(got);
    return true;
}

}  // namespace hsm::clone
