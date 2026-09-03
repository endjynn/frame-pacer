#include <windows.h>

static HMODULE probe_module;

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH)
        probe_module = instance;
    return TRUE;
}

__declspec(dllexport) int WINAPI frame_pacer_probe_status(void)
{
    WCHAR system_directory[MAX_PATH];
    UINT length = GetSystemDirectoryW(system_directory, MAX_PATH);
    HMODULE real_dxgi;
    if (!length || length >= MAX_PATH || length + 10 >= MAX_PATH)
        return 0;
    lstrcatW(system_directory, L"\\dxgi.dll");
    real_dxgi = LoadLibraryW(system_directory);
    return real_dxgi && real_dxgi != probe_module;
}
