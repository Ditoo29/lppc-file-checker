#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#pragma warning(push, 0)
#include "EuroScopePlugIn.h"
#pragma warning(pop)

#include "FileCheckerCore.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>

namespace {

constexpr const char* kPluginName = "LPPC File Checker";
constexpr const char* kPluginVersion = "1.0.0";
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

std::string CheckedSuffix(const filechecker::PackageMetadata& package) {
    const std::string stem = filechecker::BuildExpectedStem(package);
    const auto first = stem.find('-');
    if (first == std::string::npos) return {};
    const auto second = stem.find('-', first + 1);
    if (second == std::string::npos || second + 1 >= stem.size()) return {};
    return stem.substr(second + 1);
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

std::string BuildErrorMessage(const filechecker::PackageCheckResult& result, const filechecker::PackageMetadata& package) {
    std::ostringstream out;
    out << "Your LPPC files are outdated or missing.\n\n";
    out << "Please update your files from:\n" << kPackagesUrl << "\n\n";
    out << "Current check location:\n";
    out << result.packageRoot.u8string() << "\n\n";
    out << "AIRAC: " << package.airac << "\n";
    out << "Version: " << package.version << "\n";
    return out.str();
}

void ShowInfoPopup(const std::string& text, const char* caption) {
    MessageBoxA(nullptr, text.c_str(), caption, MB_OK | MB_ICONINFORMATION);
}

void ShowWarningPopup(const std::string& text, const char* caption) {
    MessageBoxA(nullptr, text.c_str(), caption, MB_OK | MB_ICONWARNING);
}

class LPPCFileCheckerPlugin final : public EuroScopePlugIn::CPlugIn {
  public:
    LPPCFileCheckerPlugin()
        : EuroScopePlugIn::CPlugIn(EuroScopePlugIn::COMPATIBILITY_CODE, kPluginName, kPluginVersion, kPluginAuthor, kPluginCopyright), m_initialCheckDone(false) {}

    bool OnCompileCommand(const char* sCommandLine) override {
        if (!ParseLPPCFileCheckerCommand(sCommandLine)) return false;
        std::thread([this]() { RunCheck(true); }).detach();
        return true;
    }

  private:
    bool m_initialCheckDone;

    void SendChat(const std::string& message) {
        DisplayUserMessage(kPluginName, "", message.c_str(), true, false, false, false, false);
    }

    void OnTimer(int Counter) override {
        if (!m_initialCheckDone && Counter >= 3) {
            m_initialCheckDone = true;
            std::thread([this]() { RunCheck(false); }).detach();
        }
    }

    void RunCheck(bool fromCommand) {
        const std::string pageText = filechecker::FetchUrl(kPackagesUrl);
        if (pageText.empty()) {
            ShowWarningPopup("Could not verify LPPC files because the package website could not be reached.\n\nPlease check your internet connection and try again.\n\nReference website:\nhttps://files.aero-nav.com/LPPC", kPluginName);
            if (fromCommand) SendChat("Unable to fetch LPPC package data from " + std::string(kPackagesUrl));
            return;
        }

        const auto package = filechecker::ParseLatestUpdatePackage(pageText);
        if (!package) {
            ShowWarningPopup("Could not verify LPPC files because package information could not be parsed.\n\nReference website:\nhttps://files.aero-nav.com/LPPC", kPluginName);
            if (fromCommand) SendChat("Could not parse package rows from " + std::string(kPackagesUrl));
            return;
        }

        std::filesystem::path root = filechecker::DefaultPackageRoot();
        if (!root.empty() && std::filesystem::is_regular_file(root)) root = root.parent_path();

        const auto result = filechecker::CheckPackageFiles(root, *package);

        if (!result.success) {
            ShowWarningPopup(BuildErrorMessage(result, *package), kPluginName);
        } else if (fromCommand) {
            ShowInfoPopup(BuildSuccessMessage(result, *package), kPluginName);
            SendChat("LPPC files are up to date.");
        }
    }
};

LPPCFileCheckerPlugin* g_plugin = nullptr;

}  // namespace

extern "C" IMAGE_DOS_HEADER __ImageBase;

void __declspec(dllexport) EuroScopePlugInInit(EuroScopePlugIn::CPlugIn** ppPlugInInstance) {
    g_plugin = new LPPCFileCheckerPlugin();
    *ppPlugInInstance = g_plugin;
}

void __declspec(dllexport) EuroScopePlugInExit(void) {
    delete g_plugin;
    g_plugin = nullptr;
}