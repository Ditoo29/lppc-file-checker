#include "FileCheckerCore.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <optional>
#include <regex>
#include <system_error>

#include <windows.h>
#include <objbase.h>
#include <shlobj.h>
#include <shellapi.h>
#include <winhttp.h>

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

std::optional<PackageMetadata> ParseUpdateRow(const std::string& pageText) {
    static const std::regex htmlRowPattern(
        R"(<tr>\s*<td>\s*ES\s*</td>\s*<td>\s*LPPC Update-Package\s*</td>\s*<td>\s*([0-9]{4}\s*/\s*[0-9]{2})\s*</td>\s*<td>\s*([0-9]+)\s*</td>\s*<td>\s*([0-9]{4}-[0-9]{2}-[0-9]{2}\s+[0-9]{2}:[0-9]{2}:[0-9]{2})\s*<td)",
        std::regex::ECMAScript);

    static const std::regex markdownRowPattern(
        R"(\|\s*ES\s*\|\s*LPPC Update-Package\s*\|\s*([0-9]{4}\s*/\s*[0-9]{2})\s*\|\s*([0-9]+)\s*\|\s*([0-9]{4}-[0-9]{2}-[0-9]{2}\s+[0-9]{2}:[0-9]{2}:[0-9]{2})\s*\|)",
        std::regex::ECMAScript);

    std::smatch match;
    if (!std::regex_search(pageText, match, htmlRowPattern) && !std::regex_search(pageText, match, markdownRowPattern)) {
        return std::nullopt;
    }

    PackageMetadata metadata;
    metadata.packageName = "LPPC Update-Package";
    metadata.airac = Trim(match[1].str());
    metadata.version = Trim(match[2].str());
    metadata.released = Trim(match[3].str());
    return metadata;
}

std::string BuildFileSuffix(const PackageMetadata& package) {
    std::string versionText = package.version;
    try {
        char buffer[16]{};
        std::snprintf(buffer, sizeof(buffer), "%04d", std::stoi(package.version));
        versionText = buffer;
    } catch (...) {}

    return RemoveSpacesAndSlashes(package.airac) + "-" + versionText;
}

bool MatchesPackageFile(const std::filesystem::path& file, const std::string& suffix) {
    if (file.extension() != ".ese" && file.extension() != ".sct") return false;
    const std::string name = file.filename().u8string();
    if (name.rfind("LPPC", 0) != 0) return false;
    return name.find(suffix) != std::string::npos;
}

std::filesystem::path TarExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    const UINT length = GetSystemDirectoryW(buffer, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        const std::filesystem::path candidate = std::filesystem::path(buffer) / L"tar.exe";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }

    wchar_t windir[MAX_PATH]{};
    const DWORD windirLength = GetEnvironmentVariableW(L"WINDIR", windir, MAX_PATH);
    if (windirLength > 0 && windirLength < MAX_PATH) {
        const std::filesystem::path candidate = std::filesystem::path(windir) / L"Sysnative" / L"tar.exe";
        std::error_code ec;
        if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
    }

    return L"tar.exe";
}

