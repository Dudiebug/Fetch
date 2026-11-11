// When a clone may be in the game: which levels have fighters, the Steam lobby rule, and who the recorder's foe may be. docs/CLONE.md.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

#include "hsm_owner.h"

namespace hsm::clone {

inline std::wstring FetchMutexName(uint32_t pid) { return L"Local\\Fetch-" + std::to_wstring(pid); }

// Clones are offline only, and being in a Steam lobby is what online means here. Steam has no call for "which lobby am I in", so
// entering one is seen as a callback, and leaving is seen by asking how many are in it: outside a lobby, Steam says 0.
inline uint64_t LobbyAfterEnter(uint64_t was, uint64_t lobby, bool ok) { return ok ? lobby : was; }
inline uint64_t LobbyAfterPoll(uint64_t lobby, int members) { return members == 0 ? 0 : lobby; }

// Lobbies can only be seen through Steam, and Half Sword never starts it. Another mod may, so it gets a moment; then Fetch does.
enum class SteamStep { Wait, Start, Use };
inline SteamStep NextSteamStep(bool up, int waitedMs, int waitMs) { return up ? SteamStep::Use : waitedMs >= waitMs ? SteamStep::Start : SteamStep::Wait; }

// Whose play the recorder keeps against: the nearest living fighter that may be the foe. Offline any of them may (the clone is team
// 2, the player 1). In a Steam lobby HSMMod gives a teammate's stand-in my Team Int and a foe another, so one of my team is not the
// foe. A Team Int that could not be read rules nothing out.
constexpr int kTeamUnknown = -2147483647 - 1;
inline bool MayBeFoe(bool online, int myTeam, int theirTeam) {
    return !online || myTeam == kTeamUnknown || theirTeam == kTeamUnknown || theirTeam != myTeam;
}

// A fighter's guard, as the recorder keeps it and a clone reads it (VitalsMsg.state). Steam build 24185754 set the guard flag
// whenever a hand was raised. Build 26568321 has Combat Stance in its place, which a raised hand alone does not always set (it
// also wants a grip, the other hand, or a fast swing). So a raised hand counts as guarding on both builds. On the older build
// this changes nothing.
inline uint8_t GuardState(uint8_t state) {
    return (state & (kVitalsStateGuardR | kVitalsStateGuardL)) ? uint8_t(state | kVitalsStateGuarding) : state;
}

// Menus have no fighters, and neither does the Tavern outside Free Mode.
inline bool IsMenuLevel(const std::string& level) { return level.find("/Maps/Menus/") != std::string::npos || level.find("Map_Menu_") != std::string::npos; }
inline bool IsFighterLevel(const std::string& level, bool freeMode) {
    if (level.empty() || IsMenuLevel(level)) return false;
    return freeMode || level.find("Hub") == std::string::npos;
}

}  // namespace hsm::clone
