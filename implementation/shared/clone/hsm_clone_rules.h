// When a clone may be in the game: which levels have fighters, and the Steam lobby rule. docs/CLONE.md.
#pragma once
#include <algorithm>
#include <cstdint>
#include <string>

namespace hsm::clone {

inline std::wstring FetchMutexName(uint32_t pid) { return L"Local\\Fetch-" + std::to_wstring(pid); }

// Clones are offline only, and being in a Steam lobby is what online means here. Steam has no call for "which lobby am I in", so
// entering one is seen as a callback, and leaving is seen by asking how many are in it: outside a lobby, Steam says 0.
inline uint64_t LobbyAfterEnter(uint64_t was, uint64_t lobby, bool ok) { return ok ? lobby : was; }
inline uint64_t LobbyAfterPoll(uint64_t lobby, int members) { return members == 0 ? 0 : lobby; }

// Lobbies can only be seen through Steam, and Half Sword never starts it. Another mod may, so it gets a moment; then Fetch does.
enum class SteamStep { Wait, Start, Use };
inline SteamStep NextSteamStep(bool up, int waitedMs, int waitMs) { return up ? SteamStep::Use : waitedMs >= waitMs ? SteamStep::Start : SteamStep::Wait; }

// Menus have no fighters, and neither does the Tavern outside Free Mode.
inline bool IsMenuLevel(const std::string& level) { return level.find("/Maps/Menus/") != std::string::npos || level.find("Map_Menu_") != std::string::npos; }
inline bool IsFighterLevel(const std::string& level, bool freeMode) {
    if (level.empty() || IsMenuLevel(level)) return false;
    return freeMode || level.find("Hub") == std::string::npos;
}

}  // namespace hsm::clone
