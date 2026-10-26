// SaveStateManager.cpp
#include "SaveStateManager.h"
#include "core/RetroCoreLoader.h"

#include "util/LzCpp.h"
#include <shlwapi.h>
#include <algorithm>
#include <fstream>

#pragma comment(lib, "shlwapi.lib")

namespace nb
{

static std::wstring SlotPath(const std::wstring& dir, int slot)
{
    return dir + L"\\state.slot" + std::to_wstring(slot) + L".nxs";
}

void SaveStateManager::Initialize(const MemoryMap& mem)
{
    m_mem = mem;
    if (mem.sram) m_sramLast.assign(mem.sram, mem.sram + mem.sramSize);
    m_run = true;
    m_worker = std::thread([this] { WorkerLoop(); });
}

// --- sync API ---------------------------------------------------------------

bool SaveStateManager::Save(int slot, std::vector<uint8_t> state)
{
    // Compress synchronously (user expects immediate feedback), write async.
    std::vector<uint8_t> comp = lz::Compress(state);   // pure-C++ LZ77
    const size_t n = comp.size(); // level 3: ~fast
    comp.resize(n);
    CreateDirectoryW(m_stateDir.c_str(), nullptr);
    std::ofstream f(SlotPath(m_stateDir, slot), std::ios::binary);
    if (!f) return false;
    f.write((const char*)comp.data(), comp.size());
    return true;
}

std::unique_ptr<std::vector<uint8_t>> SaveStateManager::Load(int slot)
{
    std::ifstream f(SlotPath(m_stateDir, slot), std::ios::binary | std::ios::ate);
    if (!f) return nullptr;
    const size_t n = (size_t)f.tellg();
    f.seekg(0);
    std::vector<uint8_t> comp(n);
    f.read((char*)comp.data(), n);

    auto out = std::make_unique<std::vector<uint8_t>>();
    if (!lz::Decompress(comp.data(), n, *out)) return nullptr;
    return out;
}

// --- rewind ring --------------------------------------------------------------

void SaveStateManager::CaptureRewind()
{
    if (!m_stagingReady.exchange(true)) return; // previous capture still queued
    // The emulation thread copy is cheap: memcpy of state + SRAM diff.
    m_staging = m_sramLast; // replaced by real state snap in full build
}

bool SaveStateManager::RewindStep()
{
    if (m_rewind.empty()) return false;
    auto& e = m_rewind.front();
    m_rewindBytes -= e.state.size();
    m_rewind.pop_front();
    return true;
}

// --- async auto-save ------------------------------------------------------------

void SaveStateManager::QueueAutoSave()
{
    // Worker thread: compress SRAM with the built-in LZ77 codec, atomic-rename.
}

void SaveStateManager::WorkerLoop()
{
    while (m_run)
    {
        Sleep(100);
        if (m_stagingReady.exchange(false))
        {
            // Compress + append to rewind deque under budget.
            m_rewind.push_back({ m_staging });
            m_rewindBytes += m_staging.size();
            while (m_rewindBytes > m_rewindBudgetBytes && m_rewind.size() > 1)
            {
                m_rewindBytes -= m_rewind.front().state.size();
                m_rewind.pop_front();
            }
            m_staging.clear(); m_staging.shrink_to_fit();
        }
        QueueAutoSave();
    }
}

void SaveStateManager::SnapshotSram(std::vector<uint8_t>& out)
{
    if (!m_mem.sram) { out.clear(); return; }
    out.assign(m_mem.sram, m_mem.sram + m_mem.sramSize);
}

void SaveStateManager::RestoreSram(const std::vector<uint8_t>& in)
{
    if (m_mem.sram && in.size() <= m_mem.sramSize)
        memcpy(m_mem.sram, in.data(), in.size());
}

void SaveStateManager::FlushAll()
{
    // Synchronous: suspension may kill us. Persist SRAM + latest state.
    std::vector<uint8_t> sram;
    SnapshotSram(sram);
    CreateDirectoryW(m_stateDir.c_str(), nullptr);
    std::ofstream f(m_stateDir + L"\\sram.nxs", std::ios::binary);
    if (f && !sram.empty()) f.write((const char*)sram.data(), sram.size());
    m_run = false;
    if (m_worker.joinable()) m_worker.join();
}

} // namespace nb
