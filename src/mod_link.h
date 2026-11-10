// The link page with HSMMod (dwmapi.dll): one small named mapping per game process (shared/interop/hsm_mod_link.h has the layout
// and the rules). Fetch writes its half (the menu open, a name typed) and reads HSMMod's (in a session, typing in chat, playing held-
// back keys again). Mapped once at start and kept for the life of the process. Without HSMMod, or with one too old to know the page,
// HSMMod's half stays zero and reads as not there.
#pragma once
#include "interop/hsm_mod_link.h"
#include <cstdint>

namespace fetch {

class ModLink {
public:
    // Worker thread, before the first tick. False: no page (the log says why); every read then says HSMMod is not there.
    bool Map();
    // Game thread, once a tick: Fetch's half written, HSMMod's read, and whether HSMMod is there by its beat. dtMs: the time since
    // the last tick.
    void Tick(const hsm::interop::FetchState& mine, uint32_t dtMs);
    // Game thread: Fetch's half again at once, when the menu opens or shuts inside a tick.
    void Write(const hsm::interop::FetchState& mine);
    bool live() const { return live_; }
    const hsm::interop::HsmState& hsm() const { return hsm_; }
    // At hook time, from the page itself (the replay flag changes inside a tick): a key now is HSMMod playing a held-back one again.
    bool SkipsInput() const { return hsm::interop::FetchSkipsInput(live_, hsm::interop::ReadHsm(page_)); }
private:
    uint8_t* page_ = nullptr;
    hsm::interop::LinkWatch watch_;
    hsm::interop::HsmState hsm_;
    bool live_ = false;
};

ModLink& Link();   // the one of the process

}  // namespace fetch
