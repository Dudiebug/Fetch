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
enum class MenuRow : uint8_t { Record, RecordTo, Rename, Clone, Brain, CloneWeapon, Duel, Respawn, Training, TrainingLength, Merge, ShowMind, Close };
constexpr int kMenuRows = 13;
constexpr int kMenuLines = 16;   // lines a page shows at most (the Merge page scrolls its list of recordings)
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
    // The recording play goes into (Fetch.ini name=): empty for the player's Steam name, which is `mine` (empty: not known yet).
    std::string recordTo, mine;
    bool targetThere = false;          // the target's file is in Clones/ (Rename needs one)
    std::string renameTo;              // MenuChange::Rename: the new name typed
    // MenuChange::MergeMake: the recordings to merge, in the order picked, and the merged clone's name.
    std::vector<std::string> mergeFrom;
    std::string mergeName;
    bool merging = false;              // a merge runs now (shown; no second one meanwhile)
    bool showMind = false;             // show_mind=: the overlay that shows the clone thinking
    bool online = false;               // in a Steam lobby: the clone's rows read "offline only" and do nothing (OfflineOnly)
};

// The rows of what only happens offline: the clone, its brain and weapon, the duel, training and the clone-thinking overlay. In a
// Steam lobby the menu still records, names and merges recordings, but these rows cannot be changed, and no key on them spawns a
// clone or starts a session.
inline bool OfflineOnly(MenuRow r) {
    switch (r) {
        case MenuRow::Clone: case MenuRow::Brain: case MenuRow::CloneWeapon: case MenuRow::Duel: case MenuRow::Respawn:
        case MenuRow::Training: case MenuRow::TrainingLength: case MenuRow::ShowMind: return true;
        default: return false;
    }
}
constexpr const char* kOfflineOnly = "offline only";

// A line of the menu as drawn: the main page's rows, or the Merge page's.
struct MenuLine {
    std::string label, value;
    bool chosen = false;   // the cursor is on it
    bool on = false;       // shown lit: a setting ON, a recording picked
};

enum class MenuChange : uint8_t { None, Record, Clone, Brain, CloneWeapon, Duel, Respawn, Drill, DrillMinutes, Training, Closed, RecordTo, Rename, MergeMake, ShowMind };

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

