#pragma once
// SaveStateManager.h — save states with the built-in LZ77 codec, async auto-save, rewind ring buffer.
// All heavy work (compression, disk IO) runs on a worker pool: the emulation
// thread only memcpy-snaps state into a staging arena (lock-free handoff).
#include <cstdint>
#include <memory>
#include <atomic>
#include <thread>
#include <vector>
#include <deque>

#include <windows.h>
#include <string>

#include "core/RetroCoreLoader.h"   // MemoryMap (complete type for the member)

namespace nb {

class SaveStateManager
{
public:
    void Initialize(const MemoryMap& mem);

    bool Save(int slot, std::vector<uint8_t> state);       // synchronous, user-invoked
    std::unique_ptr<std::vector<uint8_t>> Load(int slot);  // returns null if absent

    void CaptureRewind();               // emulation thread: snap current state
    bool RewindStep();                  // pops the newest rewind frame (deserialize)
    void SetRewindBudgetMB(size_t mb)   { m_rewindBudgetBytes = mb << 20; }

    void FlushAll();                    // synchronous SRAM + pending auto-save
    void SetAutoSaveIntervalSec(float s){ m_autoSaveInterval = s; }

private:
    void WorkerLoop();
    void QueueAutoSave();
    void SnapshotSram(std::vector<uint8_t>& out);
    void RestoreSram(const std::vector<uint8_t>& in);

    struct MemoryMap m_mem{};
    std::vector<uint8_t> m_sramLast;              // dirty-detection baseline

    struct RewindEntry { std::vector<uint8_t> state; };
    std::deque<RewindEntry> m_rewind;             // newest back? newest front
    size_t m_rewindBudgetBytes = 64u << 20;       // default 64MB ring
    size_t m_rewindBytes = 0;

    std::vector<uint8_t> m_staging;               // emulation thread writes here
    std::atomic<bool>    m_stagingReady{ false };

    std::thread       m_worker;
    std::atomic<bool> m_run{ false };
    float             m_autoSaveInterval = 30.0f;
    std::wstring      m_stateDir;
};

} // namespace nb
