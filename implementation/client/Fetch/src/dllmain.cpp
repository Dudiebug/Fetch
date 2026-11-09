// Fetch: style clones, docs/CLONE.md. DllMain only wires the dsound forwarders and starts a
// worker thread; everything else happens on that thread and on the game thread.
#include <windows.h>
#include "clone/hsm_clone_rules.h"
#include "log.h"

bool DsInitProxy();
namespace fetch { DWORD WINAPI Main(void*); }

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        DsInitProxy();
        HANDLE once = CreateMutexW(nullptr, FALSE, hsm::clone::FetchMutexName(GetCurrentProcessId()).c_str());
        if (once && GetLastError() == ERROR_ALREADY_EXISTS) return TRUE;
        if (HANDLE t = CreateThread(nullptr, 0, fetch::Main, nullptr, 0, nullptr)) CloseHandle(t);
    } else if (reason == DLL_PROCESS_DETACH && reserved) {
        hsm::LogExit();
    }
    return TRUE;
}
