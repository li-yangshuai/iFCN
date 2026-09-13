#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>

// A small native launcher keeps the portable application's entry point at the
// archive root. The GUI itself discovers its bundled runtime and opens IFCN
// arguments, including paths containing spaces or non-ASCII characters.
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    std::vector<wchar_t> path(32768);
    const DWORD length = GetModuleFileNameW(nullptr, path.data(),
                                             static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size()) return 1;
    const std::wstring root = std::wstring(path.data(), length).substr(
        0, std::wstring(path.data(), length).find_last_of(L"\\/"));
    const std::wstring executable = root + L"\\bin\\fcnx_gui.exe";

    const wchar_t *arguments = GetCommandLineW();
    bool quoted = false;
    while (*arguments) {
        if (*arguments == L'"') quoted = !quoted;
        else if (!quoted && (*arguments == L' ' || *arguments == L'\t')) break;
        ++arguments;
    }
    while (*arguments == L' ' || *arguments == L'\t') ++arguments;
    std::wstring command = L"\"" + executable + L"\" " + arguments;
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process)) {
        MessageBoxW(nullptr,
                    L"iFCN could not start. Extract the complete archive before opening iFCN.exe.",
                    L"iFCN", MB_OK | MB_ICONERROR);
        return 1;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return 0;
}
