// What a recording keeps of the player's input past the samples: the mouse sensitivity the play was recorded at, and every key
// press that went to something other than a fighter (an InpActEvt_ function of a mod's gun, say: the fighter's own keys are in
// the samples). It goes in an Input block after the Play block's Events and More blocks (Collector::Take); an older Fetch skips
// a block kind it does not know, and a file from before it has none: its sensitivity is not known, and it has no such keys.
// Pure logic: client/Fetch reads the game, docs/CLONE.md.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace hsm::clone {

// The game's "Mouse Sensitivity" and "Invert Mouse" (GI_Settings) while the play was recorded. sens 0: not known. guessed: the
// recording did not keep it, and it was read off its turns (LookGuess, hsm_clone.h).
struct LookScale {
    float sens = 0;
    bool invert = false;
    bool guessed = false;
    bool known() const { return sens > 0; }
    bool operator==(const LookScale&) const = default;
};
constexpr float kMinSens = 0.01f, kMaxSens = 100.f;
inline LookScale MakeLook(float sens, bool invert) { return sens >= kMinSens && sens <= kMaxSens ? LookScale{sens, invert} : LookScale{}; }

// A key press: before which sample of its Play block, and which class and function (indices into InputLog::names).
struct KeyPress { uint32_t sample = 0; uint16_t cls = 0, fn = 0; };
constexpr uint32_t kInputVersion = 1, kMaxKeyNames = 1024, kMaxKeyPresses = 1000000;
constexpr size_t kMaxKeyName = 96;
constexpr uint16_t kNoKeyName = 0xFFFF;

// A name as UE spells a class or function: letters, digits and _ only, 1 to kMaxKeyName long.
inline bool ValidKeyName(const std::string& s) {
    if (s.empty() || s.size() > kMaxKeyName) return false;
    for (char c : s)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}

struct InputLog {
    LookScale look;
    std::vector<std::string> names;
    std::vector<KeyPress> presses;
    // The index of a name, added if new. kNoKeyName: not a valid name, or the table is full.
    uint16_t Intern(const std::string& s) {
        if (!ValidKeyName(s)) return kNoKeyName;
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == s) return uint16_t(i);
        if (names.size() >= kMaxKeyNames) return kNoKeyName;
        names.push_back(s);
        return uint16_t(names.size() - 1);
    }
    // A press of cls's fn before sample `at`. false: not kept (a bad name, or the table or the list full).
    bool Press(uint32_t at, const std::string& cls, const std::string& fn) {
        if (presses.size() >= kMaxKeyPresses) return false;
        const uint16_t c = Intern(cls), f = Intern(fn);
        if (c == kNoKeyName || f == kNoKeyName) return false;
        presses.push_back({at, c, f});
        return true;
    }
    void Clear() { names.clear(); presses.clear(); }   // the look stays: it is what the game is set to now
};

namespace detail {
inline void Put32(std::vector<uint8_t>& o, uint32_t v) { for (int i = 0; i < 4; ++i) o.push_back(uint8_t(v >> (8 * i))); }
inline uint32_t Get32(const uint8_t* d) { uint32_t v; std::memcpy(&v, d, 4); return v; }
}  // namespace detail

// u32 version, f32 sens, u8 invert, 3 bytes 0; u32 names, each u8 length and its bytes; u32 presses, each u32 sample, u16 cls, u16 fn.
inline std::vector<uint8_t> InputPayload(const InputLog& log) {
    std::vector<uint8_t> o;
    detail::Put32(o, kInputVersion);
    uint32_t sens;
    std::memcpy(&sens, &log.look.sens, 4);
    detail::Put32(o, sens);
    o.push_back(log.look.invert ? 1 : 0); o.push_back(0); o.push_back(0); o.push_back(0);
    detail::Put32(o, uint32_t(log.names.size()));
    for (const std::string& n : log.names) { o.push_back(uint8_t(n.size())); o.insert(o.end(), n.begin(), n.end()); }
    detail::Put32(o, uint32_t(log.presses.size()));
    for (const KeyPress& k : log.presses) {
        detail::Put32(o, k.sample);
        o.push_back(uint8_t(k.cls)); o.push_back(uint8_t(k.cls >> 8)); o.push_back(uint8_t(k.fn)); o.push_back(uint8_t(k.fn >> 8));
    }
    return o;
}

// false: not well formed, or a press is not valid for a Play block of `samples` samples (a name it does not have, out of time
// order, at or past the block's end). Then none of it is kept: a file is someone else's.
inline bool ReadInputPayload(const uint8_t* d, size_t len, uint32_t samples, InputLog& out) {
    size_t p = 0;
    auto have = [&](size_t n) { return len - p >= n; };
    if (!have(16) || detail::Get32(d) != kInputVersion) return false;
    float sens;
    std::memcpy(&sens, d + 4, 4);
    if (d[8] > 1) return false;
    InputLog log;
    log.look = MakeLook(sens, d[8] != 0);   // nonsense reads as not known
    const uint32_t names = detail::Get32(d + 12);
    p = 16;
    if (names > kMaxKeyNames) return false;
    for (uint32_t i = 0; i < names; ++i) {
        if (!have(1)) return false;
        const size_t n = d[p++];
        if (!have(n)) return false;
        std::string s(reinterpret_cast<const char*>(d + p), n);
        p += n;
        if (!ValidKeyName(s)) return false;
        log.names.push_back(std::move(s));
    }
    if (!have(4)) return false;
    const uint32_t presses = detail::Get32(d + p);
    p += 4;
    if (presses > kMaxKeyPresses || len - p != size_t(presses) * 8) return false;
    uint32_t last = 0;
    for (uint32_t i = 0; i < presses; ++i, p += 8) {
        KeyPress k;
        k.sample = detail::Get32(d + p);
        k.cls = uint16_t(d[p + 4] | d[p + 5] << 8); k.fn = uint16_t(d[p + 6] | d[p + 7] << 8);
        if (k.sample >= samples || k.sample < last || k.cls >= names || k.fn >= names) return false;
        last = k.sample;
        log.presses.push_back(k);
    }
    out = std::move(log);
    return true;
}

// The pressed and released ends of one key are two functions, "InpActEvt_<key>_K2Node_<node>_<n>", the n told apart: what is
// before "_K2Node" says which key it is.
inline std::string KeyOf(const std::string& fn) {
    const size_t at = fn.find("_K2Node");
    return at == std::string::npos ? fn : fn.substr(0, at);
}

}  // namespace hsm::clone
