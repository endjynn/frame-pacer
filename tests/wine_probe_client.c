#include <windows.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR command, int show)
{
    HMODULE module;
    (void)instance;
    (void)previous;
    (void)command;
    (void)show;
    module = LoadLibraryW(L"frame_pacer_probe.dll");
    return module ? 0 : 1;
}
