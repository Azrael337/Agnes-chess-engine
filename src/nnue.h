#pragma once

// NNUE inference for the Stockfish 17 "big net" architecture, i.e. the layout of
// official-stockfish/networks nn-1111cefa1111.nnue and of any net trained with
// nnue-pytorch to the same specification:
//
//     features        HalfKAv2_hm   (22528 inputs = 32 king buckets * 11 planes * 64 squares)
//     feature xform   L1 = 3072 (int16 accumulators, pairwise-product output), 8 PSQT buckets
//     layer stacks    8 buckets selected by (piece count - 1) / 4
//                     fc_0: 3072 -> 16   (15 outputs + 1 skip-connection output)
//                     sqr-clipped-relu(15) ++ clipped-relu(15)  -> 30 (padded to 32)
//                     fc_1: 32 -> 32 , clipped-relu
//                     fc_2: 32 -> 1  (+ scaled skip output of fc_0)
//
// The arithmetic reproduces Stockfish 17's integer inference exactly (see
// nnue_feature_transformer.h / nnue_architecture.h / layers/*.h in SF17):
//   * on load, FT biases and weights are multiplied by 2
//   * FT output = clamp(a,0,254) * clamp(b,0,254) / 512, pairwise over each half
//   * squared activation  = min(127, (x*x) >> 19), clipped relu = clamp(x >> 6, 0, 127)
//   * skip = fc_0_out[15] * (600*16) / (127*64)
//   * result = psqt/16 + positional/16, from the side to move's point of view
//
// To change the architecture (a net trained with other L1/L2/L3), edit the
// constants right below; the SIMD kernels need L1 % 128 == 0, (L2+1) % 8 == 0.

#include "chess.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define NNUE_RESTRICT __restrict__
#else
#define NNUE_RESTRICT
#endif

