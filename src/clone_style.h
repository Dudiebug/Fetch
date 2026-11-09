// Style clones, the game's side (shared/clone/hsm_clone.h has the rules; docs/CLONE.md): record=1 writes how the
// local player fights to Clones/<name>.hsmclone, a few KB a second, in any fight level. It reads the game and changes nothing.
// Off, it costs one branch per tick and hooks nothing.
#pragma once
#include "clone/hsm_clone.h"
#include "fight_watch.h"
#include "gameplay/standin_rig.h"
#include "hsm_input.h"
#include "ue/engine.h"
#include <cstdint>
#include <string>
#include <vector>

namespace hsm::game {
class Adapter;

// The clone a clone= value means: Clones/<value> without its extension, or for "1" the first one there (a model before a recording;
// empty: none).
std::wstring CloneFileBase(const std::string& value);
bool ReadCloneFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxSize);

class StyleRecorder {
public:
    StyleRecorder() = default;
    ~StyleRecorder() { Flush(); }
    StyleRecorder(const StyleRecorder&) = delete;
    StyleRecorder& operator=(const StyleRecorder&) = delete;
    // Per GameMode tick, game thread. clone: a style clone in this fight (nothing is written then: a clone is not the player's
    // own opponent to learn from). paused: keep nothing (a Steam lobby). name: the file's, on the first write.
    void Tick(Adapter& adapter, ue::UObject* me, ue::UObject* clone, bool fightLevel, bool paused, float dt, const std::string& name);
    // The menu's record switch: off writes what is kept and stops; on starts again (the same file). Game thread.
    void SetOn(bool on);
    bool on() const { return on_; }
    bool live() const { return live_; }               // the last tick kept play
    double seconds() const { return seconds_; }       // play kept since Fetch started
    long long bytes() const { return size_; }         // the file's size
    const clone::Tally& tally() const { return tally_; }
    // An event of the mod's own (a training stage) into the recording, when play is being kept.
    void Note(const clone::FightEvent& e) { if (live_) collector_.Note(e); }
private:
    void CountNew();

    void Hook(Adapter& adapter, ue::UObject* me);
    void FindFoe(Adapter& adapter, ue::UObject* me);
    void Flush();
    bool Open(const std::string& name);
    void Append(const std::vector<uint8_t>& bytes);

    bool on_ = true;                 // until the file cannot take more
    long long size_ = 0;
    bool hooked_ = false, opened_ = false, kitSeen_ = false;
    std::wstring path_;
    ue::UObject* me_ = nullptr;
    ue::UObject* foe_ = nullptr;
    int32_t foeIndex_ = -1;
    double nextFoeMs_ = 0, nextLooseMs_ = 0, nextTypesMs_ = 0;
    bool looseThere_ = false;
    sync::V3 loose_{};                                    // the rows' loose weapon and armor are a moment old: finding them is a walk
    uint8_t forged_[2] = {}, foeForged_[2] = {};
    FighterRig rig_, foeRig_;
    InputCapture input_;
    clone::Collector collector_;
    std::vector<uint8_t> kit_;
    bool live_ = false;
    double seconds_ = 0;
    FightWatch watch_{[this](const clone::FightEvent& e) { collector_.Note(e); }, [this] { return live_; }};
    clone::Tally tally_;
    size_t counted_ = 0;             // the collector's pending events tally_ has seen
};

}  // namespace hsm::game
