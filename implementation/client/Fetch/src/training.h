// A training session (the ; menu's "Training session" row; docs/CLONE.md): stage after stage, the player is given one of the
// game's weapon types and fights foes of one tier, picked from what their recording is short of (shared/clone/hsm_clone_drill.h
// plans it). Offline, in Free Mode, with no clone in the fight. Recording is on throughout, into the player's own file, and each
// stage is marked in it (a Stage event). The player's armour is left as they chose it.
#pragma once
#include "clone/hsm_clone_drill.h"
#include "ue/engine.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace hsm::game { class Adapter; class StyleRecorder; }

namespace fetch {

class TrainingSession {
public:
    ~TrainingSession();
    // Game thread (the menu). name: the player's recording (Clones/<name>.hsmclone), read on a thread of its own for the plan.
    void Start(const std::string& name, hsm::clone::DrillMode mode, int minutes);
    void Stop(hsm::game::Adapter& a, hsm::game::StyleRecorder& style, const char* why);
    bool running() const { return state_ != State::Off && state_ != State::Done; }
    bool fighting() const { return state_ == State::Fight || state_ == State::Countdown; }
    hsm::ue::UObject* foe() const { return foe_; }
    // Per GameMode tick, game thread. mayTrain: a fight level in Free Mode, offline, no clone in it.
    void Tick(hsm::game::Adapter& a, hsm::game::StyleRecorder& style, bool mayTrain);
    std::string Banner() const;   // on screen while a session runs or just ended; empty: none
    std::string Status() const;   // the menu's line
private:
    enum class State : uint8_t { Off, Planning, Countdown, Fight, Done };
    void BeginStage(hsm::game::Adapter& a, hsm::game::StyleRecorder& style, size_t i);
    void EndStage(hsm::game::Adapter& a, hsm::game::StyleRecorder& style);
    void Finish(hsm::game::Adapter& a, hsm::game::StyleRecorder& style, const char* why);
    void DropFoe(hsm::game::Adapter& a);

    State state_ = State::Off;
    std::thread planner_;
    std::atomic<bool> planned_{false};
    std::vector<hsm::clone::Stage> plan_, stages_;
    hsm::clone::Coverage before_, played_;
    size_t stage_ = 0;
    uint32_t at_ = 0, stageEnd_ = 0, stageStart_ = 0, foeAt_ = 0, sweepAt_ = 0, doneUntil_ = 0, startMs_ = 0;
    hsm::ue::UObject* foe_ = nullptr;
    int32_t foeIndex_ = -1;
    bool foeDown_ = false;
    hsm::ue::UObject* armFor_ = nullptr;  // the player's body the stage's weapon is for (a respawn is a new one)
    bool armed_ = false;
    int armTries_ = 0;
    uint32_t armAt_ = 0;
    int foes_ = 0;
    hsm::clone::Tally from_, sessionFrom_;
    std::string summary_;
};

}  // namespace fetch
