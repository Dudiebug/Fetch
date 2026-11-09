// Worker thread: reads Fetch.ini and the clone's files, then, if there is anything to do, hooks the GameMode's tick. With
// recording, clones and the menu all off, the mod hooks nothing at all. The menu (;) changes the settings in game and writes
// them back to Fetch.ini.
#include <windows.h>
#include "clone_player.h"
#include "clone_style.h"
#include "clone/hsm_clone_rules.h"
#include "clone/hsm_fetch_menu.h"
#include "log.h"
#include "paths.h"
#include "steam_lobby.h"
#include "training.h"
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
    "; name= names that file. Empty: your Steam name.\r\n"
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
    "; clone_weapon=kit: the clone holds what its recording held. random: a weapon type it has moves for, new each time it comes in.\r\n"
    "; match: your weapon's type, when it has moves for it. Or one type: sword1h sword2h mace1h mace2h axe1h axe2h polearm.\r\n"
    "clone_weapon=kit\r\n"
    "; drill=tour: a training session (the menu) takes every weapon type against every kind of foe. gaps: what your recording is short of.\r\n"
    "drill=tour\r\n"
    "; drill_minutes=20: a training session's length, 5 to 60.\r\n"
    "drill_minutes=20\r\n";

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

// The keyboard as the menu reads it: the OS key state, so it works while the game's own input is held off. Only while the
// game's window has the keyboard: keys typed into another window are not for it.
class Keys {
public:
    void Poll() {
        DWORD pid = 0;
        HWND fg = GetForegroundWindow();
        const bool ours = fg && GetWindowThreadProcessId(fg, &pid) && pid == GetCurrentProcessId();
        was_ = now_;
        for (int vk : watched_) if (vk > 0 && vk < 256) now_[size_t(vk)] = ours && (GetAsyncKeyState(vk) & 0x8000);
    }
    void Watch(int vk) { if (vk > 0 && vk < 256 && std::find(watched_.begin(), watched_.end(), vk) == watched_.end()) watched_.push_back(vk); }
    bool Pressed(int vk) const { return vk > 0 && vk < 256 && now_[size_t(vk)] && !was_[size_t(vk)]; }
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
    std::array<bool, 256> now_{}, was_{};
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
    const std::string weapon = ReadIni(ini, L"clone_weapon", L"kit");
    c.cloneWeapon = clone::ParseCloneWeapon(weapon);
    if (c.cloneWeapon == 0xFF) { HSM_LOG("fetch: clone_weapon=%s is not one Fetch knows; the clone keeps its kit", weapon.c_str()); c.cloneWeapon = clone::kCloneKit; }
    c.drill = clone::Lower(ReadIni(ini, L"drill", L"tour")).find("gap") != std::string::npos ? 1 : 0;
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
    return c;
}

// Asked only once Steam is up, which Offline() says. Only the two calls for this one name, so nothing of Steam's is held.
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
    explicit Mod(const Config& c) : cfg_(c) { keys_.Watch(cfg_.menuKey); }
    SteamLobby lobby;
    // Worker thread, before the first tick.
    void LoadClone() { player_->Load(cfg_); }
    bool CloneReady() const { return player_->ready(); }

    void Tick(UObject* gameMode, float dt) {
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
        SwapLoadedClone();
        if (name_.empty() && offline) { name_ = cfg_.name.empty() ? SteamName() : cfg_.name; if (name_.empty()) name_ = "player"; }
        // A training session records throughout, into the player's own file, whatever record= says.
        const bool training = training_.running();
        if (cfg_.record || training) {
            if (training && !cfg_.record && !style_.on()) style_.SetOn(true);
            style_.Tick(adapter_, adapter_.PlayerWillie(0), player_->fighter(), fightLevel_, !offline, dt, name_);
        }
        training_.Tick(adapter_, style_, mayFight && adapter_.FreeModeActive() && !player_->inFight());
        if (training_.running() != training && !training_.running()) EndTraining();
        player_->Tick(adapter_, mayFight && !training_.running(), dt, cloneWatch_);   // the clone stays out of a session
        ui::BannerTick(menu_.open() ? std::string() : training_.Banner());
        if (!player_->watching()) cloneWatch_.Pause();
        MenuTick(offline);
        adapter_.TickDeferredDestroys();
    }
