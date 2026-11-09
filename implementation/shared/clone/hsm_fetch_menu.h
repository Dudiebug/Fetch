// The Fetch menu (P in game): what it shows and what each key does to the settings. The game side draws it and reads the keys;
// everything a key does is here. docs/CLONE.md.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "hsm_clone_drill.h"

namespace hsm::clone {

enum class MenuKey : uint8_t { None, Up, Down, Left, Right, Enter, Close };
enum class MenuRow : uint8_t { Record, Clone, Brain, CloneWeapon, Duel, Respawn, Training, TrainingLength, Close };
constexpr int kMenuRows = 9;
constexpr int kDrillStepMinutes = 5;
constexpr const char* kBrainNames[3] = {"auto", "model", "replay"};   // BrainSetting order (hsm_model.h)

// What Fetch.ini holds, as the menu changes it. clone: a name in Clones/, "1" the first there, empty none.
struct MenuSettings {
    bool record = false;
    std::string clone;
    uint8_t brain = 0;   // kBrainNames
    bool duel = true;
    uint8_t cloneWeapon = kCloneKit;   // CloneWeaponToken
    uint8_t drill = 0;                 // DrillMode: 0 full tour, 1 fill gaps
    int drillMinutes = 20;
    bool training = false;             // a session runs now (shown; Enter starts or stops one)
};

enum class MenuChange : uint8_t { None, Record, Clone, Brain, CloneWeapon, Duel, Respawn, Drill, DrillMinutes, Training, Closed };

inline bool SameName(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        auto low = [](char c) { return char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c); };
        if (low(a[i]) != low(b[i])) return false;
    }
    return true;
}

// The clone after `current` in [none, clones...], `step` +1 or -1, wrapping. A current one not in the list (a name since
// deleted, or "1") counts as before the first.
inline std::string NextClone(const std::string& current, const std::vector<std::string>& clones, int step) {
    const int n = int(clones.size()) + 1;   // 0: none
    int at = current.empty() ? 0 : -1;
    for (size_t i = 0; i < clones.size() && at < 0; ++i) if (SameName(clones[i], current)) at = int(i) + 1;
    if (at < 0) at = step > 0 ? 0 : 1;
    at = ((at + step) % n + n) % n;
    return at == 0 ? std::string{} : clones[size_t(at - 1)];
}

class Menu {
public:
    bool open() const { return open_; }
    void Open() { open_ = true; }
    void Close() { open_ = false; }
    MenuRow row() const { return MenuRow(row_); }
    // A key while open. Up and down choose a row and wrap; left and right change it; Enter changes it forwards or runs it.
    MenuChange Key(MenuKey k, MenuSettings& s, const std::vector<std::string>& clones) {
        if (!open_ || k == MenuKey::None) return MenuChange::None;
        if (k == MenuKey::Close) { open_ = false; return MenuChange::Closed; }
        if (k == MenuKey::Up) { row_ = (row_ + kMenuRows - 1) % kMenuRows; return MenuChange::None; }
        if (k == MenuKey::Down) { row_ = (row_ + 1) % kMenuRows; return MenuChange::None; }
        const int step = k == MenuKey::Left ? -1 : 1;
        switch (MenuRow(row_)) {
            case MenuRow::Record: s.record = !s.record; return MenuChange::Record;
            case MenuRow::Clone: {
                const std::string next = NextClone(s.clone, clones, step);
                if (next == s.clone) return MenuChange::None;
                s.clone = next;
                return MenuChange::Clone;
            }
            case MenuRow::Brain: s.brain = uint8_t((s.brain + 3 + step) % 3); return MenuChange::Brain;
            case MenuRow::CloneWeapon:
                s.cloneWeapon = uint8_t((s.cloneWeapon % kCloneWeaponChoices + kCloneWeaponChoices + step) % kCloneWeaponChoices);
                return MenuChange::CloneWeapon;
            case MenuRow::Duel: s.duel = !s.duel; return MenuChange::Duel;
            case MenuRow::Respawn: return k == MenuKey::Enter ? MenuChange::Respawn : MenuChange::None;
            // Left and right choose the kind of session; Enter starts or stops one.
            case MenuRow::Training:
                if (k == MenuKey::Enter) return MenuChange::Training;
                s.drill = uint8_t(1 - (s.drill & 1)); return MenuChange::Drill;
            case MenuRow::TrainingLength: {
                const int m = std::clamp((s.drillMinutes + step * kDrillStepMinutes) / kDrillStepMinutes * kDrillStepMinutes, kMinDrillMinutes, kMaxDrillMinutes);
                if (m == s.drillMinutes) return MenuChange::None;
                s.drillMinutes = m; return MenuChange::DrillMinutes;
            }
            case MenuRow::Close: if (k != MenuKey::Enter) return MenuChange::None; open_ = false; return MenuChange::Closed;
        }
        return MenuChange::None;
    }
private:
    bool open_ = false;
    int row_ = 0;
};