namespace nnue {

// ---------------------------------------------------------------------
// Architecture
// ---------------------------------------------------------------------
constexpr int L1      = 3072;
constexpr int L2      = 15;          // fc_0 has L2 + 1 outputs, the last one is the skip connection
constexpr int FC0_OUT = L2 + 1;      // 16
constexpr int L3      = 32;
constexpr int FC1_REAL_IN = 2 * L2;                      // 30: sqr(15) ++ crelu(15)
constexpr int FC1_IN      = ((FC1_REAL_IN + 31) / 32) * 32;  // 32 (padded on disk)
constexpr int PSQT_BUCKETS = 8;
constexpr int LS_BUCKETS   = 8;

constexpr int NUM_SQ            = 64;
constexpr int NUM_KING_BUCKETS  = 32;
constexpr int NUM_EXPORT_PLANES = 11;                           // 10 piece planes + 1 shared king plane
constexpr int PS_NB             = NUM_EXPORT_PLANES * NUM_SQ;   // 704
constexpr int NUM_REAL_FEATURES = PS_NB * NUM_KING_BUCKETS;     // 22528

constexpr uint32_t FILE_VERSION = 0x7AF32F20u;
constexpr uint32_t FEATURE_HASH = 0x7F234CB8u;   // HalfKAv2_hm

constexpr int OUTPUT_SCALE      = 16;
constexpr int WEIGHT_SCALE_BITS = 6;
constexpr int FT_MAX_VAL        = 127 * 2;   // 254
constexpr int FT_WEIGHT_MUL     = 2;         // applied to FT biases/weights on load
constexpr int NNUE2SCORE        = 600;

static_assert(L1 % 128 == 0, "SIMD kernels need L1 % 128 == 0");
static_assert(FC0_OUT % 8 == 0, "SIMD fc_0 kernel needs (L2+1) % 8 == 0");
static_assert((FC0_OUT / 8) % 8 == 0 || (FC0_OUT / 8) % 8 == 1 || (FC0_OUT / 8) % 8 == 2 ||
              (FC0_OUT / 8) % 8 == 4, "unsupported fc_0 width (blocks of 8/4/2/1 ymm)");
static_assert(FC1_IN % 32 == 0, "fc_1 input must be a multiple of 32");

constexpr uint32_t compute_fc_hash() {
    uint32_t prev = 0xEC42E90Du ^ (uint32_t)(L1 * 2);
    const int outs[3] = {FC0_OUT, L3, 1};
    uint32_t h = 0;
    for (int li = 0; li < 3; ++li) {
        h = 0xCC03DAE4u + (uint32_t)outs[li];
        h ^= (prev >> 1);
        h ^= (uint32_t)(prev << 31);
        if (outs[li] != 1) h += 0x538D24C7u;
        prev = h;
    }
    return h;
}
constexpr uint32_t FC_HASH = compute_fc_hash();

// KingBuckets indexed by the *oriented* king square; -1 where an oriented king
// can never be (files a-d after the horizontal mirror).
// clang-format off
constexpr int KingBucketOriented[NUM_SQ] = {
    -1, -1, -1, -1, 31, 30, 29, 28,
    -1, -1, -1, -1, 27, 26, 25, 24,
    -1, -1, -1, -1, 23, 22, 21, 20,
    -1, -1, -1, -1, 19, 18, 17, 16,
    -1, -1, -1, -1, 15, 14, 13, 12,
    -1, -1, -1, -1, 11, 10,  9,  8,
    -1, -1, -1, -1,  7,  6,  5,  4,
    -1, -1, -1, -1,  3,  2,  1,  0,
};
// clang-format on

// ---------------------------------------------------------------------
// File reading helpers (little-endian files)
// ---------------------------------------------------------------------
namespace detail {

inline bool read_bytes(std::ifstream& f, void* dst, size_t n) {
    f.read(reinterpret_cast<char*>(dst), (std::streamsize)n);
    return (bool)f;
}

inline bool read_u32(std::ifstream& f, uint32_t& out) {
    uint8_t b[4];
    if (!read_bytes(f, b, 4)) return false;
    out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return true;
}

inline bool peek_leb128_magic(std::ifstream& f) {
    static constexpr char magic[] = "COMPRESSED_LEB128";
    constexpr size_t magicLen = sizeof(magic) - 1;
    std::streampos pos = f.tellg();
    char buf[magicLen];
    f.read(buf, (std::streamsize)magicLen);
    bool ok = (f.gcount() == (std::streamsize)magicLen) && std::memcmp(buf, magic, magicLen) == 0;
    f.clear();
    f.seekg(pos);
    return ok;
}

// Reads `count` values of `width` bytes each (2 or 4), either raw or
// COMPRESSED_LEB128, straight into `out` (no temporary 64-bit buffer: the FT
// weights are ~69M values).
template <typename T>
inline bool read_tensor(std::ifstream& f, std::vector<T>& out, size_t count, int width, std::string& err) {
    out.resize(count);
    if (peek_leb128_magic(f)) {
        constexpr size_t magicLen = sizeof("COMPRESSED_LEB128") - 1;
        char magic[magicLen];
        if (!read_bytes(f, magic, magicLen)) return false;
        uint32_t byteLen;
        if (!read_u32(f, byteLen)) return false;
        std::vector<uint8_t> buf(byteLen);
        if (byteLen && !read_bytes(f, buf.data(), byteLen)) return false;

        size_t pos = 0;
        for (size_t i = 0; i < count; ++i) {
            uint64_t result = 0;
            int shift = 0;
            uint8_t byte;
            do {
                if (pos >= buf.size()) { err = "LEB128 stream ended early"; return false; }
                byte = buf[pos++];
                result |= (uint64_t)(byte & 0x7F) << shift;
                shift += 7;
            } while ((byte & 0x80) && shift < 64);
            int64_t v = (int64_t)result;
            if (shift < 64 && (byte & 0x40)) v |= -((int64_t)1 << shift);
            out[i] = (T)v;
        }
        if (pos != buf.size()) { err = "LEB128 stream longer than expected"; return false; }
        return true;
    }
    for (size_t i = 0; i < count; ++i) {
        if (width == 2) {
            uint8_t b[2];
            if (!read_bytes(f, b, 2)) return false;
            out[i] = (T)(int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
        } else {
            uint32_t u;
            if (!read_u32(f, u)) return false;
            out[i] = (T)(int32_t)u;
        }
    }
    return true;
}

// ----- SIMD kernels --------------------------------------------------------

// Feature-transformer output: for both perspectives (side to move first),
// out[i] = clamp(a[i],0,254) * clamp(a[i+half],0,254) / 512.
inline void ft_output(const int16_t* stmAcc, const int16_t* oppAcc, uint8_t* out) {
    constexpr int half = L1 / 2;
    const int16_t* accs[2] = {stmAcc, oppAcc};
#if defined(__AVX2__)
    const __m256i zero = _mm256_setzero_si256();
    const __m256i maxv = _mm256_set1_epi16(FT_MAX_VAL);
    for (int p = 0; p < 2; ++p) {
        const int16_t* a = accs[p];
        uint8_t* o = out + half * p;
        for (int i = 0; i < half; i += 32) {
            __m256i a0 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(a + i)), zero), maxv);
            __m256i a1 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(a + i + 16)), zero), maxv);
            __m256i b0 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(a + i + half)), zero), maxv);
            __m256i b1 = _mm256_min_epi16(_mm256_max_epi16(_mm256_load_si256((const __m256i*)(a + i + half + 16)), zero), maxv);
            // (x << 7) * y >> 16  ==  x * y >> 9  ==  x * y / 512 for x,y >= 0 ; max 126 so packus never saturates
            __m256i p0 = _mm256_mulhi_epi16(_mm256_slli_epi16(a0, 7), b0);
            __m256i p1 = _mm256_mulhi_epi16(_mm256_slli_epi16(a1, 7), b1);
            _mm256_store_si256((__m256i*)(o + i),
                               _mm256_permute4x64_epi64(_mm256_packus_epi16(p0, p1), 0xD8));
        }
    }
