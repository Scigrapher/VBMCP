#include "common.h"

HMODULE g_moduleHandle = NULL;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_moduleHandle = instance;
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
