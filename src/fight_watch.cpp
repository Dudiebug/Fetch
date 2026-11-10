#include "fight_watch.h"
#include "gameplay/adapter.h"
#include "log.h"
#include "ue/script_hook.h"
#include "ue/hook.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace hsm::game {
using namespace hsm::ue;

namespace {
// The Willie_BP_C fields the fight events come from (re/out/bp/Character/Blueprints/Willie_BP.txt): FightWatch::Flags order.
constexpr const char* kWillieBoolFields[] = {"Grabbed R", "Grabbed L", "R_Thrusting", "L_Thrusting", "Kicking  R", "Kicking  L",
                                             "Parrying R", "Parrying L", "R Hand Stuck", "L Hand Stuck", "Being Grabbed", "Fallen", "DED"};
constexpr const char* kGrabComponent[2] = {"Grab Component R", "Grab Component L"};
constexpr const char* kGrabBone[2] = {"Grab Bone R", "Grab Bone L"};
constexpr const char* kWeapon[2] = {"Weapon R", "Weapon L"};
constexpr const char* kFistsPrefix = "Weapon_Fists";   // an empty hand's pseudo weapon: it strikes, but holds nothing

double SteadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

constexpr const char* kStrikeClass = "Willie_BP_C";
constexpr const char* kStrikeFunction = "Deal Complex Damage";
constexpr const char* kStrikeOwner = "fetch-strike";
// RT-Damage is set at the end of Deal Complex Damage's body only (Willie_BP.cpp:3229), so a call that left early leaves it as
// it was, and its outputs stale. Before each call it is set to this; still this after means the body left early.
constexpr double kNotRun = -1e30;
constexpr double kStrikeGapMs = 150;     // calls of one striking component on one fighter this close together are one blow
constexpr double kReadArmourMs = 2000;
constexpr int kStrikeLines = 12;         // strikes logged in a level, for a tester
constexpr size_t kOpenStrikes = 32;
// The game's parry (ModularWeaponBP's weapon hit, ModularWeaponBP.cpp:914): a blade that meets the weapon of a fighter who guards,
// wins the weapon-skill roll and is not falling or out on their feet adds to its own Just Parried Rate (a full one stops its cut),
// and then calls StopThatBlade on the fighter holding it. Blueprint to Blueprint, so it is watched in the script VM.
constexpr const char* kParryFunction = "StopThatBlade";
constexpr const char* kParryOwner = "fetch-parry";
constexpr const char* kParryRate = "Just Parried Rate";
constexpr double kParryGapMs = 150;      // catches of one blade this close together are one parry
constexpr int kParryLines = 12;          // parries logged in a level, for a tester
}  // namespace

// One Deal Complex Damage observer for every FightWatch (the recorder's and the clone's): two would each set and read the
// RT-Damage mark and see the other's.
struct StrikeHub {
    static bool& on() { static bool v = true; return v; }
    static bool& installed() { static bool v = false; return v; }
    static std::vector<FightWatch*>& watches() { static std::vector<FightWatch*> v; return v; }
    struct Mark { UObject* self; double was; double* at; };
    static std::vector<Mark>& marks() { static std::vector<Mark> v; return v; }
    static double* RtDamage(UObject* w) {
        static UClass* cls = nullptr;
        static int32_t off = -1;
        UClass* c = ClassOf(w);
        if (!c) return nullptr;
        if (c != cls) {
            cls = c;
            FProperty* p = FindProperty(c, "RT-Damage");
            off = p && PropertyType(p) == "DoubleProperty" ? PropertyOffset(p) : -1;
            if (off < 0) HSM_LOG("clone: strikes: %s has no RT-Damage; a blow's outputs are taken as they are", ObjectName(reinterpret_cast<UObject*>(c)).c_str());
        }
        return off >= 0 ? reinterpret_cast<double*>(reinterpret_cast<uint8_t*>(w) + off) : nullptr;
    }
    static void Add(FightWatch* w) {
        watches().push_back(w);
        if (installed() || !on()) return;
        installed() = true;
        if (!InstallScriptHook()) { HSM_LOG("clone: strikes: the script VM hook is not available on this build; no Strike events"); return; }
        const int n = ObserveScript(kStrikeOwner, kStrikeClass, kStrikeFunction, Before, After);
        HSM_LOG("clone: strikes: watching %s (%d function%s)", kStrikeFunction, n, n == 1 ? "" : "s");
    }
    static void Before(ScriptCall& call) {
        UObject* self = call.self();
        double* at = self ? RtDamage(self) : nullptr;
        if (marks().size() < 64) marks().push_back({self, at ? *at : 0.0, at});
        if (at) *at = kNotRun;
        for (FightWatch* w : watches()) w->StrikeBefore(call, 0);
    }
    static void After(ScriptCall& call) {
        bool ran = true;
        UObject* self = call.self();
        while (!marks().empty()) {   // this call's mark (one whose after never came goes with it)
            const Mark m = marks().back();
            marks().pop_back();
            if (m.self != self) continue;
            if (m.at) { ran = *m.at != kNotRun; if (!ran) *m.at = m.was; }
            break;
        }
        uint8_t surface = 0;
        double damage = 0, cut = 0;
        if (ran) { call.get("Hit Surface", surface); call.get("Damage Out", damage); call.get("Cutting Rate Out", cut); }
        for (FightWatch* w : watches()) w->StrikeAfter(call, ran, surface, damage, cut);
    }
};