#else
    for (int p = 0; p < 2; ++p) {
        const int16_t* a = accs[p];
        uint8_t* o = out + half * p;
        for (int i = 0; i < half; ++i) {
            int32_t v0 = std::clamp<int32_t>(a[i], 0, FT_MAX_VAL);
            int32_t v1 = std::clamp<int32_t>(a[i + half], 0, FT_MAX_VAL);
            o[i] = (uint8_t)((v0 * v1) / 512);
        }
    }
#endif
}

#if defined(__AVX2__)
template <int NG>
inline void fc0_block(const uint32_t* in32, const uint16_t* nz, int nnz, const int8_t* W,
                      size_t chunkStride, const int32_t* bias, int32_t* out) {
    const __m256i ones = _mm256_set1_epi16(1);
    __m256i r[NG];
    for (int g = 0; g < NG; ++g) r[g] = _mm256_loadu_si256((const __m256i*)(bias + g * 8));
    for (int n = 0; n < nnz; ++n) {
        const int c = nz[n];
        const __m256i x = _mm256_set1_epi32((int)in32[c]);
        const int8_t* w = W + (size_t)c * chunkStride;
        for (int g = 0; g < NG; ++g)
            r[g] = _mm256_add_epi32(
                r[g], _mm256_madd_epi16(
                          _mm256_maddubs_epi16(x, _mm256_loadu_si256((const __m256i*)(w + g * 32))), ones));
    }
    for (int g = 0; g < NG; ++g) _mm256_storeu_si256((__m256i*)(out + g * 8), r[g]);
}

inline int hsum_epi32(__m256i s) {
    __m128i t = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0x4E));
    t = _mm_add_epi32(t, _mm_shuffle_epi32(t, 0xB1));
    return _mm_cvtsi128_si32(t);
}
#endif

// fc_0: L1 -> FC0_OUT, skipping all-zero groups of 4 input bytes. `P` is the weight
// matrix repacked as [L1/4 groups][FC0_OUT outputs][4 inputs].
inline void fc0_sparse(const uint8_t* ftOut, const int8_t* P, const int32_t* bias, int32_t* out) {
    constexpr int NCHUNK = L1 / 4;
    uint32_t in32[NCHUNK];
    std::memcpy(in32, ftOut, sizeof(in32));
    uint16_t nz[NCHUNK];
    int nnz = 0;
#if defined(__AVX2__)
    {
        const __m256i zero = _mm256_setzero_si256();
        for (int c = 0; c < NCHUNK; c += 8) {
            __m256i v = _mm256_loadu_si256((const __m256i*)(in32 + c));
            unsigned m = (~(unsigned)_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpeq_epi32(v, zero)))) & 0xFFu;
            while (m) {
                nz[nnz++] = (uint16_t)(c + __builtin_ctz(m));
                m &= m - 1;
            }
        }
    }
    constexpr int G = FC0_OUT / 8;
    for (int b0 = 0; b0 < G; b0 += 8) {
        const int ng = std::min(8, G - b0);
        const int8_t* W = P + (size_t)b0 * 32;
        switch (ng) {
            case 8: fc0_block<8>(in32, nz, nnz, W, FC0_OUT * 4, bias + b0 * 8, out + b0 * 8); break;
            case 4: fc0_block<4>(in32, nz, nnz, W, FC0_OUT * 4, bias + b0 * 8, out + b0 * 8); break;
            case 2: fc0_block<2>(in32, nz, nnz, W, FC0_OUT * 4, bias + b0 * 8, out + b0 * 8); break;
            default: fc0_block<1>(in32, nz, nnz, W, FC0_OUT * 4, bias + b0 * 8, out + b0 * 8); break;
        }
    }
