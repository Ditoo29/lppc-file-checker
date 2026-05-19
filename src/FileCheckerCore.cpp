#include "FileCheckerCore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <regex>
#include <sstream>
#include <system_error>

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace filechecker {

namespace {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) return {};
    const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(required), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), required);
    return wide;
}

std::string Trim(std::string text) {
    const auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](char ch) { return !isSpace(static_cast<unsigned char>(ch)); }).base(), text.end());
    return text;
}

std::string RemoveSpacesAndSlashes(std::string text) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char ch) { return ch == ' ' || ch == '/'; }), text.end());
    return text;
}

std::string NormalizeTimestamp(std::string released) {
    released.erase(std::remove_if(released.begin(), released.end(), [](unsigned char ch) { return ch == '-' || ch == ':' || ch == ' '; }), released.end());
    return released;
}

std::wstring GetModuleFolderWide() {
    wchar_t modulePath[MAX_PATH]{};
    GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
    std::wstring folder(modulePath);
    const size_t pos = folder.find_last_of(L"\\/");
    if (pos != std::wstring::npos) folder.erase(pos);
    return folder;
}

std::filesystem::path FindLppcRoot(std::filesystem::path path) {
    while (!path.empty()) {
        if (path.filename() == L"LPPC") return path;
        if (!path.has_parent_path()) break;
        path = path.parent_path();
    }
    return {};
}

std::string FetchUrlWithWinHttp(const std::string& url) {
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);

    std::wstring wideUrl = Utf8ToWide(url);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &components)) return {};

    const bool secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    std::wstring host(components.lpszHostName, components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);

    HINTERNET session = WinHttpOpen(L"LPPC File Checker", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return {};

    HINTERNET connect = WinHttpConnect(session, host.c_str(), components.nPort, 0);
    if (!connect) { WinHttpCloseHandle(session); return {}; }

    const DWORD flags = secure ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!request) { WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return {}; }

    const BOOL sent = WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (!sent || !WinHttpReceiveResponse(request, nullptr)) {
        WinHttpCloseHandle(request); WinHttpCloseHandle(connect); WinHttpCloseHandle(session); return {};
    }

    std::string response;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::string buffer(static_cast<size_t>(available), '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer.data(), available, &read)) { response.clear(); break; }
        buffer.resize(read);
        response.append(buffer);
    }

    WinHttpCloseHandle(request);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return response;
}

std::optional<PackageMetadata> ParseInstallPackage(const std::string& pageText) {
    static const std::regex htmlRowPattern(
        R"(<tr>\s*<td>\s*ES\s*</td>\s*<td>\s*LPPC Install-Package\s*</td>\s*<td>\s*([0-9]{4}\s*/\s*[0-9]{2})\s*</td>\s*<td>\s*([0-9]+)\s*</td>\s*<td>\s*([0-9]{4}-[0-9]{2}-[0-9]{2}\s+[0-9]{2}:[0-9]{2}:[0-9]{2})\s*<td)",
        std::regex::ECMAScript);

    static const std::regex markdownRowPattern(
        R"(\|\s*ES\s*\|\s*LPPC Install-Package\s*\|\s*([0-9]{4}\s*/\s*[0-9]{2})\s*\|\s*([0-9]+)\s*\|\s*([0-9]{4}-[0-9]{2}-[0-9]{2}\s+[0-9]{2}:[0-9]{2}:[0-9]{2})\s*\|)",
        std::regex::ECMAScript);

    std::smatch match;
    if (!std::regex_search(pageText, match, htmlRowPattern) && !std::regex_search(pageText, match, markdownRowPattern)) {
        return std::nullopt;
    }

    PackageMetadata metadata;
    metadata.packageName = "LPPC Install-Package";
    metadata.airac = Trim(match[1].str());
    metadata.version = Trim(match[2].str());
    metadata.released = Trim(match[3].str());
    return metadata;
}

std::string SuffixAfterSecondHyphen(const std::string& stem) {
    const auto first = stem.find('-');
    if (first == std::string::npos) return {};
    const auto second = stem.find('-', first + 1);
    if (second == std::string::npos || second + 1 >= stem.size()) return {};
    return stem.substr(second + 1);
}

bool MatchesSuffixAndPrefix(const std::filesystem::path& file, const std::string& suffix) {
    if (file.extension() != ".ese" && file.extension() != ".sct") return false;
    const std::string name = file.filename().u8string();
    if (name.rfind("LPPC", 0) != 0) return false;
    return name.find(suffix) != std::string::npos;
}

}  // namespace

std::string FetchUrl(const std::string& url) {
    return FetchUrlWithWinHttp(url);
}

std::optional<PackageMetadata> ParseLatestUpdatePackage(const std::string& pageText) {
    return ParseInstallPackage(pageText);
}

std::string BuildExpectedStem(const PackageMetadata& package) {
    std::string versionText = package.version;
    try {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%04d", std::stoi(package.version));
        versionText = buffer;
    } catch (...) {}

    return "LPPC-Package_" + NormalizeTimestamp(package.released) + "-" + RemoveSpacesAndSlashes(package.airac) + "-" + versionText;
}

PackageCheckResult CheckPackageFiles(const std::filesystem::path& packageRoot, const PackageMetadata& package) {
    PackageCheckResult result;
    result.packageRoot = packageRoot;
    result.expectedStem = BuildExpectedStem(package);

    std::error_code ec;
    if (!std::filesystem::exists(packageRoot, ec)) {
        result.issues.push_back("Package root does not exist: " + packageRoot.u8string());
        return result;
    }

    const std::string suffix = SuffixAfterSecondHyphen(result.expectedStem);
    bool hasEse = false;
    bool hasSct = false;

    for (const auto& entry : std::filesystem::directory_iterator(packageRoot, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (!MatchesSuffixAndPrefix(entry.path(), suffix)) continue;

        if (entry.path().extension() == ".ese") {
            hasEse = true;
            result.matchingFiles.push_back(entry.path());
        } else if (entry.path().extension() == ".sct") {
            hasSct = true;
            result.matchingFiles.push_back(entry.path());
        }
    }

    result.success = hasEse && hasSct;
    if (!result.success) {
        result.issues.push_back("Missing matching .ese and .sct files for suffix: " + suffix);
    }

    return result;
}

std::filesystem::path DefaultPackageRoot() {
    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(appData) / "EuroScope";
}

std::string BuildReportText(const PackageCheckResult& result, const PackageMetadata& package) {
    std::ostringstream report;
    report << "LPPC file checker\n";
    report << "Package: " << package.packageName << "\n";
    report << "AIRAC: " << package.airac << "\n";
    report << "Version: " << package.version << "\n";
    report << "Released: " << package.released << "\n";
    report << "Expected stem: " << result.expectedStem << "\n";
    report << "Root: " << result.packageRoot.u8string() << "\n";
    report << "Status: " << (result.success ? "OK" : "FAILED") << "\n";

    if (!result.matchingFiles.empty()) {
        report << "Matched files:\n";
        for (const auto& file : result.matchingFiles) report << "- " << file.u8string() << "\n";
    }

    if (!result.issues.empty()) {
        report << "Issues:\n";
        for (const auto& issue : result.issues) report << "- " << issue << "\n";
    }

    return report.str();
}

}  // namespace filechecker