bool RunTar(const std::wstring& arguments, const std::filesystem::path& outputFile, DWORD timeoutMs, std::string& error) {
    const std::filesystem::path tar = TarExecutablePath();
    const std::wstring commandLine = L"\"" + tar.wstring() + L"\" " + arguments;

    SECURITY_ATTRIBUTES inheritable{};
    inheritable.nLength = sizeof(inheritable);
    inheritable.bInheritHandle = TRUE;

    HANDLE output = CreateFileW(outputFile.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &inheritable, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (output == INVALID_HANDLE_VALUE) {
        error = "A temporary file for the archive operation could not be created.";
        return false;
    }

    HANDLE nullInput = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &inheritable, OPEN_EXISTING,
                                   FILE_ATTRIBUTE_NORMAL, nullptr);
    if (nullInput == INVALID_HANDLE_VALUE) {
        CloseHandle(output);
        error = "A temporary input handle for the archive operation could not be created.";
        return false;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = output;
    startup.hStdError = output;
    startup.hStdInput = nullInput;

    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    const BOOL created = CreateProcessW(tar.c_str(), mutableCommand.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
                                        nullptr, nullptr, &startup, &process);
    CloseHandle(output);
    if (nullInput != INVALID_HANDLE_VALUE) CloseHandle(nullInput);

    if (!created) {
        error = "Windows tar could not be started. tar.exe ships with Windows 10 version 1803 or later.";
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(process.hProcess, timeoutMs);
    if (waitResult == WAIT_TIMEOUT) {
        TerminateProcess(process.hProcess, 1);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        error = "The archive operation timed out.";
        return false;
    }

    DWORD exitCode = 1;
    GetExitCodeProcess(process.hProcess, &exitCode);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);

    if (exitCode != 0) {
        error = "The archive operation failed (tar exit code " + std::to_string(exitCode) + ").";
        return false;
    }
    return true;
}

}  // namespace

std::string FetchUrl(const std::string& url) {
    return FetchUrlWithWinHttp(url);
}

std::optional<PackageMetadata> ParseUpdatePackage(const std::string& pageText) {
    return ParseUpdateRow(pageText);
}

PackageCheckResult CheckPackageFiles(const std::filesystem::path& packageRoot, const PackageMetadata& package) {
    PackageCheckResult result;
    result.packageRoot = packageRoot;
    result.expectedSuffix = BuildFileSuffix(package);

    std::error_code ec;
    if (!std::filesystem::exists(packageRoot, ec)) {
        result.issues.push_back("Package root does not exist: " + packageRoot.u8string());
        return result;
    }

    bool hasEse = false;
    bool hasSct = false;

    for (const auto& entry : std::filesystem::directory_iterator(packageRoot, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        if (!MatchesPackageFile(entry.path(), result.expectedSuffix)) continue;

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
        result.issues.push_back("Missing matching .ese and .sct files for suffix: " + result.expectedSuffix);
    }

    return result;
}

std::filesystem::path DefaultPackageRoot() {
    wchar_t appData[MAX_PATH]{};
    DWORD n = GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(appData) / "EuroScope";
}

std::filesystem::path ResolvePackageRoot() {
    std::error_code ec;

    wchar_t modulePath[MAX_PATH]{};
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ResolvePackageRoot), &module)) {
        const DWORD n = GetModuleFileNameW(module, modulePath, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) modulePath[0] = L'\0';
    }

    std::filesystem::path dir = std::filesystem::path(modulePath).parent_path();
    while (!dir.empty()) {
        if (std::filesystem::exists(dir / "LPPC ACS.prf", ec) || std::filesystem::exists(dir / "LPPC APS.prf", ec)) {
            return dir;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) break;
        dir = parent;
    }

    return DefaultPackageRoot();
}

std::filesystem::path DownloadsFolder() {
    static const GUID downloadsFolderId = {0x374DE290, 0x123F, 0x4565, {0x91, 0x64, 0x39, 0xC4, 0x92, 0x5E, 0x46, 0x7B}};
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(downloadsFolderId, KF_FLAG_DEFAULT, nullptr, &path)) && path != nullptr) {
        const std::filesystem::path folder(path);
        CoTaskMemFree(path);
        return folder;
    }
    if (path != nullptr) CoTaskMemFree(path);

    wchar_t profile[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return {};
    return std::filesystem::path(profile) / L"Downloads";
}

