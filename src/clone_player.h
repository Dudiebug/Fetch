// The clone in the fight: its brain (a model, or replay of a recording), its body, and how the game gets its input.
// shared/clone has the rules; docs/CLONE.md.
#pragma once
#include "clone/hsm_clone.h"
#include "clone/hsm_clone_drill.h"
#include "clone/hsm_model.h"
#include "fight_watch.h"
#include "gameplay/adapter.h"
#include "gameplay/standin_rig.h"
#include "hsm_drivelean.h"
#include "hsm_input.h"
#include "hsm_owner.h"
#include "ui/clone_overlay.h"
#include "ue/engine.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fetch {

struct Config {
    bool record = false;      // write how this player fights to Clones/<name>.hsmclone
    std::string name;         // that file's name; empty: the Steam name
    std::string clone;        // the clone to fight; empty: none, "1": the first in Clones/
    bool duel = true;         // Free Mode's own fighters leave while the clone is in
    hsm::clone::BrainSetting brain = hsm::clone::BrainSetting::Auto;
    int menuKey = 0xBA;       // the menu's key (a virtual-key code, ';'); 0: no menu
    int mindKey = 0xBB;       // mind_key=: shows or hides the clone-thinking overlay ('='); 0: no key
    bool strikes = true;      // Deal Complex Damage is watched for Strike events (a script VM hook)
    bool parries = true;      // StopThatBlade is watched for Parried events (likewise)
    uint8_t cloneWeapon = hsm::clone::kCloneKit;   // clone_weapon=: what the clone holds as it comes in (hsm_clone_drill.h)
    uint8_t drill = 0;        // drill=: a training session's kind, 0 full tour, 1 fill gaps
    int drillMinutes = 20;    // drill_minutes=
    bool showMind = false;    // show_mind=: the overlay that shows the clone thinking (its brain keeps a trace of each look)
};

class ClonePlayer {
public:
    // On the worker thread, before any tick: reading files never costs a frame. Refuses bad files whole and says why in the log.
    void Load(const Config& cfg);
    bool ready() const { return use_ != hsm::clone::BrainUse::None; }
    bool inFight() const { return fighter_ != nullptr; }
    hsm::ue::UObject* fighter() const { return fighter_; }
    // Per GameMode tick, game thread. mayFight: an offline fight level. watch: the clone's own fight's events (the clone is its
    // kMe), read before the brain looks; its sink calls Event.
    void Tick(hsm::game::Adapter& a, bool mayFight, float dt, hsm::game::FightWatch& watch);
    // An event of the clone's fight, as it happens: a model of version 4 hears it (ModelBrain::Event). Game thread.
    void Event(const hsm::clone::FightEvent& e) { if (modelBrain_) modelBrain_->Event(e); }
    bool watching() const { return watching_; }   // the clone's fight is being read: a hit now is its
    // The menu (game thread). Despawn takes the clone out of the fight; the next tick brings a new one (when there is a brain).
    void Despawn(hsm::game::Adapter& a);
    void SetDuel(bool on) { duel_ = on; }
    void SetCloneWeapon(uint8_t v) { cloneWeapon_ = v; }
    // The overlay (game thread): SetMind has the brain keep what each look weighed; Mind says it, false with no clone fighting.
    void SetMind(bool on);
    bool Mind(ui::MindView& v) const;
    // "Ana, model, in the fight": what the menu shows.
    std::string Describe() const;
private:
    template <class B> void Play(hsm::game::Adapter& a, B& brain, hsm::game::FightWatch& watch, hsm::ue::UObject* me, hsm::ue::UObject* w, uint32_t now, float dt);
    void Drive(hsm::game::Adapter& a, hsm::ue::UObject* w, float dt);
    void Spawn(hsm::game::Adapter& a, hsm::ue::UObject* me, uint32_t now);
    void Duel(hsm::game::Adapter& a, hsm::ue::UObject* me, uint32_t now);
    void LetGo(hsm::game::Adapter& a, hsm::ue::UObject* w);
    void PlayKeys(hsm::game::Adapter& a, hsm::ue::UObject* w, const std::vector<uint16_t>& keys);
    void Forget();
    void ResetBrain(float yaw);

    bool duel_ = true;
    uint8_t cloneWeapon_ = hsm::clone::kCloneKit;
    uint32_t had_ = 0;           // the weapon groups it has moves enough for, a bit each
    uint32_t armAt_ = 0;         // when the clone's weapon is given (a moment after it comes in)
    hsm::clone::BrainUse use_ = hsm::clone::BrainUse::None;
    hsm::clone::Db db_;
    hsm::clone::Model model_;
    std::unique_ptr<hsm::clone::Brain> replay_;
    std::unique_ptr<hsm::clone::ModelBrain> modelBrain_;
    std::vector<uint8_t> kit_;   // the player's, when it passes
    std::string name_;           // the clone's, from its file
    hsm::ue::UObject* fighter_ = nullptr;
    int32_t fighterIndex_ = -1;
    hsm::game::FighterRig rig_, foeRig_;
    hsm::InputApplier input_;
    hsm::LeanFeed lean_;
    uint32_t seq_ = 0, spawnAt_ = 0, duelAt_ = 0;
    bool watching_ = false;
    bool lookLogged_ = false;          // this fighter's sensitivity, said once
    bool mind_ = false;                // SetMind
    double lookMs_ = 0, lookMaxMs_ = 0;   // what the looks since lookLogAt_ took, to say in the log
    uint32_t looks_ = 0, lookLogAt_ = 0;
    std::vector<uint8_t> keyLogged_;   // each key of the recording's (Db::keys), said once
};

}  // namespace fetch
