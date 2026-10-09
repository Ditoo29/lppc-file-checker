#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#pragma warning(push, 0)
#include "EuroScopePlugIn.h"
#pragma warning(pop)

#include "FileCheckerCore.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr const char* kPluginName = "LPPC File Checker";
constexpr const char* kPluginVersion = "2.0.0";
constexpr const char* kPluginAuthor = "Dito29";
constexpr const char* kPluginCopyright = "(c) 2026 Dito29";
constexpr const char* kPackagesUrl = "https://files.aero-nav.com/LPPC";

std::string Trim(std::string text) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), text.end());
    return text;
}

std::string ToLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return text;
}

bool ParseLPPCFileCheckerCommand(const char* sCommandLine) {
    if (sCommandLine == nullptr) return false;
    const std::string commandLine = Trim(sCommandLine);
    if (commandLine.empty()) return false;
    std::istringstream iss(commandLine);
    std::string command;
    iss >> command;
    command = ToLower(command);
    return command == ".lppcversion" || command == "lppcversion";
}

std::string BuildSuccessMessage(const filechecker::PackageCheckResult& result, const filechecker::PackageMetadata& package) {
    std::ostringstream out;
    out << "All LPPC files are up to date.\n\n";
    out << "Current check location:\n";
    out << result.packageRoot.u8string() << "\n\n";
    out << "AIRAC: " << package.airac << "\n";
    out << "Version: " << package.version << "\n";
    return out.str();
}

std::string BuildUpdateIntro() {
    std::ostringstream out;
    out << "Your LPPC files are outdated or missing.\n\n";
    out << "Download the LPPC Update-Package (.zip or .7z) and come back to EuroScope.\n\n";
    out << "Open the download page now?";
    return out.str();
}

std::string BuildInstallPrompt(const filechecker::PackageMetadata& package) {
    const std::string base = filechecker::BuildUpdateArchiveBaseName(package);
    std::ostringstream out;
    out << "Click OK once the LPPC Update-Package is in your Downloads folder.\n\n";
    out << "Expected file:\n" << base << ".zip\nor\n" << base << ".7z\n\n";
    out << "Cancel to abort the update.";
    return out.str();
}

std::string BuildArchiveNotFound(const filechecker::PackageMetadata& package) {
    const std::string base = filechecker::BuildUpdateArchiveBaseName(package);
    std::ostringstream out;
    out << "Update package not found in your Downloads folder.\n\n";
    out << "Expected file:\n" << base << ".zip\nor\n" << base << ".7z\n";
    out << "In folder:\n" << filechecker::DownloadsFolder().u8string() << "\n\n";
    out << "Click OK to check again, or Cancel to abort.";
    return out.str();
}

std::string BuildArchiveNotReady(const std::filesystem::path& archive) {
    std::ostringstream out;
    out << "The archive could not be read yet:\n" << archive.u8string() << "\n\n";
    out << "The download may still be in progress. Wait until it has finished.\n\n";
    out << "Click OK to check again, or Cancel to abort.";
    return out.str();
}

std::string BuildClosePrompt(const std::filesystem::path& archive) {
    std::ostringstream out;
    out << "The update package is ready:\n" << archive.filename().u8string() << "\n\n";
    out << "Click OK to close EuroScope and install the update now.\n";
    out << "EuroScope restarts automatically when the update is finished.\n\n";
    out << "Cancel to abort the update.";
    return out.str();
}

struct MainWindowSearch {
    DWORD pid;
    HWND main;
    HWND any;
};

BOOL CALLBACK FindMainWindowCallback(HWND window, LPARAM lParam) {
    MainWindowSearch* search = reinterpret_cast<MainWindowSearch*>(lParam);
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != search->pid) return TRUE;
    if (!IsWindowVisible(window)) return TRUE;
    if (search->any == nullptr) search->any = window;
    if (GetWindow(window, GW_OWNER) == nullptr) {
        search->main = window;
        return FALSE;
    }
    return TRUE;
}

HWND FindMainWindow() {
    MainWindowSearch search{GetCurrentProcessId(), nullptr, nullptr};
    EnumWindows(FindMainWindowCallback, reinterpret_cast<LPARAM>(&search));
    return search.main != nullptr ? search.main : search.any;
}