#else
    for (int c = 0; c < NCHUNK; ++c)
        if (in32[c]) nz[nnz++] = (uint16_t)c;
    for (int j = 0; j < FC0_OUT; ++j) out[j] = bias[j];
    for (int n = 0; n < nnz; ++n) {
        const int c = nz[n];
        const uint8_t* x = ftOut + c * 4;
        const int8_t* w = P + (size_t)c * (FC0_OUT * 4);
        for (int j = 0; j < FC0_OUT; ++j)
            out[j] += w[j * 4 + 0] * x[0] + w[j * 4 + 1] * x[1] + w[j * 4 + 2] * x[2] + w[j * 4 + 3] * x[3];
    }
#endif
}

}  // namespace detail

// ---------------------------------------------------------------------
// Accumulator (both perspectives)
// ---------------------------------------------------------------------
struct PerspectiveAccumulator {
    alignas(32) std::array<int16_t, L1> acc{};
    std::array<int32_t, PSQT_BUCKETS> psqt{};
};

struct Accumulator {
    PerspectiveAccumulator side[2];   // indexed by chess::Color (WHITE=0, BLACK=1)
};

// ---------------------------------------------------------------------
// One quantized fully-connected layer: int32 biases, int8 weights stored
// [out][padded_in] with the input dimension padded to a multiple of 32.
// ---------------------------------------------------------------------
struct FCLayer {
    int in_features = 0;
    int out_features = 0;
    int padded_in = 0;
    std::vector<int32_t> bias;
    std::vector<int8_t> weight;

    bool load(std::ifstream& f, int inF, int outF) {
        in_features = inF;
        out_features = outF;
        padded_in = ((inF + 31) / 32) * 32;
        bias.resize(out_features);
        for (int i = 0; i < out_features; ++i) {
            uint32_t u;
            if (!detail::read_u32(f, u)) return false;
            bias[i] = (int32_t)u;
        }
        weight.resize((size_t)out_features * padded_in);
        return detail::read_bytes(f, weight.data(), weight.size());
    }
};

// ---------------------------------------------------------------------
// The network
// ---------------------------------------------------------------------
struct Network {
    std::vector<int16_t> ftBias;          // [L1]                       (already *2)
    std::vector<int16_t> ftWeight;        // [NUM_REAL_FEATURES][L1]    (already *2)
    std::vector<int32_t> ftPsqtWeight;    // [NUM_REAL_FEATURES][PSQT_BUCKETS]

    FCLayer fc0[LS_BUCKETS];   // L1 -> FC0_OUT
    FCLayer fc1[LS_BUCKETS];   // FC1_REAL_IN (padded to FC1_IN) -> L3
    FCLayer fc2[LS_BUCKETS];   // L3 -> 1
    std::vector<int8_t> fc0Packed[LS_BUCKETS];   // fc0 weights as [L1/4][FC0_OUT][4]

    bool loaded = false;
    std::string lastError;     // reason of the last failed load()

