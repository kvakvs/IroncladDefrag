#include "FileClassifier.h"

#include <algorithm>
#include <cwctype>
#include <sstream>

namespace icd {

namespace {
void Increment(count64_t& value)
{
    value += count64_t(1);
}

template <typename T>
void IncrementMap(std::map<T, count64_t>& counts, T key)
{
    counts[key] += count64_t(1);
}

std::wstring ToLower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring LowerExtension(const std::filesystem::path& path)
{
    return ToLower(path.extension().wstring());
}

bool ContainsPathHint(const std::filesystem::path& path, const std::vector<std::wstring>& hints)
{
    const std::wstring lowerPath = ToLower(path.wstring());
    for (const std::wstring& hint : hints) {
        if (!hint.empty() && lowerPath.find(ToLower(hint)) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

// Assigns the file to the size bucket used by data-drive placement defaults.
FileSizeClass ClassifySize(byte_count64_t size, const ClassificationRules& rules)
{
    const std::uint64_t bytes = size.getValue();
    if (bytes < rules.tinyFileThreshold.getValue()) {
        return FileSizeClass::Tiny;
    }
    if (bytes < rules.smallFileThreshold.getValue()) {
        return FileSizeClass::Small;
    }
    if (bytes < rules.mediumFileThreshold.getValue()) {
        return FileSizeClass::Medium;
    }
    if (bytes < rules.largeFileThreshold.getValue()) {
        return FileSizeClass::Large;
    }
    return FileSizeClass::Huge;
}

std::filesystem::file_time_type ChooseRecencyTime(const FileMetadata& file)
{
    if (file.GetLastAccessTime() != std::filesystem::file_time_type{}) {
        return file.GetLastAccessTime();
    }
    return file.GetModificationTime();
}

// Converts access or modification age into the hot-to-stale recency buckets.
FileTemperature ClassifyRecency(const FileMetadata& file, const ClassificationRules& rules)
{
    const auto timestamp = ChooseRecencyTime(file);
    if (timestamp == std::filesystem::file_time_type{}) {
        return FileTemperature::Unknown;
    }

    const auto now = std::filesystem::file_time_type::clock::now();
    if (timestamp >= now) {
        return FileTemperature::Hot;
    }

    const auto age = std::chrono::duration_cast<std::chrono::hours>(now - timestamp);
    if (age <= rules.hotRecency) {
        return FileTemperature::Hot;
    }
    if (age <= rules.warmRecency) {
        return FileTemperature::Warm;
    }
    if (age <= rules.coolRecency) {
        return FileTemperature::Cool;
    }
    if (age <= rules.coldRecency) {
        return FileTemperature::Cold;
    }
    return FileTemperature::Stale;
}

// Maps extensions and existing metadata into the broad type groups used by placement rules.
BroadFileType ClassifyBroadType(const FileMetadata& file)
{
    const std::wstring extension = LowerExtension(file.GetPath());
    if (extension == L".cpp" || extension == L".c" || extension == L".h" || extension == L".hpp" ||
        extension == L".cs" || extension == L".rs" || extension == L".py" || extension == L".js" ||
        extension == L".ts" || extension == L".sln" || extension == L".vcxproj" || extension == L".cmake") {
        return BroadFileType::SourceProject;
    }
    if (extension == L".vhd" || extension == L".vhdx" || extension == L".vmdk" || extension == L".qcow2") {
        return BroadFileType::VirtualDisk;
    }
    if (extension == L".bak" || extension == L".bkf" || extension == L".backup") {
        return BroadFileType::Backup;
    }
    if (extension == L".zip" || extension == L".7z" || extension == L".rar" || extension == L".tar" ||
        extension == L".gz" || extension == L".iso") {
        return BroadFileType::Archive;
    }
    if (extension == L".mp4" || extension == L".mkv" || extension == L".avi" || extension == L".mov" ||
        extension == L".mp3" || extension == L".flac" || extension == L".jpg" || extension == L".jpeg" ||
        extension == L".png" || extension == L".raw") {
        return BroadFileType::Media;
    }
    if (extension == L".exe" || extension == L".dll" || extension == L".sys" || extension == L".msi") {
        return BroadFileType::Executable;
    }
    if (extension == L".txt" || extension == L".pdf" || extension == L".doc" || extension == L".docx" ||
        extension == L".xls" || extension == L".xlsx" || extension == L".ppt" || extension == L".pptx") {
        return BroadFileType::Document;
    }

    switch (file.GetFileType()) {
    case FileMetadata::FileType::Media:
        return BroadFileType::Media;
    case FileMetadata::FileType::Archive:
        return BroadFileType::Archive;
    case FileMetadata::FileType::Document:
        return BroadFileType::Document;
    case FileMetadata::FileType::Executable:
        return BroadFileType::Executable;
    default:
        return BroadFileType::Other;
    }
}

FragmentationBenefit BenefitFromScore(double score)
{
    if (score <= 0.0) {
        return FragmentationBenefit::None;
    }
    if (score >= 24.0) {
        return FragmentationBenefit::High;
    }
    if (score >= 8.0) {
        return FragmentationBenefit::Medium;
    }
    return FragmentationBenefit::Low;
}

double ComputeFragmentationBenefitScore(const FileMetadata& file)
{
    if (!file.HasExtents()) {
        return -1.0;
    }

    const std::size_t fragmentCount = file.GetFragments().size();
    if (fragmentCount <= 1) {
        return 0.0;
    }

    const double sizeMiB = static_cast<double>(file.GetSize().getValue()) / (1024.0 * 1024.0);
    const double rawSizeWeight = sizeMiB / 128.0;
    const double sizeWeight = rawSizeWeight < 8.0 ? rawSizeWeight : 8.0;
    return static_cast<double>(fragmentCount - 1) * (1.0 + sizeWeight);
}

std::wstring BuildDirectoryReason(const FileMetadata& file, const ClassificationRules& rules, bool hotHint, bool coldHint)
{
    if (hotHint) {
        return L"hot directory hint";
    }
    if (coldHint) {
        return L"cold directory hint";
    }
    if (ContainsPathHint(file.GetPath(), rules.userHotDirectoryOverrides)) {
        return L"user hot directory override";
    }
    if (ContainsPathHint(file.GetPath(), rules.userColdDirectoryOverrides)) {
        return L"user cold directory override";
    }
    return {};
}

// Decides whether a file should be excluded or require explicit movement later.
std::wstring BuildExclusionReason(const FileMetadata& file, bool& excluded, bool& explicitOnly)
{
    const FileMetadata::AttributeFlags& flags = file.GetAttributeFlags();
    if (flags.system || flags.reparsePoint || flags.encrypted) {
        excluded = true;
        return L"system, reparse, or encrypted file";
    }
    if (flags.sparse || flags.compressed || file.IsRiskyOrUnmovable()) {
        explicitOnly = true;
        return L"risky file requires explicit movement";
    }
    if (!file.HasExtents()) {
        explicitOnly = true;
        return L"extent data unavailable";
    }
    return {};
}

// Chooses the default target zone without creating a move plan.
ExpectedPlacementZone ChoosePlacement(const FileClass& classification, bool hotHint, bool coldHint)
{
    if (classification.excluded || classification.moveOnlyWhenExplicit) {
        return ExpectedPlacementZone::None;
    }
    if (classification.broadType == BroadFileType::Media || classification.broadType == BroadFileType::VirtualDisk ||
        classification.sizeClass == FileSizeClass::Large || classification.sizeClass == FileSizeClass::Huge) {
        return ExpectedPlacementZone::LargeFile;
    }
    if (coldHint || classification.temperature == FileTemperature::Stale ||
        (classification.temperature == FileTemperature::Cold &&
         (classification.broadType == BroadFileType::Archive || classification.broadType == BroadFileType::Backup))) {
        return ExpectedPlacementZone::Slow;
    }
    if (hotHint || classification.temperature == FileTemperature::Hot ||
        classification.broadType == BroadFileType::SourceProject) {
        if (classification.sizeClass == FileSizeClass::Tiny || classification.sizeClass == FileSizeClass::Small ||
            classification.broadType == BroadFileType::SourceProject) {
            return ExpectedPlacementZone::Fast;
        }
    }
    return ExpectedPlacementZone::Balanced;
}

void AddToSummary(ClassificationSummary& summary, const FileClass& classification)
{
    IncrementMap(summary.sizeCounts, classification.sizeClass);
    IncrementMap(summary.typeCounts, classification.broadType);
    IncrementMap(summary.temperatureCounts, classification.temperature);
    IncrementMap(summary.placementCounts, classification.expectedPlacement);
    if (classification.excluded) {
        Increment(summary.excludedFiles);
    }
    if (classification.moveOnlyWhenExplicit) {
        Increment(summary.explicitOnlyFiles);
    }
    if (classification.fragmentationBenefit == FragmentationBenefit::Low ||
        classification.fragmentationBenefit == FragmentationBenefit::Medium ||
        classification.fragmentationBenefit == FragmentationBenefit::High) {
        Increment(summary.beneficialFragmentationFiles);
    }
    if (classification.fragmentationBenefit == FragmentationBenefit::High) {
        Increment(summary.highBenefitFragmentationFiles);
    }
}
} // namespace

// Translates current optimization defaults into deterministic classification thresholds.
ClassificationRules FileClassifier::BuildRules(const OptimizationSettings& settings) const
{
    ClassificationRules rules;
    rules.smallFileThreshold = settings.smallFileThreshold;
    rules.mediumFileThreshold = settings.largeFileThreshold;
    rules.largeFileThreshold = settings.hugeFileThreshold;
    rules.hotRecency = settings.hotRecency;
    rules.warmRecency = settings.warmRecency;
    rules.coolRecency = settings.coolRecency;
    rules.coldRecency = settings.coldRecency;
    for (const DirectoryOverrideRule& rule : settings.directoryOverrides) {
        if (rule.placement == ExpectedPlacementZone::Fast) {
            rules.userHotDirectoryOverrides.push_back(rule.path.wstring());
        } else if (rule.placement == ExpectedPlacementZone::Slow || rule.placement == ExpectedPlacementZone::LargeFile) {
            rules.userColdDirectoryOverrides.push_back(rule.path.wstring());
        }
    }
    return rules;
}

// Produces per-file classifications and aggregate summaries for one analysis snapshot.
AnalysisResult FileClassifier::Classify(AnalysisResult result, const OptimizationSettings& settings) const
{
    const ClassificationRules rules = BuildRules(settings);
    result.classifications.clear();
    result.classificationSummary = ClassificationSummary();
    result.classifications.reserve(result.files.size());

    for (std::size_t index = 0; index < result.files.size(); ++index) {
        const FileMetadata& file = result.files[index];
        const bool hotHint = ContainsPathHint(file.GetPath(), rules.hotDirectoryHints) ||
                             ContainsPathHint(file.GetPath(), rules.userHotDirectoryOverrides);
        const bool coldHint = ContainsPathHint(file.GetPath(), rules.coldDirectoryHints) ||
                              ContainsPathHint(file.GetPath(), rules.userColdDirectoryOverrides);

        FileClass classification;
        classification.sizeClass = ClassifySize(file.GetSize(), rules);
        classification.temperature = ClassifyRecency(file, rules);
        classification.type = file.GetFileType();
        classification.broadType = ClassifyBroadType(file);
        classification.directoryRuleReason = BuildDirectoryReason(file, rules, hotHint, coldHint);

        classification.exclusionReason =
            BuildExclusionReason(file, classification.excluded, classification.moveOnlyWhenExplicit);

        classification.fragmentationBenefitScore = ComputeFragmentationBenefitScore(file);
        classification.fragmentationBenefit = classification.fragmentationBenefitScore < 0.0
                                                   ? FragmentationBenefit::Unknown
                                                   : BenefitFromScore(classification.fragmentationBenefitScore);
        classification.expectedPlacement = ChoosePlacement(classification, hotHint, coldHint);

        AddToSummary(result.classificationSummary, classification);
        result.classifications.push_back({index, std::move(classification)});
    }

    return result;
}

} // namespace icd
