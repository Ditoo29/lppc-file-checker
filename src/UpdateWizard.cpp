#include "FileCheckerCore.hpp"

#include <windows.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <string>

namespace {

constexpr const char* kCaption = "LPPC Update Wizard";

void ShowInfo(const std::string& text, HWND owner) {
    MessageBoxA(owner, text.c_str(), kCaption, MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

void ShowError(const std::string& text, HWND owner) {
    MessageBoxA(owner, text.c_str(), kCaption, MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
}

void Log(const std::string& text) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::ofstream output(std::filesystem::path(tempPath) / "LPPCFileChecker_update.log", std::ios::app);
    if (output) output << text << "\n";
}

std::wstring ToLowerWide(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(std::towlower(ch)); });
    return text;
}

struct WindowSearch {
    DWORD pid = 0;
    HWND window = nullptr;
    HWND candidate = nullptr;
};

BOOL CALLBACK FindProcessWindow(HWND window, LPARAM lParam) {
    WindowSearch* search = reinterpret_cast<WindowSearch*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid) return TRUE;
    if (!IsWindowVisible(window)) return TRUE;
    if (search->candidate == nullptr) search->candidate = window;
    if (GetWindow(window, GW_OWNER) == nullptr) {
        search->window = window;
        return FALSE;
    }
    return TRUE;
}

HANDLE FindEuroScopeProcess(const std::filesystem::path& expectedPath, DWORD& pidOut) {
    const std::wstring expected = ToLowerWide(expectedPath.wstring());

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return nullptr;

    HANDLE fallback = nullptr;
    DWORD fallbackPid = 0;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"EuroScope.exe") != 0) continue;
            HANDLE process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE, FALSE,
                                         entry.th32ProcessID);
            if (process == nullptr) continue;

            wchar_t imagePath[MAX_PATH]{};
            DWORD size = MAX_PATH;
            if (QueryFullProcessImageNameW(process, 0, imagePath, &size) && ToLowerWide(imagePath) == expected) {
                CloseHandle(snapshot);
                pidOut = entry.th32ProcessID;
                return process;
            }
            if (fallback == nullptr) {
                fallback = process;
                fallbackPid = entry.th32ProcessID;
            } else {
                CloseHandle(process);
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    pidOut = fallbackPid;
    return fallback;
}

std::string CloseEuroScope(HANDLE process, DWORD pid) {
    WindowSearch search{pid, nullptr, nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    const HWND target = search.window != nullptr ? search.window : search.candidate;
    Log(std::string("close: unowned window ") + (search.window != nullptr ? "found" : "not found") +
        ", any visible window " + (search.candidate != nullptr ? "found" : "not found"));

    DWORD threadId = 0;
    if (target != nullptr) {
        threadId = GetWindowThreadProcessId(target, nullptr);
        SendMessageTimeout(target, WM_CLOSE, 0, 0, SMTO_ABORTIFHUNG | SMTO_NORMAL, 2000, nullptr);
        Log("close: WM_CLOSE sent to window");
    } else {
        Log("close: no window found to close");
    }

    if (WaitForSingleObject(process, 2000) == WAIT_OBJECT_0) {
        Log("close: euroscope exited after WM_CLOSE");
        return {};
    }
    Log("close: still running after WM_CLOSE");

    if (threadId != 0) {
        PostThreadMessage(threadId, WM_QUIT, 0, 0);
        Log("close: WM_QUIT posted to thread " + std::to_string(threadId));
        if (WaitForSingleObject(process, 3000) == WAIT_OBJECT_0) {
            Log("close: euroscope exited after WM_QUIT");
            return {};
        }
        Log("close: still running after WM_QUIT");
    }

    const int choice = MessageBoxA(target,
                                   "EuroScope did not close on its own.\n\nForce-close it now? Any unsaved changes will be lost.\n\n"
                                   "Yes = force close and continue with the update\nNo = abort the update",
                                   kCaption, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2 | MB_TOPMOST | MB_SETFOREGROUND);
    if (choice != IDYES) {
        Log("close: user declined force close");
        return "The update was aborted because EuroScope could not be closed.";
    }

    if (!TerminateProcess(process, 0)) {
        const DWORD error = GetLastError();
        Log("close: TerminateProcess failed");
        return "EuroScope could not be closed (Windows error " + std::to_string(error) + ").";
    }
    WaitForSingleObject(process, 5000);
    Log("close: euroscope force-closed");
    return {};
}

}  // namespace

HWND FindEuroScopeWindow(DWORD pid) {
    WindowSearch search{pid, nullptr, nullptr};
    EnumWindows(FindProcessWindow, reinterpret_cast<LPARAM>(&search));
    return search.window != nullptr ? search.window : search.candidate;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    Log("wizard started");
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr || argc < 2) {
        Log("wizard: no handover path argument");
        ShowError("The update information file was not specified.", nullptr);
        return 1;
    }
    const std::filesystem::path handoverPath(argv[1]);
    LocalFree(argv);

    const auto handover = filechecker::ReadUpdateHandover(handoverPath);
    if (!handover) {
        Log("wizard: handover unreadable: " + handoverPath.u8string());
        ShowError("The update information file could not be read:\n" + handoverPath.u8string(), nullptr);
        return 1;
    }
    Log("wizard: root=" + handover->root.u8string() + " archive=" + handover->archive.u8string() +
        " euroscope=" + handover->euroscope.u8string());

    Sleep(300);

    DWORD pid = 0;
    HANDLE euroScope = FindEuroScopeProcess(handover->euroscope, pid);
    HWND owner = nullptr;
    if (euroScope != nullptr) {
        owner = FindEuroScopeWindow(pid);
        Log("wizard: euroscope found pid=" + std::to_string(pid));
        const std::string closeError = CloseEuroScope(euroScope, pid);
        CloseHandle(euroScope);
        if (!closeError.empty()) {
            Log("wizard: close failed: " + closeError);
            ShowError(closeError, owner);
            return 1;
        }
        owner = nullptr;
    } else {
        Log("wizard: euroscope process not found - skipping close");
    }

    Log("wizard: extracting archive");
    const std::string extractError = filechecker::ExtractArchive(handover->archive, handover->root);
    if (!extractError.empty()) {
        Log("wizard: extract failed: " + extractError);
        ShowError("Automatic installation failed:\n" + extractError + "\n\nExtract the archive manually into:\n" +
                  handover->root.u8string(), nullptr);
        return 1;
    }
    Log("wizard: extraction finished");

    filechecker::PackageMetadata package;
    package.packageName = "LPPC Update-Package";
    package.airac = handover->airac;
    package.version = handover->version;
    package.released = handover->released;

    const auto check = filechecker::CheckPackageFiles(handover->root, package);
    if (!check.success) {
        std::string issues;
        for (const auto& issue : check.issues) issues += issue + "\n";
        Log("wizard: verification failed: " + issues);
        ShowError("The archive was extracted but verification failed:\n" + issues + "\nExtract the archive manually into:\n" +
                  handover->root.u8string(), nullptr);
        return 1;
    }
    Log("wizard: verification ok");

    ShowInfo("LPPC update installed successfully.\n\nAIRAC: " + handover->airac + "\nVersion: " + handover->version +
             "\n\nLocation:\n" + handover->root.u8string() + "\n\nEuroScope will now restart.", nullptr);

    if (!handover->euroscope.empty()) {
        Log("wizard: restarting euroscope");
        ShellExecuteW(nullptr, L"open", handover->euroscope.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    std::error_code ec;
    std::filesystem::remove(handoverPath, ec);
    Log("wizard: done");
    return 0;
}