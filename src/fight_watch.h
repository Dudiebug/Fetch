// A fight's events as they happen (shared/clone/hsm_clone_events.h has the rules): both fighters' Willie_BP flags once a tick,
// whose edges are events, and the contacts between their bodies and weapons. The recorder writes them down; the clone hears its
// own fight with them. What each fighter wears (read every couple of seconds, an Armour event when it changes), and each
// blow's Strike: how the game's Deal Complex Damage met the body (skin or armour, how much it hurt), folded over the physics
// steps of one blow. It reads the game and changes nothing.
#pragma once
#include "clone/hsm_clone.h"
#include "clone/hsm_clone_armor.h"
#include "clone/hsm_clone_events.h"
#include "ue/engine.h"
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace hsm::ue { class ScriptCall; }
namespace hsm::game {
class Adapter;

class FightWatch {
public:
    using Sink = std::function<void(const clone::FightEvent&)>;
    // sink: each event, from me's side (clone::kMe). live: whether a hit now is this fight's (asked on every contact).
    FightWatch(Sink sink, std::function<bool()> live) : sink_(std::move(sink)), live_(std::move(live)) {}
    FightWatch(const FightWatch&) = delete;
    FightWatch& operator=(const FightWatch&) = delete;
    // Once: the hit hook, and the strike watch when strikes are on. It holds this, so a FightWatch lives as long as the mod.
    void Hook();
    // Fetch.ini strikes=: whether Deal Complex Damage is watched (a script VM hook). Set before the first Hook.
    static void SetStrikes(bool on);
    static void LevelLoaded();                                // the new level's Blueprint overrides get the strike watch too
    // Every couple of seconds, game thread: what both wear (Adapter::ReadArmour). An Armour event when it changed, and a log line.
    void ReadArmour(Adapter& adapter, ue::UObject* me, ue::UObject* foe);
    bool armourKnown() const { return armour_[0].known && armour_[1].known; }
    const clone::ArmourView& armour(uint8_t side) const { return armour_[side & 1].view; }
    // The feature row's armour columns for a side, or null when not read (Seen::armor).
    const float* cover(uint8_t side) const { return armourKnown() ? armour_[side & 1].cover : nullptr; }
    // Once a tick, game thread: the flags' edges since the last tick. A fighter seen for the first time has no edges yet.
    void Read(ue::UObject* me, ue::UObject* foe);
    void Pause() { for (Side& s : side_) s.known = false; }   // a tick not read: the next has no edges
    void Forget();                                            // and whose they were
    void ForgetLevel();                                       // the level's functions go with it
private:
    // A fighter's Willie_BP fields, found once per class: what it is doing (clone::Doing) and what each hand holds.
    struct Flags {
        enum : int { kGrabR, kGrabL, kThrustR, kThrustL, kKickR, kKickL, kParryR, kParryL, kStuckR, kStuckL, kBeingGrabbed, kFallen, kDed, kCount };
        ue::UClass* cls = nullptr;
        ue::FProperty* bools[kCount] = {};
        int32_t grabComp[2] = {-1, -1}, grabBone[2] = {-1, -1}, weapon[2] = {-1, -1};
        bool Bind(ue::UObject* willie);
        clone::Doing Read(ue::UObject* willie, ue::UObject* other, ue::UObject* (&held)[2]) const;
    };
    struct Side { Flags flags; clone::Doing was; bool known = false; ue::UObject* weapon[2] = {}; ue::UObject* willie = nullptr; };
    struct HitParams { int32_t other = -1, impulse = -1, hit = -1; };
    void OnHit(ue::UObject* obj, ue::UFunction* fn, void* params);
    const HitParams& ParamsOf(ue::UFunction* fn);
    // One blow: the Deal Complex Damage calls of one striking component on one fighter, a few ms apart.
    struct OpenStrike {
        ue::UObject* victim = nullptr; ue::UObject* collider = nullptr;
        uint8_t who = 0, hand = clone::kBody, part = 0xFF, surface = 0;
        float damage = 0, cut = 0, stab = 0, best = -1;
        double lastMs = 0;
        int contacts = 0, ran = 0;
    };
    friend struct StrikeHub;
    void StrikeBefore(ue::ScriptCall& call, double now);
    void StrikeAfter(ue::ScriptCall& call, bool ran, uint8_t surface, double damage, double cut);
    void FlushStrikes(double olderThanMs);
    bool StrikerOf(ue::UObject* collider, uint8_t victimSide, uint8_t& hand) const;
    struct ArmourSide { ue::UObject* willie = nullptr; clone::ArmourView view; float cover[clone::kParts] = {}; bool known = false; };

    Sink sink_;
    std::function<bool()> live_;
    bool hooked_ = false;
    Side side_[2];                   // clone::kMe, clone::kFoe
    std::unordered_map<ue::UFunction*, HitParams> hitParams_;   // per hit function, dropped when the level goes
    int32_t boneOff_ = -1, myBoneOff_ = -1;                        // FHitResult::BoneName, MyBoneName
    bool boneOffRead_ = false;
    clone::HitGate hitGate_;
    clone::ClashGate clashGate_;
    ArmourSide armour_[2];
    double nextArmourMs_ = 0;
    std::vector<OpenStrike> strikes_;
    // The calls running now (Deal Complex Damage can run inside another's body): what each was asked, read before it ran.
    struct Asked { ue::UObject* victim = nullptr; ue::UObject* collider = nullptr; uint8_t who = 0, hand = 0, part = 0xFF; float stab = 0; bool ours = false; };
    std::vector<Asked> asked_;
    int strikeLines_ = 0;                                    // strikes logged (the first few of each level, for a tester)
};

}  // namespace hsm::game