// One StopThatBlade observer for every FightWatch, like the strike watch.
struct ParryHub {
    static bool& on() { static bool v = true; return v; }
    static bool& installed() { static bool v = false; return v; }
    static std::vector<FightWatch*>& watches() { static std::vector<FightWatch*> v; return v; }
    static void Add(FightWatch* w) {
        watches().push_back(w);
        if (installed() || !on()) return;
        installed() = true;
        if (!InstallScriptHook()) { HSM_LOG("clone: parries: the script VM hook is not available on this build; no Parried events"); return; }
        const int n = ObserveScript(kParryOwner, kStrikeClass, kParryFunction, Caught);
        HSM_LOG("clone: parries: watching %s (%d function%s)", kParryFunction, n, n == 1 ? "" : "s");
    }
    static void Caught(ScriptCall& call) {
        double power = 0;
        call.get("Power", power);
        for (FightWatch* w : watches()) w->Caught(call.self(), power);
    }
};

void FightWatch::SetStrikes(bool on) { StrikeHub::on() = on; }
void FightWatch::SetParries(bool on) { ParryHub::on() = on; }
void FightWatch::LevelLoaded() {
    StrikeHub::marks().clear();
    if ((StrikeHub::installed() || ParryHub::installed()) && ScriptHookActive()) ResolveScriptHooks();
}

// A held weapon's Just Parried Rate (0 to 1), or -1 when it has none (fists, an older build).
static double ParryRate(UObject* weapon) {
    static UClass* cls = nullptr;
    static int32_t off = -1;
    UClass* c = weapon ? ClassOf(weapon) : nullptr;
    if (!c) return -1;
    if (c != cls) {
        cls = c;
        FProperty* p = FindProperty(c, kParryRate);
        off = p && PropertyType(p) == "DoubleProperty" ? PropertyOffset(p) : -1;
    }
    if (off < 0) return -1;
    double r = 0;
    std::memcpy(&r, reinterpret_cast<const uint8_t*>(weapon) + off, sizeof r);
    return std::isfinite(r) ? std::clamp(r, 0.0, 1.0) : -1;
}

// StopThatBlade on self: one of the blades it holds was caught. Which one: the one whose parry is fullest, as the game just
// added to it. Catches a few ms apart fold into one parry, kept at its fullest and fastest.
void FightWatch::Caught(UObject* self, double power) {
    uint8_t s = 2;
    for (uint8_t i = 0; i < 2; ++i) if (self && self == side_[i].willie) s = i;
    if (s > 1 || !live_()) return;
    uint8_t hand = clone::kNoPart;
    double rate = -1;
    for (uint8_t h = 0; h < 2; ++h) {
        const double r = ParryRate(side_[s].weapon[h]);
        if (r > rate) { rate = r; hand = h; }
    }
    const double now = SteadyMs();
    OpenParry* o = nullptr;
    for (OpenParry& p : parries_) if (p.caught == s && p.hand == hand && now - p.lastMs < kParryGapMs) o = &p;
    if (!o) {
        if (parries_.size() >= 8) FlushParries(now + 1);
        parries_.push_back({});
        o = &parries_.back();
        o->caught = s; o->hand = hand;
    }
    o->lastMs = now; ++o->catches;
    o->power = std::max(o->power, std::isfinite(power) ? float(std::clamp(power, 0.0, 1e6)) : 0.f);
    o->rate = std::max(o->rate, float(std::max(rate, 0.0)));
}

