#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace filechecker {

struct PackageMetadata {
    std::string packageName;
    std::string airac;
    std::string version;
    std::string released;
};

struct PackageCheckResult {
    std::filesystem::path packageRoot;
    std::string expectedSuffix;
    std::vector<std::filesystem::path> matchingFiles;
    std::vector<std::string> issues;
    bool success = false;
};

std::string FetchUrl(const std::string& url);
std::optional<PackageMetadata> ParseUpdatePackage(const std::string& pageText);
std::filesystem::path DefaultPackageRoot();
std::filesystem::path ResolvePackageRoot();
PackageCheckResult CheckPackageFiles(const std::filesystem::path& packageRoot, const PackageMetadata& package);

std::filesystem::path DownloadsFolder();
bool ValidateArchive(const std::filesystem::path& archiveFile, const std::string& expectedSuffix);
std::string ExtractArchive(const std::filesystem::path& archiveFile, const std::filesystem::path& destinationRoot);
std::string BuildUpdateArchiveBaseName(const PackageMetadata& package);
std::filesystem::path FindUpdateArchive(const std::filesystem::path& directory, const std::string& baseName);
void OpenUrlInBrowser(const std::string& url);

struct UpdateHandover {
    std::filesystem::path root;
    std::filesystem::path archive;
    std::filesystem::path euroscope;
    std::string airac;
    std::string version;
    std::string released;
};

bool WriteUpdateHandover(const std::filesystem::path& file, const UpdateHandover& handover);
std::optional<UpdateHandover> ReadUpdateHandover(const std::filesystem::path& file);

}  // namespace filechecker