bool ValidateArchive(const std::filesystem::path& archiveFile, const std::string& expectedSuffix) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(archiveFile, ec)) return false;
    const auto size = std::filesystem::file_size(archiveFile, ec);
    if (ec || size == 0 || size > 512ull * 1024 * 1024) return false;

    const std::filesystem::path listingFile = std::filesystem::temp_directory_path(ec) / "LPPCUpdate_listing.txt";
    if (ec) return false;

    std::string error;
    if (!RunTar(L"-tf \"" + archiveFile.wstring() + L"\"", listingFile, 60000, error)) {
        std::filesystem::remove(listingFile, ec);
        return false;
    }

    bool hasEse = false;
    bool hasSct = false;
    std::ifstream input(listingFile);
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::filesystem::path entry(line);
        if (!MatchesPackageFile(entry, expectedSuffix)) continue;
        if (entry.extension() == ".ese") {
            hasEse = true;
        } else if (entry.extension() == ".sct") {
            hasSct = true;
        }
        if (hasEse && hasSct) break;
    }
    std::filesystem::remove(listingFile, ec);
    return hasEse && hasSct;
}

std::string ExtractArchive(const std::filesystem::path& archiveFile, const std::filesystem::path& destinationRoot) {
    std::error_code ec;
    if (!std::filesystem::is_directory(destinationRoot, ec)) {
        return "The target folder does not exist:\n" + destinationRoot.u8string();
    }

    const std::filesystem::path outputFile = std::filesystem::temp_directory_path(ec) / "LPPCUpdate_extract.txt";
    if (ec) return "A temporary file for the archive operation could not be created.";

    const std::wstring arguments = L"-xf \"" + archiveFile.wstring() + L"\" -C \"" + destinationRoot.wstring() + L"\"";
    std::string error;
    if (!RunTar(arguments, outputFile, 300000, error)) {
        std::filesystem::remove(outputFile, ec);
        return error;
    }
    std::filesystem::remove(outputFile, ec);
    return {};
}

bool WriteUpdateHandover(const std::filesystem::path& file, const UpdateHandover& handover) {
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output) return false;
    output << "root=" << handover.root.u8string() << "\n"
           << "archive=" << handover.archive.u8string() << "\n"
           << "euroscope=" << handover.euroscope.u8string() << "\n"
           << "airac=" << handover.airac << "\n"
           << "version=" << handover.version << "\n"
           << "released=" << handover.released << "\n";
    return output.good();
}

std::optional<UpdateHandover> ReadUpdateHandover(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) return std::nullopt;

    UpdateHandover handover;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const size_t split = line.find('=');
        if (split == std::string::npos) continue;
        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);
        if (key == "root") {
            handover.root = std::filesystem::path(Utf8ToWide(value));
        } else if (key == "archive") {
            handover.archive = std::filesystem::path(Utf8ToWide(value));
        } else if (key == "euroscope") {
            handover.euroscope = std::filesystem::path(Utf8ToWide(value));
        } else if (key == "airac") {
            handover.airac = value;
        } else if (key == "version") {
            handover.version = value;
        } else if (key == "released") {
            handover.released = value;
        }
    }

    if (handover.root.empty() || handover.archive.empty()) return std::nullopt;
    return handover;
}

std::string BuildUpdateArchiveBaseName(const PackageMetadata& package) {
    std::string stamp;
    for (const char ch : package.released) {
        if (ch == '-' || ch == ' ' || ch == ':') continue;
        stamp.push_back(ch);
    }
    return "LPPC-Update-Package_" + stamp + "-" + BuildFileSuffix(package);
}

std::filesystem::path FindUpdateArchive(const std::filesystem::path& directory, const std::string& baseName) {
    std::error_code ec;
    if (!std::filesystem::is_directory(directory, ec)) return {};

    const auto toLower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        return text;
    };
    const std::string zipName = toLower(baseName + ".zip");
    const std::string sevenName = toLower(baseName + ".7z");

    std::filesystem::directory_iterator it(directory, ec);
    while (!ec && it != std::filesystem::directory_iterator()) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            const std::string name = toLower(it->path().filename().u8string());
            if (name == zipName || name == sevenName) return it->path();
        }
        it.increment(ec);
    }
    return {};
}

void OpenUrlInBrowser(const std::string& url) {
    ShellExecuteW(nullptr, L"open", Utf8ToWide(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace filechecker