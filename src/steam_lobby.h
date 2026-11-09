// Whether this game is in a Steam lobby: clones and recording are offline only. shared/clone/hsm_clone_rules.h has the rule.
#pragma once
#include <atomic>
#include <cstdint>

namespace fetch {

class SteamLobby {
public:
    // Worker thread, before the game can reach a lobby. Half Sword never starts Steam itself: this waits for a mod that does, then
    // starts it. False: no way to tell, so Offline stays false.
    bool Start();
    // Worker thread, after a false Start: tries every few seconds until Steam is there.
    void KeepTrying();
    // Game thread: asks Steam once a second whether the lobby is still there.
    void Tick(uint64_t nowMs);
    bool Offline() const;
private:
    bool Try(int waitedMs);
    bool Use(void* dll, bool startedHere);
    void* matchmaking_ = nullptr;
    int (*members_)(void*, uint64_t) = nullptr;
    void (*pump_)() = nullptr;   // set when Fetch started Steam: then nobody else may run its callbacks
    uint64_t nextPoll_ = 0;
    std::atomic<bool> started_{false};
    bool told_ = false;
};

}  // namespace fetch
