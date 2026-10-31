#include "platform/win/SystemResources.h"

#include <windows.h>

#include <algorithm>
#include <bit>
#include <vector>

namespace icd::platform {

    namespace {

        // Processors in all groups, narrowed by the affinity mask when the process is confined to one group.
        std::uint32_t affinityProcessors() {
            const DWORD all = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
            USHORT groupCount = 0;
            GetProcessGroupAffinity(GetCurrentProcess(), &groupCount, nullptr); // Asks for the group count.
            std::vector<USHORT> groups(std::max<USHORT>(groupCount, 1));
            groupCount = static_cast<USHORT>(groups.size());
            if (!GetProcessGroupAffinity(GetCurrentProcess(), &groupCount, groups.data()) || groupCount != 1) {
                return all; // Several groups (the Windows 11 default): the process may use every processor.
            }
            DWORD_PTR processMask = 0;
            DWORD_PTR systemMask = 0;
            if (!GetProcessAffinityMask(GetCurrentProcess(), &processMask, &systemMask)) {
                return all;
            }
            const auto allowed = static_cast<std::uint32_t>(std::popcount(processMask));
            return allowed == 0 ? all : std::min<std::uint32_t>(allowed, all);
        }

        // Applies a hard job-object CPU rate cap (e.g. a container limited to 25% of the machine).
        std::uint32_t applyJobCpuRate(std::uint32_t processors) {
            JOBOBJECT_CPU_RATE_CONTROL_INFORMATION rate{};
            if (!QueryInformationJobObject(nullptr, JobObjectCpuRateControlInformation, &rate, sizeof(rate), nullptr)) {
                return processors;
            }
            const DWORD flags = rate.ControlFlags;
            if ((flags & JOB_OBJECT_CPU_RATE_CONTROL_ENABLE) == 0 ||
                (flags & JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP) == 0) {
                return processors;
            }
            // CpuRate is in 1/100 of a percent of the whole machine.
            const auto limited = static_cast<std::uint32_t>(std::uint64_t{processors} * rate.CpuRate / 10000);
            return std::max<std::uint32_t>(limited, 1);
        }

        // Physical cores across all groups (one PROCESSOR_RELATIONSHIP record per core).
        std::uint32_t physicalCores() {
            DWORD bytes = 0;
            GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &bytes);
            if (bytes == 0) {
                return 1;
            }
            std::vector<std::byte> buffer(bytes);
            auto* first = reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data());
            if (!GetLogicalProcessorInformationEx(RelationProcessorCore, first, &bytes)) {
                return 1;
            }
            std::uint32_t cores = 0;
            for (DWORD offset = 0; offset < bytes;) {
                const auto* entry = reinterpret_cast<const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(&buffer[offset]);
                cores += entry->Relationship == RelationProcessorCore ? 1 : 0;
                offset += entry->Size;
            }
            return std::max<std::uint32_t>(cores, 1);
        }

        // Lowers total and available memory to a job-object memory limit, if one is set.
        void applyJobMemoryLimit(core::DetectedResources& resources) {
            JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
            if (!QueryInformationJobObject(nullptr, JobObjectExtendedLimitInformation, &limits, sizeof(limits),
                                           nullptr)) {
                return;
            }
            const DWORD flags = limits.BasicLimitInformation.LimitFlags;
            std::uint64_t limit = UINT64_MAX;
            if ((flags & JOB_OBJECT_LIMIT_JOB_MEMORY) != 0) {
                limit = std::min<std::uint64_t>(limit, limits.JobMemoryLimit);
            }
            if ((flags & JOB_OBJECT_LIMIT_PROCESS_MEMORY) != 0) {
                limit = std::min<std::uint64_t>(limit, limits.ProcessMemoryLimit);
            }
            resources.totalMemoryBytes = std::min(resources.totalMemoryBytes, limit);
            resources.availableMemoryBytes = std::min(resources.availableMemoryBytes, limit);
        }

    } // namespace

    core::DetectedResources detectSystemResources() {
        core::DetectedResources resources;
        resources.logicalProcessors = std::max<std::uint32_t>(applyJobCpuRate(affinityProcessors()), 1);
        resources.physicalCores = physicalCores();
        MEMORYSTATUSEX memory{};
        memory.dwLength = sizeof(memory);
        if (GlobalMemoryStatusEx(&memory)) {
            resources.totalMemoryBytes = memory.ullTotalPhys;
            resources.availableMemoryBytes = memory.ullAvailPhys;
        }
        applyJobMemoryLimit(resources);
        return resources;
    }

    LowMemorySignal::LowMemorySignal() : handle_(CreateMemoryResourceNotification(LowMemoryResourceNotification)) {}

    LowMemorySignal::~LowMemorySignal() {
        if (handle_ != nullptr) {
            CloseHandle(handle_);
        }
    }

    bool LowMemorySignal::signalled() const {
        BOOL low = FALSE;
        return handle_ != nullptr && QueryMemoryResourceNotification(handle_, &low) && low != FALSE;
    }

} // namespace icd::platform
