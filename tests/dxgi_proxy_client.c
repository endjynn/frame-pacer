#include <windows.h>
#include <string.h>

typedef int (WINAPI *probe_status_fn)(void);

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    HMODULE module;
    probe_status_fn status;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    module = LoadLibraryW(L"dxgi.dll");
    if (!module) return 1;
    {
        FARPROC raw = GetProcAddress(module, "frame_pacer_probe_status");
        memcpy(&status, &raw, sizeof(status));
    }
    return status && status() ? 0 : 1;
}
