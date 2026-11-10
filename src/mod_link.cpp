#include "mod_link.h"
#include "log.h"
#include <windows.h>

namespace fetch {
using namespace hsm::interop;

ModLink& Link() { static ModLink l; return l; }

bool ModLink::Map() {
    if (page_) return true;
    // Either mod may make it; the other opens the same one. Never closed: the page lives as long as the game.
    HANDLE h = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, kLinkBytes, LinkName(GetCurrentProcessId()).c_str());
    if (!h) { HSM_LOG("fetch: no link page for HSMMod (error %lu); the menu stays shut in its lobbies", GetLastError()); return false; }
    void* v = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kLinkBytes);
    if (!v) { HSM_LOG("fetch: the link page for HSMMod could not be mapped (error %lu); the menu stays shut in its lobbies", GetLastError()); CloseHandle(h); return false; }
    page_ = static_cast<uint8_t*>(v);
    WriteFetch(page_, FetchState{});   // Fetch is there from now, menu shut
    return true;
}

void ModLink::Write(const FetchState& mine) { WriteFetch(page_, mine); }

void ModLink::Tick(const FetchState& mine, uint32_t dtMs) {
    Write(mine);
    live_ = watch_.Tick(HsmBeat(page_), dtMs);
    hsm_ = live_ ? ReadHsm(page_) : HsmState{};
}

}  // namespace fetch
