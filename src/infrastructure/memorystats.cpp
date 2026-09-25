#include "infrastructure/memorystats.h"

#include <algorithm>

#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <psapi.h>
#endif

namespace mbl::infrastructure {

MemorySample sampleMemory()
{
    MemorySample sample;
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS_EX process{};
    PERFORMANCE_INFORMATION system{};
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&process),
                             sizeof(process))
        && GetPerformanceInfo(&system, sizeof(system)) && GlobalMemoryStatusEx(&status)) {
        sample.available = true;
        sample.processPrivateBytes = process.PrivateUsage;
        sample.processWorkingSetBytes = process.WorkingSetSize;
        sample.systemCommitBytes = quint64(system.CommitTotal) * system.PageSize;
        sample.systemCommitLimitBytes = quint64(system.CommitLimit) * system.PageSize;
        sample.systemAvailablePhysicalBytes = status.ullAvailPhys;
    }
#endif
    return sample;
}

void MemoryPeaks::add(const MemorySample& sample)
{
    if (!sample.available)
        return;
    processPrivateBytes = std::max(processPrivateBytes, sample.processPrivateBytes);
    systemCommitBytes = std::max(systemCommitBytes, sample.systemCommitBytes);
    systemCommitLimitBytes = std::max(systemCommitLimitBytes, sample.systemCommitLimitBytes);
    minAvailablePhysicalBytes = samples == 0 ? sample.systemAvailablePhysicalBytes
                                             : std::min(minAvailablePhysicalBytes, sample.systemAvailablePhysicalBytes);
    ++samples;
}

} // namespace mbl::infrastructure