// Windows' device names: Clones/CON.hsmclone is the console, not a file, whatever the extension.
inline bool IsReservedName(const std::string& stem) {
    std::string u;
    for (char c : stem) u.push_back(char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    if (u == "CON" || u == "PRN" || u == "AUX" || u == "NUL") return true;
    return u.size() == 4 && (u.compare(0, 3, "COM") == 0 || u.compare(0, 3, "LPT") == 0) && u[3] >= '1' && u[3] <= '9';
}

enum class NameCheck : uint8_t { Ok, Empty, Reserved, Taken };
inline const char* NameCheckText(NameCheck c) {
    switch (c) {
        case NameCheck::Ok: return "";
        case NameCheck::Empty: return "Type a name first";
        case NameCheck::Reserved: return "Windows keeps that name for itself: choose another";
        case NameCheck::Taken: return "There is a recording of that name already";
    }
    return "";
}

// A name typed in the menu: only what a file name keeps (FileStem), so what is typed is what the file is called, and no longer
// than one.
class TextEntry {
public:
    bool on() const { return on_; }
    const std::string& text() const { return text_; }
    void Begin(const std::string& prefill) {
        on_ = true; text_.clear();
        for (char c : prefill) Add(c);
    }
    bool Add(char c) {
        if (!on_ || text_.size() >= kMaxName) return false;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || (c == ' ' && !text_.empty());
        if (ok) text_.push_back(c);
        return ok;
    }
    void Back() { if (on_ && !text_.empty()) text_.pop_back(); }
    void Cancel() { on_ = false; text_.clear(); }
    // Whether the text can be a recording's name: taken, the names of the recordings there (a name that makes the same file, as
    // "A B" and "A_B" do, counts). The text, its spaces at the end gone, stays for the caller to take.
    NameCheck Check(const std::vector<std::string>& taken) {
        while (!text_.empty() && text_.back() == ' ') text_.pop_back();
        if (text_.empty()) return NameCheck::Empty;
        const std::string stem = FileStem(text_);
        if (IsReservedName(stem)) return NameCheck::Reserved;
        for (const std::string& t : taken) if (SameName(FileStem(t), stem)) return NameCheck::Taken;
        return NameCheck::Ok;
    }
    void End() { on_ = false; }
private:
    bool on_ = false;
    std::string text_;
};

// A key while a name is typed: a character, or what Backspace, Enter and Esc do.
struct TypeKey {
    enum Kind : uint8_t { None, Char, Back, Enter, Esc } kind = None;
    char c = 0;
};

// Where Record to goes from `current`, step +1 or -1, through [the player's own, recordings..., New...], wrapping. A recording
// of the player's own name (mine) is their own; one not in the list (typed, not yet fought into) counts as before the first.
// Returns 0 for the player's own, i + 1 for recordings[i], -1 for New.
inline int NextRecording(const std::string& current, bool onNew, const std::vector<std::string>& recordings, const std::string& mine, int step) {
    auto own = [&](const std::string& r) { return !mine.empty() && SameName(FileStem(r), FileStem(mine)); };
    std::vector<size_t> list;
    for (size_t i = 0; i < recordings.size(); ++i) if (!own(recordings[i])) list.push_back(i);
    const int n = int(list.size()) + 2;   // own, recordings, New
    int at = -1;
    if (onNew) at = n - 1;
    else if (current.empty() || own(current)) at = 0;
    else for (size_t i = 0; i < list.size() && at < 0; ++i) if (SameName(FileStem(recordings[list[i]]), FileStem(current))) at = int(i) + 1;
    if (at < 0) at = step > 0 ? 0 : 1;
    at = ((at + step) % n + n) % n;
    if (at == n - 1) return -1;
    return at == 0 ? 0 : int(list[size_t(at - 1)]) + 1;
}

inline const char* MenuLabel(MenuRow r);

class Menu {
public:
    bool open() const { return open_; }
    void Open() { open_ = true; }
    void Close() { open_ = false; entry_.Cancel(); onNew_ = false; note_.clear(); merge_ = false; }
    MenuRow row() const { return MenuRow(row_); }
    // The Merge page: the recordings in Clones/, then the merged clone's name, Make and Back. Enter, A or D picks a recording or
    // takes it out again; the order they are picked in is the order they are merged in, and the first with a look gives the clone
    // its look. Esc goes back to the main page.
    bool onMerge() const { return merge_; }
    const std::vector<std::string>& picks() const { return picks_; }
    const std::string& mergeName() const { return mergeName_; }
    bool typing() const { return entry_.on(); }
    const std::string& typed() const { return entry_.text(); }
    // Why the last thing asked could not be done (a name typed, Rename with no file); cleared by the next key.
    const std::string& note() const { return note_; }
    bool onNew() const { return onNew_; }
    // The row's value as shown: what is being typed, on the row it is typed for.
    std::string Value(MenuRow r, const MenuSettings& s) const;
    // The name play goes into: the one chosen, else the player's own.
    static std::string Target(const MenuSettings& s) { return s.recordTo.empty() ? s.mine : s.recordTo; }
    // A key while a name is typed. Enter keeps the name: on Record to, it is the recording play goes into (MenuChange::RecordTo;
    // a recording there already is fine); on Rename, the target's new name (MenuChange::Rename, s.renameTo; not one there). Esc
    // leaves things as they were. recordings: the names of the recordings in Clones/.
    // On the Merge page, Enter keeps the merged clone's name (not one there).
    MenuChange Type(TypeKey k, MenuSettings& s, const std::vector<std::string>& recordings) {
        if (!open_ || !entry_.on() || k.kind == TypeKey::None) return MenuChange::None;
        note_.clear();
        switch (k.kind) {
            case TypeKey::Char: entry_.Add(k.c); return MenuChange::None;
            case TypeKey::Back: entry_.Back(); return MenuChange::None;
            case TypeKey::Esc: entry_.Cancel(); return MenuChange::None;
            case TypeKey::Enter: {
                if (merge_) {
                    const NameCheck c = entry_.Check(recordings);
                    if (c != NameCheck::Ok) { note_ = NameCheckText(c); return MenuChange::None; }
                    mergeName_ = entry_.text();
                    entry_.End();
                    mcur_ = int(recordings.size()) + 1;   // on to Make
                    return MenuChange::None;
                }
                const bool rename = MenuRow(row_) == MenuRow::Rename;
                std::vector<std::string> taken;   // a rename may change its own name's case
                if (rename) for (const std::string& r : recordings) if (!SameName(FileStem(r), FileStem(Target(s)))) taken.push_back(r);
                const NameCheck c = entry_.Check(taken);
                if (c != NameCheck::Ok) { note_ = NameCheckText(c); return MenuChange::None; }
                if (rename && entry_.text() == Target(s)) { entry_.Cancel(); return MenuChange::None; }   // no change
                const std::string name = entry_.text();
                entry_.End();
                if (rename) { s.renameTo = name; return MenuChange::Rename; }
                onNew_ = false;
                s.recordTo = name;
                return MenuChange::RecordTo;
            }
            default: return MenuChange::None;
        }
    }
    // A key while open. Up and down choose a row and wrap; left and right change it; Enter changes it forwards or runs it.
    MenuChange Key(MenuKey k, MenuSettings& s, const std::vector<std::string>& clones, const std::vector<std::string>& recordings = {}) {
        if (!open_ || k == MenuKey::None || entry_.on()) return MenuChange::None;
        note_.clear();
        if (merge_) return MergeKey(k, s, recordings);
        if (k == MenuKey::Close) { open_ = false; return MenuChange::Closed; }
        if (k == MenuKey::Up) { row_ = (row_ + kMenuRows - 1) % kMenuRows; onNew_ = false; return MenuChange::None; }
        if (k == MenuKey::Down) { row_ = (row_ + 1) % kMenuRows; onNew_ = false; return MenuChange::None; }
        const int step = k == MenuKey::Left ? -1 : 1;
        if (s.online && OfflineOnly(MenuRow(row_))) { note_ = std::string(MenuLabel(MenuRow(row_))) + ": offline only"; return MenuChange::None; }
        switch (MenuRow(row_)) {
            case MenuRow::Record: s.record = !s.record; return MenuChange::Record;
            // Left and right go through the recordings, then New; Enter on New types a name.
            case MenuRow::RecordTo: {
                if (k == MenuKey::Enter) { if (onNew_) entry_.Begin(""); return MenuChange::None; }
                const int next = NextRecording(s.recordTo, onNew_, recordings, s.mine, step);
                onNew_ = next < 0;
                if (onNew_) return MenuChange::None;
                const std::string to = next == 0 ? std::string{} : recordings[size_t(next - 1)];
                if (to == s.recordTo) return MenuChange::None;
                s.recordTo = to;
                return MenuChange::RecordTo;
            }
            case MenuRow::Rename:
                if (k != MenuKey::Enter) return MenuChange::None;
                if (!s.targetThere) { note_ = "Nothing to rename yet: a recording is made by its first fight"; return MenuChange::None; }
                entry_.Begin(Target(s));
                return MenuChange::None;
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
            case MenuRow::Merge:
                if (k != MenuKey::Enter) return MenuChange::None;
                if (recordings.size() < 2) { note_ = "Merge: there are fewer than two recordings in Clones"; return MenuChange::None; }
                merge_ = true; mcur_ = 0;
                // What was picked before stays picked while it is there.
                picks_.erase(std::remove_if(picks_.begin(), picks_.end(), [&](const std::string& p) {
                                 return std::none_of(recordings.begin(), recordings.end(), [&](const std::string& r) { return SameName(r, p); });
                             }), picks_.end());
                return MenuChange::None;
            case MenuRow::ShowMind: s.showMind = !s.showMind; return MenuChange::ShowMind;
            case MenuRow::Close: if (k != MenuKey::Enter) return MenuChange::None; open_ = false; return MenuChange::Closed;
        }
        return MenuChange::None;
    }
    // The lines to draw: the main page's rows, or the Merge page's (a window of the recordings round the cursor, then Name, Make
    // and Back). title: what the page is.
    std::vector<MenuLine> Lines(const MenuSettings& s, const std::vector<std::string>& recordings, std::string& title) const;
private:
    MenuChange MergeKey(MenuKey k, MenuSettings& s, const std::vector<std::string>& recordings) {
        const int n = int(recordings.size()), items = n + 3;   // recordings, Name, Make, Back
        if (k == MenuKey::Close) { merge_ = false; return MenuChange::None; }
        if (k == MenuKey::Up) { mcur_ = (mcur_ + items - 1) % items; return MenuChange::None; }
        if (k == MenuKey::Down) { mcur_ = (mcur_ + 1) % items; return MenuChange::None; }
        mcur_ = std::min(mcur_, items - 1);
        if (mcur_ < n) {   // Enter, A or D: picked, or not
            const std::string& r = recordings[size_t(mcur_)];
            const auto at = std::find_if(picks_.begin(), picks_.end(), [&](const std::string& p) { return SameName(p, r); });
            if (at != picks_.end()) picks_.erase(at);
            else picks_.push_back(r);
            return MenuChange::None;
        }
        if (k != MenuKey::Enter) return MenuChange::None;
        if (mcur_ == n) { entry_.Begin(mergeName_); return MenuChange::None; }
        if (mcur_ == n + 2) { merge_ = false; return MenuChange::None; }
        // Make
        if (s.merging) { note_ = "Merge: one is being made now"; return MenuChange::None; }
        if (picks_.size() < 2) { note_ = "Merge: pick two recordings or more"; return MenuChange::None; }
        if (mergeName_.empty()) { note_ = "Merge: type the new clone's name first"; mcur_ = n; return MenuChange::None; }
        TextEntry check;   // the name is checked again: a recording of that name may have come since
        check.Begin(mergeName_);
        if (const NameCheck c = check.Check(recordings); c != NameCheck::Ok) { note_ = NameCheckText(c); mcur_ = n; return MenuChange::None; }
        s.mergeFrom = picks_;
        s.mergeName = mergeName_;
        picks_.clear(); mergeName_.clear();
        merge_ = false;
        return MenuChange::MergeMake;
    }
    bool open_ = false;
    int row_ = 0;
    bool onNew_ = false;   // Record to shows New...
    TextEntry entry_;
    std::string note_;
    bool merge_ = false;   // on the Merge page
    int mcur_ = 0;         // its cursor: a recording, then Name, Make, Back
    std::vector<std::string> picks_;
    std::string mergeName_;
};

inline const char* MenuLabel(MenuRow r) {
    switch (r) {
        case MenuRow::Record: return "Record my fighting";
        case MenuRow::RecordTo: return "Record to";
        case MenuRow::Rename: return "Rename this recording";
        case MenuRow::Clone: return "Clone to fight";
        case MenuRow::Brain: return "Clone brain";
        case MenuRow::CloneWeapon: return "Clone's weapon";
        case MenuRow::Duel: return "One on one";
        case MenuRow::Respawn: return "Bring the clone back";
        case MenuRow::Training: return "Training session";
        case MenuRow::TrainingLength: return "Session length";
        case MenuRow::Merge: return "Merge recordings";
        case MenuRow::ShowMind: return "Show the clone thinking";
        case MenuRow::Close: return "Close";
    }
    return "";
}

inline std::string MenuValue(MenuRow r, const MenuSettings& s) {
    if (s.online && OfflineOnly(r)) return kOfflineOnly;
    switch (r) {
        case MenuRow::Record: return s.record ? "ON" : "OFF";
        case MenuRow::RecordTo: return !s.recordTo.empty() ? s.recordTo : s.mine.empty() ? "my Steam name" : "my Steam name (" + s.mine + ")";
        case MenuRow::Rename: return s.targetThere ? "Enter: type a new name" : "";
        case MenuRow::Clone: return s.clone.empty() ? "none" : s.clone == "1" ? "the first in Clones" : s.clone;
        case MenuRow::Brain: return kBrainNames[s.brain % 3];
        case MenuRow::CloneWeapon: return CloneWeaponLabel(s.cloneWeapon);
        case MenuRow::Duel: return s.duel ? "ON" : "OFF";
        case MenuRow::Training: return std::string(s.drill & 1 ? "fill gaps" : "full tour") + (s.training ? " (running, Enter stops)" : " (Enter starts)");
        case MenuRow::TrainingLength: return std::to_string(s.drillMinutes) + " min";
        case MenuRow::Merge: return s.merging ? "merging..." : "Enter: pick them";
        case MenuRow::ShowMind: return s.showMind ? "ON" : "OFF";
        default: return "";
    }
}

inline std::string Menu::Value(MenuRow r, const MenuSettings& s) const {
    if (entry_.on() && !merge_ && r == row()) return entry_.text() + "_";
    if (r == MenuRow::RecordTo && onNew_) return "New... (Enter: type a name)";
    return MenuValue(r, s);
}

inline std::vector<MenuLine> Menu::Lines(const MenuSettings& s, const std::vector<std::string>& recordings, std::string& title) const {
    std::vector<MenuLine> o;
    if (!merge_) {
        title = "style clones: record how you fight, then fight yourself";
        for (int r = 0; r < kMenuRows; ++r) {
            const std::string v = Value(MenuRow(r), s);
            o.push_back({MenuLabel(MenuRow(r)), v, r == row_, v == "ON"});
        }
        return o;
    }
    const int n = int(recordings.size()), shown = std::min(n, kMenuLines - 3);
    const int cur = std::min(mcur_, n + 2);
    title = "Merge: pick recordings in the order to merge them (" + std::to_string(picks_.size()) + " picked)";
    int first = cur < n ? std::clamp(cur - shown / 2, 0, n - shown) : n - shown;   // the window keeps the cursor in it
    for (int i = first; i < first + shown; ++i) {
        const std::string& r = recordings[size_t(i)];
        size_t at = 0;
        while (at < picks_.size() && !SameName(picks_[at], r)) ++at;
        const bool picked = at < picks_.size();
        std::string label = (picked ? "[" + std::to_string(at + 1) + "] " : "[  ] ") + r;
        std::string value;
        if (i == first && first > 0) value = std::to_string(first) + " more above";
        if (i == first + shown - 1 && first + shown < n) value = std::to_string(n - first - shown) + " more below";
        o.push_back({label, value, i == cur, picked});
    }
    const std::string name = entry_.on() ? entry_.text() + "_" : mergeName_.empty() ? "Enter: type one" : mergeName_;
    o.push_back({"Name of the new clone", name, cur == n, false});
    const std::string make = s.merging ? "one is being made" : picks_.size() < 2 ? "pick two or more" : mergeName_.empty() ? "name it first"
                                                                : "Enter: merge " + std::to_string(picks_.size());
    o.push_back({"Make it", make, cur == n + 1, false});
    o.push_back({"Back", "", cur == n + 2, false});
    return o;
}

// Fetch.ini menu_key= as a Windows virtual-key code: a letter, a digit 1-9, F1 to F12 or ';', any case, spaces round it
// ignored. 0: no menu ("0", "off" or empty). -1: not a key (the caller says so and keeps the default).
// ';' by default: P is the game's own pause key, so it paused the game and the menu would not open over the pause menu.
constexpr int kMenuKeyDefault = 0xBA;   // ';' (VK_OEM_1)
constexpr int kMindKeyDefault = 0xBB;   // '=' (VK_OEM_PLUS): shows or hides the clone-thinking overlay
inline int ParseMenuKey(const std::string& value) {
    std::string s;
    for (char c : value) if (c != ' ' && c != '\t' && c != '\r' && c != '\n') s.push_back(char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c));
    if (s.empty() || s == "0" || s == "OFF") return 0;
    if (s.size() == 1 && ((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= '1' && s[0] <= '9'))) return s[0];
    if (s == ";") return 0xBA;                                   // VK_OEM_1 (US layout)
    if (s == "=") return 0xBB;                                   // VK_OEM_PLUS, the = and + key
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

// Why a key is one of HSMMod's own (its defaults: chat on Enter, T, Y or U, class bite H, build card K, voice V, and G, B, Tab and
// the arrows), or null. In its lobbies HSMMod leaves its keys alone while the Fetch menu has the keyboard, but a menu or overlay key
// of these would also do HSMMod's thing whenever the menu is shut. Said in the log when a lobby is joined.
inline const char* HsmModKeyClash(int vk) {
    if (vk == 0x0D || vk == 'T' || vk == 'Y' || vk == 'U') return "a key HSMMod's chat may open on";
    if (vk == 'B') return "HSMMod's class bite key";
    if (vk == 'K') return "HSMMod's build card key";
    if (vk == 'V') return "HSMMod's voice key";
    if (vk == 'G' || vk == 'H' || vk == 0x09 || (vk >= 0x25 && vk <= 0x28)) return "one of HSMMod's keys";
    return nullptr;
}

// A key ParseMenuKey gives, as the menu and the log name it.
inline std::string MenuKeyName(int vk) {
    if (vk == 0xBA) return ";";
    if (vk == 0xBB) return "=";
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
