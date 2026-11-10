#include "clone_style.h"
#include "gameplay/adapter.h"
#include "hsm_owner.h"
#include "clone/hsm_clone_rules.h"
#include "log.h"
#include "mod_link.h"
#include "paths.h"
#include "ue/hook.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <io.h>
#include <windows.h>

namespace hsm::game {
using namespace hsm::ue;

namespace {
constexpr double kFindFoeMs = 500;
constexpr double kFindLooseMs = 500, kReadTypesMs = 500;
constexpr uint32_t kFlushSamples = 900;   // about 15 s: what a crash can lose
constexpr wchar_t kDir[] = L"Clones\\";

double SteadyMs() { return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
std::wstring Wide(const std::string& ascii) { return std::wstring(ascii.begin(), ascii.end()); }   // FileStem: ASCII by construction
std::wstring BaseInClones(const std::string& stem) { return ModFilePath((kDir + Wide(stem)).c_str()); }
std::wstring InClones(const std::string& stem) { return BaseInClones(stem) + L".hsmclone"; }

// Where the last whole block ends: a block cut by a crash must not sit in front of what is written next.
long long GoodEnd(FILE* f, long long size);

std::vector<uint8_t> NameBlock(const std::string& name) {
    return clone::EncodeBlock(clone::Block::Name, reinterpret_cast<const uint8_t*>(name.data()), std::min(name.size(), clone::kMaxName * 4));
}

long long GoodEnd(FILE* f, long long size) {
    long long p = (long long)(clone::FileHeader().size());
    while (size - p >= 9) {
        uint8_t head[5];
        if (_fseeki64(f, p, SEEK_SET) || std::fread(head, 1, 5, f) != 5) break;
        uint32_t len;
        std::memcpy(&len, head + 1, 4);
        if (size - p - 9 < (long long)(len)) break;
        p += (long long)(len) + 9;
    }
    return p;
}
}  // namespace

std::wstring RecordingBase(const std::string& name) { return BaseInClones(clone::FileStem(name)); }

bool AppendNameBlock(const std::wstring& path, const std::string& name) {
    FILE* f = _wfopen(path.c_str(), L"r+b");
    if (!f) return false;
    const std::vector<uint8_t> header = clone::FileHeader();
    uint8_t got[64] = {};
    const size_t n = std::fread(got, 1, header.size(), f);
    bool ok = clone::IsFileHeader(got, n) && !_fseeki64(f, 0, SEEK_END);
    if (ok) {
        const long long size = _ftelli64(f), end = GoodEnd(f, size);
        if (end != size) { HSM_LOG("clone: a cut block at the end of the file is dropped"); _chsize_s(_fileno(f), end); }
        const std::vector<uint8_t> b = NameBlock(name);
        ok = !_fseeki64(f, end, SEEK_SET) && std::fwrite(b.data(), 1, b.size(), f) == b.size();
    }
    return std::fclose(f) == 0 && ok;
}

std::wstring CloneFileBase(const std::string& value) {
    if (value != "1") return BaseInClones(clone::FileStem(value));
    const std::wstring dir = ModFilePath(kDir);
    for (const wchar_t* pattern : {L"*.hsmmodel", L"*.hsmclone"}) {
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileW((dir + pattern).c_str(), &d);
        if (h == INVALID_HANDLE_VALUE) continue;
        FindClose(h);
        const std::wstring file = d.cFileName;
        return dir + file.substr(0, file.find_last_of(L'.'));
    }
    return {};
}

bool ReadCloneFile(const std::wstring& path, std::vector<uint8_t>& out, size_t maxSize) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    bool ok = false;
    if (!_fseeki64(f, 0, SEEK_END)) {
        const long long size = _ftelli64(f);
        if (size > 0 && size_t(size) <= maxSize && !_fseeki64(f, 0, SEEK_SET)) {
            out.resize(size_t(size));
            ok = std::fread(out.data(), 1, out.size(), f) == out.size();
        }
    }
    std::fclose(f);
    return ok;
}

void StyleRecorder::CountNew() {
    const std::vector<clone::FightEvent>& p = collector_.pending();
    for (size_t i = counted_; i < p.size(); ++i) tally_.Count(p[i]);
    counted_ = p.size();
}

void StyleRecorder::SetOn(bool on) {
    if (on) {
        if (!on_) { on_ = true; opened_ = false; size_ = 0; path_.clear(); kitSeen_ = false; HSM_LOG("clone: recording on"); }
        return;
    }
    if (!on_) return;
    Flush();
    rig_.Unbind(); foeRig_.Unbind();
    me_ = nullptr; foe_ = nullptr; live_ = false; kitSeen_ = false;
    watch_.Forget();
    on_ = false;
    HSM_LOG("clone: recording off");
}

void StyleRecorder::Retarget() {
    Flush();
    collector_.Break();
    opened_ = false; path_.clear(); size_ = 0; kitSeen_ = false;
    kit_.clear();   // the new file gets the kit too
}

void StyleRecorder::Hook(Adapter& adapter, UObject* me) {
    if (hooked_ || !adapter.BindInputs(me)) return;
    hooked_ = true;
    for (const auto& [fn, b] : adapter.Bindings()) {
        const InputBinding bind = b;
        HookFunction(fn, [this, bind](UObject* obj, UFunction* f, void* params) {
            if (!obj || obj != me_ || ClonePressing() || fetch::Link().SkipsInput()) return false;
            if (bind.isAxis) input_.OnAxis(Axis(bind.index), Adapter::ReadAxisParam(f, params));
            else input_.OnAction(Action(bind.index), bind.pressed);
            return false;
        });
    }
    // Every other key: one bound on anything but a fighter (a mod's gun, say) goes in the recording by its class and function, so
    // a clone carrying the same thing presses it too (hsm_clone_input.h). The fighter's own keys are the samples.
    HookFunctionsMatching([](const std::string& n) { return n.rfind("InpActEvt_", 0) == 0; }, [this](UObject* obj, UFunction* f, void*) {
        if (!obj || !on_ || !live_ || !me_ || ClonePressing() || fetch::Link().SkipsInput() || IsA(obj, ClassOf(me_))) return false;
        const std::string cls = ObjectName(reinterpret_cast<UObject*>(ClassOf(obj))), fn = ObjectName(reinterpret_cast<UObject*>(f));
        const bool kept = collector_.NoteKey(cls, fn);
        const std::string key = cls + "." + fn;
        if (keysSeen_.size() < 32 && std::find(keysSeen_.begin(), keysSeen_.end(), key) == keysSeen_.end()) {
            keysSeen_.push_back(key);
            HSM_LOG("clone: %s the key %s of %s", kept ? "recording" : "cannot record", fn.c_str(), cls.c_str());
        }
        return false;
    });
    watch_.Hook();
    HSM_LOG("clone: recording how this player fights, the fight's events, and the keys of what the player carries");
}

// The nearest living fighter that may be the foe. In a Steam lobby, not one of my team (clone::MayBeFoe).
void StyleRecorder::FindFoe(Adapter& adapter, UObject* me, bool online) {
    const FVector at = adapter.ActorLocation(me);
    double best = double(clone::kMaxRange) * clone::kMaxRange;
    foe_ = nullptr; foeIndex_ = -1;
    auto team = [](UObject* w) { const int32_t* t = Field<int32_t>(w, "Team Int"); return t ? int(*t) : clone::kTeamUnknown; };
    const int mine = online ? team(me) : clone::kTeamUnknown;
    for (UObject* w : adapter.AllWillies()) {
        if (w == me || adapter.IsDead(w) || !clone::MayBeFoe(online, mine, online ? team(w) : clone::kTeamUnknown)) continue;
        const FVector p = adapter.ActorLocation(w);
        const double d = (p.x - at.x) * (p.x - at.x) + (p.y - at.y) * (p.y - at.y) + (p.z - at.z) * (p.z - at.z);
        if (d < best) { best = d; foe_ = w; foeIndex_ = ObjectIndex(w); }
    }
}

bool StyleRecorder::Open(const std::string& name) {
    if (opened_) return !path_.empty();
    opened_ = true;
    CreateDirectoryW(ModFilePath(kDir).c_str(), nullptr);
    const std::wstring path = InClones(clone::FileStem(name));
    const std::vector<uint8_t> header = clone::FileHeader();
    if (FILE* f = _wfopen(path.c_str(), L"r+b")) {   // play goes on the end of what is there
        uint8_t got[64] = {};
        const size_t n = std::fread(got, 1, header.size(), f);
        _fseeki64(f, 0, SEEK_END);
        const long long size = _ftelli64(f);
        const bool ours = clone::IsFileHeader(got, n);
        if (ours) {
            const long long end = GoodEnd(f, size);
            if (end != size) { HSM_LOG("clone: a cut block at the end of the file is dropped"); _chsize_s(_fileno(f), end); }
            size_ = end;
        }
        std::fclose(f);
        if (ours) { path_ = path; return true; }
        // An older file: its rows are not this version's, and it holds someone's play, so it moves and a new one starts.
        const std::string stem = clone::FileStem(name);
        if (!MoveFileW(path.c_str(), (path + L".old").c_str())) { HSM_LOG("clone: Clones/%s.hsmclone is not a clone file of this version and could not move; nothing is written", stem.c_str()); return false; }
        HSM_LOG("clone: Clones/%s.hsmclone was of an older version: it now ends in .old, and recording starts a new file", stem.c_str());
    }
    path_ = path;
    std::vector<uint8_t> first = header;
    const std::vector<uint8_t> n = NameBlock(name);
    first.insert(first.end(), n.begin(), n.end());
    Append(first);
    return !path_.empty();
}

void StyleRecorder::Append(const std::vector<uint8_t>& bytes) {
    if (path_.empty() || bytes.empty()) return;
    FILE* f = _wfopen(path_.c_str(), L"ab");
    const bool ok = f && std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (f) std::fclose(f);
    size_ += (long long)(bytes.size());
    if (!ok) { HSM_LOG("clone: writing to Clones/ failed; recording stops"); path_.clear(); on_ = false; }
    else if (size_t(size_) >= clone::kMaxWrite) { HSM_LOG("clone: the file is full; recording stops"); path_.clear(); on_ = false; }
}

void StyleRecorder::Flush() {
    CountNew();
    if (opened_) Append(collector_.Take());
    else collector_.Take();
    counted_ = 0;
}

void StyleRecorder::Tick(Adapter& adapter, UObject* me, UObject* clone, bool fightLevel, bool online, bool paused, float dt, const std::string& name) {
    if (!on_) return;
    live_ = false;
    if (!fightLevel || !me) {
        if (me_) { Flush(); rig_.Unbind(); foeRig_.Unbind(); }
        me_ = nullptr; foe_ = nullptr; kitSeen_ = false;
        if (!fightLevel) watch_.ForgetLevel();   // the old level's functions go with it
        else watch_.Forget();
        return;
    }
    Hook(adapter, me);
    me_ = me;
    const InputFrame f = input_.Take(0);
    const double now = SteadyMs();
    if (now >= nextFoeMs_) { nextFoeMs_ = now + kFindFoeMs; FindFoe(adapter, me, online); }
    const bool foeThere = foe_ && adapter.StillValid(foe_, foeIndex_);
    const bool skip = clone && !recordClones_;   // record_clones=0: nothing while a clone is in, as before pre13
    if (foeThere && !paused && !skip) watch_.Read(me, foe_);   // before the check below: a death is the last event of a fight
    else watch_.Pause();
    if (!foeThere || adapter.IsDead(foe_) || adapter.IsDead(me) || paused || skip) { collector_.Break(); CountNew(); return; }
    if (clone && foe_ == clone && !cloneLogged_) { cloneLogged_ = true; HSM_LOG("clone: recording your fight against the clone too (record_clones=0 stops it)"); }
    if (online && !onlineLogged_) { onlineLogged_ = true; HSM_LOG("clone: recording in a Steam lobby too (record_online=0 stops it)"); }
    if (!Open(name)) { on_ = false; return; }
    if (!kitSeen_) {   // once a level: the kit the clone will wear is the one last fought in
        kitSeen_ = true;
        std::vector<uint8_t> kit;
        if (adapter.ExportPassport(kit) && kit != kit_) { kit_ = std::move(kit); Append(clone::EncodeBlock(clone::Block::Kit, kit_.data(), kit_.size())); }
    }
    float sens = 0; bool invert = false;   // the mouse axes are kept before the game scales them: the scale goes with them
    if (adapter.MouseLook(sens, invert)) {
        const clone::LookScale look = clone::MakeLook(sens, invert);
        if (look.known() && !(look == collector_.look())) {
            if (collector_.samples()) Flush();
            collector_.SetLook(look);
            HSM_LOG("clone: recording at mouse sensitivity %.2f%s", look.sens, look.invert ? ", inverted" : "");
        }
    }
    clone::Played p{dt * 1000.f, f.axes, f.buttons, 0, 0};
    if (!adapter.ControlRotation(0, p.yaw, p.pitch)) { collector_.Break(); CountNew(); return; }
    live_ = true;
    seconds_ += double(dt);
    if (collector_.Add(p)) {
        if ((rig_.Willie() == me || rig_.Bind(me)) && (foeRig_.Willie() == foe_ || foeRig_.Bind(foe_))) {
            sync::PoseFrame mine, theirs;
            VitalsMsg mv{}, tv{};
            if (rig_.Read(uint32_t(now), 0, &mine, nullptr) && foeRig_.Read(uint32_t(now), 0, &theirs, nullptr) &&
                adapter.ReadVitals(me, 0, mv) && adapter.ReadVitals(foe_, 0, tv)) {
                if (now >= nextLooseMs_) {
                    nextLooseMs_ = now + kFindLooseMs;
                    const sync::V3 at = mine.bones[0].pos; FVector v{};
                    looseThere_ = adapter.NearestLooseWeapon({at.x, at.y, at.z}, v);
                    loose_ = {v.x, v.y, v.z};
                }
                watch_.ReadArmour(adapter, me, foe_);   // every couple of seconds; an Armour event when it changes
                if (now >= nextTypesMs_) { nextTypesMs_ = now + kReadTypesMs; adapter.ReadForgedTypes(me, forged_); adapter.ReadForgedTypes(foe_, foeForged_); }
                if (mv.state & kVitalsStateGiveUp) collector_.Break();   // the fight is over for me: no style in this
                else collector_.Row({&mine, &mv, watch_.cover(clone::kMe), forged_}, {&theirs, &tv, watch_.cover(clone::kFoe), foeForged_}, looseThere_ ? &loose_ : nullptr, uint64_t(uint32_t(foeIndex_)));
            }
        }
    }
    CountNew();
    if (collector_.samples() >= kFlushSamples) Flush();
}

}  // namespace hsm::game