    // -----------------------------------------------------------------
    // Loading
    // -----------------------------------------------------------------
    bool load(const std::string& path) {
        loaded = false;
        lastError.clear();
        auto fail = [&](const std::string& why) {
            lastError = why;
            ftBias.clear(); ftWeight.clear(); ftPsqtWeight.clear();
            ftBias.shrink_to_fit(); ftWeight.shrink_to_fit(); ftPsqtWeight.shrink_to_fit();
            return false;
        };

        std::ifstream f(path, std::ios::binary);
        if (!f) return fail("cannot open file '" + path + "'");

        uint32_t version, headerHash, descLen, ftHash;
        if (!detail::read_u32(f, version)) return fail("file too short");
        if (version != FILE_VERSION) {
            char buf[160];
            std::snprintf(buf, sizeof(buf),
                          "unsupported file version 0x%08X (expected 0x%08X: Stockfish 17 net layout)",
                          (unsigned)version, (unsigned)FILE_VERSION);
            return fail(buf);
        }
        if (!detail::read_u32(f, headerHash)) return fail("file too short (header hash)");
        const uint32_t expectedHeader = FC_HASH ^ FEATURE_HASH ^ (uint32_t)(L1 * 2);
        if (headerHash != expectedHeader) {
            char buf[200];
            std::snprintf(buf, sizeof(buf),
                          "architecture mismatch (header hash 0x%08X, expected 0x%08X for "
                          "HalfKAv2_hm L1=%d L2=%d L3=%d)",
                          (unsigned)headerHash, (unsigned)expectedHeader, L1, L2, L3);
            return fail(buf);
        }
        if (!detail::read_u32(f, descLen)) return fail("file too short (description)");
        f.seekg((std::streamoff)descLen, std::ios::cur);
        if (!f) return fail("bad description length");
        if (!detail::read_u32(f, ftHash)) return fail("file too short (feature hash)");
        if (ftHash != (FEATURE_HASH ^ (uint32_t)(L1 * 2))) return fail("feature transformer hash mismatch");

        std::string err;
        if (!detail::read_tensor(f, ftBias, (size_t)L1, 2, err)) return fail("failed reading FT biases " + err);
        if (!detail::read_tensor(f, ftWeight, (size_t)NUM_REAL_FEATURES * L1, 2, err))
            return fail("failed reading FT weights " + err);
        if (!detail::read_tensor(f, ftPsqtWeight, (size_t)NUM_REAL_FEATURES * PSQT_BUCKETS, 4, err))
            return fail("failed reading PSQT weights " + err);

        // Stockfish doubles the FT biases/weights on load (its accumulator is in 2x units).
        for (auto& b : ftBias) b = (int16_t)(b * FT_WEIGHT_MUL);
        for (auto& w : ftWeight) w = (int16_t)(w * FT_WEIGHT_MUL);

        for (int b = 0; b < LS_BUCKETS; ++b) {
            uint32_t bucketHash;
            if (!detail::read_u32(f, bucketHash)) return fail("file too short (layer stack " + std::to_string(b) + ")");
            if (bucketHash != FC_HASH) return fail("layer stack " + std::to_string(b) + " hash mismatch");
            if (!fc0[b].load(f, L1, FC0_OUT)) return fail("failed reading fc_0 of bucket " + std::to_string(b));
            if (!fc1[b].load(f, FC1_REAL_IN, L3)) return fail("failed reading fc_1 of bucket " + std::to_string(b));
            if (!fc2[b].load(f, L3, 1)) return fail("failed reading fc_2 of bucket " + std::to_string(b));
        }
        f.peek();
        if (!f.eof()) return fail("unexpected trailing data after last layer stack");

        // Repack fc_0 for the sparse kernel.
        for (int b = 0; b < LS_BUCKETS; ++b) {
            fc0Packed[b].assign((size_t)L1 * FC0_OUT, 0);
            for (int i = 0; i < L1; ++i)
                for (int j = 0; j < FC0_OUT; ++j)
                    fc0Packed[b][(size_t)(i / 4) * (FC0_OUT * 4) + (size_t)j * 4 + (i % 4)] =
                        fc0[b].weight[(size_t)j * fc0[b].padded_in + i];
        }

        loaded = true;
        return true;
    }

    // -----------------------------------------------------------------
    // HalfKAv2_hm feature index
    // `perspective` is the accumulator side; `ksq` is that side's own king square;
    // (color, pt, sq) describe the piece.
    // -----------------------------------------------------------------
    static inline int feature_row(chess::Color perspective, chess::Square ksq, chess::PieceType pt,
                                  chess::Color color, chess::Square sq) {
        const bool whitePov = (perspective == chess::Color::WHITE);
        const int kfile = ksq.index() & 7;
        const int flip = (kfile < 4 ? 7 : 0) ^ (whitePov ? 0 : 56);
        const int bucket = KingBucketOriented[ksq.index() ^ flip];

        int plane;
        switch (static_cast<int>(pt)) {
            case static_cast<int>(chess::PieceType::PAWN):   plane = 0; break;
            case static_cast<int>(chess::PieceType::KNIGHT): plane = 2; break;
            case static_cast<int>(chess::PieceType::BISHOP): plane = 4; break;
            case static_cast<int>(chess::PieceType::ROOK):   plane = 6; break;
            case static_cast<int>(chess::PieceType::QUEEN):  plane = 8; break;
            case static_cast<int>(chess::PieceType::KING):   plane = 10; break;
            default: return -1;
        }
        if (pt != chess::PieceType::KING && color != perspective) plane += 1;
        return bucket * PS_NB + plane * NUM_SQ + (sq.index() ^ flip);
    }

