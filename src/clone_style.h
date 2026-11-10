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
// True while the clone presses a key of its own (ClonePlayer, ClonePress). The recorder never takes those for the player's:
// the clone's keys are game functions on its fighter and what it carries, the same ones the player's are.
inline bool& ClonePressing() { static bool on = false; return on; }
struct ClonePress {
    ClonePress() { ClonePressing() = true; }
    ~ClonePress() { ClonePressing() = false; }
    ClonePress(const ClonePress&) = delete;
    ClonePress& operator=(const ClonePress&) = delete;
};

class Adapter;

// The clone a clone= value means: Clones/<value> without its extension, or for "1" the first one there (a model before a recording;
// empty: none).
std::wstring CloneFileBase(const std::string& value);
bool ReadCloneFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxSize);
// Clones/<FileStem(name)>, no extension: where the recording of that name (.hsmclone) and its model (.hsmmodel) are.
std::wstring RecordingBase(const std::string& name);
// A Name block on the end of a recording, its cut tail first dropped: the name it shows from now on (the last one counts), in
// every Fetch. False when it is not a recording of this version or cannot be written.
bool AppendNameBlock(const std::wstring& path, const std::string& name);

class StyleRecorder {
public:
    StyleRecorder() = default;
    ~StyleRecorder() { Flush(); }
    StyleRecorder(const StyleRecorder&) = delete;
    StyleRecorder& operator=(const StyleRecorder&) = delete;
    // Per GameMode tick, game thread. clone: a style clone in this fight. It is recorded as any foe is, unless record_clones=0
    // (SetRecordClones). paused: keep nothing (a Steam lobby). name: the file's, on the first write.
    void Tick(Adapter& adapter, ue::UObject* me, ue::UObject* clone, bool fightLevel, bool paused, float dt, const std::string& name);
    // Fetch.ini record_clones=: whether play while a clone is in the fight is recorded. Game thread.
    void SetRecordClones(bool on) { recordClones_ = on; }
    // The menu's record switch: off writes what is kept and stops; on starts again (the same file). Game thread.
    void SetOn(bool on);
    // The menu's Record to: what is kept goes into the file it was going to; the next play goes into the one of the name the next
    // Tick passes (a new file, or the end of the one there). Game thread.
    void Retarget();
    // Writes what is kept so far into the file (a merge is about to read it). Play goes on as it was. Game thread.
    void Save() { Flush(); }
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
    std::vector<std::string> keysSeen_;   // the keys of things other than the fighter logged once each (a few)
    FightWatch watch_{[this](const clone::FightEvent& e) { collector_.Note(e); }, [this] { return live_; }};
    clone::Tally tally_;
    bool recordClones_ = true, cloneLogged_ = false;
    size_t counted_ = 0;             // the collector's pending events tally_ has seen
};

}  // namespace hsm::game