void ShowInfoPopup(const std::string& text, const char* caption) {
    MessageBoxA(FindMainWindow(), text.c_str(), caption, MB_OK | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

void ShowWarningPopup(const std::string& text, const char* caption) {
    MessageBoxA(FindMainWindow(), text.c_str(), caption, MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
}

bool ShowQuestionPopup(const std::string& text, const char* caption) {
    return MessageBoxA(FindMainWindow(), text.c_str(), caption, MB_YESNO | MB_ICONQUESTION | MB_TOPMOST | MB_SETFOREGROUND) == IDYES;
}

bool ShowContinuePopup(const std::string& text, const char* caption) {
    return MessageBoxA(FindMainWindow(), text.c_str(), caption, MB_OKCANCEL | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND) == IDOK;
}

void LogUpdate(const std::string& text) {
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    std::ofstream output(std::filesystem::path(tempPath) / "LPPCFileChecker_update.log", std::ios::app);
    if (output) output << "[plugin] " << text << "\n";
}

class LPPCFileCheckerPlugin final : public EuroScopePlugIn::CPlugIn {
  public:
    LPPCFileCheckerPlugin()
        : EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, kPluginName, kPluginVersion, kPluginAuthor, kPluginCopyright),
          m_initialCheckDone(false) {}

    bool OnCompileCommand(const char* sCommandLine) override {
        if (!ParseLPPCFileCheckerCommand(sCommandLine)) return false;
        StartCheck(true, true);
        return true;
    }

  private:
    bool m_initialCheckDone;
    std::atomic<bool> m_checkRunning{false};
    std::atomic<bool> m_updateLaunched{false};

    void SendChat(const std::string& message) {
        DisplayUserMessage(kPluginName, "", message.c_str(), true, false, false, false, false);
    }

    void StartCheck(bool fromCommand, bool announceIfBusy) {
        if (m_updateLaunched) {
            if (announceIfBusy) SendChat("The update is being installed - EuroScope will close shortly.");
            return;
        }
        if (m_checkRunning.exchange(true)) {
            if (announceIfBusy) SendChat("An LPPC check or update is already running.");
            return;
        }
        std::thread([this, fromCommand]() {
            RunCheck(fromCommand);
            m_checkRunning = false;
        }).detach();
    }

    void OnTimer(int Counter) override {
        if (!m_initialCheckDone && Counter >= 3) {
            m_initialCheckDone = true;
            StartCheck(false, false);
        }
    }

    void RunCheck(bool fromCommand) {
        const std::string pageText = filechecker::FetchUrl(kPackagesUrl);
        if (pageText.empty()) {
            ShowWarningPopup("Could not verify LPPC files because the package website could not be reached.\n\nPlease check your internet connection and try again.\n\nReference website:\nhttps://files.aero-nav.com/LPPC", kPluginName);
            if (fromCommand) SendChat("Unable to fetch LPPC package data from " + std::string(kPackagesUrl));
            return;
        }

        const auto package = filechecker::ParseUpdatePackage(pageText);
        if (!package) {
            ShowWarningPopup("Could not verify LPPC files because package information could not be parsed.\n\nReference website:\nhttps://files.aero-nav.com/LPPC", kPluginName);
            if (fromCommand) SendChat("Could not parse package rows from " + std::string(kPackagesUrl));
            return;
        }

        const std::filesystem::path root = filechecker::ResolvePackageRoot();

        const auto result = filechecker::CheckPackageFiles(root, *package);

        if (result.success) {
            if (fromCommand) {
                ShowInfoPopup(BuildSuccessMessage(result, *package), kPluginName);
                SendChat("LPPC files are up to date.");
            }
            return;
        }

        if (!ShowQuestionPopup(BuildUpdateIntro(), kPluginName)) {
            if (fromCommand) SendChat("LPPC update skipped. Manual download: " + std::string(kPackagesUrl));
            return;
        }

        filechecker::OpenUrlInBrowser(kPackagesUrl);

        InstallUpdate(result, *package, fromCommand);
    }

    void InstallUpdate(const filechecker::PackageCheckResult& result, const filechecker::PackageMetadata& package, bool fromCommand) {
        const std::filesystem::path downloads = filechecker::DownloadsFolder();
        const std::string baseName = filechecker::BuildUpdateArchiveBaseName(package);

        for (;;) {
            if (!ShowContinuePopup(BuildInstallPrompt(package), kPluginName)) {
                if (fromCommand) SendChat("LPPC update cancelled. Manual download: " + std::string(kPackagesUrl));
                return;
            }

            const std::filesystem::path archive = filechecker::FindUpdateArchive(downloads, baseName);
            if (archive.empty()) {
                ShowWarningPopup(BuildArchiveNotFound(package), kPluginName);
                continue;
            }

            if (!filechecker::ValidateArchive(archive, result.expectedSuffix)) {
                ShowWarningPopup(BuildArchiveNotReady(archive), kPluginName);
                continue;
            }

            if (!ShowContinuePopup(BuildClosePrompt(archive), kPluginName)) {
                if (fromCommand) SendChat("LPPC update cancelled. Manual download: " + std::string(kPackagesUrl));
                return;
            }

            m_updateLaunched = true;
            std::string launchError;
            if (!StartUpdateWizard(result, package, archive, launchError)) {
                m_updateLaunched = false;
                ShowWarningPopup("The update wizard could not be started:\n" + launchError +
                                     "\n\nNothing was changed and EuroScope is still open.",
                                 kPluginName);
                return;
            }
            return;
        }
    }

    bool StartUpdateWizard(const filechecker::PackageCheckResult& result, const filechecker::PackageMetadata& package,
                           const std::filesystem::path& archive, std::string& error) {
        std::error_code tempEc;
        const std::filesystem::path tempDir = std::filesystem::temp_directory_path(tempEc);
        if (tempEc) {
            error = "The temporary folder could not be determined.";
            return false;
        }

        wchar_t euroScopePath[MAX_PATH]{};
        const DWORD pathLength = GetModuleFileNameW(nullptr, euroScopePath, MAX_PATH);
        if (pathLength == 0 || pathLength >= MAX_PATH) {
            error = "The EuroScope executable path could not be determined.";
            return false;
        }

        const std::filesystem::path handoverFile = tempDir / "LPPCUpdate_handover.txt";
        filechecker::UpdateHandover handover;
        handover.root = result.packageRoot;
        handover.archive = archive;
        handover.euroscope = euroScopePath;
        handover.airac = package.airac;
        handover.version = package.version;
        handover.released = package.released;
        if (!filechecker::WriteUpdateHandover(handoverFile, handover)) {
            error = "The update information file could not be written:\n" + handoverFile.u8string();
            return false;
        }
        LogUpdate("handover written: " + handoverFile.u8string());

        HMODULE module = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                reinterpret_cast<LPCWSTR>(&filechecker::DownloadsFolder), &module)) {
            error = "The plugin module could not be located.";
            return false;
        }

        HRSRC resource = FindResourceA(module, MAKEINTRESOURCE(1), RT_RCDATA);
        if (resource == nullptr) {
            error = "The embedded update wizard is missing from LPPCFileChecker.dll. Please reinstall the plugin.";
            return false;
        }
        const DWORD size = SizeofResource(module, resource);
        HGLOBAL loaded = LoadResource(module, resource);
        const void* data = loaded != nullptr ? LockResource(loaded) : nullptr;
        if (data == nullptr || size == 0) {
            error = "The embedded update wizard could not be read.";
            return false;
        }

        const std::filesystem::path wizardFile = tempDir / ("LPPCUpdateWizard_" + std::to_string(GetTickCount64()) + ".exe");
        HANDLE file = CreateFileW(wizardFile.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = "The update wizard could not be written to:\n" + wizardFile.u8string();
            return false;
        }
        DWORD written = 0;
        const BOOL writeOk = WriteFile(file, data, size, &written, nullptr);
        CloseHandle(file);
        if (!writeOk || written != size) {
            error = "The update wizard could not be written to:\n" + wizardFile.u8string();
            return false;
        }
        LogUpdate("wizard exe written: " + wizardFile.u8string());

        std::wstring commandLine = L"\"" + wizardFile.wstring() + L"\" \"" + handoverFile.wstring() + L"\"";
        std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
        mutableCommand.push_back(L'\0');

        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(wizardFile.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr,
                            &startup, &process)) {
            LogUpdate("wizard launch failed: Windows error " + std::to_string(GetLastError()));
            error = "The update wizard could not be started (Windows error " + std::to_string(GetLastError()) + ").";
            return false;
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        LogUpdate("wizard launched");
        return true;
    }
};

LPPCFileCheckerPlugin* g_plugin = nullptr;

}  // namespace

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    g_plugin = new LPPCFileCheckerPlugin();
    *ppPlugInInstance = g_plugin;
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
    delete g_plugin;
    g_plugin = nullptr;
}