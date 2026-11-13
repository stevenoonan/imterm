#include "application.h"

#include <cstdio>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif

namespace {
int Run()
{
    try {
        imterm::RunApplication();
        return 0;
    }
    catch (const std::exception& error) {
        std::fprintf(stderr, "imterm: %s\n", error.what());
#ifdef _WIN32
        MessageBoxA(nullptr, error.what(), "imterm startup/rendering error", MB_OK | MB_ICONERROR);
#endif
        return 1;
    }
}
} // namespace

#ifdef _WIN32
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) { return Run(); }
#else
int main() { return Run(); }
#endif
