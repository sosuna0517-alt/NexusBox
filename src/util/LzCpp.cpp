// LzCpp.cpp — LZ77 codec (pure C++20, no third-party libraries).
#include "LzCpp.h"

#include <array>
#include <cstring>

namespace nb {
namespace lz {

namespace {

constexpr uint32_t kWindow = 65536;     // u16 offsets
constexpr uint32_t kMinMatch = 4;
constexpr uint32_t kMaxMatch = 255 + kMinMatch;  // varint lengths allow more; cap for simplicity

// Hash chain over 4-byte sequences.
constexpr int kHashBits = 16;
constexpr int kHashSize = 1 << kHashBits;
constexpr uint32_t kHashMul = 2654435761u;

inline uint32_t Hash4(const uint8_t* p)
{
    uint32_t v;
    std::memcpy(&v, p, 4);
    return (v * kHashMul) >> (32 - kHashBits);
}

void PutVarint(std::vector<uint8_t>& out, uint64_t v)
{
    while (v >= 0x80) { out.push_back((uint8_t)(v | 0x80)); v >>= 7; }
    out.push_back((uint8_t)v);
}

const uint8_t* GetVarint(const uint8_t* p, const uint8_t* end, uint64_t& v)
{
    v = 0; int shift = 0;
    while (p < end)
    {
        uint8_t b = *p++;
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) return p;
        shift += 7;
    }
    return nullptr;   // truncated
}

} // namespace

std::vector<uint8_t> Compress(const uint8_t* data, size_t size)
{
    std::vector<uint8_t> out;
    out.reserve(size + size / 8 + 16);
    out.insert(out.end(), {'N','Z','B','1'});

    const uint32_t orig = (uint32_t)size;
    out.push_back((uint8_t)(orig & 0xFF));
    out.push_back((uint8_t)((orig >> 8) & 0xFF));
    out.push_back((uint8_t)((orig >> 16) & 0xFF));
    out.push_back((uint8_t)((orig >> 24) & 0xFF));

    std::vector<int32_t> head(kHashSize, -1);
    std::vector<int32_t> prev(size > 0 ? size : 1, -1);

    size_t i = 0;
    std::vector<uint8_t> literals;
    auto flushLiterals = [&]()
    {
        PutVarint(out, literals.size());
        out.insert(out.end(), literals.begin(), literals.end());
        literals.clear();
    };

    while (i < size)
    {
        // Find the best match within the window.
        size_t bestLen = 0, bestOff = 0;
        if (i + kMinMatch <= size)
        {
            const uint32_t h = Hash4(data + i);
            int32_t j = head[h];
            const size_t limit = (i > kWindow) ? i - kWindow : 0;
            while (j >= (int32_t)limit)
            {
                const size_t pos = (size_t)j;
                size_t len = 0;
                while (i + len < size && data[pos + len] == data[i + len] && len < kMaxMatch)
                    ++len;
                if (len >= kMinMatch && len > bestLen)
                {
                    bestLen = len;
                    bestOff = i - pos;
                    if (len >= kMaxMatch) break;
                }
                j = prev[pos];
            }
        }

        if (bestLen >= kMinMatch)
        {
            flushLiterals();
            PutVarint(out, bestLen - kMinMatch + 1);      // matchLen-4+1 (1..: match; 0: end marker)
            out.push_back((uint8_t)(bestOff & 0xFF));      // u16 LE offset
            out.push_back((uint8_t)((bestOff >> 8) & 0xFF));
            // Insert hashes for the covered range (bounded work).
            for (size_t k = 0; k < bestLen && i + k + kMinMatch <= size; ++k)
            {
                const uint32_t hh = Hash4(data + i + k);
                prev[i + k] = head[hh];
                head[hh] = (int32_t)(i + k);
            }
            i += bestLen;
        }
        else
        {
            literals.push_back(data[i]);
            if (i + kMinMatch <= size)
            {
                const uint32_t hh = Hash4(data + i);
                prev[i] = head[hh];
                head[hh] = (int32_t)i;
            }
            ++i;
        }
    }

    flushLiterals();
    PutVarint(out, 0);   // end-of-stream marker: matchLen-4 == 0
    return out;
}

bool Decompress(const uint8_t* data, size_t size, std::vector<uint8_t>& out)
{
    out.clear();
    if (size < 8 || std::memcmp(data, "NZB1", 4) != 0) return false;

    uint64_t orig = 0;
    std::memcpy(&orig, data + 4, 4);
    out.resize((size_t)orig);

    const uint8_t* p = data + 8;
    const uint8_t* end = data + size;
    size_t o = 0;

    while (true)
    {
        uint64_t litLen = 0;
        p = GetVarint(p, end, litLen);
        if (!p) return false;
        if ((size_t)litLen > out.size() - o) return false;   // overflow guard
        std::memcpy(out.data() + o, p, (size_t)litLen);
        p += litLen;
        o += litLen;

        uint64_t matchLenM4 = 0;
        p = GetVarint(p, end, matchLenM4);
        if (!p) return false;
        if (matchLenM4 == 0) break;                          // end marker

        if (end - p < 2) return false;
        const uint16_t off = (uint16_t)(p[0] | (p[1] << 8));
        p += 2;
        if (off == 0 || off > o) return false;               // invalid back-ref

        const size_t len = (size_t)(matchLenM4 - 1) + kMinMatch;
        if (len > out.size() - o) return false;
        // Byte-wise copy (overlapping matches allowed, LZ77 semantics).
        for (size_t k = 0; k < len; ++k)
            out[o + k] = out[o - off + k];
        o += len;
    }

    return o == out.size();
}

std::vector<uint8_t> CompressBytes(const void* src, size_t n)
{ return Compress(static_cast<const uint8_t*>(src), n); }

bool DecompressBytes(const void* src, size_t n, std::vector<uint8_t>& out)
{ return Decompress(static_cast<const uint8_t*>(src), n, out); }

} // namespace lz
} // namespace nb
