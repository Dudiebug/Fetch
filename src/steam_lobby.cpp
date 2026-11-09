#include "steam_lobby.h"
#include "clone/hsm_clone_rules.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <iterator>
#include <string>

namespace fetch {
using namespace hsm;

namespace {
constexpr int kCbLobbyEnter = 504;
constexpr int kRetrySteamMs = 10000;
constexpr int kWaitForSteamMs = 15000;   // HSMMod starts Steam within about 10 s when it is in; Fetch does it if nobody has by then

// CCallbackBase as laid out by MSVC: vptr, uint8 flags, int callback id. The vtable has Run(p, ioFailure, call), Run(p) and
// GetCallbackSizeBytes(). Both Run overloads share one handler: rcx = this, rdx = payload in each case.
struct CallbackObj { void** vtable; uint8_t flags; int32_t id; int32_t size; };
struct LobbyEnter { uint64_t lobby; uint32_t permissions; bool locked; uint32_t response; };   // LobbyEnter_t

std::atomic<uint64_t> g_lobby{0};

void __cdecl OnEnter(CallbackObj*, void* p) {
    const auto* c = static_cast<const LobbyEnter*>(p);
    g_lobby = clone::LobbyAfterEnter(g_lobby, c->lobby, c->response == 1);
    HSM_LOG("fetch: in a Steam lobby; clones stay out until it is left");
}
int32_t __cdecl OnSize(CallbackObj* self) { return self->size; }
void* g_vtable[3] = { reinterpret_cast<void*>(&OnEnter), reinterpret_cast<void*>(&OnEnter), reinterpret_cast<void*>(&OnSize) };
CallbackObj g_enter{g_vtable, 0, kCbLobbyEnter, int32_t(sizeof(LobbyEnter))};

template <class F> F Fn(HMODULE dll, const char* name) { return reinterpret_cast<F>(reinterpret_cast<void*>(GetProcAddress(dll, name))); }

// Pinned: the game's exit can unload its copy, and the lobby is asked about on every tick until then.
HMODULE LoadedSteam() {
    HMODULE steam = nullptr;
    return GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, L"steam_api64.dll", &steam) ? steam : nullptr;
}

bool SteamUp(HMODULE steam) {
    auto pipe = steam ? Fn<int (*)()>(steam, "SteamAPI_GetHSteamPipe") : nullptr;
    return pipe && pipe() != 0;
}

// The engine's own copy, from HalfswordUE5/Binaries/Win64. The game ships its Steam subsystem off, so nothing else loads it.
HMODULE LoadGameSteam() {
    wchar_t exe[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameW(nullptr, exe, DWORD(std::size(exe)));
    if (!n || n >= std::size(exe)) return nullptr;
    std::wstring dir(exe, n);
    for (int up = 0; up < 4; ++up) {
        const size_t cut = dir.find_last_of(L"\\/");
        if (cut == std::wstring::npos) return nullptr;
        dir.resize(cut);
    }
    return LoadLibraryExW((dir + L"\\Engine\\Binaries\\ThirdParty\\Steamworks\\Steamv157\\Win64\\steam_api64.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
}
}  // namespace

bool SteamLobby::Use(void* dll, bool startedHere) {
    HMODULE steam = static_cast<HMODULE>(dll);
    auto get = Fn<void* (*)()>(steam, "SteamAPI_SteamMatchmaking_v009");
    auto add = Fn<void (*)(void*, int)>(steam, "SteamAPI_RegisterCallback");
    members_ = Fn<int (*)(void*, uint64_t)>(steam, "SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    if (startedHere) pump_ = Fn<void (*)()>(steam, "SteamAPI_RunCallbacks");
    matchmaking_ = get ? get() : nullptr;
    if (!matchmaking_ || !add || !members_) return false;
    add(&g_enter, kCbLobbyEnter);
    started_ = true;
    HSM_LOG("fetch: Steam is up (%s)", startedHere ? "started by Fetch" : "already started");
    return true;
}

bool SteamLobby::Try(int waitedMs) {
    HMODULE steam = LoadedSteam();
    switch (clone::NextSteamStep(SteamUp(steam), waitedMs, kWaitForSteamMs)) {
        case clone::SteamStep::Wait: return false;
        case clone::SteamStep::Use: return Use(steam, false);
        case clone::SteamStep::Start: break;
    }
    if (!steam) steam = LoadGameSteam();
    auto init = steam ? Fn<bool (*)()>(steam, "SteamAPI_Init") : nullptr;
    if (init && init() && SteamUp(steam)) return Use(steam, true);
    if (!told_) {
        told_ = true;
        HSM_LOG("fetch: Steam did not start (%s); no clones and no recording until it does. Is Steam running, and was the game started from it?",
                steam ? "SteamAPI_Init failed" : "the game's steam_api64.dll did not load");
    }
    return false;
}

bool SteamLobby::Start() {
    for (int waited = 0; waited <= kWaitForSteamMs; waited += 250, Sleep(250))
        if (Try(waited)) return true;
    return false;
}

void SteamLobby::KeepTrying() {
    while (!Try(kWaitForSteamMs)) Sleep(kRetrySteamMs);
}

void SteamLobby::Tick(uint64_t nowMs) {
    if (!started_ || nowMs < nextPoll_) return;
    nextPoll_ = nowMs + 1000;
    if (pump_) pump_();
    const uint64_t was = g_lobby;
    if (!was) return;
    const uint64_t now = clone::LobbyAfterPoll(was, members_(matchmaking_, was));
    if (now == was) return;
    g_lobby = now;
    HSM_LOG("fetch: left the Steam lobby");
}

bool SteamLobby::Offline() const { return started_ && !g_lobby; }

}  // namespace fetch
