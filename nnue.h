#pragma once

// NNUE inference for networks exported by the official
// official-stockfish/nnue-pytorch trainer's serialize.py, trained with:
//
//     --features HalfKAv2_hm^
//     --l1 32  --l2 8  --l3 8
//
// This is a *from-scratch* rewrite. It deliberately reproduces the exact
// binary layout and fixed-point arithmetic used by the actual Stockfish
// engine for this architecture (HalfKAv2_hm feature transformer + the
// "LayerStacks" squared/clipped-relu FC head with PSQT buckets), since that
// is precisely the format serialize.py writes. The quantization constants
// (OutputScale, WeightScaleBits, HiddenOneVal, FtMaxVal, nnue2score) are not
// architecture-size-dependent -- they come straight from the trainer /
// engine source and are reused verbatim here.
//
// Do NOT feed this loader files trained with any other --features setting

#include "chess.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <iostream>
namespace nnue {

// ---------------------------------------------------------------------
// Architecture constants for THIS network (HalfKAv2_hm^, L1=128,L2=32,L3=8)
// ---------------------------------------------------------------------
constexpr int L1 = 128;
constexpr int L2 = 32;
constexpr int L3 = 8;
constexpr int PSQT_BUCKETS = 8;
constexpr int LS_BUCKETS   = 8;   // "layer stack" (positional head) buckets

constexpr int NUM_SQ            = 64;
constexpr int NUM_KING_BUCKETS  = 32;
constexpr int NUM_EXPORT_PLANES = 11;                          // 10 piece planes + 1 merged king plane
constexpr int PS_NB             = NUM_EXPORT_PLANES * NUM_SQ;   // 704
constexpr int NUM_REAL_FEATURES = PS_NB * NUM_KING_BUCKETS;     // 22528

// File format constants (from nnue-pytorch/model/utils/serialize.py and the
// matching Stockfish engine reader). These are fixed by the trainer/engine,
// not by L1/L2/L3.
constexpr uint32_t FILE_VERSION = 0x6A448AFAu;
constexpr uint32_t FEATURE_HASH = 0x7F234CB8u;  // HalfKAv2_hm feature-set hash

constexpr int OUTPUT_SCALE      = 16;
constexpr int WEIGHT_SCALE_BITS = 6;    // 2^6 = 64 = weight_scale_l2
constexpr int FT_MAX_VAL        = 255;
constexpr int HIDDEN_ONE        = 128;  // 2^7 = weight_scale_l1
constexpr int NNUE2SCORE        = 600;

// KingBuckets, indexed by the *oriented* king square (0..63); -1 where the
// mirrored king can never land (files a-d after orientation are impossible).
// Must match nnue-pytorch's model/modules/features/halfka_v2_hm.py exactly.
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
// Little binary-reading helpers (little-endian file, matching NNUEWriter)
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
    constexpr size_t magicLen = sizeof(magic) - 1;  // 17, no NUL
    std::streampos pos = f.tellg();
    char buf[magicLen];
    f.read(buf, (std::streamsize)magicLen);
    bool ok = (f.gcount() == (std::streamsize)magicLen) && std::memcmp(buf, magic, magicLen) == 0;
    f.clear();
    f.seekg(pos);
    return ok;
}