// Parries with no catch for a while are over: each is a Parried event for the side that caught the blade.
void FightWatch::FlushParries(double olderThanMs) {
    for (size_t i = 0; i < parries_.size();) {
        const OpenParry o = parries_[i];
        if (o.lastMs >= olderThanMs) { ++i; continue; }
        parries_.erase(parries_.begin() + std::ptrdiff_t(i));
        const uint8_t by = uint8_t(1 - o.caught);
        sink_(clone::MakeEvent(clone::Ev::Parried, by, clone::kNoHand, o.hand, o.power, o.rate));
        if (parryLines_ < kParryLines) {
            ++parryLines_;
            HSM_LOG("clone: parry: %s caught the %s blade (%s hand) speed=%.0f full=%.2f catches=%d", by == clone::kMe ? "me" : "foe",
                    by == clone::kMe ? "foe's" : "my", o.hand == 0 ? "R" : o.hand == 1 ? "L" : "?", double(o.power), double(o.rate), o.catches);
        }
    }
}

// The striker's hand from the component that struck: its actor is the other side's body or one of its held weapons.
bool FightWatch::StrikerOf(UObject* collider, uint8_t victimSide, uint8_t& hand) const {
    const uint8_t s = uint8_t(1 - victimSide);
    UObject* actor = collider ? OuterOf(collider) : nullptr;
    if (!actor || !side_[s].willie) return false;
    if (actor == side_[s].willie) { hand = clone::kBody; return true; }
    for (uint8_t h = 0; h < 2; ++h) if (actor == side_[s].weapon[h]) { hand = h; return true; }
    Call owner(actor, "GetOwner");   // a foot weapon or another part the fighter spawned for itself
    if (owner.valid() && owner.invoke() && owner.get<UObject*>("ReturnValue") == side_[s].willie) { hand = clone::kBody; return true; }
    return false;
}

void FightWatch::StrikeBefore(ScriptCall& call, double) {
    Asked a;
    a.victim = call.self();
    uint8_t v = 2;
    for (uint8_t s = 0; s < 2; ++s) if (a.victim && a.victim == side_[s].willie) v = s;
    if (v < 2 && live_()) {
        call.get("Collided Component", a.collider);
        uint8_t hand = 0;
        if (StrikerOf(a.collider, v, hand)) {
            a.ours = true; a.who = uint8_t(1 - v); a.hand = hand;
            FName bone{};
            if (call.get("Hit Bone", bone)) a.part = clone::ArmourPartOfBone(NameToString(bone));
            double stab = 0;
            if (call.get("Stab Rate", stab) && std::isfinite(stab)) a.stab = float(std::clamp(stab, 0.0, 1.0));
        }
    }
    if (asked_.size() < 64) asked_.push_back(a);
}

void FightWatch::StrikeAfter(ScriptCall& call, bool ran, uint8_t surface, double damage, double cut) {
    Asked a;
    bool found = false;
    while (!asked_.empty()) { a = asked_.back(); asked_.pop_back(); if (a.victim == call.self()) { found = true; break; } }
    if (!found || !a.ours) return;
    const double now = SteadyMs();
    OpenStrike* o = nullptr;
    for (OpenStrike& s : strikes_) if (s.victim == a.victim && s.collider == a.collider && now - s.lastMs < kStrikeGapMs) o = &s;
    if (!o) {
        if (strikes_.size() >= kOpenStrikes) FlushStrikes(now + 1);
        strikes_.push_back({});
        o = &strikes_.back();
        o->victim = a.victim; o->collider = a.collider; o->who = a.who; o->hand = a.hand;
    }
    o->lastMs = now;
    o->fold.Add(ran, a.part, a.stab, surface, damage, cut);   // a call another mod blocked before its body is a contact only
}

