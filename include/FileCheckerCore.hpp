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
    std::string expectedStem;
    std::vector<std::filesystem::path> matchingFiles;
    std::vector<std::string> issues;
    bool success = false;
};

std::string FetchUrl(const std::string& url);
std::optional<PackageMetadata> ParseLatestUpdatePackage(const std::string& pageText);
std::filesystem::path DefaultPackageRoot();
std::string BuildExpectedStem(const PackageMetadata& package);
PackageCheckResult CheckPackageFiles(const std::filesystem::path& packageRoot, const PackageMetadata& package);
std::string BuildReportText(const PackageCheckResult& result, const PackageMetadata& package);

}  // namespace filechecker