    // -----------------------------------------------------------------
    // Accumulator maintenance
    // -----------------------------------------------------------------
    inline const int16_t* wrow(int row) const { return &ftWeight[(size_t)row * L1]; }
    inline const int32_t* prow(int row) const { return &ftPsqtWeight[(size_t)row * PSQT_BUCKETS]; }

    inline void add_feature(PerspectiveAccumulator& pa, int row) const {
        int16_t* NNUE_RESTRICT a = pa.acc.data();
        const int16_t* NNUE_RESTRICT w = wrow(row);
        for (int i = 0; i < L1; ++i) a[i] = (int16_t)(a[i] + w[i]);
        const int32_t* p = prow(row);
        for (int i = 0; i < PSQT_BUCKETS; ++i) pa.psqt[i] += p[i];
    }

    // dst = src + add - sub1 [- sub2], one pass over the accumulator.
    inline void fused_update(PerspectiveAccumulator& d, const PerspectiveAccumulator& s, int rAdd, int rSub1,
                             int rSub2) const {
        int16_t* NNUE_RESTRICT dp = d.acc.data();
        const int16_t* NNUE_RESTRICT sp = s.acc.data();
        const int16_t* NNUE_RESTRICT wa = wrow(rAdd);
        const int16_t* NNUE_RESTRICT w1 = wrow(rSub1);
        if (rSub2 >= 0) {
            const int16_t* NNUE_RESTRICT w2 = wrow(rSub2);
            for (int i = 0; i < L1; ++i) dp[i] = (int16_t)(sp[i] + wa[i] - w1[i] - w2[i]);
        } else {
            for (int i = 0; i < L1; ++i) dp[i] = (int16_t)(sp[i] + wa[i] - w1[i]);
        }
        const int32_t* pa = prow(rAdd);
        const int32_t* p1 = prow(rSub1);
        if (rSub2 >= 0) {
            const int32_t* p2 = prow(rSub2);
            for (int i = 0; i < PSQT_BUCKETS; ++i) d.psqt[i] = s.psqt[i] + pa[i] - p1[i] - p2[i];
        } else {
            for (int i = 0; i < PSQT_BUCKETS; ++i) d.psqt[i] = s.psqt[i] + pa[i] - p1[i];
        }
    }

    // Full refresh of one perspective from a board.
    void refresh_perspective(chess::Color perspective, const chess::Board& board, Accumulator& acc) const {
        using namespace chess;
        PerspectiveAccumulator& pa = acc.side[(int)perspective];
        std::memcpy(pa.acc.data(), ftBias.data(), sizeof(int16_t) * L1);
        pa.psqt.fill(0);

        const Square ksq = board.kingSq(perspective);
        Bitboard occ = board.occ();
        while (occ) {
            Square sq = Square(occ.pop());
            Piece p = board.at(sq);
            if (p == Piece::NONE) continue;
            add_feature(pa, feature_row(perspective, ksq, p.type(), p.color(), sq));
        }
    }

    void refresh(const chess::Board& board, Accumulator& acc) const {
        refresh_perspective(chess::Color::WHITE, board, acc);
        refresh_perspective(chess::Color::BLACK, board, acc);
    }

    // Refresh one perspective whose own king is moving (`board` = PRE-move position,
    // not castling) directly into `dst`.
    void refresh_after_king_move(chess::Color perspective, const chess::Board& board, const chess::Move& move,
                                 PerspectiveAccumulator& dst) const {
        using namespace chess;
        std::memcpy(dst.acc.data(), ftBias.data(), sizeof(int16_t) * L1);
        dst.psqt.fill(0);

        const Square fromSq = move.from();
        const Square toSq = move.to();
        Bitboard occ = board.occ();
        while (occ) {
            Square sq = Square(occ.pop());
            if (sq == toSq) continue;   // captured piece disappears
            Piece p = board.at(sq);
            if (p == Piece::NONE) continue;
            Square actualSq = (sq == fromSq) ? toSq : sq;
            add_feature(dst, feature_row(perspective, toSq, p.type(), p.color(), actualSq));
        }
    }

    static inline void copy_acc(Accumulator& dst, const Accumulator& src) { dst = src; }