// Decode `count` signed LEB128 values from `bytes` into `out` (as int64,
// truncated to whatever integer type the caller ultimately stores).
inline bool decode_leb128(const std::vector<uint8_t>& bytes, size_t count, std::vector<int64_t>& out) {
    out.resize(count);
    size_t pos = 0;
    for (size_t i = 0; i < count; ++i) {
        uint64_t result = 0;
        int shift = 0;
        uint8_t byte;
        do {
            if (pos >= bytes.size()) return false;
            byte = bytes[pos++];
            result |= (uint64_t)(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        int64_t v = (int64_t)result;
        if (shift < 64 && (byte & 0x40)) v |= -((int64_t)1 << shift);
        out[i] = v;
    }
    return true;
}

// Reads a tensor of `count` scalar values (each `width` bytes when stored
// raw) that may or may not be LEB128-compressed on disk, matching Python's
// NNUEReader.tensor()/determine_compression(). Values are widened to
// int64 by the caller-provided narrow() function and stored into `out`.
template <typename T>
inline bool read_tensor(std::ifstream& f, std::vector<T>& out, size_t count, int width) {
    out.resize(count);
    if (peek_leb128_magic(f)) {
    constexpr size_t magicLen = sizeof("COMPRESSED_LEB128") - 1;
    char magic[magicLen];

    if (!read_bytes(f, magic, magicLen))
        return false;

    uint32_t byteLen;
        if (!read_u32(f, byteLen)) return false;
        std::vector<uint8_t> buf(byteLen);
        if (byteLen && !read_bytes(f, buf.data(), byteLen)) return false;
        std::vector<int64_t> decoded;
        if (!decode_leb128(buf, count, decoded)) return false;
        for (size_t i = 0; i < count; ++i) out[i] = (T)decoded[i];
        return true;
    } else {
        for (size_t i = 0; i < count; ++i) {
            if (width == 1) {
                int8_t v;
                if (!read_bytes(f, &v, 1)) return false;
                out[i] = (T)v;
            } else if (width == 2) {
                uint8_t b[2];
                if (!read_bytes(f, b, 2)) return false;
                int16_t v = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
                out[i] = (T)v;
            } else {
                uint8_t b[4];
                if (!read_bytes(f, b, 4)) return false;
                int32_t v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                                       ((uint32_t)b[3] << 24));
                out[i] = (T)v;
            }
        }
        return true;
    }
}

}  // namespace detail

// ---------------------------------------------------------------------
// One incremental accumulator (both perspectives).
// ---------------------------------------------------------------------
struct PerspectiveAccumulator {
    alignas(32) std::array<int16_t, L1> acc{};
    std::array<int32_t, PSQT_BUCKETS> psqt{};
};

struct Accumulator {
    // Indexed by chess::Color (WHITE=0, BLACK=1).
    PerspectiveAccumulator side[2];
};

// ---------------------------------------------------------------------
// One quantized FC layer, stored per layer-stack bucket.
// FC inputs are padded to a multiple of 32 on disk (per spec).
// ---------------------------------------------------------------------
struct FCLayer {
    int in_features = 0;
    int out_features = 0;
    int padded_in = 0;
    std::vector<int32_t> bias;    // [out_features]
    std::vector<int8_t> weight;   // [out_features][padded_in], row-major

    bool load(std::ifstream& f, int inF, int outF) {
        in_features = inF;
        out_features = outF;
        padded_in = ((inF + 31) / 32) * 32;
        bias.resize(out_features);
        for (int i = 0; i < out_features; ++i) {
            uint8_t b[4];
            if (!detail::read_bytes(f, b, 4)) return false;
            bias[i] = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
                                 ((uint32_t)b[3] << 24));
        }
        weight.resize((size_t)out_features * padded_in);
        for (size_t i = 0; i < weight.size(); ++i) {
            int8_t v;
            if (!detail::read_bytes(f, &v, 1)) return false;
            weight[i] = v;
        }
        return true;
    }
};

// ---------------------------------------------------------------------
// The network.
// ---------------------------------------------------------------------
struct Network {
    // Feature transformer.
    std::vector<int16_t> ftBias;                 // [L1]
    std::vector<int16_t> ftWeight;                // [NUM_REAL_FEATURES][L1], row-major
    std::vector<int32_t> ftPsqtWeight;             // [NUM_REAL_FEATURES][PSQT_BUCKETS], row-major

    // Positional head, one set of 3 layers per layer-stack bucket.
    FCLayer l1[LS_BUCKETS];  // L1 -> L2
    FCLayer l2[LS_BUCKETS];  // 2*L2 -> L3
    FCLayer l3[LS_BUCKETS];  // 2*L2 + 2*L3 -> 1