// Blows with no call for a while are over: each is a Strike, classed by what its victim wore.
void FightWatch::FlushStrikes(double olderThanMs) {
    for (size_t i = 0; i < strikes_.size();) {
        const OpenStrike s = strikes_[i];
        if (s.lastMs >= olderThanMs) { ++i; continue; }
        strikes_.erase(strikes_.begin() + std::ptrdiff_t(i));
        const clone::BlowFold& o = s.fold;
        if (!o.strikes()) continue;   // every call left early, or was blocked: the body never weighed it
        const uint8_t victim = uint8_t(1 - s.who);
        const clone::ArmourView& wears = armour_[victim].view;   // bare until read
        const clone::StrikeClass how = clone::ClassifyStrike(o.surface, o.damage, o.cut, o.part, wears);
        sink_(clone::MakeEvent(clone::Ev::Strike, s.who, s.hand, o.part, o.damage, clone::PackStrike(o.surface, how, o.stab)));
        if (strikeLines_ < kStrikeLines) {
            ++strikeLines_;
            HSM_LOG("clone: strike: %s hand=%s part=%s surface=%u damage=%.2f cut=%.2f stab=%.2f calls=%d/%d -> %s%s",
                    s.who == clone::kMe ? "me" : "foe", s.hand == clone::kBody ? "body" : s.hand == 0 ? "R" : "L",
                    clone::ArmourPartName(o.part), unsigned(o.surface), double(o.damage), double(o.cut), double(o.stab), o.ran, o.contacts,
                    clone::StrikeClassName(how), armour_[victim].known ? "" : " (armour not read yet)");
        }
    }
}

// What both wear, every couple of seconds (at once for a new fighter): an Armour event and a log line when it changes.
void FightWatch::ReadArmour(Adapter& adapter, UObject* me, UObject* foe) {
    const double now = SteadyMs();
    UObject* who[2] = {me, foe};
    bool changed = false;
    for (uint8_t s = 0; s < 2; ++s)
        if (armour_[s].willie != who[s]) { armour_[s] = ArmourSide{}; armour_[s].willie = who[s]; changed = true; }
    if (!changed && now < nextArmourMs_) return;
    nextArmourMs_ = now + kReadArmourMs;
    std::vector<clone::ArmourPiece> pieces;
    for (uint8_t s = 0; s < 2; ++s) {
        ArmourSide& a = armour_[s];
        if (!a.willie || !adapter.ReadArmour(a.willie, pieces)) { a.known = false; continue; }
        const clone::ArmourView v = clone::ViewOf(pieces);
        const bool fresh = !a.known || !(v == a.view);
        a.view = v; a.known = true;
        clone::Cover7(v, a.cover);
        if (!fresh) continue;
        HSM_LOG("clone: armour: %s %s (%zu piece%s)", s == clone::kMe ? "me" : "foe", clone::Describe(v).c_str(), pieces.size(),
                pieces.size() == 1 ? "" : "s");
        if (live_()) sink_(clone::MakeEvent(clone::Ev::Armour, s, clone::kNoHand, clone::kNoPart, float(v.Pack())));
    }
}

bool FightWatch::Flags::Bind(UObject* willie) {
    UClass* c = willie ? ClassOf(willie) : nullptr;
    if (!c) return false;
    if (c == cls) return true;
    static_assert(sizeof(kWillieBoolFields) / sizeof(kWillieBoolFields[0]) == kCount, "one field per flag");
    *this = Flags{};
    cls = c;
    std::string missing;
    for (int i = 0; i < kCount; ++i) {
        FProperty* p = FindProperty(c, kWillieBoolFields[i]);
        bools[i] = p && PropertyType(p) == "BoolProperty" ? p : nullptr;
        if (!bools[i]) missing += std::string(missing.empty() ? "" : ", ") + kWillieBoolFields[i];
    }
    auto offset = [&](const char* name, const char* type) {
        FProperty* p = FindProperty(c, name);
        if (p && PropertyType(p) == type) return PropertyOffset(p);
        missing += std::string(missing.empty() ? "" : ", ") + name;
        return int32_t(-1);
    };
    for (int h = 0; h < 2; ++h) {
        grabComp[h] = offset(kGrabComponent[h], "ObjectProperty");
        grabBone[h] = offset(kGrabBone[h], "NameProperty");
        weapon[h] = offset(kWeapon[h], "ObjectProperty");
    }
    if (!missing.empty())
        HSM_LOG("clone: events: %s has no %s; those events are not recorded", ObjectName(reinterpret_cast<UObject*>(c)).c_str(), missing.c_str());
    return true;
}