private:
    // Offline only, as the clones: in a Steam lobby another mod's chat may hold the keys.
    void OpenMenu(bool offline) {
        UObject* me = adapter_.PlayerWillie(0);
        if (!offline || clone::IsMenuLevel(level_) || !me || adapter_.IsDead(me) || adapter_.PauseMenuOpen()) return;
        if (!ui::MenuCanShow()) { HSM_LOG("fetch: menu: no HUD on screen yet"); return; }
        settings_.record = cfg_.record;
        settings_.clone = cfg_.clone;
        settings_.brain = uint8_t(cfg_.brain);
        settings_.duel = cfg_.duel;
        settings_.cloneWeapon = cfg_.cloneWeapon;
        settings_.drill = cfg_.drill;
        settings_.drillMinutes = cfg_.drillMinutes;
        settings_.training = training_.running();
        clones_ = ListClones();
        menu_.Open();
        adapter_.CaptureInput(true);   // the menu's keys do not reach the fighter
        HSM_LOG("fetch: menu open (%zu clones in Clones/)", clones_.size());
    }

    // giveBack false: the game's own screen (the death screen) has taken the input mode, and keeps it.
    void CloseMenu(bool giveBack = true) {
        menu_.Close();
        if (giveBack) adapter_.CaptureInput(false);
        ui::MenuTick(false, {});
        HSM_LOG("fetch: menu closed");
    }

    void MenuTick(bool offline) {
        if (!cfg_.menuKey) return;
        keys_.Poll();
        if (keys_.Pressed(cfg_.menuKey)) { if (menu_.open()) CloseMenu(); else OpenMenu(offline); }
        else if (menu_.open()) Apply(menu_.Key(keys_.Nav(), settings_, clones_));
        if (!menu_.open()) return;
        if (!offline) { CloseMenu(); return; }
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
        }
        if (c != clone::MenuChange::None && c != clone::MenuChange::Closed) HSM_LOG("fetch: menu: %s is now %s", clone::MenuLabel(menu_.row()), clone::MenuValue(menu_.row(), settings_).c_str());
    }

    // Only in a Free Mode fight, offline, with no clone in it (the menu says why not on its status line).
    void StartTraining() {
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
        v.row = menu_.row();
        v.key = clone::MenuKeyName(cfg_.menuKey);
        for (int r = 0; r < clone::kMenuRows; ++r) v.values[size_t(r)] = clone::MenuValue(clone::MenuRow(r), settings_);
        char b[160];
        if (!cfg_.record) v.status[0] = "Recording: off";
        else if (!style_.on()) v.status[0] = "Recording: stopped (Fetch.log says why)";
        else {
            std::snprintf(b, sizeof b, "Recording%s: %s of play kept, %.1f MB", style_.live() ? " now" : " (waiting for a fight)",
                          clone::Clock(style_.seconds()).c_str(), double(style_.bytes()) / (1024.0 * 1024.0));
            v.status[0] = b;
        }
        const clone::Tally& t = style_.tally();
        std::snprintf(b, sizeof b, "Hits: %u landed, %u taken   Clashes: %u   Parries: %u", t.landed, t.taken, t.clashes, t.parries);
        v.status[1] = b;
        std::snprintf(b, sizeof b, "Damage: %.0f dealt, %.0f taken   Kills: %u   Deaths: %u", double(t.dealt), double(t.suffered), t.kills, t.deaths);
        v.status[2] = b;
        std::snprintf(b, sizeof b, "Strikes: %u through a gap, %u on bare skin, %u beat the armour, %u held by it", t.gaps, t.bare, t.beat, t.held);
        v.status[3] = b;
        v.status[4] = "Clone: " + (loading_ ? std::string("loading...") : player_->Describe());
        v.status[5] = trainingNote_.empty() ? training_.Status() : trainingNote_;
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
    std::vector<std::string> clones_;
    Keys keys_;
    UObject* lastMode_ = nullptr;
    std::string level_, name_;
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
    const bool menu = cfg.menuKey != 0;
    if (!cfg.record && cfg.clone.empty() && !menu) { HSM_LOG("fetch: record=0, clone=0 and menu_key=0: nothing to do, nothing hooked"); return 0; }
    Mod* mod = new Mod(cfg);
    if (!cfg.clone.empty()) mod->LoadClone();
    if (!cfg.record && !mod->CloneReady() && !menu) { HSM_LOG("fetch: no clone to fight, record=0 and menu_key=0: nothing hooked"); return 0; }
    if (!Init()) { HSM_LOG("fetch: this game build is not supported; the game continues unmodified"); return 0; }
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
