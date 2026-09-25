// Process and system memory figures for diagnostics. Windows only; other
// platforms report zeros (`available` is false).
#pragma once

#include <QtGlobal>

namespace mbl::infrastructure {

struct MemorySample {
    bool available = false;
    quint64 processPrivateBytes = 0;   // Commit charged to this process.
    quint64 processWorkingSetBytes = 0;
    quint64 systemCommitBytes = 0;     // System-wide commit charge.
    quint64 systemCommitLimitBytes = 0;
    quint64 systemAvailablePhysicalBytes = 0;
};

MemorySample sampleMemory();

// Tracks peaks across samples.
struct MemoryPeaks {
    quint64 processPrivateBytes = 0;
    quint64 systemCommitBytes = 0;
    quint64 systemCommitLimitBytes = 0;
    quint64 minAvailablePhysicalBytes = 0;
    int samples = 0;

    void add(const MemorySample& sample);
};

} // namespace mbl::infrastructure
