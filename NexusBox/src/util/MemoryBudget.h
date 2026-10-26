#pragma once
// MemoryBudget.h — Dev Mode RAM budget guard.
// Dev Mode grants a much larger commit than retail, but exceeding the budget
// still terminates the sandbox. We track our own usage and proactively shed
// rewind buffer / shader cache when pressure rises.
#include <winrt/Windows.System.h>
#include <cstdint>

namespace nb {

class MemoryBudget
{
public:
    static uint64_t Usage()  { return winrt::Windows::System::AppMemoryReport().TotalCommit(); }
    static uint64_t Budget() { return winrt::Windows::System::AppMemoryReport().PeakCommitment(); }

    static bool Pressure()   { return Usage() > Budget() * 85 / 100; }
};

} // namespace nb