clone::Doing FightWatch::Flags::Read(UObject* willie, UObject* other, UObject* (&held)[2]) const {
    clone::Doing d;
    const uint8_t* base = reinterpret_cast<const uint8_t*>(willie);
    auto flag = [&](int i) { bool v = false; return bools[i] && GetBoolValue(bools[i], base + PropertyOffset(bools[i]), v) && v; };
    auto object = [&](int32_t off) { return off >= 0 ? *reinterpret_cast<UObject* const*>(base + off) : nullptr; };
    for (int h = 0; h < 2; ++h) {
        d.grab[h] = flag(kGrabR + h);
        d.thrust[h] = flag(kThrustR + h);
        d.kick[h] = flag(kKickR + h);
        d.parry[h] = flag(kParryR + h);
        d.stuck[h] = flag(kStuckR + h);
        if (d.grab[h]) {   // a grip on the other fighter: which part of them (a component's outer is its actor)
            UObject* comp = object(grabComp[h]);
            d.grabFoe[h] = comp && other && OuterOf(comp) == other;
            if (d.grabFoe[h] && grabBone[h] >= 0) d.grabPart[h] = clone::PartOfBone(NameToString(*reinterpret_cast<const FName*>(base + grabBone[h])));
        }
        held[h] = object(weapon[h]);
        d.holding[h] = held[h] && ObjectName(reinterpret_cast<UObject*>(ClassOf(held[h]))).rfind(kFistsPrefix, 0) != 0;
    }
    d.grabbed = flag(kBeingGrabbed);
    d.fallen = flag(kFallen);
    d.dead = flag(kDed);
    return d;
}

// Both fighters' flags once a tick: their edges are events. A fighter seen for the first time (a new foe, a new body) has no
// edges yet.
void FightWatch::Read(UObject* me, UObject* foe) {
    FlushStrikes(SteadyMs() - kStrikeGapMs);
    FlushParries(SteadyMs() - kParryGapMs);
    UObject* who[2] = {me, foe};
    for (uint8_t s = 0; s < 2; ++s) {
        Side& side = side_[s];
        if (side.willie != who[s]) { side.willie = who[s]; side.known = false; }
        if (!side.flags.Bind(side.willie)) { side.known = false; continue; }
        const clone::Doing now = side.flags.Read(side.willie, who[1 - s], side.weapon);
        if (side.known) {
            std::vector<clone::FightEvent> got;
            clone::DoingEvents(side.was, now, s, got);
            for (const clone::FightEvent& e : got) sink_(e);
        }
        side.was = now;
        side.known = true;
    }
}

void FightWatch::Forget() {
    for (Side& s : side_) { s.known = false; s.willie = nullptr; s.weapon[0] = s.weapon[1] = nullptr; }
    strikes_.clear(); asked_.clear(); parries_.clear();
    for (ArmourSide& a : armour_) a = ArmourSide{};
}

void FightWatch::ForgetLevel() {
    Forget();
    hitParams_.clear(); hitGate_.Forget(); clashGate_.Forget();
    strikeLines_ = parryLines_ = 0;
}

void FightWatch::Hook() {
    if (hooked_) return;
    hooked_ = true;
    StrikeHub::Add(this);
    ParryHub::Add(this);
    HookFunctionsMatching([](const std::string& n) { return n.find("ComponentHitSignature") != std::string::npos; },
                          [this](UObject* obj, UFunction* fn, void* params) { OnHit(obj, fn, params); return false; });
}

