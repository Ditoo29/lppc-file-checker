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
std::optional<PackageMetadata> ParseInstallPackage(const std::string& pageText);
std::filesystem::path DefaultPackageRoot();
std::filesystem::path ResolvePackageRoot();
PackageCheckResult CheckPackageFiles(const std::filesystem::path& packageRoot, const PackageMetadata& package);

}  // namespace filechecker