// Profiler.cpp
#include "Profiler.h"
#include <winrt/Windows.System.h>
#include <profileapi.h>
#include <synchapi.h>

namespace nb {

double Profiler::NowUs()
{
    LARGE_INTEGER f{}, now{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&now);
    return (double)now.QuadPart * 1'000'000.0 / (double)f.QuadPart;
}

double Profiler::MemMB()
{
    // UWP AppMemoryReport: usage vs the Dev Mode budget. Series X|S Dev Mode
    // builds get far more RAM than retail; we still guard against termination.
    auto report = winrt::Windows::System::AppMemoryReport();
    const uint64_t usage = report.TotalCommit();
    const uint64_t limit = report.PeakCommitment();
    (void)limit;
    return (double)usage / (1024.0 * 1024.0);
}

} // namespace nb
