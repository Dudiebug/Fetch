#include "training.h"
#include "clone_style.h"
#include "clone/hsm_fetch_menu.h"
#include "gameplay/adapter.h"
#include "log.h"
#include "match/hsm_match.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <windows.h>

namespace fetch {
using namespace hsm;
using namespace hsm::ue;
using game::Adapter;
using game::StyleRecorder;

namespace {
uint32_t NowMs() { return uint32_t(GetTickCount64()); }
bool Reached(uint32_t now, uint32_t at) { return int32_t(now - at) >= 0; }
constexpr uint32_t kCountdownMs = 5000;
constexpr uint32_t kNextFoeMs = 3000;      // a beaten foe's body stays this long, then the next one comes
constexpr uint32_t kSweepMs = 1000;        // Free Mode's own fighters are taken out this often
constexpr uint32_t kArmRetryMs = 3000;
constexpr int kArmTries = 3;
constexpr uint32_t kSummaryMs = 20000;     // the summary stays on screen this long
constexpr int kWeaponTier = 4;             // the player's weapon: a soldier's (inside the 3..6 that Generate Weapon kept a typed weapon to before build 26615445)
constexpr double kFoeAhead = 500;          // uu: 5 m in front of the player, as the clone comes in
}  // namespace

TrainingSession::~TrainingSession() { if (planner_.joinable()) planner_.join(); }

void TrainingSession::Start(const std::string& name, clone::DrillMode mode, int minutes) {
    if (state_ != State::Off && state_ != State::Done) return;
    if (planner_.joinable()) planner_.join();
    state_ = State::Planning;
    planned_.store(false, std::memory_order_relaxed);
    plan_.clear(); stages_.clear(); summary_.clear();
    before_ = {}; played_ = {};
    const uint32_t seed = NowMs();
    HSM_LOG("drill: planning a %d min %s from %s's recording", minutes, mode == clone::DrillMode::Gaps ? "session on the gaps" : "tour",
            name.c_str());
    planner_ = std::thread([this, name, mode, minutes, seed] {
        clone::Coverage have;
        const std::wstring base = game::CloneFileBase(name);
        std::vector<uint8_t> bytes;
        if (!base.empty() && game::ReadCloneFile(base + L".hsmclone", bytes, clone::kMaxFile)) {
            clone::Db db;
            if (db.Load(bytes.data(), bytes.size())) have = clone::CoverageOf(db);
        }
        before_ = have;
        plan_ = clone::PlanSession(have, minutes, mode, seed);
        planned_.store(true, std::memory_order_release);
    });
}

void TrainingSession::DropFoe(Adapter& a) {
    if (foe_ && a.StillValid(foe_, foeIndex_)) a.DestroyFighter(foe_, foeIndex_);
    foe_ = nullptr; foeIndex_ = -1; foeDown_ = false;
}

void TrainingSession::BeginStage(Adapter& a, StyleRecorder& style, size_t i) {
    stage_ = i;
    DropFoe(a);
    const clone::Stage& s = stages_[i];
    state_ = State::Countdown;
    at_ = NowMs() + kCountdownMs;
    armFor_ = nullptr;
    foes_ = 0;
    from_ = style.tally();
    style.Note(clone::StageEvent(uint8_t(std::min<size_t>(i, 0xFE)), s));
    HSM_LOG("drill: stage %zu/%zu %s vs %s (tier %d), %.0f s", i + 1, stages_.size(), clone::GroupName(s.group), clone::FoeArmourName(s.armour),
            int(s.tier), double(s.seconds));
}

void TrainingSession::EndStage(Adapter& a, StyleRecorder& style) {
    const clone::Stage& s = stages_[stage_];
    const uint32_t played = state_ == State::Fight ? NowMs() - stageStart_ : 0;
    played_.Add(s.group, s.armour, float(played) / 1000.f);
    const clone::Tally& t = style.tally();
    HSM_LOG("drill: stage %zu/%zu over: %d foe%s beaten, hits %u landed %u taken, strikes %u through a gap %u bare %u beat the armour %u held, "
            "deaths %u",
            stage_ + 1, stages_.size(), foes_, foes_ == 1 ? "" : "s", t.landed - from_.landed, t.taken - from_.taken, t.gaps - from_.gaps,
            t.bare - from_.bare, t.beat - from_.beat, t.held - from_.held, t.deaths - from_.deaths);
    DropFoe(a);
}

void TrainingSession::Finish(Adapter& a, StyleRecorder& style, const char* why) {
    if (state_ == State::Countdown || state_ == State::Fight) EndStage(a, style);
    DropFoe(a);
    if (state_ == State::Planning) {
        if (planner_.joinable()) planner_.join();
        state_ = State::Off;
        HSM_LOG("drill: stopped before it began (%s)", why);
        return;
    }
    style.Note(clone::SessionEndEvent());
    // Coverage of the pairs this session asked for, before it and with what it added.
    float was = 0, now = 0;
    std::vector<std::pair<uint8_t, clone::FoeArmour>> seen;
    for (const clone::Stage& s : stages_) {
        if (std::find(seen.begin(), seen.end(), std::make_pair(s.group, s.armour)) != seen.end()) continue;
        seen.emplace_back(s.group, s.armour);
        was += before_.at(s.group, s.armour);
        now += before_.at(s.group, s.armour) + played_.at(s.group, s.armour);
    }
    const clone::Tally& t = style.tally();
    const double minutes = double(NowMs() - startMs_) / 60000.0;
    char b[256];
    std::snprintf(b, sizeof b, "Training %s after %.1f min: %zu of %zu stages, %u hits landed, %u taken. These pairs: %s -> %s of play.",
                  why, minutes, std::min(stage_ + 1, stages_.size()), stages_.size(), t.landed - sessionFrom_.landed, t.taken - sessionFrom_.taken,
                  clone::Clock(was).c_str(), clone::Clock(now).c_str());
    summary_ = b;
    HSM_LOG("drill: done (%s), %.1f min, coverage %.0f s -> %.0f s; hits %u landed %u taken; strikes %u through a gap %u held", why, minutes,
            double(was), double(now), t.landed - sessionFrom_.landed, t.taken - sessionFrom_.taken, t.gaps - sessionFrom_.gaps,
            t.held - sessionFrom_.held);
    state_ = State::Done;
    doneUntil_ = NowMs() + kSummaryMs;
}

void TrainingSession::Stop(Adapter& a, StyleRecorder& style, const char* why) {
    if (running()) Finish(a, style, why);
}

void TrainingSession::Tick(Adapter& a, StyleRecorder& style, bool mayTrain) {
    const uint32_t now = NowMs();
    if (state_ == State::Off) return;
    if (state_ == State::Done) { if (Reached(now, doneUntil_)) { state_ = State::Off; summary_.clear(); } return; }
    if (state_ == State::Planning) {
        if (!planned_.load(std::memory_order_acquire)) return;
        planner_.join();
        stages_ = plan_;
        if (stages_.empty()) { state_ = State::Off; HSM_LOG("drill: nothing to plan; no session"); return; }
        std::string list;
        for (const clone::Stage& s : stages_) list += std::string(list.empty() ? "" : ", ") + clone::GroupName(s.group) + "/" + clone::FoeArmourName(s.armour);
        HSM_LOG("drill: plan: %zu stages (%s); the recording had %.0f s with armour known", stages_.size(), list.c_str(), double(before_.total));
        startMs_ = now;
        sessionFrom_ = style.tally();
        if (!mayTrain) { Finish(a, style, "not in a Free Mode fight offline"); return; }
        BeginStage(a, style, 0);
        return;
    }
    if (!mayTrain) { Finish(a, style, "left the fight"); return; }
    UObject* me = a.PlayerWillie(0);
    if (!me) return;
    if (Reached(now, sweepAt_)) {   // the session's foe only: Free Mode's own fighters go
        sweepAt_ = now + kSweepMs;
        for (UObject* w : a.AllWillies()) if (w != me && w != foe_) a.DestroyFighter(w, ObjectIndex(w));
    }
    const clone::Stage& s = stages_[stage_];
    // The stage's weapon, in the countdown and again whenever the player's body is a new one (a respawn).
    if (armFor_ != me) { armFor_ = me; armed_ = false; armTries_ = 0; armAt_ = now; }
    if (!armed_ && armTries_ < kArmTries && !a.IsDead(me) && Reached(now, armAt_)) {
        armed_ = a.GiveWeapon(me, s.group, kWeaponTier);
        if (!armed_ && ++armTries_ >= kArmTries) HSM_LOG("drill: the stage's weapon could not be given; the stage goes on with what you hold");
        armAt_ = now + kArmRetryMs;
    }
    if (state_ == State::Countdown) {
        if (!Reached(now, at_)) return;
        state_ = State::Fight;
        stageStart_ = now;
        stageEnd_ = now + uint32_t(s.seconds * 1000.f);
        foeAt_ = now;
    }
    if (Reached(now, stageEnd_)) {
        EndStage(a, style);
        if (stage_ + 1 < stages_.size()) BeginStage(a, style, stage_ + 1);
        else Finish(a, style, "done");
        return;
    }
    if (foe_ && !a.StillValid(foe_, foeIndex_)) { foe_ = nullptr; foeIndex_ = -1; foeDown_ = false; foeAt_ = now + kNextFoeMs; }
    if (foe_ && a.IsDead(foe_)) {
        if (!foeDown_) { foeDown_ = true; ++foes_; foeAt_ = now + kNextFoeMs; HSM_LOG("drill: foe down (%d this stage)", foes_); }
        else if (Reached(now, foeAt_)) DropFoe(a);
        return;
    }
    if (!foe_ && Reached(now, foeAt_) && !a.IsDead(me)) {
        float yaw = 0, pitch = 0;
        a.ControlRotation(0, yaw, pitch);
        const double r = double(yaw) * 3.14159265358979323846 / 180.0;
        const FVector p = a.ActorLocation(me);
        const FVector at{p.x + kFoeAhead * std::cos(r), p.y + kFoeAhead * std::sin(r), p.z};
        foe_ = a.SpawnBot(s.tier, at, yaw + 180, 2, double(hsm::kTierBudget[std::min<int>(s.tier, 7)]));
        if (!foe_) { foeAt_ = now + 10000; HSM_LOG("drill: the foe did not spawn; trying again in 10 s"); return; }
        foeIndex_ = ObjectIndex(foe_);
        HSM_LOG("drill: foe spawned: tier %d, budget %d", int(s.tier), hsm::kTierBudget[std::min<int>(s.tier, 7)]);
    }
}

std::string TrainingSession::Banner() const {
    if (state_ == State::Done) return summary_;
    if (state_ == State::Planning) return "Training: reading your recording to plan the session...";
    if (state_ != State::Countdown && state_ != State::Fight) return {};
    const uint32_t now = NowMs();
    const clone::Stage& s = stages_[stage_];
    if (state_ == State::Countdown) {
        const float in = float(int32_t(at_ - now)) / 1000.f;
        return "Get ready, " + clone::StageBanner(stage_, stages_.size(), s, s.seconds) + " (starts in " + std::to_string(std::max(0, int(in + .999f))) + ")";
    }
    return clone::StageBanner(stage_, stages_.size(), s, float(int32_t(stageEnd_ - now)) / 1000.f);
}

std::string TrainingSession::Status() const {
    switch (state_) {
        case State::Off: return "Training: off";
        case State::Planning: return "Training: planning...";
        case State::Done: return "Training: " + summary_;
        default: return "Training: " + Banner();
    }
}

}  // namespace fetch
