#include "play_runner/play_runner.h"

#include <windows.h>

namespace {

    void InitDpiAwareness() {
        HMODULE user32 = ::LoadLibraryW(L"user32.dll");
        if (user32) {
            using SetProcessDpiAwarenessContextFunc =
                BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
            auto set_ctx = reinterpret_cast<SetProcessDpiAwarenessContextFunc>(
                ::GetProcAddress(user32, "SetProcessDpiAwarenessContext")
            );
            if (set_ctx) {
                if (set_ctx(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) {
                    ::FreeLibrary(user32);
                    return;
                }
            }
            ::FreeLibrary(user32);
        }

    } // namespace

} // namespace

int main() {
    InitDpiAwareness();

    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    play_runner::PlayRunner runner;
    return runner.Run();
}