    // dst = accumulator after `move`, computed from src (the accumulator before it).
    // `board` is the PRE-move position. Not for castling (caller refreshes the post-move board).
    void apply_move(Accumulator& dst, const Accumulator& src, const chess::Board& board,
                    const chess::Move& move) const {
        using namespace chess;
        assert(move.typeOf() != Move::CASTLING);

        const Square from = move.from(), to = move.to();
        const Piece moved = board.at(from);
        if (moved == Piece::NONE) { dst = src; return; }

        const bool isEP = (move.typeOf() == Move::ENPASSANT);
        const bool hasNormalCapture = !isEP && board.at(to) != Piece::NONE;
        const Piece normalCaptured = hasNormalCapture ? board.at(to) : Piece::NONE;
        const PieceType destPT = (move.typeOf() == Move::PROMOTION) ? move.promotionType() : moved.type();

        for (int side = 0; side < 2; ++side) {
            const Color persp = Color(side);
            if (moved.type() == PieceType::KING && moved.color() == persp) {
                refresh_after_king_move(persp, board, move, dst.side[side]);
                continue;
            }
            const Square ksq = board.kingSq(persp);
            const int rAdd = feature_row(persp, ksq, destPT, moved.color(), to);
            const int rSub1 = feature_row(persp, ksq, moved.type(), moved.color(), from);
            int rSub2 = -1;
            if (isEP) {
                Square epCapSq = Square(to.index() + ((moved.color() == Color::WHITE) ? -8 : 8));
                rSub2 = feature_row(persp, ksq, PieceType::PAWN, ~moved.color(), epCapSq);
            } else if (hasNormalCapture) {
                rSub2 = feature_row(persp, ksq, normalCaptured.type(), normalCaptured.color(), to);
            }
            fused_update(dst.side[side], src.side[side], rAdd, rSub1, rSub2);
        }
    }

    // -----------------------------------------------------------------
    // Forward pass
    // -----------------------------------------------------------------
    // psqt and positional parts, each already divided by OUTPUT_SCALE (as in Stockfish),
    // from the side to move's point of view.
    void eval_parts(const Accumulator& acc, chess::Color stm, int pieceCount, int& psqtOut,
                    int& positionalOut) const {
        int bucket = (pieceCount - 1) / 4;
        bucket = std::clamp(bucket, 0, LS_BUCKETS - 1);
        const PerspectiveAccumulator& us = acc.side[(int)stm];
        const PerspectiveAccumulator& them = acc.side[(int)(~stm)];

        alignas(32) uint8_t ftOut[L1];
        detail::ft_output(us.acc.data(), them.acc.data(), ftOut);

        alignas(32) int32_t fc0_out[FC0_OUT];
        detail::fc0_sparse(ftOut, fc0Packed[bucket].data(), fc0[bucket].bias.data(), fc0_out);

        // sqr-clipped-relu(fc0[0..L2)) ++ clipped-relu(fc0[0..L2)), zero padded to FC1_IN
        alignas(32) uint8_t act1[FC1_IN];
        std::memset(act1, 0, sizeof(act1));
        for (int j = 0; j < L2; ++j) {
            const int64_t sq = (int64_t)fc0_out[j] * (int64_t)fc0_out[j];
            act1[j] = (uint8_t)std::min<int64_t>(127, sq >> (2 * WEIGHT_SCALE_BITS + 7));
            act1[L2 + j] = (uint8_t)std::clamp(fc0_out[j] >> WEIGHT_SCALE_BITS, 0, 127);
        }

        // fc_1 : FC1_IN -> L3
        const FCLayer& l1 = fc1[bucket];
        alignas(32) uint8_t act2[L3];
#if defined(__AVX2__)
        {
            const __m256i ones = _mm256_set1_epi16(1);
            for (int k = 0; k < L3; ++k) {
                const int8_t* w = &l1.weight[(size_t)k * l1.padded_in];
                __m256i s = _mm256_setzero_si256();
                for (int i = 0; i < FC1_IN; i += 32)
                    s = _mm256_add_epi32(s, _mm256_madd_epi16(_mm256_maddubs_epi16(
                            _mm256_load_si256((const __m256i*)(act1 + i)),
                            _mm256_loadu_si256((const __m256i*)(w + i))), ones));
                const int32_t v = l1.bias[k] + detail::hsum_epi32(s);
                act2[k] = (uint8_t)std::clamp(v >> WEIGHT_SCALE_BITS, 0, 127);
            }
        }
#else
        for (int k = 0; k < L3; ++k) {
            int32_t s = l1.bias[k];
            const int8_t* w = &l1.weight[(size_t)k * l1.padded_in];
            for (int i = 0; i < FC1_IN; ++i) s += (int32_t)w[i] * (int32_t)act1[i];
            act2[k] = (uint8_t)std::clamp(s >> WEIGHT_SCALE_BITS, 0, 127);
        }
#endif

        // fc_2 : L3 -> 1
        const FCLayer& l2 = fc2[bucket];
        int32_t fc2_out = l2.bias[0];
        for (int i = 0; i < L3; ++i) fc2_out += (int32_t)l2.weight[i] * (int32_t)act2[i];

        const int32_t fwdOut =
            (int32_t)((int64_t)fc0_out[L2] * (NNUE2SCORE * OUTPUT_SCALE) / (127 * (1 << WEIGHT_SCALE_BITS)));
        const int32_t positional = fc2_out + fwdOut;

        const int32_t psqt = (us.psqt[bucket] - them.psqt[bucket]) / 2;
        psqtOut = psqt / OUTPUT_SCALE;
        positionalOut = positional / OUTPUT_SCALE;
    }

