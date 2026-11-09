// dsound.dll proxy: the game exe imports DSOUND at load time, by ordinal, so this DLL (next to the exe) loads with the process.
// Every export is one jump to the same export of the real system dsound.dll. tools/check_dll.py --fetch checks the table.
#include <windows.h>
#include <cstddef>

#define DS_EXPORTS(X) \
    X(0, "DirectSoundCreate") X(1, "DirectSoundEnumerateA") X(2, "DirectSoundEnumerateW") X(3, "DllCanUnloadNow") \
    X(4, "DllGetClassObject") X(5, "DirectSoundCaptureCreate") X(6, "DirectSoundCaptureEnumerateA") \
    X(7, "DirectSoundCaptureEnumerateW") X(8, "GetDeviceID") X(9, "DirectSoundFullDuplexCreate") \
    X(10, "DirectSoundCreate8") X(11, "DirectSoundCaptureCreate8")

#define DS_STUB(i, name) __asm__(".text\n.globl DsFwd" #i "\nDsFwd" #i ":\n\tjmp *g_dsForward+" #i "*8(%rip)\n");
DS_EXPORTS(DS_STUB)

namespace {
#define DS_NAME(i, name) name,
constexpr const char* kNames[] = { DS_EXPORTS(DS_NAME) };
constexpr size_t kCount = sizeof kNames / sizeof kNames[0];
// What a caller of an export the real DLL lacks gets: an error it has to handle anyway.
extern "C" HRESULT WINAPI DsMissing() { return E_NOTIMPL; }
}  // namespace

extern "C" void* g_dsForward[kCount];
void* g_dsForward[kCount];

bool DsInitProxy() {
    for (void*& p : g_dsForward) p = reinterpret_cast<void*>(&DsMissing);
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (n == 0 || n + 16 >= MAX_PATH) return false;
    lstrcatW(path, L"\\dsound.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) return false;
    for (size_t i = 0; i < kCount; ++i)
        if (FARPROC p = GetProcAddress(real, kNames[i])) g_dsForward[i] = reinterpret_cast<void*>(p);
    return true;
}