const FightWatch::HitParams& FightWatch::ParamsOf(UFunction* fn) {
    auto it = hitParams_.find(fn);
    if (it != hitParams_.end()) return it->second;
    if (!boneOffRead_) {
        boneOffRead_ = true;
        UObject* hitResult = FindStruct("HitResult");
        const FProperty* bone = StructMember(hitResult, "BoneName");
        const FProperty* myBone = StructMember(hitResult, "MyBoneName");
        boneOff_ = bone ? PropertyOffset(bone) : -1;
        myBoneOff_ = myBone ? PropertyOffset(myBone) : -1;
        if (boneOff_ < 0 || myBoneOff_ < 0) HSM_LOG("clone: events: FHitResult has no BoneName; hits are recorded without where they landed");
    }
    HitParams h;
    if (FProperty* p = FindProperty(fn, "OtherActor"); p && PropertyType(p) == "ObjectProperty") h.other = PropertyOffset(p);
    if (FProperty* p = FindProperty(fn, "NormalImpulse"); p && PropertyType(p) == "StructProperty") h.impulse = PropertyOffset(p);
    if (FProperty* p = FindProperty(fn, "Hit"); p && PropertyType(p) == "StructProperty") h.hit = PropertyOffset(p);
    return hitParams_.emplace(fn, h).first->second;
}

// A contact between the two fighters' bodies and what is in their hands; anything else (the ground, a loose weapon, someone
// else) is not this fight's. A weapon against a body is a Hit by the weapon's holder, two weapons a Clash, and body against
// body a Hit only by a fighter kicking (else it is a grapple or a stumble).
void FightWatch::OnHit(UObject* obj, UFunction* fn, void* params) {
    if (!obj || !params || !live_()) return;
    const HitParams& hp = ParamsOf(fn);
    if (hp.other < 0) return;
    uint8_t* const at = static_cast<uint8_t*>(params);
    UObject* other = *reinterpret_cast<UObject**>(at + hp.other);
    if (!other || other == obj) return;
    auto what = [&](UObject* a, uint8_t& who, uint8_t& hand) {
        for (uint8_t s = 0; s < 2; ++s) {
            if (a == side_[s].willie) { who = s; hand = clone::kBody; return true; }
            for (uint8_t h = 0; h < 2; ++h) if (a == side_[s].weapon[h]) { who = s; hand = h; return true; }
        }
        return false;
    };
    uint8_t wa = 0, ha = 0, wb = 0, hb = 0;
    if (!what(obj, wa, ha) || !what(other, wb, hb) || wa == wb) return;
    float impulse = 0;
    if (hp.impulse >= 0) {
        FVector v;
        std::memcpy(&v, at + hp.impulse, sizeof v);
        const double m = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        impulse = std::isfinite(m) ? float(std::min(m, 1e9)) : 0.f;
    }
    const double now = SteadyMs();
    if (ha != clone::kBody && hb != clone::kBody) {
        const uint8_t mine = wa == clone::kMe ? ha : hb, theirs = wa == clone::kMe ? hb : ha;
        if (clashGate_.Allow(mine, theirs, now)) sink_(clone::MakeEvent(clone::Ev::Clash, clone::kMe, mine, theirs, impulse));
        return;
    }
    bool objStrikes;
    if (ha != clone::kBody) objStrikes = true;
    else if (hb != clone::kBody) objStrikes = false;
    else {
        auto kicking = [&](uint8_t s) { return side_[s].was.kick[0] || side_[s].was.kick[1]; };
        if (kicking(wa)) objStrikes = true;
        else if (kicking(wb)) objStrikes = false;
        else return;
    }
    const uint8_t who = objStrikes ? wa : wb, hand = objStrikes ? ha : hb;
    if (!hitGate_.Allow(who, hand, now)) return;
    uint8_t part = clone::kNoPart;
    const int32_t bone = objStrikes ? boneOff_ : myBoneOff_;   // BoneName: the other actor's bone; MyBoneName: this one's
    if (hp.hit >= 0 && bone >= 0) part = clone::PartOfBone(NameToString(*reinterpret_cast<const FName*>(at + hp.hit + bone)));
    sink_(clone::MakeEvent(clone::Ev::Hit, who, hand, part, impulse));
}

}  // namespace hsm::game