    bool loaded = false;

    // ---- fc_hash, matching NNUEWriter.fc_hash() exactly ----
    static uint32_t compute_fc_hash() {
        uint32_t prev = 0xEC42E90Du ^ (uint32_t)(L1 * 2);
        const int outs[3] = {L2, L3, 1};
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

    bool load(const std::string& path) {
    loaded = false;

    std::ifstream f(path, std::ios::binary);
    if (!f)
        return false;

    const uint32_t fcHash = compute_fc_hash();

    // ------------------------------------------------------------
    // FILE VERSION
    // ------------------------------------------------------------
    uint32_t version;
    if (!detail::read_u32(f, version))
        return false;

    if (version != FILE_VERSION)
        return false;

    // ------------------------------------------------------------
    // HEADER HASH
    // ------------------------------------------------------------
    uint32_t headerHash;
    if (!detail::read_u32(f, headerHash))
        return false;

    const uint32_t expectedHeaderHash =
        fcHash ^ FEATURE_HASH ^ (uint32_t)(L1 * 2);

    if (headerHash != expectedHeaderHash)
        return false;

    // ------------------------------------------------------------
    // DESCRIPTION
    // ------------------------------------------------------------
    uint32_t descLen;
    if (!detail::read_u32(f, descLen))
        return false;

    f.seekg((std::streamoff)descLen, std::ios::cur);

    if (!f)
        return false;

    // ------------------------------------------------------------
    // FEATURE HASH
    // ------------------------------------------------------------
    uint32_t ftHash;
    if (!detail::read_u32(f, ftHash))
        return false;

    const uint32_t expectedFtHash =
        FEATURE_HASH ^ (uint32_t)(L1 * 2);

    if (ftHash != expectedFtHash)
        return false;

    // ------------------------------------------------------------
    // FEATURE TRANSFORMER BIAS
    // ------------------------------------------------------------
    if (!detail::read_tensor(
            f,
            ftBias,
            (size_t)L1,
            2))
        return false;

    // ------------------------------------------------------------
    // FEATURE TRANSFORMER WEIGHTS
    // ------------------------------------------------------------
    const size_t ftWeightCount =
        (size_t)NUM_REAL_FEATURES * L1;

    if (!detail::read_tensor(
            f,
            ftWeight,
            ftWeightCount,
            2))
        return false;

    // ------------------------------------------------------------
    // PSQT WEIGHTS
    // ------------------------------------------------------------
    const size_t psqtCount =
        (size_t)NUM_REAL_FEATURES * PSQT_BUCKETS;

    if (!detail::read_tensor(
            f,
            ftPsqtWeight,
            psqtCount,
            4))
        return false;

    // ------------------------------------------------------------
    // LAYER STACKS
    // ------------------------------------------------------------
    for (int b = 0; b < LS_BUCKETS; ++b) {

        uint32_t bucketHash;

        if (!detail::read_u32(f, bucketHash))
            return false;

        if (bucketHash != fcHash)
            return false;

        // L1 -> L2
        if (!l1[b].load(f, L1, L2))
            return false;

        // 2*L2 -> L3
        if (!l2[b].load(f, 2 * L2, L3))
            return false;

        // 2*L2 + 2*L3 -> 1
        if (!l3[b].load(
                f,
                2 * L2 + 2 * L3,
                1))
            return false;
    }

    // ------------------------------------------------------------
    // FINAL STREAM CHECK
    // ------------------------------------------------------------
    if (!f)
        return false;

    loaded = true;
    return true;
}

    // ---- HalfKAv2_hm feature index ----
    // `perspective` is the accumulator side being updated; `ksq` is that
    // side's own king square; (color, pt, sq) describe the piece feature.
    static inline int feature_row(chess::Color perspective, chess::Square ksq, chess::PieceType pt,
                                   chess::Color color, chess::Square sq) {
        const bool whitePov = (perspective == chess::Color::WHITE);
        const int kfile = ksq.index() & 7;
        const int flip = (kfile < 4 ? 7 : 0) ^ (whitePov ? 0 : 56);
        const int oksq = ksq.index() ^ flip;
        const int bucket = KingBucketOriented[oksq];
        // Stockfish's HalfKAv2_hm export planes are:
        //   0/1 = pawn, 2/3 = knight, 4/5 = bishop,
        //   6/7 = rook, 8/9 = queen, 10 = king.
        // chess.hpp PieceType values are not the exported plane numbers,
        // so do NOT multiply the enum value directly.
        int planeBase;

switch (static_cast<int>(pt)) {
    case static_cast<int>(chess::PieceType::PAWN):
        planeBase = 0;
        break;

    case static_cast<int>(chess::PieceType::KNIGHT):
        planeBase = 2;
        break;

    case static_cast<int>(chess::PieceType::BISHOP):
        planeBase = 4;
        break;

    case static_cast<int>(chess::PieceType::ROOK):
        planeBase = 6;
        break;

    case static_cast<int>(chess::PieceType::QUEEN):
        planeBase = 8;
        break;

    case static_cast<int>(chess::PieceType::KING):
        planeBase = 10;
        break;

    default:
        return -1;
}

        const int plane =
            (pt == chess::PieceType::KING)
                ? 10
                : planeBase + ((color != perspective) ? 1 : 0);

        const int osq = sq.index() ^ flip;
        return bucket * PS_NB + plane * NUM_SQ + osq;
    }

    inline void toggle(Accumulator& acc, chess::Color perspective, chess::Square ksq,
                        chess::PieceType pt, chess::Color color, chess::Square sq, bool add) const {
        const int row = feature_row(perspective, ksq, pt, color, sq);
        assert(row >= 0 && row < NUM_REAL_FEATURES);
        PerspectiveAccumulator& pa = acc.side[(int)perspective];
        const int16_t* wrow = &ftWeight[(size_t)row * L1];
        const int32_t* prow = &ftPsqtWeight[(size_t)row * PSQT_BUCKETS];
        if (add) {
            for (int i = 0; i < L1; ++i) pa.acc[i] = (int16_t)(pa.acc[i] + wrow[i]);
            for (int i = 0; i < PSQT_BUCKETS; ++i) pa.psqt[i] += prow[i];
        } else {
            for (int i = 0; i < L1; ++i) pa.acc[i] = (int16_t)(pa.acc[i] - wrow[i]);
            for (int i = 0; i < PSQT_BUCKETS; ++i) pa.psqt[i] -= prow[i];
        }
    }

    // ---- Full refresh of one perspective from a board ----
    void refresh_perspective(chess::Color perspective, const chess::Board& board, Accumulator& acc) const {
        using namespace chess;
        PerspectiveAccumulator& pa = acc.side[(int)perspective];
        for (int i = 0; i < L1; ++i) pa.acc[i] = ftBias[i];
        pa.psqt.fill(0);

        const Square ksq = board.kingSq(perspective);
        Bitboard occ = board.occ();
        while (occ) {
            Square sq = Square(occ.pop());
            Piece p = board.at(sq);
            if (p == Piece::NONE) continue;
            toggle(acc, perspective, ksq, p.type(), p.color(), sq, true);
        }
    }

    void refresh(const chess::Board& board, Accumulator& acc) const {
        refresh_perspective(chess::Color::WHITE, board, acc);
        refresh_perspective(chess::Color::BLACK, board, acc);
    }

    // ---- Refresh a single perspective whose own king just moved ----
    // `board` must be in the PRE-move state (same convention as
    // update_for_move below). Must not be used for castling.
    void refresh_perspective_after_king_move(chess::Color perspective, const chess::Board& board,
                                              const chess::Move& move, Accumulator& acc) const {
        using namespace chess;
        PerspectiveAccumulator& pa = acc.side[(int)perspective];
        for (int i = 0; i < L1; ++i) pa.acc[i] = ftBias[i];
        pa.psqt.fill(0);

        const Square newKsq = move.to();
        const Square fromSq = move.from();
        const Square toSq = move.to();

        Bitboard occ = board.occ();
        while (occ) {
            Square sq = Square(occ.pop());
            if (sq == toSq) continue;  // captured piece (if any) disappears
            Piece p = board.at(sq);
            if (p == Piece::NONE) continue;
            Square actualSq = (sq == fromSq) ? newKsq : sq;  // the moving king itself
            toggle(acc, perspective, newKsq, p.type(), p.color(), actualSq, true);
        }
    }

    // ---- Incremental update for a normal (non-castling) move ----
    // `board` is in the PRE-move state, mirroring the previous nnue.h's
    // convention. Castling must instead be handled by the caller via a
    // full refresh() on the post-move board.
    void update_for_move(Accumulator& acc, const chess::Board& board, const chess::Move& move) const {
        using namespace chess;
        assert(move.typeOf() != Move::CASTLING);

        Square from = move.from(), to = move.to();
        Piece moved = board.at(from);
        if (moved == Piece::NONE) return;

        const bool isEP = (move.typeOf() == Move::ENPASSANT);
        const bool hasNormalCapture = !isEP && board.at(to) != Piece::NONE;
        Piece normalCaptured = hasNormalCapture ? board.at(to) : Piece::NONE;
        const PieceType destPT =
            (move.typeOf() == Move::PROMOTION) ? move.promotionType() : moved.type();

        for (int side = 0; side < 2; ++side) {
            Color persp = Color(side);

            if (moved.type() == PieceType::KING && moved.color() == persp) {
                refresh_perspective_after_king_move(persp, board, move, acc);
                continue;
            }

            const Square ksq = board.kingSq(persp);
            toggle(acc, persp, ksq, moved.type(), moved.color(), from, false);
            if (isEP) {
                Square epCapSq = Square(to.index() + ((moved.color() == Color::WHITE) ? -8 : 8));
                toggle(acc, persp, ksq, PieceType::PAWN, ~moved.color(), epCapSq, false);
            } else if (hasNormalCapture) {
                toggle(acc, persp, ksq, normalCaptured.type(), normalCaptured.color(), to, false);
            }
            toggle(acc, persp, ksq, destPT, moved.color(), to, true);
        }
    }

    // No board-occupancy change, so nothing to do (HalfKAv2_hm has no
    // castling-rights / en-passant-file features).
    void update_for_null_move(Accumulator& acc, const chess::Board& board) const {
        (void)acc;
        (void)board;
    }

    // ---- Forward pass ----
    // Returns the evaluation from the side-to-move's perspective, in
    // centipawns (calibrated by the trainer's nnue2score=600), matching
    // Stockfish's own NNUE::evaluate() scale/convention exactly.
    int evaluate_from_accumulator(const Accumulator& acc, chess::Color stm, int pieceCount) const {
        int bucket = (pieceCount - 1) / 4;
        if (bucket < 0) bucket = 0;
        if (bucket >= LS_BUCKETS) bucket = LS_BUCKETS - 1;

        const chess::Color persp0 = stm;
        const chess::Color persp1 = ~stm;

        // Feature-transformer output: split each perspective's L1
        // accumulator into two halves, clip to [0,255], multiply the
        // halves pairwise and shift right by 9 (divide by 512). This is
        // exactly what Stockfish's FeatureTransformer::transform_perspective
        // computes; it is NOT a plain concatenated clipped-relu.
        std::array<uint8_t, L1> ftOut{};
        constexpr int half = L1 / 2;
        for (int p = 0; p < 2; ++p) {
            const auto& a = acc.side[(int)(p == 0 ? persp0 : persp1)].acc;
            const int offset = half * p;
            for (int i = 0; i < half; ++i) {
                int32_t v0 = std::clamp<int32_t>(a[i], 0, FT_MAX_VAL);
                int32_t v1 = std::clamp<int32_t>(a[i + half], 0, FT_MAX_VAL);
                ftOut[offset + i] = (uint8_t)std::min(127, (v0 * v1) >> 9);
            }
        }

        int32_t psqt =
            (acc.side[(int)persp0].psqt[bucket] - acc.side[(int)persp1].psqt[bucket]) / 2;

        // fc_0 : L1 -> L2 (sparse-input affine transform, dense math here)
        const FCLayer& fc0 = l1[bucket];
        int32_t fc0_out[L2];
        for (int j = 0; j < L2; ++j) {
            int32_t s = fc0.bias[j];
            const int8_t* w = &fc0.weight[(size_t)j * fc0.padded_in];
            for (int i = 0; i < L1; ++i) s += (int32_t)w[i] * (int32_t)ftOut[i];
            fc0_out[j] = s;
        }

        // ac_sqr_0 (WeightScaleBits+1) / ac_0 (WeightScaleBits+1), concatenated
        constexpr int WSB0 = WEIGHT_SCALE_BITS + 1;  // 7
        uint8_t concat1[2 * L2];
        for (int j = 0; j < L2; ++j) {
            int64_t sq = (int64_t)fc0_out[j] * (int64_t)fc0_out[j];
            concat1[j] = (uint8_t)std::min<int64_t>(127, sq >> (2 * WSB0 + 7));
            concat1[L2 + j] = (uint8_t)std::clamp(fc0_out[j] >> WSB0, 0, 127);
        }

        // fc_1 : 2*L2 -> L3
        const FCLayer& fc1 = l2[bucket];
        int32_t fc1_out[L3];
        for (int k = 0; k < L3; ++k) {
            int32_t s = fc1.bias[k];
            const int8_t* w = &fc1.weight[(size_t)k * fc1.padded_in];
            for (int i = 0; i < 2 * L2; ++i) s += (int32_t)w[i] * (int32_t)concat1[i];
            fc1_out[k] = s;
        }

        // ac_sqr_1 / ac_1 (WeightScaleBits), concatenated after concat1
        constexpr int WSB1 = WEIGHT_SCALE_BITS;  // 6
        uint8_t concatFinal[2 * L2 + 2 * L3];
        std::memcpy(concatFinal, concat1, sizeof(concat1));
        for (int k = 0; k < L3; ++k) {
            int64_t sq = (int64_t)fc1_out[k] * (int64_t)fc1_out[k];
            concatFinal[2 * L2 + k] = (uint8_t)std::min<int64_t>(127, sq >> (2 * WSB1 + 7));
            concatFinal[2 * L2 + L3 + k] = (uint8_t)std::clamp(fc1_out[k] >> WSB1, 0, 127);
        }

        // fc_2 : (2*L2 + 2*L3) -> 1
        const FCLayer& fc2 = l3[bucket];
        int32_t fwdOut = fc2.bias[0];
        {
            const int8_t* w = &fc2.weight[0];
            const int n = 2 * L2 + 2 * L3;
            for (int i = 0; i < n; ++i) fwdOut += (int32_t)w[i] * (int32_t)concatFinal[i];
        }

        // LayerStacks skip connection from the last two fc_0 outputs.
        static_assert(L2 >= 2, "L2 must be at least 2 for the skip connection");
        fwdOut += fc0_out[L2 - 2] - fc0_out[L2 - 1];

        constexpr int64_t multiplier = (int64_t)NNUE2SCORE * OUTPUT_SCALE;
        constexpr int64_t denominator = (int64_t)HIDDEN_ONE * (1 << WEIGHT_SCALE_BITS) * 2;
        int32_t positional = (int32_t)(((int64_t)fwdOut * multiplier) / denominator);

        return psqt / OUTPUT_SCALE + positional / OUTPUT_SCALE;
    }

    int evaluate(const chess::Board& board) const {
        Accumulator acc;
        refresh(board, acc);
        return evaluate_from_accumulator(acc, board.sideToMove(), board.occ().count());
    }
};

}  // namespace nnue
