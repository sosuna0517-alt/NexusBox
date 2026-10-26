#pragma once
// LzCpp.h — pure C++20 LZ77 codec for save states.
// No external compression library: a simple byte-oriented LZ77 with a
// 64 KiB sliding window, tuned for fast compression of emulator state blocks
// (typically 1-4 MB of SRAM/video/CPU state). Ratios land ~2-3x on typical
// console state payloads; zstd-class ratios were intentionally traded away
// so the whole build depends on nothing but C++ and the Windows SDK.
//
// Format (little-endian, self-contained, version-tagged):
//   magic  "NZB1"          4 bytes
//   u32    originalSize    uncompressed length
//   then sequences until done:
//     varint  litLen        number of literal bytes that follow
//     litLen  literals
//     varint  matchLen-4+1  (>=1: match; 0: end-of-stream marker)
//     u16     offset        1..65535, back-reference distance
#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

namespace nb {
namespace lz {

std::vector<uint8_t> Compress(const uint8_t* data, size_t size);
inline std::vector<uint8_t> Compress(const std::vector<uint8_t>& v)
{ return Compress(v.data(), v.size()); }

bool Decompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out);
inline bool Decompress(const std::vector<uint8_t>& v, std::vector<uint8_t>& out)
{ return Decompress(v.data(), v.size(), out); }

// Convenience wrappers used by SaveStateManager.
std::vector<uint8_t> CompressBytes(const void* src, size_t n);
bool DecompressBytes(const void* src, size_t n, std::vector<uint8_t>& out);

} // namespace lz
} // namespace nb