    // Evaluation from the side to move's perspective, in Stockfish internal units.
    int evaluate_from_accumulator(const Accumulator& acc, chess::Color stm, int pieceCount) const {
        int psqt, positional;
        eval_parts(acc, stm, pieceCount, psqt, positional);
        return psqt + positional;
    }

    int evaluate(const chess::Board& board) const {
        Accumulator acc;
        refresh(board, acc);
        return evaluate_from_accumulator(acc, board.sideToMove(), board.occ().count());
    }

    // Plain scalar re-implementation of the forward pass, mirroring Stockfish's
    // non-SIMD code path. Used only to test the SIMD kernels.
    void eval_parts_ref(const Accumulator& acc, chess::Color stm, int pieceCount, int& psqtOut,
                        int& positionalOut) const {
        int bucket = std::clamp((pieceCount - 1) / 4, 0, LS_BUCKETS - 1);
        const PerspectiveAccumulator& us = acc.side[(int)stm];
        const PerspectiveAccumulator& them = acc.side[(int)(~stm)];
        std::vector<uint8_t> ftOut(L1);
        const PerspectiveAccumulator* persp[2] = {&us, &them};
        for (int p = 0; p < 2; ++p)
            for (int j = 0; j < L1 / 2; ++j) {
                int s0 = std::clamp<int>(persp[p]->acc[j], 0, FT_MAX_VAL);
                int s1 = std::clamp<int>(persp[p]->acc[j + L1 / 2], 0, FT_MAX_VAL);
                ftOut[(L1 / 2) * p + j] = (uint8_t)(unsigned(s0 * s1) / 512);
            }
        int32_t o0[FC0_OUT];
        for (int j = 0; j < FC0_OUT; ++j) {
            int32_t s = fc0[bucket].bias[j];
            for (int i = 0; i < L1; ++i) s += (int32_t)fc0[bucket].weight[(size_t)j * fc0[bucket].padded_in + i] * ftOut[i];
            o0[j] = s;
        }
        uint8_t a1[FC1_IN] = {};
        for (int j = 0; j < L2; ++j) {
            a1[j] = (uint8_t)std::min<long long>(127, ((long long)o0[j] * o0[j]) >> (2 * WEIGHT_SCALE_BITS + 7));
            a1[L2 + j] = (uint8_t)std::clamp(o0[j] >> WEIGHT_SCALE_BITS, 0, 127);
        }
        uint8_t a2[L3];
        for (int k = 0; k < L3; ++k) {
            int32_t s = fc1[bucket].bias[k];
            for (int i = 0; i < FC1_IN; ++i) s += (int32_t)fc1[bucket].weight[(size_t)k * fc1[bucket].padded_in + i] * a1[i];
            a2[k] = (uint8_t)std::clamp(s >> WEIGHT_SCALE_BITS, 0, 127);
        }
        int32_t o2 = fc2[bucket].bias[0];
        for (int i = 0; i < L3; ++i) o2 += (int32_t)fc2[bucket].weight[i] * a2[i];
        int32_t fwd = o0[L2] * (NNUE2SCORE * OUTPUT_SCALE) / (127 * (1 << WEIGHT_SCALE_BITS));
        psqtOut = ((us.psqt[bucket] - them.psqt[bucket]) / 2) / OUTPUT_SCALE;
        positionalOut = (o2 + fwd) / OUTPUT_SCALE;
    }
};

}  // namespace nnue
