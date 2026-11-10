// Worker thread: reads Fetch.ini and the clone's files, then, if there is anything to do, hooks the GameMode's tick. With
// recording, clones and the menu all off, the mod hooks nothing at all. The menu (;) changes the settings in game and writes
// them back to Fetch.ini.
#include <windows.h>
#include "clone_player.h"
#include "clone_style.h"
#include "clone/hsm_clone_merge.h"
#include "clone/hsm_clone_rules.h"
#include "clone/hsm_fetch_menu.h"
#include "log.h"
#include "mod_link.h"
#include "paths.h"
#include "steam_lobby.h"
#include "training.h"
#include "ui/clone_overlay.h"
#include "ui/fetch_menu.h"
#include "ue/engine.h"
#include "ue/hook.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fetch {
using namespace hsm;
using namespace hsm::ue;

namespace {
constexpr char kDefaultIni[] =
    "; Fetch: style clones (docs/CLONE.md). Clones only ever fight offline.\r\n"
    "[fetch]\r\n"
    "; record=1 writes how you fight to Clones/<name>.hsmclone next to the game, about 25 MB an hour.\r\n"
    "record=0\r\n"
    "; record_clones=0 records nothing while a clone is in the fight. By default your fight against a clone is recorded too.\r\n"
    "record_clones=1\r\n"
    "; record_online=0 records nothing in a Steam lobby (a multiplayer fight). By default your own play there is recorded too.\r\n"
    "record_online=1\r\n"
    "; name= names that file (the menu's Record to sets it, and Rename renames it). Empty: your Steam name.\r\n"
    "name=\r\n"
    "; clone=<name>: offline, the clone in Clones/<name>.hsmmodel or .hsmclone joins each fight level. 1: the first there. 0: none.\r\n"
    "clone=0\r\n"
    "; duel=0 keeps Free Mode's own fighters in the fight. By default it is one against one.\r\n"
    "duel=1\r\n"
    "; brain=auto uses the trained model if there is one. model or replay ask for one of them.\r\n"
    "brain=auto\r\n"
    "; menu_key=; opens the Fetch menu in game: a letter, a digit, F1 to F12 or ;. Not P, the game's pause key. 0: no menu.\r\n"
    "menu_key=;\r\n"
    "; strikes=0 stops Fetch reading how each blow met skin or armour (the game's damage function, watched; it changes nothing).\r\n"
    "strikes=1\r\n"
    "; parries=0 stops Fetch reading each parry (the game's StopThatBlade, watched; it changes nothing).\r\n"
    "parries=1\r\n"
    "; clone_weapon=kit: the clone holds what its recording held. random: a weapon type it has moves for, new each time it comes in.\r\n"
    "; match: your weapon's type, when it has moves for it. Or one type: sword1h sword2h mace1h mace2h axe1h axe2h polearm.\r\n"
    "clone_weapon=kit\r\n"
    "; drill=tour: a training session (the menu) takes every weapon type against every kind of foe. gaps: what your recording is short of.\r\n"
    "drill=tour\r\n"
    "; drill_minutes=20: a training session's length, 5 to 60.\r\n"
    "drill_minutes=20\r\n"
    "; show_mind=1 shows the clone thinking while it fights: what it plays and why, the moments it weighed, what they cost.\r\n"
    "show_mind=0\r\n"
    "; mind_key== shows or hides it in game (the = key): a letter, a digit, F1 to F12, ; or =. 0: no key.\r\n"
    "mind_key==\r\n";

std::string ReadIni(const std::wstring& ini, const wchar_t* key, const wchar_t* fallback) {
    wchar_t w[256];
    const DWORD n = GetPrivateProfileStringW(L"fetch", key, fallback, w, 256, ini.c_str());
    const int len = WideCharToMultiByte(CP_UTF8, 0, w, int(n), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(len > 0 ? len : 0), '\0');
    if (len > 0) WideCharToMultiByte(CP_UTF8, 0, w, int(n), s.data(), len, nullptr, nullptr);
    return s;
}

std::wstring Wide(const std::string& utf8) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, utf8.data(), int(utf8.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// The menu's change, kept for the next start. The rest of the file (comments, other keys) stays as the player left it.
void WriteIni(const wchar_t* key, const std::string& value) {
    if (!WritePrivateProfileStringW(L"fetch", key, Wide(value).c_str(), ModFilePath(L"Fetch.ini").c_str()))
        HSM_LOG("fetch: could not write %ls to Fetch.ini", key);
}

// The clones the menu offers: Clones/*.hsmmodel and *.hsmclone, by name, once each, in name order.
std::vector<std::string> ListClones() {
    std::vector<std::string> out;
    const std::wstring dir = ModFilePath(L"Clones\\");
    for (const wchar_t* pattern : {L"*.hsmmodel", L"*.hsmclone"}) {
        WIN32_FIND_DATAW d;
        HANDLE h = FindFirstFileW((dir + pattern).c_str(), &d);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            const std::wstring file = d.cFileName;
            const std::string stem = Narrow(file.substr(0, file.find_last_of(L'.')));
            if (stem.empty()) continue;
            if (std::none_of(out.begin(), out.end(), [&](const std::string& o) { return clone::SameName(o, stem); })) out.push_back(stem);
        } while (FindNextFileW(h, &d));
        FindClose(h);
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The recordings the menu can record to and rename: Clones/*.hsmclone, by name, in name order.
std::vector<std::string> ListRecordings() {
    std::vector<std::string> out;
    WIN32_FIND_DATAW d;
    HANDLE h = FindFirstFileW(ModFilePath(L"Clones\\*.hsmclone").c_str(), &d);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (d.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        const std::wstring file = d.cFileName;
        if (file.size() < 10 || _wcsicmp(file.c_str() + file.size() - 9, L".hsmclone") != 0) continue;   // not .hsmclone.old
        const std::string stem = Narrow(file.substr(0, file.size() - 9));
        if (!stem.empty()) out.push_back(stem);
    } while (FindNextFileW(h, &d));
    FindClose(h);
    std::sort(out.begin(), out.end());
    return out;
}

bool FileThere(const std::wstring& path) {
    const DWORD a = GetFileAttributesW(path.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// The keyboard as the menu reads it: the OS key state, so it works while the game's own input is held off. Only while the
// game's window has the keyboard: keys typed into another window are not for it.
class Keys {
public:
    Keys() {
        for (int vk = 'A'; vk <= 'Z'; ++vk) Watch(vk);
        for (int vk = '0'; vk <= '9'; ++vk) Watch(vk);
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) Watch(vk);
        for (int vk : {VK_BACK, VK_OEM_MINUS, VK_SUBTRACT, VK_SHIFT}) Watch(vk);
    }
    void Poll() {
        DWORD pid = 0;
        HWND fg = GetForegroundWindow();
        const bool ours = fg && GetWindowThreadProcessId(fg, &pid) && pid == GetCurrentProcessId();
        was_ = now_;
        for (int vk : watched_) {
            if (vk <= 0 || vk >= 256) continue;
            const SHORT s = GetAsyncKeyState(vk);
            now_[size_t(vk)] = ours && (s & 0x8000);
            tapped_[size_t(vk)] = ours && (s & 1) && !now_[size_t(vk)];   // down and up again since the last tick
        }
    }
    void Watch(int vk) { if (vk > 0 && vk < 256 && std::find(watched_.begin(), watched_.end(), vk) == watched_.end()) watched_.push_back(vk); }
    bool Pressed(int vk) const { return vk > 0 && vk < 256 && ((now_[size_t(vk)] && !was_[size_t(vk)]) || tapped_[size_t(vk)]); }
    // While a name is typed: the keys that make one this tick. Backspace held goes on erasing.
    std::vector<clone::TypeKey> Typed() {
        std::vector<clone::TypeKey> out;
        using K = clone::TypeKey;
        const bool shift = now_[VK_SHIFT], upper = shift != ((GetKeyState(VK_CAPITAL) & 1) != 0);
        for (int vk = 'A'; vk <= 'Z'; ++vk) if (Pressed(vk)) out.push_back({K::Char, char(upper ? vk : vk - 'A' + 'a')});
        for (int vk = '0'; vk <= '9'; ++vk) if (Pressed(vk) && !shift) out.push_back({K::Char, char(vk)});
        for (int vk = VK_NUMPAD0; vk <= VK_NUMPAD9; ++vk) if (Pressed(vk)) out.push_back({K::Char, char('0' + vk - VK_NUMPAD0)});
        if (Pressed(VK_SPACE)) out.push_back({K::Char, ' '});
        if (Pressed(VK_OEM_MINUS)) out.push_back({K::Char, shift ? '_' : '-'});
        if (Pressed(VK_SUBTRACT)) out.push_back({K::Char, '-'});
        const ULONGLONG t = GetTickCount64();
        if (Pressed(VK_BACK)) { out.push_back({K::Back}); backAt_ = t + 450; }
        else if (now_[VK_BACK] && t >= backAt_) { out.push_back({K::Back}); backAt_ = t + 50; }
        if (Pressed(VK_RETURN)) out.push_back({K::Enter});
        if (Pressed(VK_ESCAPE)) out.push_back({K::Esc});
        return out;
    }
    clone::MenuKey Nav() const {
        if (Pressed(VK_UP) || Pressed('W')) return clone::MenuKey::Up;
        if (Pressed(VK_DOWN) || Pressed('S')) return clone::MenuKey::Down;
        if (Pressed(VK_LEFT) || Pressed('A')) return clone::MenuKey::Left;
        if (Pressed(VK_RIGHT) || Pressed('D')) return clone::MenuKey::Right;
        if (Pressed(VK_RETURN) || Pressed(VK_SPACE)) return clone::MenuKey::Enter;
        if (Pressed(VK_ESCAPE)) return clone::MenuKey::Close;
        return clone::MenuKey::None;
    }
private:
    std::vector<int> watched_ = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, 'W', 'S', 'A', 'D', VK_RETURN, VK_SPACE, VK_ESCAPE};
    std::array<bool, 256> now_{}, was_{}, tapped_{};
    ULONGLONG backAt_ = 0;
};

Config LoadConfig() {
    const std::wstring ini = ModFilePath(L"Fetch.ini");
    if (GetFileAttributesW(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        HANDLE h = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) { DWORD wrote = 0; WriteFile(h, kDefaultIni, DWORD(sizeof kDefaultIni - 1), &wrote, nullptr); CloseHandle(h); }
    }
    Config c;
    c.record = ReadIni(ini, L"record", L"0") != "0";
    c.name = ReadIni(ini, L"name", L"");
    c.clone = ReadIni(ini, L"clone", L"0");
    if (c.clone == "0") c.clone.clear();
    c.duel = ReadIni(ini, L"duel", L"1") != "0";
    c.brain = clone::ParseBrain(ReadIni(ini, L"brain", L"auto"));
    c.strikes = ReadIni(ini, L"strikes", L"1") != "0";
    c.parries = ReadIni(ini, L"parries", L"1") != "0";
    c.recordClones = ReadIni(ini, L"record_clones", L"1") != "0";
    c.recordOnline = ReadIni(ini, L"record_online", L"1") != "0";
    const std::string weapon = ReadIni(ini, L"clone_weapon", L"kit");
    c.cloneWeapon = clone::ParseCloneWeapon(weapon);
    if (c.cloneWeapon == 0xFF) { HSM_LOG("fetch: clone_weapon=%s is not one Fetch knows; the clone keeps its kit", weapon.c_str()); c.cloneWeapon = clone::kCloneKit; }
    c.drill = clone::Lower(ReadIni(ini, L"drill", L"tour")).find("gap") != std::string::npos ? 1 : 0;
    c.showMind = ReadIni(ini, L"show_mind", L"0") == "1";
    c.drillMinutes = std::clamp(std::atoi(ReadIni(ini, L"drill_minutes", L"20").c_str()), clone::kMinDrillMinutes, clone::kMaxDrillMinutes);
    const std::string key = ReadIni(ini, L"menu_key", L";");
    c.menuKey = clone::ParseMenuKey(key);
    const std::string fallback = clone::MenuKeyName(clone::kMenuKeyDefault);
    if (c.menuKey < 0) { HSM_LOG("fetch: menu_key=%s is not a key Fetch knows; the menu is on %s", key.c_str(), fallback.c_str()); c.menuKey = clone::kMenuKeyDefault; }
    else if (const char* why = clone::MenuKeyClash(c.menuKey)) {
        // pre6 wrote menu_key=P: P opened the game's pause menu instead, and the Fetch menu does not open over it
        HSM_LOG("fetch: menu_key=%s is %s; the menu is on %s, and Fetch.ini says so now", key.c_str(), why, fallback.c_str());
        c.menuKey = clone::kMenuKeyDefault;
        WriteIni(L"menu_key", fallback);
    }
    const std::string mind = ReadIni(ini, L"mind_key", L"=");
    c.mindKey = clone::ParseMenuKey(mind);
    if (c.mindKey < 0 || clone::MenuKeyClash(c.mindKey)) {
        HSM_LOG("fetch: mind_key=%s is not a key Fetch can use; the clone-thinking overlay is on =", mind.c_str());
        c.mindKey = clone::kMindKeyDefault;
    }
    if (c.mindKey && c.mindKey == c.menuKey) { HSM_LOG("fetch: mind_key is the menu's key; the overlay has no key (the menu's row still turns it on)"); c.mindKey = 0; }
    return c;
}

// Asked only once Steam is up, which SteamLobby::Up() says. Only the two calls for this one name, so nothing of Steam's is held.
std::string SteamName() {
    HMODULE steam = GetModuleHandleW(L"steam_api64.dll");
    if (!steam) return {};
    auto friends = reinterpret_cast<void* (*)()>(reinterpret_cast<void*>(GetProcAddress(steam, "SteamAPI_SteamFriends_v017")));
    auto persona = reinterpret_cast<const char* (*)(void*)>(reinterpret_cast<void*>(GetProcAddress(steam, "SteamAPI_ISteamFriends_GetPersonaName")));
    void* f = friends ? friends() : nullptr;
    const char* n = f && persona ? persona(f) : nullptr;
    return n ? n : "";
}

class Mod {
public:
    explicit Mod(const Config& c) : cfg_(c) {
        keys_.Watch(cfg_.menuKey); keys_.Watch(cfg_.mindKey);
        style_.SetRecordClones(cfg_.recordClones);
        if (!cfg_.recordClones) HSM_LOG("fetch: record_clones=0: nothing is recorded while a clone is in the fight");
        if (!cfg_.recordOnline) HSM_LOG("fetch: record_online=0: nothing is recorded in a Steam lobby");
    }
    SteamLobby lobby;
    // Worker thread, before the first tick.
    void LoadClone() { player_->Load(cfg_); }
    bool CloneReady() const { return player_->ready(); }

    void Tick(UObject* gameMode, float dt) {
        LinkTick();
        const bool changed = gameMode != lastMode_;
        if (changed) { FlushReflectionCache(); cloneWatch_.ForgetLevel(); }   // the old level's classes are gone
        if (!adapter_.Resolve(gameMode)) return;
        if (changed) {
            lastMode_ = gameMode;
            level_ = ObjectName(OuterOf(OuterOf(gameMode)));   // GameMode -> PersistentLevel -> World
            fightLevel_ = clone::IsFighterLevel(level_, adapter_.FreeModeActive());
            game::FightWatch::LevelLoaded();   // the new level's fighter classes get the strike watch
            HSM_LOG("fetch: level %s%s", level_.c_str(), fightLevel_ ? ", a fight level" : "");
            if (menu_.open()) CloseMenu();
            if (clone::IsMenuLevel(level_)) adapter_.MenuInputBack();   // the menus' cursor, if the Fetch menu ever held the keys
        }
        adapter_.SetDestroyAware(true);
        adapter_.SetSubclassWeapons(true);
        lobby.Tick(GetTickCount64());
        const bool offline = lobby.Offline(), mayFight = fightLevel_ && offline;
        if (lobby.Up() && !offline != online_) { online_ = !offline; if (online_) Joined(); }
        SwapLoadedClone();
        MergeDone();
        if (!named_ && lobby.Up()) {   // the game may start straight into a lobby
            named_ = true;
            mine_ = SteamName();
            if (mine_.empty()) mine_ = "player";
            name_ = cfg_.name.empty() ? mine_ : cfg_.name;
        }
        // A training session records throughout, into the player's own file, whatever record= says.
        const bool training = training_.running();
        if (cfg_.record || training) {
            if (training && !cfg_.record && !style_.on()) style_.SetOn(true);
            // Nothing until Steam says whose the play is (named_), and nothing in a lobby with record_online=0.
            style_.Tick(adapter_, adapter_.PlayerWillie(0), player_->fighter(), fightLevel_, !offline, !named_ || (!offline && !cfg_.recordOnline), dt,
                        name_);
        }
        training_.Tick(adapter_, style_, mayFight && adapter_.FreeModeActive() && !player_->inFight());
        if (training_.running() != training && !training_.running()) EndTraining();
        player_->Tick(adapter_, mayFight && !training_.running(), dt, cloneWatch_);   // the clone stays out of a session
        ui::BannerTick(menu_.open() ? std::string() : training_.Banner());
        if (!player_->watching()) cloneWatch_.Pause();
        MenuTick(offline);
        MindTick(offline);
        if (const interop::FetchState f = LinkState(); !(f == told_)) { told_ = f; Link().Write(f); }   // HSMMod leaves its keys at once
        adapter_.TickDeferredDestroys();
    }
private:
    // What HSMMod is told. The menu counts as open for a moment after it shuts: the Enter that chose Close, or Esc, is still
    // down then, and HSMMod must not take that press for its chat key.
    static constexpr ULONGLONG kMenuTailMs = 300;
    interop::FetchState LinkState() const { return {menu_.open() || GetTickCount64() < menuTailUntil_, menu_.typing()}; }

    // The link page with HSMMod (mod_link.h): Fetch's half out and HSMMod's in, every tick, before anything else (a level that
    // will not resolve too, so the beat goes on). dt is real time: the game's own is slowed down with the fight.
    void LinkTick() {
        const ULONGLONG now = GetTickCount64();
        const uint32_t dtMs = linkAt_ ? uint32_t(std::min<ULONGLONG>(now - linkAt_, 60000)) : 0;
        linkAt_ = now;
        told_ = LinkState();
        Link().Tick(told_, dtMs);
        const bool live = Link().live();
        const interop::HsmState& h = Link().hsm();
        if (live && (!hsmLive_ || h.syncMode != hsmMode_)) {
            static constexpr const char* kModes[] = {"none yet", "owner", "host"};
            HSM_LOG("fetch: HSMMod is here (sync mode %s): in its lobbies the Fetch menu opens, and keys it plays again are not recorded twice",
                    kModes[size_t(h.syncMode) < 3 ? size_t(h.syncMode) : 0]);
        } else if (!live && hsmLive_) {
            HSM_LOG("fetch: HSMMod has gone quiet; in a Steam lobby the menu stays shut until it is back");
        }
        if (live && h.hostPlaysMine && !hostPlaysLogged_) {
            hostPlaysLogged_ = true;
            HSM_LOG("fetch: in this lobby the host's machine moves your fighter (HSMMod host sync); your keys are recorded as you press them");
        }
        hsmLive_ = live;
        hsmMode_ = h.syncMode;
    }

    // A Steam lobby joined: a menu or overlay key that is also one of HSMMod's says so.
    void Joined() {
        for (const auto& [name, vk] : {std::pair<const char*, int>{"menu_key", cfg_.menuKey}, {"mind_key", cfg_.mindKey}})
            if (const char* why = clone::HsmModKeyClash(vk))
                HSM_LOG("fetch: %s=%s is also %s; in a lobby one press may do both. Another key in Fetch.ini avoids that", name,
                        clone::MenuKeyName(vk).c_str(), why);
    }

    // Offline as always. In a Steam lobby only with HSMMod on the link and not typing in its chat (interop::FetchMenuMayOpen):
    // HSMMod then leaves the menu's keys alone.
    void OpenMenu(bool offline) {
        UObject* me = adapter_.PlayerWillie(0);
        if (!interop::FetchMenuMayOpen(offline, Link().live(), Link().hsm())) {
            if (lobby.Up() && !Link().live() && !shutTold_) {
                shutTold_ = true;
                HSM_LOG("fetch: menu stays shut in this lobby: HSMMod 0.8.0-pre44 or newer is needed for it");
            }
            return;
        }
        if (clone::IsMenuLevel(level_) || !me || adapter_.IsDead(me) || adapter_.PauseMenuOpen()) return;
        if (!ui::MenuCanShow()) { HSM_LOG("fetch: menu: no HUD on screen yet"); return; }
        settings_.record = cfg_.record;
        settings_.clone = cfg_.clone;
        settings_.brain = uint8_t(cfg_.brain);
        settings_.duel = cfg_.duel;
        settings_.cloneWeapon = cfg_.cloneWeapon;
        settings_.drill = cfg_.drill;
        settings_.drillMinutes = cfg_.drillMinutes;
        settings_.showMind = cfg_.showMind;
        settings_.training = training_.running();
        settings_.recordTo = cfg_.name;
        settings_.mine = mine_;
        settings_.online = !offline;
        clones_ = ListClones();
        recordings_ = ListRecordings();
        settings_.targetThere = FileThere(game::RecordingBase(name_) + L".hsmclone");
        menuNote_.clear();
        menu_.Open();
        adapter_.CaptureInput(true);   // the menu's keys do not reach the fighter
        HSM_LOG("fetch: menu open (%zu clones in Clones/)%s", clones_.size(), offline ? "" : " in a Steam lobby: the clone rows are offline only");
    }

    // giveBack false: the game's own screen (the death screen) has taken the input mode, and keeps it.
    void CloseMenu(bool giveBack = true) {
        menu_.Close();
        menuTailUntil_ = GetTickCount64() + kMenuTailMs;
        if (giveBack) adapter_.CaptureInput(false);
        ui::MenuTick(false, {});
        HSM_LOG("fetch: menu closed");
    }

    // by: what turned it, to say in the log (null: the menu, which says so itself).
    void SetShowMind(bool on, const char* by) {
        cfg_.showMind = settings_.showMind = on;
        player_->SetMind(on);
        WriteIni(L"show_mind", on ? "1" : "0");
        mindAt_ = 0;
        if (by) HSM_LOG("fetch: the clone-thinking overlay is %s (%s, %s)", on ? "ON" : "OFF", by, clone::MenuKeyName(cfg_.mindKey).c_str());
    }

    // The overlay that shows the clone thinking: offline, while a clone fights, never over the menu. Five times a second.
    void MindTick(bool offline) {
        if (!cfg_.showMind || !offline || menu_.open() || !player_->inFight()) { ui::MindTick(false, mindView_); return; }
        const ULONGLONG now = GetTickCount64();
        if (now < mindAt_) return;
        mindAt_ = now + kMindEveryMs;
        ui::MindTick(player_->Mind(mindView_), mindView_);
    }

    void MenuTick(bool offline) {
        if (!cfg_.menuKey && !cfg_.mindKey) return;
        keys_.Poll();
        // The overlay's key, offline, but not while a name is typed.
        if (cfg_.mindKey && offline && !menu_.typing() && keys_.Pressed(cfg_.mindKey)) SetShowMind(!cfg_.showMind, "its key");
        if (!cfg_.menuKey) return;
        if (menu_.typing()) { for (const clone::TypeKey& k : keys_.Typed()) Apply(menu_.Type(k, settings_, recordings_)); }   // the menu key is a letter too
        else if (keys_.Pressed(cfg_.menuKey)) { if (menu_.open()) CloseMenu(); else OpenMenu(offline); }
        else if (menu_.open()) Apply(menu_.Key(keys_.Nav(), settings_, clones_, recordings_));
        if (!menu_.open()) return;
        settings_.online = !offline;   // a lobby joined or left with the menu open
        if (interop::FetchMenuMustClose(offline, Link().live(), Link().hsm())) {
            HSM_LOG("fetch: menu closes: %s", Link().live() ? "HSMMod's chat is open" : "no HSMMod in this Steam lobby");
            CloseMenu();
            return;
        }
        if (UObject* me = adapter_.PlayerWillie(0); !me || adapter_.IsDead(me)) { CloseMenu(false); return; }
        if (!ui::MenuCanShow()) { CloseMenu(); return; }   // the HUD went (a level change, a death screen): the keys go back
        ui::MenuTick(true, View());
    }

    void Apply(clone::MenuChange c) {
        switch (c) {
            case clone::MenuChange::None: break;
            case clone::MenuChange::Record:
                cfg_.record = settings_.record;
                style_.SetOn(cfg_.record);
                WriteIni(L"record", cfg_.record ? "1" : "0");
                break;
            case clone::MenuChange::Clone:
                cfg_.clone = settings_.clone;
                WriteIni(L"clone", cfg_.clone.empty() ? "0" : cfg_.clone);
                StartLoad();
                break;
            case clone::MenuChange::Brain:
                cfg_.brain = clone::BrainSetting(settings_.brain % 3);
                WriteIni(L"brain", clone::kBrainNames[settings_.brain % 3]);
                if (!cfg_.clone.empty()) StartLoad();
                break;
            case clone::MenuChange::Duel:
                cfg_.duel = settings_.duel;
                player_->SetDuel(cfg_.duel);
                WriteIni(L"duel", cfg_.duel ? "1" : "0");
                break;
            case clone::MenuChange::Respawn: player_->Despawn(adapter_); break;
            case clone::MenuChange::CloneWeapon:
                cfg_.cloneWeapon = settings_.cloneWeapon;
                player_->SetCloneWeapon(cfg_.cloneWeapon);
                WriteIni(L"clone_weapon", clone::CloneWeaponToken(cfg_.cloneWeapon));
                break;
            case clone::MenuChange::Drill:
                cfg_.drill = settings_.drill;
                WriteIni(L"drill", cfg_.drill ? "gaps" : "tour");
                break;
            case clone::MenuChange::DrillMinutes:
                cfg_.drillMinutes = settings_.drillMinutes;
                WriteIni(L"drill_minutes", std::to_string(cfg_.drillMinutes));
                break;
            case clone::MenuChange::Training:
                if (training_.running()) { training_.Stop(adapter_, style_, "stopped"); EndTraining(); }
                else StartTraining();
                settings_.training = training_.running();
                break;
            case clone::MenuChange::Closed: CloseMenu(); break;
            case clone::MenuChange::RecordTo: SetTarget(settings_.recordTo); break;
            case clone::MenuChange::Rename: RenameRecording(name_, settings_.renameTo); break;
            case clone::MenuChange::MergeMake: StartMerge(); break;
            case clone::MenuChange::ShowMind: SetShowMind(settings_.showMind, nullptr); break;
        }
        if (c != clone::MenuChange::None && c != clone::MenuChange::Closed) HSM_LOG("fetch: menu: %s is now %s", clone::MenuLabel(menu_.row()), clone::MenuValue(menu_.row(), settings_).c_str());
    }

    // Record to: the name play goes into from the next tick (empty: the player's own), kept in Fetch.ini name=.
    void SetTarget(const std::string& name) {
        cfg_.name = name;
        settings_.recordTo = name;
        WriteIni(L"name", name);
        name_ = name.empty() ? mine_ : name;
        style_.Retarget();
        settings_.targetThere = FileThere(game::RecordingBase(name_) + L".hsmclone");
        HSM_LOG("fetch: recording into Clones/%s.hsmclone%s", clone::FileStem(name_).c_str(), settings_.targetThere ? "" : " (a new file, made by the first fight)");
    }

    // Rename: the recording's file (and its model, if it has one), and the name inside it, which goes on its end. Not while play is
    // being kept into it. Record to and clone= follow it.
    void RenameRecording(const std::string& from, const std::string& to) {
        if (style_.live()) { menuNote_ = "Rename: not in the middle of a recorded fight"; return; }
        const std::wstring a = game::RecordingBase(from), b = game::RecordingBase(to);
        const bool sameFile = clone::SameName(clone::FileStem(from), clone::FileStem(to));   // only its case changes
        style_.Retarget();   // what is kept goes in first
        if (!MoveFileW((a + L".hsmclone").c_str(), (b + L".hsmclone").c_str())) {
            const DWORD e = GetLastError();
            menuNote_ = e == ERROR_ALREADY_EXISTS ? "Rename: there is a recording of that name already" : "Rename: the file could not move (Fetch.log)";
            HSM_LOG("fetch: rename %s to %s: the file could not move (error %lu)", from.c_str(), to.c_str(), e);
            return;
        }
        for (const wchar_t* ext : {L".hsmmodel", L".hsmmodel.bak"}) {
            if (!FileThere(a + ext)) continue;
            if ((!sameFile && FileThere(b + ext)) || !MoveFileW((a + ext).c_str(), (b + ext).c_str()))
                HSM_LOG("fetch: rename %s to %s: Clones/%s%ls stays (there is one of the new name, or it could not move)", from.c_str(), to.c_str(), clone::FileStem(from).c_str(), ext);
        }
        if (!game::AppendNameBlock(b + L".hsmclone", to)) HSM_LOG("fetch: rename %s to %s: the name inside the file could not change", from.c_str(), to.c_str());
        HSM_LOG("fetch: renamed Clones/%s.hsmclone to %s.hsmclone", clone::FileStem(from).c_str(), clone::FileStem(to).c_str());
        SetTarget(to);
        if (!cfg_.clone.empty() && clone::SameName(clone::FileStem(cfg_.clone), clone::FileStem(from))) {
            cfg_.clone = settings_.clone = to;
            WriteIni(L"clone", to);
            StartLoad();
        }
        clones_ = ListClones();
        recordings_ = ListRecordings();
        menuNote_ = "Renamed " + from + " to " + to;
    }

    // Merge: the recordings picked, in that order, into a new clone of the name typed (hsm_clone_merge.h). Made on a thread of
    // its own, as the files can be big; it goes in as a part file and takes its name only when whole, so nothing is overwritten
    // and a half-made one is never a clone.
    void StartMerge() {
        if (settings_.merging) return;
        const std::vector<std::string> from = settings_.mergeFrom;
        const std::string name = settings_.mergeName;
        const std::wstring out = game::RecordingBase(name) + L".hsmclone";
        if (FileThere(out)) { menuNote_ = "Merge: there is a recording of that name already"; return; }
        for (const std::string& f : from)
            if (clone::SameName(f, clone::FileStem(name_))) { style_.Save(); break; }   // the play kept so far goes in too
        settings_.merging = true;
        mergeNote_.clear();
        mergeAt_.store(0, std::memory_order_relaxed);
        mergeDone_.store(false, std::memory_order_relaxed);
        mergeName_ = name;
        HSM_LOG("fetch: merging %zu recordings into Clones/%s.hsmclone", from.size(), clone::FileStem(name).c_str());
        merger_ = std::thread([this, from, name, out] {
            clone::MergeResult r;
            std::vector<FILE*> files;
            std::vector<clone::MergeSource> in;
            for (const std::string& f : from) {
                FILE* h = _wfopen(ModFilePath((L"Clones\\" + Wide(f) + L".hsmclone").c_str()).c_str(), L"rb");
                if (!h) { r.why = f + " could not be read"; break; }
                files.push_back(h);
                const long long size = _fseeki64(h, 0, SEEK_END) == 0 ? _ftelli64(h) : -1;
                clone::MergeSource src;
                src.stem = f;
                src.size = uint64_t(size < 0 ? 0 : size);
                src.read = [h](uint64_t at, uint8_t* to, size_t n) {
                    return _fseeki64(h, (long long)at, SEEK_SET) == 0 && std::fread(to, 1, n, h) == n;
                };
                in.push_back(std::move(src));
            }
            if (in.size() == from.size()) {
                const std::wstring part = out + L".part";
                if (FILE* w = _wfopen(part.c_str(), L"wb")) {
                    r = clone::MergeFiles(in, name, [w](const uint8_t* d, size_t n) { return std::fwrite(d, 1, n, w) == n; },
                                          [this](bool writing, uint32_t i, uint32_t n) {
                                              mergeAt_.store(uint32_t(writing) << 31 | (i + 1) << 16 | n, std::memory_order_relaxed);
                                          });
                    const bool closed = std::fclose(w) == 0;
                    if (r.ok && !closed) { r.ok = false; r.why = "the new file could not be written"; }
                    if (r.ok && !MoveFileW(part.c_str(), out.c_str())) { r.ok = false; r.why = "there is a recording of that name already"; }
                    if (!r.ok) DeleteFileW(part.c_str());
                } else {
                    r.why = "the new file could not be made";
                }
            }
            for (FILE* h : files) std::fclose(h);
            merged_ = std::move(r);
            mergeDone_.store(true, std::memory_order_release);
        });
    }

    void MergeDone() {
        if (!settings_.merging || !mergeDone_.load(std::memory_order_acquire)) return;
        merger_.join();
        settings_.merging = false;
        const clone::MergeResult& r = merged_;
        const std::string stem = clone::FileStem(mergeName_);
        if (r.ok) {
            std::string names;
            for (const std::string& n : r.names) names += (names.empty() ? "" : ", ") + n;
            HSM_LOG("fetch: made Clones/%s.hsmclone: %s of play, %llu rows, %.1f MB, from %s%s%s", stem.c_str(), clone::Clock(r.seconds).c_str(),
                    static_cast<unsigned long long>(r.rows), double(r.bytes) / (1024.0 * 1024.0), names.c_str(), r.why.empty() ? "" : ". ", r.why.c_str());
            mergeNote_ = "Made " + stem + ": " + clone::Clock(r.seconds) + " of play from " + std::to_string(r.used) +
                         (r.used < r.asked ? " of " + std::to_string(r.asked) : std::string()) + " recordings. Train it with train.bat";
        } else {
            HSM_LOG("fetch: merge into Clones/%s.hsmclone: not made: %s", stem.c_str(), r.why.c_str());
            mergeNote_ = "Merge: " + r.why;
        }
        clones_ = ListClones();
        recordings_ = ListRecordings();
    }

    // Only in a Free Mode fight, offline, with no clone in it (the menu says why not on its status line).
    void StartTraining() {
        if (!lobby.Offline()) { trainingNote_ = "Training: offline only"; return; }
        if (!fightLevel_ || !adapter_.FreeModeActive()) { trainingNote_ = "Training: only in a Free Mode fight"; return; }
        if (player_->inFight()) player_->Despawn(adapter_);   // the clone goes for the session
        trainingNote_.clear();
        trainedRecording_ = cfg_.record;
        training_.Start(name_, clone::DrillMode(cfg_.drill & 1), cfg_.drillMinutes);
        CloseMenu();
    }
    // A session that turned recording on for itself turns it off again: record= is the player's.
    void EndTraining() {
        if (!trainedRecording_ && !cfg_.record && style_.on()) style_.SetOn(false);
    }

    // A clone or brain chosen in the menu is read on a thread of its own (a recording can be big) and goes in on a tick. A
    // choice made while one is read is read after it; only the last counts.
    void StartLoad() {
        if (loading_) { loadAgain_ = true; return; }
        loading_ = true;
        loadDone_.store(false, std::memory_order_relaxed);
        const Config c = cfg_;
        loader_ = std::thread([this, c] {
            auto p = std::make_unique<ClonePlayer>();
            if (!c.clone.empty()) p->Load(c);
            loaded_ = std::move(p);
            loadDone_.store(true, std::memory_order_release);
        });
    }

    void SwapLoadedClone() {
        if (!loading_ || !loadDone_.load(std::memory_order_acquire)) return;
        loader_.join();
        loading_ = false;
        if (loadAgain_) { loadAgain_ = false; loaded_.reset(); StartLoad(); return; }
        player_->Despawn(adapter_);
        player_ = std::move(loaded_);
        HSM_LOG("fetch: clone now %s", player_->Describe().c_str());
    }

    ui::MenuView View() const {
        ui::MenuView v;
        v.key = clone::MenuKeyName(cfg_.menuKey);
        v.typing = menu_.typing();
        v.merge = menu_.onMerge();
        v.lines = menu_.Lines(settings_, recordings_, v.title);
        char b[160];
        if (!cfg_.record) v.status[0] = "Recording: off";
        else if (!style_.on()) v.status[0] = "Recording: stopped (Fetch.log says why)";
        else {
            std::snprintf(b, sizeof b, "Recording%s into %s: %s of play kept, %.1f MB", style_.live() ? " now" : " (waiting for a fight)",
                          clone::FileStem(name_).c_str(), clone::Clock(style_.seconds()).c_str(), double(style_.bytes()) / (1024.0 * 1024.0));
            v.status[0] = b;
        }
        const clone::Tally& t = style_.tally();
        std::snprintf(b, sizeof b, "Hits: %u landed, %u taken   Clashes: %u   Parries: %u made, %u taken", t.landed, t.taken, t.clashes, t.parries, t.parried);
        v.status[1] = b;
        std::snprintf(b, sizeof b, "Damage: %.0f dealt, %.0f taken   Kills: %u   Deaths: %u", double(t.dealt), double(t.suffered), t.kills, t.deaths);
        v.status[2] = b;
        std::snprintf(b, sizeof b, "Strikes: %u through a gap, %u on bare skin, %u beat the armour, %u held by it", t.gaps, t.bare, t.beat, t.held);
        v.status[3] = b;
        v.status[4] = "Clone: " + (loading_ ? std::string("loading...") : player_->Describe());
        std::string merging;
        if (settings_.merging) {
            const uint32_t at = mergeAt_.load(std::memory_order_relaxed);
            std::snprintf(b, sizeof b, "Merging: %s %u of %u...", at >> 31 ? "writing" : "reading", at >> 16 & 0x7FFF, at & 0xFFFF);
            merging = b;
        }
        v.status[5] = !menu_.note().empty() ? menu_.note() : !menuNote_.empty() ? menuNote_ : !merging.empty() ? merging : !mergeNote_.empty() ? mergeNote_
                    : trainingNote_.empty() ? training_.Status() : trainingNote_;
        return v;
    }

    Config cfg_;
    game::Adapter adapter_;
    game::StyleRecorder style_;
    std::unique_ptr<ClonePlayer> player_ = std::make_unique<ClonePlayer>();
    std::unique_ptr<ClonePlayer> loaded_;          // the loader thread's, until SwapLoadedClone takes it
    // The clone's own fight, for its brain to hear. The Mod holds it, not the ClonePlayer: its hook outlives a swap.
    game::FightWatch cloneWatch_{[this](const clone::FightEvent& e) { player_->Event(e); }, [this] { return player_->watching(); }};
    std::thread loader_;
    std::atomic<bool> loadDone_{false};
    bool loading_ = false, loadAgain_ = false;
    clone::Menu menu_;
    clone::MenuSettings settings_;
    static constexpr ULONGLONG kMindEveryMs = 200;
    ULONGLONG mindAt_ = 0;          // MindTick: when the overlay is drawn next
    ui::MindView mindView_;
    std::vector<std::string> clones_, recordings_;
    Keys keys_;
    UObject* lastMode_ = nullptr;
    std::string level_, name_, mine_;   // name_: the recording play goes into; mine_: the player's own (Steam) name
    bool named_ = false;
    bool online_ = false;               // in a Steam lobby, as the last tick saw it (Joined)
    // The link with HSMMod: when it was last ticked, Fetch's half as last written, and what the log has said of HSMMod.
    ULONGLONG linkAt_ = 0, menuTailUntil_ = 0;
    interop::FetchState told_;
    bool hsmLive_ = false, hostPlaysLogged_ = false, shutTold_ = false;
    interop::SyncMode hsmMode_ = interop::SyncMode::None;
    std::string menuNote_;              // what the last Rename came to
    // The Merge page's merge, on a thread of its own (StartMerge), until MergeDone takes its result.
    std::thread merger_;
    std::atomic<bool> mergeDone_{false};
    std::atomic<uint32_t> mergeAt_{0};  // writing << 31 | the source it is on << 16 | of how many
    clone::MergeResult merged_;
    std::string mergeName_, mergeNote_; // mergeNote_: what the last merge came to
    bool fightLevel_ = false;
    TrainingSession training_;
    bool trainedRecording_ = false;
    std::string trainingNote_;
};

Mod* g_mod = nullptr;   // never deleted: the process exit outlives the game's objects
UClass* g_gameModeClass = nullptr;
constexpr const char* kGameModeClass = "BP_HalfSwordGameMode_C";
// Other mods may patch ProcessEvent as the game starts, and two patches at once can tear it. Nothing here is needed before the
// first fight, so this one goes after they are done.
constexpr DWORD kHookDelayMs = 20000;
}  // namespace

DWORD WINAPI Main(void*) {
    LogInit();
    HSM_LOG("Fetch loaded");
    const Config cfg = LoadConfig();
    game::FightWatch::SetStrikes(cfg.strikes);
    if (!cfg.strikes) HSM_LOG("fetch: strikes=0: no Strike events");
    game::FightWatch::SetParries(cfg.parries);
    if (!cfg.parries) HSM_LOG("fetch: parries=0: no Parried events");
    const bool menu = cfg.menuKey != 0;
    if (!cfg.record && cfg.clone.empty() && !menu) { HSM_LOG("fetch: record=0, clone=0 and menu_key=0: nothing to do, nothing hooked"); return 0; }
    Mod* mod = new Mod(cfg);
    if (!cfg.clone.empty()) mod->LoadClone();
    if (!cfg.record && !mod->CloneReady() && !menu) { HSM_LOG("fetch: no clone to fight, record=0 and menu_key=0: nothing hooked"); return 0; }
    if (!Init()) { HSM_LOG("fetch: this game build is not supported; the game continues unmodified"); return 0; }
    Link().Map();   // before the first tick, and kept: HSMMod may look for it any time
    const bool steam = mod->lobby.Start();
    g_mod = mod;
    HookFunctionsMatching([](const std::string& n) { return n == "ReceiveTick"; }, [](UObject* obj, UFunction* fn, void* params) {
        if (!g_gameModeClass) {
            UClass* c = ClassOf(obj);
            if (!SameNameAscii(ObjectName(reinterpret_cast<UObject*>(c)), kGameModeClass)) return false;
            g_gameModeClass = c;
        }
        if (ClassOf(obj) != g_gameModeClass) return false;
        float dt = 0.f;
        if (FProperty* p = FindProperty(fn, "DeltaSeconds")) std::memcpy(&dt, static_cast<uint8_t*>(params) + PropertyOffset(p), sizeof dt);
        if (!(dt > 0.f && dt < 1.f)) dt = 1.f / 60.f;
        g_mod->Tick(obj, dt);
        return false;
    });
    Sleep(kHookDelayMs);
    if (!InstallProcessEventHook()) { HSM_LOG("fetch: no hook; the game continues unmodified"); return 0; }
    HSM_LOG("fetch: started");
    if (!steam) mod->lobby.KeepTrying();
    return 0;
}

}  // namespace fetch

// engine.cpp logs a refused call with its stack; Fetch only says what was refused.
namespace hsm { void LogCallStack(const char* what) { HSM_LOG("%s", what); } }
