#include "clone_player.h"
#include "clone_style.h"
#include "log.h"
#include "session/hsm_gvas.h"
#include "util/hsm_sanitize.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <type_traits>
#include <windows.h>

namespace fetch {
using namespace hsm;
using namespace hsm::ue;
using game::Adapter;

namespace {
uint32_t NowMs() { return uint32_t(GetTickCount64()); }
}

void ClonePlayer::Load(const Config& cfg) {
    duel_ = cfg.duel;
    cloneWeapon_ = cfg.cloneWeapon;
    const std::wstring base = game::CloneFileBase(cfg.clone);
    if (base.empty()) { HSM_LOG("clone: nothing in Clones/ to fight"); return; }
    std::vector<uint8_t> bytes;
    bool haveModel = false, haveReplay = false;
    if (game::ReadCloneFile(base + L".hsmmodel", bytes, clone::kMaxModelFile)) {
        haveModel = model_.Load(bytes.data(), bytes.size());
        if (!haveModel) HSM_LOG("clone: the model file was refused: %s", model_.error());
    }
    if (!haveModel || cfg.brain == clone::BrainSetting::Replay) {   // a recording is big: read only if it will be used
        if (game::ReadCloneFile(base + L".hsmclone", bytes, clone::kMaxFile)) {
            haveReplay = db_.Load(bytes.data(), bytes.size());
            if (!haveReplay) HSM_LOG("clone: the recording was refused: not a clone file of this version, or nothing played in it");
        }
    }
    use_ = clone::ChooseBrain(cfg.brain, haveModel, haveReplay);
    if (use_ == clone::BrainUse::None) { HSM_LOG("clone: no usable .hsmmodel or .hsmclone for %s; no clone", cfg.clone.c_str()); return; }
    const bool isModel = use_ == clone::BrainUse::Model;
    if (isModel) modelBrain_ = std::make_unique<clone::ModelBrain>(model_, GetTickCount());
    else replay_ = std::make_unique<clone::Brain>(db_);
    SetMind(cfg.showMind);
    had_ = 0;
    if (isModel) { for (uint8_t g = 0; g < clone::kWeaponGroups; ++g) if (model_.plays(g) >> g & 1u) had_ |= 1u << g; }
    else {
        const auto secs = clone::GroupSeconds(db_);
        for (uint8_t g = 0; g < clone::kWeaponGroups; ++g) if (secs[g] >= clone::kCloneGroupSeconds) had_ |= 1u << g;
    }
    std::string groups;
    for (uint8_t g = clone::kFirstDrillGroup; g <= clone::kLastDrillGroup; ++g)
        if (had_ >> g & 1u) groups += std::string(groups.empty() ? "" : ", ") + clone::GroupName(g);
    HSM_LOG("clone: has moves for %s; clone_weapon=%s", groups.empty() ? "no weapon type of the game's" : groups.c_str(), clone::CloneWeaponToken(cloneWeapon_));
    const std::vector<uint8_t>& kit = isModel ? model_.kit() : db_.kit();
    std::string why;
    if (!kit.empty() && CheckPassportSave(kit.data(), kit.size(), &why)) kit_ = kit;
    else if (!kit.empty()) HSM_LOG("clone: the kit in the file was refused (%s); it fights in yours", why.c_str());
    const std::string name = util::StripControlChars(isModel ? model_.name() : db_.name());
    name_ = name.empty() ? cfg.clone : name;
    HSM_LOG("clone: %s: %s, %.1f h of play, %s", name.empty() ? "no name" : name.c_str(), isModel ? "model" : "replay",
            (isModel ? model_.playedSeconds() : db_.PlayedSeconds()) / 3600, kit_.empty() ? "in your kit" : "in their kit");
    if (!isModel) {
        const double all = std::max(db_.rows(), 1u) / 100.0;
        HSM_LOG("clone: it copies %.0f%% of the recording; left out: %.0f%% walking off a beaten foe, %.0f%% 3 s or more with no input",
                db_.LeftOutCount(clone::Out::Played) / all, db_.LeftOutCount(clone::Out::WalkOff) / all, db_.LeftOutCount(clone::Out::Idle) / all);
        const clone::LookScale look = db_.Look(db_.samples() - 1);
        if (look.guessed) HSM_LOG("clone: the recording keeps no mouse sensitivity (it is from before pre12), but its turns show %.2f%s: its mouse plays at that", look.sens, look.invert ? ", inverted" : "");
        else if (look.known()) HSM_LOG("clone: its latest play was recorded at mouse sensitivity %.2f%s", look.sens, look.invert ? ", inverted" : "");
        else HSM_LOG("clone: the recording keeps no mouse sensitivity (it is from before pre12), and its turns do not show it: its mouse plays at yours");
        HSM_LOG("clone: %zu presses of %zu keys of things the player carried (a mod's, say)", db_.presses().size(), db_.keys().size());
        keyLogged_.assign(db_.keys().size(), 0);
    }
}

void ClonePlayer::SetMind(bool on) {
    mind_ = on;
    if (modelBrain_) modelBrain_->SetTrace(on);
    if (replay_) replay_->SetTrace(on);
}

bool ClonePlayer::Mind(ui::MindView& v) const {
    if (!fighter_ || !mind_) return false;
    v = {};
    char b[48];
    if (modelBrain_) {
        const int c = modelBrain_->lastClip();
        v.model = true;
        v.seq = modelBrain_->seq();
        if (v.seq) v.playing = c >= 0 && uint32_t(c) < model_.clips() ? clone::ClipLabel(model_.clip(uint32_t(c))) : std::string("nothing it can play");
        v.detail = name_ + "'s trained model   " + clone::ClipText(modelBrain_->ClipMs());
        for (const clone::MoveOdds& o : clone::MergedOdds(model_, modelBrain_->odds(), c)) {
            std::snprintf(b, sizeof b, "%.0f%%", double(o.p) * 100);
            v.bars.push_back({o.label, b, o.p, -1, o.picked});
        }
        return true;
    }
    if (!replay_ || db_.sources().empty()) return false;
    const clone::LookTrace& t = replay_->lastLook();
    const uint32_t none = db_.rows();
    v.seq = t.seq;
    if (!t.seq) return true;
    std::string from;
    if (t.row < none) { v.playing = clone::MoveLabel(db_.row(t.row)); from = util::StripControlChars(db_.source(t.row)); }
    else v.playing = "turning";
    v.detail = (from.empty() ? "" : "from " + from + "   ") + clone::ClipText(replay_->ClipMs()) + "   " + clone::ReasonText(t.why);
    v.foe = clone::MapOf(t.foeMetres, t.foeDeg);
    v.foeKnown = true;
    for (const clone::CostBar& c : clone::CostBars(t, none)) {
        const int colour = clone::SourceColour(db_.SourceOf(c.row));
        v.dots.push_back({clone::MapOfRow(db_.row(c.row)), colour, c.picked});
        if (c.cost < 1000) std::snprintf(b, sizeof b, "%.2f", double(c.cost)); else std::snprintf(b, sizeof b, "999+");
        std::string value = b;
        if (const char* mark = clone::MarkText(c.mark); *mark) value += std::string("  ") + mark;
        v.bars.push_back({clone::MoveLabel(db_.row(c.row)), value, c.share, colour, c.picked});
    }
    if (db_.sources().size() > 1)
        for (size_t i = 0; i < db_.sources().size() && i < size_t(clone::kSourceColours); ++i) v.sources.push_back(util::StripControlChars(db_.sources()[i]));
    return true;
}

void ClonePlayer::ResetBrain(float yaw) {
    if (modelBrain_) modelBrain_->Reset(yaw, 0);
    if (replay_) replay_->Reset(yaw, 0);
}

void ClonePlayer::LetGo(Adapter& a, UObject* w) {
    for (Action act : input_.ReleaseAll().released) a.FireAction(w, act, false);
    if (replay_) { replay_->ReleaseKeys(); PlayKeys(a, w, replay_->TakeKeys()); }
}

// A key of something the player carried: pressed on the same kind of thing the clone carries, if it has one. Never on a fighter.
void ClonePlayer::PlayKeys(Adapter& a, UObject* w, const std::vector<uint16_t>& keys) {
    if (keys.empty()) return;
    const std::vector<UObject*> carried = a.CarriedActors(w);
    for (uint16_t k : keys) {
        const clone::Db::Key& key = db_.keys()[k];
        const bool first = k < keyLogged_.size() && !keyLogged_[k];
        if (first) keyLogged_[k] = 1;
        UObject* target = nullptr;
        for (UObject* c : carried) if (c && !a.IsWillie(c) && ObjectName(reinterpret_cast<UObject*>(ClassOf(c))) == key.cls) { target = c; break; }
        UFunction* fn = target ? FindFunction(ClassOf(target), key.fn) : nullptr;
        if (!fn) {
            if (first) HSM_LOG("clone: the key %s of %s is not played: the clone carries no %s", key.fn.c_str(), key.cls.c_str(), target ? "such function" : "such thing");
            continue;
        }
        Call(target, fn).invoke();
        if (first) HSM_LOG("clone: plays the key %s of %s, as the recording did", key.fn.c_str(), key.cls.c_str());
    }
}

void ClonePlayer::Forget() {
    fighter_ = nullptr; fighterIndex_ = -1;
    rig_.Unbind(); foeRig_.Unbind();
    input_.ReleaseAll();
    spawnAt_ = 0;
}

void ClonePlayer::Spawn(Adapter& a, UObject* me, uint32_t now) {
    if (!spawnAt_) { spawnAt_ = now + 3000; return; }
    if (now < spawnAt_) return;
    float yaw = 0, pitch = 0;
    a.ControlRotation(0, yaw, pitch);
    const double r = double(yaw) * 3.14159265358979323846 / 180.0;
    const FVector p = a.ActorLocation(me);
    std::vector<uint8_t> own;
    Adapter::SpawnOptions o;
    if (!kit_.empty()) o.blob = &kit_;
    else if (a.ExportPassport(own)) o.blob = &own;
    o.at = {p.x + 500 * std::cos(r), p.y + 500 * std::sin(r), p.z}; o.yawDeg = yaw + 180; o.teamInt = 2;   // SpawnBot's side
    UObject* w = a.SpawnFighter(o);
    spawnAt_ = 0;
    if (!w) { spawnAt_ = now + 10000; HSM_LOG("clone: the fighter did not spawn; trying again"); return; }
    fighter_ = w; fighterIndex_ = ObjectIndex(w);
    armAt_ = cloneWeapon_ != clone::kCloneKit ? now + 500 : 0;   // once its own spawn has set up its kit
    ResetBrain(yaw + 180);
    lookLogged_ = false;
    HSM_LOG("clone: in the fight, 5 m ahead of the player");
}

// Free Mode's own fighters go while the clone is in, once a second: it is a duel. A career's are the career's.
void ClonePlayer::Duel(Adapter& a, UObject* me, uint32_t now) {
    if (!duel_ || now < duelAt_ || !a.FreeModeActive()) return;
    duelAt_ = now + 1000;
    for (UObject* w : a.AllWillies()) if (w != me && w != fighter_) a.DestroyFighter(w, ObjectIndex(w));
}

// The clone is driven as a remote fighter: inputs in, the body's own physics out.
void ClonePlayer::Drive(Adapter& a, UObject* w, float dt) {
    if (!a.BindInputs(w)) return;
    input_.Target(w);
    InputApplier::Step s = input_.Tick(dt * 1000.f);
    // The camera lock flips this machine's own setting, the middle mouse button runs a debug damage trace.
    auto played = [](Action act) { return act != Action::ToggleCameraLock && act != Action::MiddleMouse; };
    for (Action act : s.released) if (played(act)) a.FireAction(w, act, false);
    for (Action act : s.pressed) if (played(act)) a.FireAction(w, act, true);
    float sens = 1.f; bool invert = false;
    if (a.MouseLook(sens, invert) && std::fabs(sens) > 0.01f) {
        s.axes[size_t(Axis::TurnMouse)] /= sens;
        s.axes[size_t(Axis::LookMouse)] /= invert ? -sens : sens;
    }
    lean_.Target(w);
    lean_.Trim(s.axes, s.skip);
    for (size_t i = 0; i < kAxisCount; ++i) if (!s.skip[i]) a.ApplyAxis(w, Axis(i), s.axes[i]);
    a.KeepRemoteFighter(w, input_.latest().controlYaw, input_.latest().controlPitch);
}

template <class B>
void ClonePlayer::Play(Adapter& a, B& brain, game::FightWatch& watch, UObject* me, UObject* w, uint32_t now, float dt) {
    watch.ReadArmour(a, w, me);   // its side first: the player is its foe
    if (brain.NeedsLook()) {
        if (rig_.Willie() != w && !rig_.Bind(w)) return;
        if (foeRig_.Willie() != me && !foeRig_.Bind(me)) return;
        sync::PoseFrame mine, theirs;
        VitalsMsg mv{}, tv{};
        if (!rig_.Read(now, 0, &mine, nullptr) || !foeRig_.Read(now, 0, &theirs, nullptr)) return;
        if (!a.ReadVitals(w, 0, mv) || !a.ReadVitals(me, 0, tv)) return;
        uint8_t forged[2], foeForged[2];
        a.ReadForgedTypes(w, forged); a.ReadForgedTypes(me, foeForged);
        float seen[clone::kFeatures];
        clone::Features({&mine, &mv, watch.cover(clone::kMe), forged}, {&theirs, &tv, watch.cover(clone::kFoe), foeForged}, nullptr, brain.yaw(), brain.pitch(),
                        brain.PrevTurn(), brain.PrevLook(), brain.buttons(), brain.guardR(), brain.guardL(), seen);
        const auto t0 = std::chrono::steady_clock::now();
        brain.Look(seen);
        // What a look costs, said each half minute: the overlay's trace makes it dearer (docs/HUMAN_TESTING.md).
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        lookMs_ += ms; lookMaxMs_ = std::max(lookMaxMs_, ms); ++looks_;
        if (!lookLogAt_) lookLogAt_ = now;
        else if (now - lookLogAt_ >= 30000) {
            HSM_LOG("clone: %u looks in %.0f s, %.2f ms each on average, %.2f ms at most (the clone-thinking overlay %s)", looks_,
                    (now - lookLogAt_) / 1000.0, lookMs_ / looks_, lookMaxMs_, mind_ ? "on" : "off");
            lookMs_ = lookMaxMs_ = 0; looks_ = 0; lookLogAt_ = now;
        }
    }
    const clone::Output o = brain.Step(dt * 1000.f);
    if constexpr (std::is_same_v<B, clone::Brain>) PlayKeys(a, w, brain.TakeKeys());
    // The mouse axes are as the player's hand moved: the game scales them by its sensitivity. Played at the recorder's, the
    // clone's arm moves as theirs did whatever yours is set to (Drive takes yours back out); not known, at yours.
    float sens = 1.f; bool invert = false;
    a.MouseLook(sens, invert);
    if (!lookLogged_ && (o.look.known() || o.axes[clone::kTurn] != 0 || o.axes[clone::kLook] != 0)) {
        lookLogged_ = true;
        if (o.look.known()) HSM_LOG("clone: its mouse plays at the recorder's sensitivity %.2f%s (yours: %.2f)", o.look.sens, o.look.guessed ? ", read off its turns" : "", sens);
        else HSM_LOG("clone: its mouse plays at your sensitivity %.2f: the %s keeps none", sens, modelBrain_ ? "model" : "recording");
    }
    if (o.look.known()) { sens = o.look.sens; invert = o.look.invert; }
    InputFrame f;
    f.seq = ++seq_; f.clientTimeMs = now; f.buttons = o.buttons; f.controlYaw = o.yaw; f.controlPitch = o.pitch; f.axes = o.axes;
    f.axes[clone::kTurn] = std::clamp(o.axes[clone::kTurn] * sens, -kAxisLimit, kAxisLimit);
    f.axes[clone::kLook] = std::clamp(o.axes[clone::kLook] * (invert ? -sens : sens), -kAxisLimit, kAxisLimit);
    input_.Receive(f);
    Drive(a, w, dt);
}

void ClonePlayer::Despawn(Adapter& a) {
    if (!fighter_) return;
    if (a.StillValid(fighter_, fighterIndex_)) { LetGo(a, fighter_); a.DestroyFighter(fighter_, fighterIndex_); }
    Forget();
    HSM_LOG("clone: out of the fight (menu)");
}

std::string ClonePlayer::Describe() const {
    if (!ready()) return "none";
    std::string s = name_ + (use_ == clone::BrainUse::Model ? ", model" : ", replay");
    if (fighter_) s += spawnAt_ ? ", down" : ", in the fight";
    else s += ", coming";
    return s;
}

void ClonePlayer::Tick(Adapter& a, bool mayFight, float dt, game::FightWatch& watch) {
    watching_ = false;
    if (!ready()) return;
    if (!mayFight) {   // left the level, or joined a Steam lobby
        if (fighter_) {
            if (a.StillValid(fighter_, fighterIndex_)) { LetGo(a, fighter_); a.DestroyFighter(fighter_, fighterIndex_); }
            Forget();
        }
        return;
    }
    UObject* me = a.PlayerWillie(0);
    if (!me) return;
    const uint32_t now = NowMs();
    if (fighter_ && !a.StillValid(fighter_, fighterIndex_)) Forget();
    if (!fighter_) { if (!a.IsDead(me)) Spawn(a, me, now); return; }
    UObject* w = fighter_;
    Duel(a, me, now);
    if (armAt_ && int32_t(now - armAt_) >= 0 && !a.IsDead(w)) {
        armAt_ = 0;
        const uint8_t g = clone::PickCloneGroup(cloneWeapon_, a.HeldWeaponType(me), had_, now);
        if (!g) HSM_LOG("clone: weapon: %s gives nothing it has moves for; it keeps its kit", clone::CloneWeaponToken(cloneWeapon_));
        else if (a.GiveWeapon(w, g, 4)) HSM_LOG("clone: weapon: %s (%s)", clone::GroupName(g), clone::CloneWeaponToken(cloneWeapon_));
    }
    if (a.IsDead(w)) {   // the body stays a while, then the next one comes
        if (!spawnAt_) {
            LetGo(a, w); spawnAt_ = now + 8000;
            if (replay_) HSM_LOG("clone: down; this fight it turned to face you first %u times, and was hurt playing %u moves it then left alone",
                                 replay_->turns(), replay_->hurtMoves());
            else HSM_LOG("clone: down");
        }
        else if (now >= spawnAt_) { a.DestroyFighter(w, fighterIndex_); Forget(); }
        return;
    }
    if (a.IsDead(me)) { LetGo(a, w); return; }
    if (modelBrain_ && model_.hearsEvents()) {   // its fight from its side: the player is its foe
        watch.Hook();
        watch.Read(w, me);
        watching_ = true;
    }
    if (modelBrain_) Play(a, *modelBrain_, watch, me, w, now, dt);
    else Play(a, *replay_, watch, me, w, now, dt);
}

}  // namespace fetch