inline const char* MenuLabel(MenuRow r) {
    switch (r) {
        case MenuRow::Record: return "Record my fighting";
        case MenuRow::Clone: return "Clone to fight";
        case MenuRow::Brain: return "Clone brain";
        case MenuRow::CloneWeapon: return "Clone's weapon";
        case MenuRow::Duel: return "One on one";
        case MenuRow::Respawn: return "Bring the clone back";
        case MenuRow::Training: return "Training session";
        case MenuRow::TrainingLength: return "Session length";
        case MenuRow::Close: return "Close";
    }
    return "";
}

inline std::string MenuValue(MenuRow r, const MenuSettings& s) {
    switch (r) {
        case MenuRow::Record: return s.record ? "ON" : "OFF";
        case MenuRow::Clone: return s.clone.empty() ? "none" : s.clone == "1" ? "the first in Clones" : s.clone;
        case MenuRow::Brain: return kBrainNames[s.brain % 3];
        case MenuRow::CloneWeapon: return CloneWeaponLabel(s.cloneWeapon);
        case MenuRow::Duel: return s.duel ? "ON" : "OFF";
        case MenuRow::Training: return std::string(s.drill & 1 ? "fill gaps" : "full tour") + (s.training ? " (running, Enter stops)" : " (Enter starts)");
        case MenuRow::TrainingLength: return std::to_string(s.drillMinutes) + " min";
        default: return "";
    }
}

// Fetch.ini menu_key= as a Windows virtual-key code: a letter, a digit 1-9, F1 to F12 or ';', any case, spaces round it
// ignored. 0: no menu ("0", "off" or empty). -1: not a key (the caller says so and keeps the default).
// ';' by default: P is the game's own pause key, so it paused the game and the menu would not open over the pause menu.
constexpr int kMenuKeyDefault = 0xBA;   // ';' (VK_OEM_1)
inline int ParseMenuKey(const std::string& value) {
    std::string s;
    for (char c : value) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s.push_back(char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    if (s.empty() || s == "0" || s == "OFF") return 0;
    if (s.size() == 1 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '1' && s[0] <= '9'))) return s[0];
    if (s == ";") return 0xBA;                                   // VK_OEM_1 (US layout)
    if (s[0] == 'F' && (s.size() == 2 || s.size() == 3)) {
        int n = 0;
        for (size_t i = 1; i < s.size(); ++i) { if (s[i] < '0' || s[i] > '9') return -1; n = n * 10 + (s[i] - '0'); }
        if (n >= 1 && n <= 12) return 0x70 + n - 1;               // VK_F1..VK_F12
    }
    return -1;
}

// Why a key Fetch knows cannot open the menu, or null when it can: the game's pause key, or one the open menu moves with.
inline const char* MenuKeyClash(int vk) {
    if (vk == 'P') return "the game's pause key";
    if (vk == 'W' || vk == 'S' || vk == 'A' || vk == 'D') return "a key the menu moves with";
    return nullptr;
}

// A key ParseMenuKey gives, as the menu and the log name it.
inline std::string MenuKeyName(int vk) {
    if (vk == 0xBA) return ";";
    if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x70 + 1);
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '1' && vk <= '9')) return std::string(1, char(vk));
    return "?";
}

// "12:05", or "1:02:05" past an hour.
inline std::string Clock(double seconds) {
    const long long t = seconds > 0 ? (long long)(seconds) : 0;
    char b[32];
    if (t >= 3600) std::snprintf(b, sizeof b, "%lld:%02lld:%02lld", t / 3600, t / 60 % 60, t % 60);
    else std::snprintf(b, sizeof b, "%lld:%02lld", t / 60, t % 60);
    return b;
}

}  // namespace hsm::clone
