/*
 * backened.hpp - a fast, single-header chess move generation / board library.
 *
 * Drop-in replacement for the public interface of chess.hpp (namespace `chess`):
 *     #include "chess.hpp"   ->   #include "backened.hpp"
 *
 * Highlights
 *   - Fully legal move generation using king-ray (sniper) based pin / check detection
 *     with precomputed `between` tables instead of per-pin magic look-ups.
 *   - Branch-free castling-right maintenance (per-square rights masks, Stockfish style),
 *     one XOR for the castling part of the Zobrist key.
 *   - Flat Zobrist piece table (piece*64+square), table based piece-type / colour decode.
 *   - Compile-time generated leaper / between / line tables, runtime generated magic
 *     (or PEXT) slider tables with an original, independently searched set of magics.
 *   - Zobrist keys are Polyglot compatible (identical to chess.hpp), so hashes,
 *     opening books and stored positions stay interchangeable.
 *
 * Configuration macros (all optional)
 *   CHESS_USE_PEXT / BACKENED_USE_PEXT   use BMI2 PEXT for slider attacks (needs -mbmi2)
 *   CHESS_NO_EXCEPTIONS                  disable exceptions in the SAN parser / Movelist::at
 *   BACKENED_NO_VIRTUAL                  make Board::placePiece/removePiece/setFen non-virtual
 *                                        (fastest make/unmake; only use if you never subclass Board)
 *
 * Requires C++17.
 */

#ifndef BACKENED_HPP
#define BACKENED_HPP

// Make an accidental `#include "chess.hpp"` elsewhere in the same TU a harmless no-op.
#ifndef CHESS_HPP
#define CHESS_HPP
#endif

#include <algorithm>
#include <array>
#include <bitset>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iostream>
#include <istream>
#include <iterator>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <typeinfo>
#include <utility>
#include <vector>

#if __has_include(<charconv>)
#    define BACKENED_HAS_CHARCONV
#    include <charconv>
#else
#    include <sstream>
#endif

#if defined(_MSC_VER)
#    include <intrin.h>
#    include <nmmintrin.h>
#endif

#if defined(BACKENED_USE_PEXT) && !defined(CHESS_USE_PEXT)
#    define CHESS_USE_PEXT
#endif
#ifdef CHESS_USE_PEXT
#    include <immintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#    define BK_INLINE inline __attribute__((always_inline))
#    define BK_LIKELY(x) __builtin_expect(!!(x), 1)
#    define BK_UNLIKELY(x) __builtin_expect(!!(x), 0)
#    define BK_PREFETCH(p) __builtin_prefetch(p)
#elif defined(_MSC_VER)
#    define BK_INLINE __forceinline
#    define BK_LIKELY(x) (x)
#    define BK_UNLIKELY(x) (x)
#    define BK_PREFETCH(p) ((void)0)
#else
#    define BK_INLINE inline
#    define BK_LIKELY(x) (x)
#    define BK_UNLIKELY(x) (x)
#    define BK_PREFETCH(p) ((void)0)
#endif

#ifdef BACKENED_NO_VIRTUAL
#    define BK_VIRTUAL
#else
#    define BK_VIRTUAL virtual
#endif

// ---------------------------------------------------------------------------------------------
//  low level bit helpers
// ---------------------------------------------------------------------------------------------
namespace chess::bk {

#if defined(__GNUC__) || defined(__clang__)
#    define BK_CONSTEXPR_BITS constexpr
BK_INLINE constexpr int ctz64(std::uint64_t x) noexcept { return __builtin_ctzll(x); }
BK_INLINE constexpr int clz64(std::uint64_t x) noexcept { return __builtin_clzll(x); }
BK_INLINE constexpr int pop64(std::uint64_t x) noexcept { return __builtin_popcountll(x); }
#elif defined(_MSC_VER)
#    define BK_CONSTEXPR_BITS
BK_INLINE int ctz64(std::uint64_t x) noexcept {
    unsigned long i;
    _BitScanForward64(&i, x);
    return static_cast<int>(i);
}
BK_INLINE int clz64(std::uint64_t x) noexcept {
    unsigned long i;
    _BitScanReverse64(&i, x);
    return 63 - static_cast<int>(i);
}
BK_INLINE int pop64(std::uint64_t x) noexcept { return static_cast<int>(_mm_popcnt_u64(x)); }
#else
#    error "Unsupported compiler"
#endif

}  // namespace chess::bk

// ---------------------------------------------------------------------------------------------
//  Color
// ---------------------------------------------------------------------------------------------
namespace chess {

class Color {
   public:
    enum class underlying : std::int8_t { WHITE = 0, BLACK = 1, NONE = -1 };

    constexpr Color() : color(underlying::NONE) {}
    constexpr Color(underlying c) : color(c) {}
    constexpr Color(int c) : color(static_cast<underlying>(c)) { assert(c == 0 || c == 1 || c == -1); }
    constexpr Color(std::string_view str)
        : color(str == "w" ? underlying::WHITE : str == "b" ? underlying::BLACK : underlying::NONE) {}

    [[nodiscard]] std::string longStr() const {
        if (color == underlying::WHITE) return "White";
        if (color == underlying::BLACK) return "Black";
        return "None";
    }

    constexpr Color operator~() const noexcept {
        assert(color != underlying::NONE);
        return Color(static_cast<underlying>(static_cast<int>(color) ^ 1));
    }

    constexpr bool operator==(const Color& rhs) const noexcept { return color == rhs.color; }
    constexpr bool operator!=(const Color& rhs) const noexcept { return color != rhs.color; }

    constexpr operator int() const noexcept { return static_cast<int>(color); }

    explicit operator std::string() const {
        return color == underlying::WHITE ? "w" : color == underlying::BLACK ? "b" : "NONE";
    }

    [[nodiscard]] constexpr underlying internal() const noexcept { return color; }

    friend std::ostream& operator<<(std::ostream& os, const Color& c) { return os << static_cast<std::string>(c); }

    static constexpr underlying WHITE = underlying::WHITE;
    static constexpr underlying BLACK = underlying::BLACK;
    static constexpr underlying NONE  = underlying::NONE;

   private:
    underlying color;
};

constexpr Color::underlying operator~(Color::underlying c) {
    return c == Color::underlying::WHITE   ? Color::underlying::BLACK
           : c == Color::underlying::BLACK ? Color::underlying::WHITE
                                           : Color::underlying::NONE;
}

namespace utils {

[[nodiscard]] inline std::vector<std::string_view> splitString(std::string_view string, const char& delimiter) {
    std::vector<std::string_view> out;
    std::size_t begin = 0;
    for (;;) {
        const std::size_t pos = string.find(delimiter, begin);
        if (pos == std::string_view::npos) break;
        out.push_back(string.substr(begin, pos - begin));
        begin = pos + 1;
    }
    out.push_back(string.substr(begin));
    return out;
}

constexpr char tolower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

}  // namespace utils

// ---------------------------------------------------------------------------------------------
//  File / Rank / Square
// ---------------------------------------------------------------------------------------------

class File {
   public:
    enum class underlying : std::uint8_t { FILE_A, FILE_B, FILE_C, FILE_D, FILE_E, FILE_F, FILE_G, FILE_H, NO_FILE };

    constexpr File() : file(underlying::NO_FILE) {}
    constexpr File(underlying f) : file(f) {}
    constexpr File(int f) : file(static_cast<underlying>(f)) {}
    constexpr File(std::string_view sw)
        : file(static_cast<underlying>(utils::tolower(static_cast<char>(sw[0])) - 'a')) {}

    [[nodiscard]] constexpr underlying internal() const noexcept { return file; }

    constexpr bool operator==(const File& rhs) const noexcept { return file == rhs.file; }
    constexpr bool operator!=(const File& rhs) const noexcept { return file != rhs.file; }
    constexpr bool operator==(const underlying& rhs) const noexcept { return file == rhs; }
    constexpr bool operator!=(const underlying& rhs) const noexcept { return file != rhs; }
    constexpr bool operator>=(const File& rhs) const noexcept { return int(*this) >= int(rhs); }
    constexpr bool operator<=(const File& rhs) const noexcept { return int(*this) <= int(rhs); }
    constexpr bool operator>(const File& rhs) const noexcept { return int(*this) > int(rhs); }
    constexpr bool operator<(const File& rhs) const noexcept { return int(*this) < int(rhs); }

    constexpr File& operator+=(int rhs) noexcept {
        file = static_cast<underlying>(static_cast<int>(file) + rhs);
        return *this;
    }

    constexpr operator int() const noexcept { return static_cast<int>(file); }

    explicit operator std::string() const { return std::string(1, static_cast<char>('a' + static_cast<int>(file))); }

    static constexpr underlying FILE_A  = underlying::FILE_A;
    static constexpr underlying FILE_B  = underlying::FILE_B;
    static constexpr underlying FILE_C  = underlying::FILE_C;
    static constexpr underlying FILE_D  = underlying::FILE_D;
    static constexpr underlying FILE_E  = underlying::FILE_E;
    static constexpr underlying FILE_F  = underlying::FILE_F;
    static constexpr underlying FILE_G  = underlying::FILE_G;
    static constexpr underlying FILE_H  = underlying::FILE_H;
    static constexpr underlying NO_FILE = underlying::NO_FILE;

   private:
    underlying file;
};

class Rank {
   public:
    enum class underlying { RANK_1, RANK_2, RANK_3, RANK_4, RANK_5, RANK_6, RANK_7, RANK_8, NO_RANK };

    constexpr Rank() : rank_(underlying::NO_RANK) {}
    constexpr Rank(underlying r) : rank_(r) {}
    constexpr Rank(int r) : rank_(static_cast<underlying>(r)) {}
    constexpr Rank(std::string_view sw)
        : rank_(static_cast<underlying>(utils::tolower(static_cast<char>(sw[0])) - '1')) {}

    [[nodiscard]] constexpr underlying internal() const noexcept { return rank_; }

    constexpr bool operator==(const Rank& rhs) const noexcept { return rank_ == rhs.rank_; }
    constexpr bool operator!=(const Rank& rhs) const noexcept { return rank_ != rhs.rank_; }
    constexpr bool operator==(const underlying& rhs) const noexcept { return rank_ == rhs; }
    constexpr bool operator!=(const underlying& rhs) const noexcept { return rank_ != rhs; }
    constexpr bool operator>=(const Rank& rhs) const noexcept { return int(*this) >= int(rhs); }
    constexpr bool operator<=(const Rank& rhs) const noexcept { return int(*this) <= int(rhs); }

    constexpr Rank& operator+=(int rhs) noexcept {
        rank_ = static_cast<underlying>(static_cast<int>(rank_) + rhs);
        return *this;
    }

    operator std::string() const { return std::string(1, static_cast<char>('1' + static_cast<int>(rank_))); }
    constexpr operator int() const noexcept { return static_cast<int>(rank_); }

    [[nodiscard]] constexpr std::uint64_t bb() const noexcept { return 0xffULL << (8 * static_cast<int>(rank_)); }

    [[nodiscard]] static constexpr bool back_rank(Rank r, Color color) noexcept {
        return static_cast<int>(r) == static_cast<int>(color) * 7;
    }

    [[nodiscard]] static constexpr Rank rank(Rank r, Color color) noexcept {
        return Rank(static_cast<int>(r) ^ (static_cast<int>(color) * 7));
    }

    static constexpr underlying RANK_1  = underlying::RANK_1;
    static constexpr underlying RANK_2  = underlying::RANK_2;
    static constexpr underlying RANK_3  = underlying::RANK_3;
    static constexpr underlying RANK_4  = underlying::RANK_4;
    static constexpr underlying RANK_5  = underlying::RANK_5;
    static constexpr underlying RANK_6  = underlying::RANK_6;
    static constexpr underlying RANK_7  = underlying::RANK_7;
    static constexpr underlying RANK_8  = underlying::RANK_8;
    static constexpr underlying NO_RANK = underlying::NO_RANK;

   private:
    underlying rank_;
};

#define BK_SQ_RANK(N)                                    \
    static constexpr auto SQ_A##N = underlying::SQ_A##N; \
    static constexpr auto SQ_B##N = underlying::SQ_B##N; \
    static constexpr auto SQ_C##N = underlying::SQ_C##N; \
    static constexpr auto SQ_D##N = underlying::SQ_D##N; \
    static constexpr auto SQ_E##N = underlying::SQ_E##N; \
    static constexpr auto SQ_F##N = underlying::SQ_F##N; \
    static constexpr auto SQ_G##N = underlying::SQ_G##N; \
    static constexpr auto SQ_H##N = underlying::SQ_H##N;

class Square {
   public:
    // clang-format off
    enum class underlying {
        SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
        SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
        SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
        SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
        SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
        SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
        SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
        SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
        NO_SQ
    };
    // clang-format on

    BK_SQ_RANK(1) BK_SQ_RANK(2) BK_SQ_RANK(3) BK_SQ_RANK(4)
    BK_SQ_RANK(5) BK_SQ_RANK(6) BK_SQ_RANK(7) BK_SQ_RANK(8)
    static constexpr auto NO_SQ = underlying::NO_SQ;

    constexpr Square() : sq(underlying::NO_SQ) {}
    constexpr Square(int s) : sq(static_cast<underlying>(s)) { assert(s <= 64 && s >= 0); }
    constexpr Square(File f, Rank r) : sq(static_cast<underlying>(int(f) + int(r) * 8)) {}
    constexpr Square(Rank r, File f) : sq(static_cast<underlying>(int(f) + int(r) * 8)) {}
    constexpr Square(underlying s) : sq(s) {}
    constexpr Square(std::string_view str)
        : sq(static_cast<underlying>((str[0] - 'a') + (str[1] - '1') * 8)) {
        assert(str.size() >= 2);
    }

    constexpr Square operator^(const Square& s) const noexcept {
        return Square(static_cast<underlying>(static_cast<int>(sq) ^ s.index()));
    }

    constexpr bool operator==(const Square& rhs) const noexcept { return sq == rhs.sq; }
    constexpr bool operator!=(const Square& rhs) const noexcept { return sq != rhs.sq; }
    constexpr bool operator>(const Square& rhs) const noexcept { return index() > rhs.index(); }
    constexpr bool operator>=(const Square& rhs) const noexcept { return index() >= rhs.index(); }
    constexpr bool operator<(const Square& rhs) const noexcept { return index() < rhs.index(); }
    constexpr bool operator<=(const Square& rhs) const noexcept { return index() <= rhs.index(); }

    constexpr Square operator+(const Square& rhs) const noexcept {
        return Square(static_cast<underlying>(index() + rhs.index()));
    }
    constexpr Square operator-(const Square& rhs) const noexcept {
        return Square(static_cast<underlying>(index() - rhs.index()));
    }

    constexpr Square& operator++() noexcept {
        sq = static_cast<underlying>(index() + 1);
        return *this;
    }
    constexpr Square operator++(int) noexcept {
        Square old(*this);
        ++*this;
        return old;
    }
    constexpr Square& operator--() noexcept {
        sq = static_cast<underlying>(index() - 1);
        return *this;
    }
    constexpr Square operator--(int) noexcept {
        Square old(*this);
        --*this;
        return old;
    }

    [[nodiscard]] operator std::string() const {
        std::string s;
        s += static_cast<std::string>(file());
        s += static_cast<std::string>(rank());
        return s;
    }

    [[nodiscard]] constexpr int index() const noexcept { return static_cast<int>(sq); }

    [[nodiscard]] constexpr File file() const noexcept { return File(index() & 7); }
    [[nodiscard]] constexpr Rank rank() const noexcept { return Rank(index() >> 3); }

    [[nodiscard]] constexpr bool is_light() const noexcept { return ((index() >> 3) + (index() & 7)) & 1; }
    [[nodiscard]] constexpr bool is_dark() const noexcept { return !is_light(); }

    [[nodiscard]] constexpr bool is_valid() const noexcept { return static_cast<std::int8_t>(sq) < 64; }

    [[nodiscard]] constexpr static bool is_valid(Rank r, File f) noexcept {
        return int(r) >= 0 && int(r) <= 7 && int(f) >= 0 && int(f) <= 7;
    }

    [[nodiscard]] static int distance(Square a, Square b) noexcept {
        const int df = std::abs((a.index() & 7) - (b.index() & 7));
        const int dr = std::abs((a.index() >> 3) - (b.index() >> 3));
        return df > dr ? df : dr;
    }

    [[nodiscard]] static int value_distance(Square a, Square b) noexcept { return std::abs(a.index() - b.index()); }

    [[nodiscard]] static constexpr bool same_color(Square a, Square b) noexcept {
        return ((9 * (a ^ b).index()) & 8) == 0;
    }

    [[nodiscard]] static constexpr bool back_rank(Square s, Color c) noexcept {
        return (s.index() >> 3) == static_cast<int>(c) * 7;
    }

    constexpr Square& flip() noexcept {
        sq = static_cast<underlying>(index() ^ 56);
        return *this;
    }

    [[nodiscard]] constexpr Square relative_square(Color c) const noexcept {
        return Square(index() ^ (static_cast<int>(c) * 56));
    }

    [[nodiscard]] constexpr int diagonal_of() const noexcept { return 7 + (index() >> 3) - (index() & 7); }
    [[nodiscard]] constexpr int antidiagonal_of() const noexcept { return (index() >> 3) + (index() & 7); }

    [[nodiscard]] constexpr Square ep_square() const noexcept {
        assert(rank() == Rank::RANK_3 || rank() == Rank::RANK_4 || rank() == Rank::RANK_5 ||
               rank() == Rank::RANK_6);
        return Square(index() ^ 8);
    }

    [[nodiscard]] static constexpr Square castling_king_square(bool is_king_side, Color c) noexcept {
        return Square(is_king_side ? 6 : 2).relative_square(c);
    }

    [[nodiscard]] static constexpr Square castling_rook_square(bool is_king_side, Color c) noexcept {
        return Square(is_king_side ? 5 : 3).relative_square(c);
    }

    [[nodiscard]] static constexpr int max() noexcept { return 64; }

    [[nodiscard]] static bool is_valid_string_sq(std::string_view str) noexcept {
        return str.size() == 2 && str[0] >= 'a' && str[0] <= 'h' && str[1] >= '1' && str[1] <= '8';
    }

    [[nodiscard]] static constexpr bool is_valid_sq(int s) noexcept { return s >= 0 && s < 64; }

   private:
    underlying sq;
};

#undef BK_SQ_RANK

inline std::ostream& operator<<(std::ostream& os, const Square& sq) { return os << static_cast<std::string>(sq); }

enum class Direction : std::int8_t {
    NORTH      = 8,
    WEST       = -1,
    SOUTH      = -8,
    EAST       = 1,
    NORTH_EAST = 9,
    NORTH_WEST = 7,
    SOUTH_WEST = -9,
    SOUTH_EAST = -7
};

[[nodiscard]] constexpr Direction make_direction(Direction dir, Color c) noexcept {
    return c == Color::BLACK ? static_cast<Direction>(-static_cast<std::int8_t>(dir)) : dir;
}

constexpr Square operator+(Square sq, Direction dir) {
    return static_cast<Square>(sq.index() + static_cast<std::int8_t>(dir));
}

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  Bitboard
// ---------------------------------------------------------------------------------------------
namespace chess {

class Bitboard {
   public:
    constexpr Bitboard() : bits(0) {}
    constexpr Bitboard(std::uint64_t b) : bits(b) {}
    constexpr Bitboard(File file) : bits(0x0101010101010101ULL << static_cast<int>(file.internal())) {
        assert(file != File::NO_FILE);
    }
    constexpr Bitboard(Rank rank) : bits(0xFFULL << (8 * static_cast<int>(rank.internal()))) {
        assert(rank != Rank::NO_RANK);
    }

    constexpr explicit operator bool() const noexcept { return bits != 0; }

    explicit operator std::string() const {
        std::bitset<64> b(bits);
        std::string full = b.to_string();
        std::string out;
        for (int i = 0; i < 64; i += 8) {
            std::string row = full.substr(i, 8);
            std::reverse(row.begin(), row.end());
            out += row + '\n';
        }
        return out;
    }

    constexpr Bitboard operator&&(bool rhs) const noexcept { return Bitboard(bits && rhs); }

    constexpr Bitboard operator&(std::uint64_t rhs) const noexcept { return Bitboard(bits & rhs); }
    constexpr Bitboard operator|(std::uint64_t rhs) const noexcept { return Bitboard(bits | rhs); }
    constexpr Bitboard operator^(std::uint64_t rhs) const noexcept { return Bitboard(bits ^ rhs); }
    constexpr Bitboard operator<<(std::uint64_t rhs) const noexcept { return Bitboard(bits << rhs); }
    constexpr Bitboard operator>>(std::uint64_t rhs) const noexcept { return Bitboard(bits >> rhs); }
    constexpr bool operator==(std::uint64_t rhs) const noexcept { return bits == rhs; }
    constexpr bool operator!=(std::uint64_t rhs) const noexcept { return bits != rhs; }

    constexpr Bitboard operator&(const Bitboard& rhs) const noexcept { return Bitboard(bits & rhs.bits); }
    constexpr Bitboard operator|(const Bitboard& rhs) const noexcept { return Bitboard(bits | rhs.bits); }
    constexpr Bitboard operator^(const Bitboard& rhs) const noexcept { return Bitboard(bits ^ rhs.bits); }
    constexpr Bitboard operator~() const noexcept { return Bitboard(~bits); }

    constexpr Bitboard& operator&=(const Bitboard& rhs) noexcept { bits &= rhs.bits; return *this; }
    constexpr Bitboard& operator|=(const Bitboard& rhs) noexcept { bits |= rhs.bits; return *this; }
    constexpr Bitboard& operator^=(const Bitboard& rhs) noexcept { bits ^= rhs.bits; return *this; }

    constexpr bool operator==(const Bitboard& rhs) const noexcept { return bits == rhs.bits; }
    constexpr bool operator!=(const Bitboard& rhs) const noexcept { return bits != rhs.bits; }
    constexpr bool operator||(const Bitboard& rhs) const noexcept { return bits || rhs.bits; }
    constexpr bool operator&&(const Bitboard& rhs) const noexcept { return bits && rhs.bits; }

    constexpr Bitboard& set(int index) noexcept {
        assert(index >= 0 && index < 64);
        bits |= (1ULL << index);
        return *this;
    }

    [[nodiscard]] constexpr bool check(int index) const noexcept {
        assert(index >= 0 && index < 64);
        return (bits >> index) & 1ULL;
    }

    constexpr Bitboard& clear(int index) noexcept {
        assert(index >= 0 && index < 64);
        bits &= ~(1ULL << index);
        return *this;
    }

    constexpr Bitboard& clear() noexcept { bits = 0; return *this; }

    [[nodiscard]] static constexpr Bitboard fromSquare(int index) noexcept {
        assert(index >= 0 && index < 64);
        return Bitboard(1ULL << index);
    }
    [[nodiscard]] static constexpr Bitboard fromSquare(Square sq) noexcept {
        assert(sq.index() >= 0 && sq.index() < 64);
        return Bitboard(1ULL << sq.index());
    }

    [[nodiscard]] constexpr bool empty() const noexcept { return bits == 0; }

    [[nodiscard]] BK_CONSTEXPR_BITS int lsb() const noexcept {
        assert(bits != 0);
        return bk::ctz64(bits);
    }
    [[nodiscard]] BK_CONSTEXPR_BITS int msb() const noexcept {
        assert(bits != 0);
        return 63 ^ bk::clz64(bits);
    }
    [[nodiscard]] BK_CONSTEXPR_BITS int count() const noexcept { return bk::pop64(bits); }

    [[nodiscard]] BK_CONSTEXPR_BITS std::uint8_t pop() noexcept {
        assert(bits != 0);
        const int i = bk::ctz64(bits);
        bits &= bits - 1;
        return static_cast<std::uint8_t>(i);
    }

    [[nodiscard]] constexpr std::uint64_t getBits() const noexcept { return bits; }

    friend std::ostream& operator<<(std::ostream& os, const Bitboard& bb);

   private:
    std::uint64_t bits;
};

inline std::ostream& operator<<(std::ostream& os, const Bitboard& bb) { return os << std::string(bb); }

constexpr Bitboard operator&(std::uint64_t lhs, const Bitboard& rhs) { return rhs & lhs; }
constexpr Bitboard operator|(std::uint64_t lhs, const Bitboard& rhs) { return rhs | lhs; }

class Board;

// ---------------------------------------------------------------------------------------------
//  PieceType / Piece
// ---------------------------------------------------------------------------------------------

class PieceType {
   public:
    enum class underlying : std::uint8_t { PAWN, KNIGHT, BISHOP, ROOK, QUEEN, KING, NONE };

    constexpr PieceType() : pt(underlying::NONE) {}
    constexpr PieceType(underlying p) : pt(p) {}
    constexpr explicit PieceType(std::string_view type) : pt(underlying::NONE) {
        assert(type.size() > 0);
        switch (type[0]) {
            case 'P': case 'p': pt = underlying::PAWN; break;
            case 'N': case 'n': pt = underlying::KNIGHT; break;
            case 'B': case 'b': pt = underlying::BISHOP; break;
            case 'R': case 'r': pt = underlying::ROOK; break;
            case 'Q': case 'q': pt = underlying::QUEEN; break;
            case 'K': case 'k': pt = underlying::KING; break;
            default: pt = underlying::NONE; break;
        }
    }

    explicit operator std::string() const {
        constexpr const char* names[] = {"p", "n", "b", "r", "q", "k"};
        return pt == underlying::NONE ? " " : names[static_cast<int>(pt)];
    }

    constexpr bool operator==(const PieceType& rhs) const noexcept { return pt == rhs.pt; }
    constexpr bool operator!=(const PieceType& rhs) const noexcept { return pt != rhs.pt; }

    constexpr operator int() const noexcept { return static_cast<int>(pt); }

    [[nodiscard]] constexpr underlying internal() const noexcept { return pt; }

    static constexpr underlying PAWN   = underlying::PAWN;
    static constexpr underlying KNIGHT = underlying::KNIGHT;
    static constexpr underlying BISHOP = underlying::BISHOP;
    static constexpr underlying ROOK   = underlying::ROOK;
    static constexpr underlying QUEEN  = underlying::QUEEN;
    static constexpr underlying KING   = underlying::KING;
    static constexpr underlying NONE   = underlying::NONE;

   private:
    underlying pt;
};

inline std::ostream& operator<<(std::ostream& os, const PieceType& pt) { return os << static_cast<std::string>(pt); }

namespace bk {
// piece index (0..12, 12 == NONE) -> type / colour
inline constexpr std::uint8_t PIECE_TYPE[13] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6};
inline constexpr std::int8_t PIECE_COLOR[13] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, -1};
}  // namespace bk

class Piece {
   public:
    enum class underlying : std::uint8_t {
        WHITEPAWN, WHITEKNIGHT, WHITEBISHOP, WHITEROOK, WHITEQUEEN, WHITEKING,
        BLACKPAWN, BLACKKNIGHT, BLACKBISHOP, BLACKROOK, BLACKQUEEN, BLACKKING,
        NONE
    };

    constexpr Piece() : piece(underlying::NONE) {}
    constexpr Piece(underlying p) : piece(p) {}
    constexpr Piece(PieceType type, Color color)
        : piece((color == Color::NONE || type == PieceType::NONE)
                    ? underlying::NONE
                    : static_cast<underlying>(static_cast<int>(color.internal()) * 6 + static_cast<int>(type))) {}
    constexpr Piece(Color color, PieceType type) : Piece(type, color) {}
    constexpr Piece(std::string_view p) : piece(fromChar(p[0])) {}

    constexpr bool operator<(const Piece& rhs) const noexcept { return piece < rhs.piece; }
    constexpr bool operator>(const Piece& rhs) const noexcept { return piece > rhs.piece; }
    constexpr bool operator==(const Piece& rhs) const noexcept { return piece == rhs.piece; }
    constexpr bool operator!=(const Piece& rhs) const noexcept { return piece != rhs.piece; }

    constexpr bool operator==(const underlying& rhs) const noexcept { return piece == rhs; }
    constexpr bool operator!=(const underlying& rhs) const noexcept { return piece != rhs; }

    constexpr bool operator==(const PieceType& rhs) const noexcept { return type() == rhs; }
    constexpr bool operator!=(const PieceType& rhs) const noexcept { return type() != rhs; }

    explicit operator std::string() const {
        constexpr const char* names[] = {"P", "N", "B", "R", "Q", "K", "p", "n", "b", "r", "q", "k"};
        return piece == underlying::NONE ? "." : names[static_cast<int>(piece)];
    }

    constexpr operator int() const noexcept { return static_cast<int>(piece); }

    [[nodiscard]] constexpr PieceType type() const noexcept {
        return static_cast<PieceType::underlying>(bk::PIECE_TYPE[static_cast<int>(piece)]);
    }

    [[nodiscard]] constexpr Color color() const noexcept {
        return Color(static_cast<int>(bk::PIECE_COLOR[static_cast<int>(piece)]));
    }

    [[nodiscard]] constexpr underlying internal() const noexcept { return piece; }

    static constexpr underlying NONE        = underlying::NONE;
    static constexpr underlying WHITEPAWN   = underlying::WHITEPAWN;
    static constexpr underlying WHITEKNIGHT = underlying::WHITEKNIGHT;
    static constexpr underlying WHITEBISHOP = underlying::WHITEBISHOP;
    static constexpr underlying WHITEROOK   = underlying::WHITEROOK;
    static constexpr underlying WHITEQUEEN  = underlying::WHITEQUEEN;
    static constexpr underlying WHITEKING   = underlying::WHITEKING;
    static constexpr underlying BLACKPAWN   = underlying::BLACKPAWN;
    static constexpr underlying BLACKKNIGHT = underlying::BLACKKNIGHT;
    static constexpr underlying BLACKBISHOP = underlying::BLACKBISHOP;
    static constexpr underlying BLACKROOK   = underlying::BLACKROOK;
    static constexpr underlying BLACKQUEEN  = underlying::BLACKQUEEN;
    static constexpr underlying BLACKKING   = underlying::BLACKKING;

   private:
    underlying piece;

    [[nodiscard]] static constexpr underlying fromChar(char c) {
        constexpr char order[] = "PNBRQKpnbrqk";
        for (int i = 0; i < 12; ++i)
            if (order[i] == c) return static_cast<underlying>(i);
        return underlying::NONE;
    }
};

// ---------------------------------------------------------------------------------------------
//  constants / Move / Movelist
// ---------------------------------------------------------------------------------------------

namespace constants {
constexpr Bitboard DEFAULT_CHECKMASK = Bitboard(0xFFFFFFFFFFFFFFFFull);
constexpr auto STARTPOS              = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr auto MAX_MOVES             = 256;
}  // namespace constants

class Move {
   public:
    Move() = default;
    constexpr Move(std::uint16_t move) : move_(move), score_(0) {}

    template <std::uint16_t MoveType = 0>
    [[nodiscard]] static constexpr Move make(Square source, Square target, PieceType pt = PieceType::KNIGHT) noexcept {
        assert(pt >= PieceType(PieceType::KNIGHT) && pt <= PieceType(PieceType::QUEEN));
        return Move(static_cast<std::uint16_t>(MoveType | ((static_cast<int>(pt) - 1) << 12) |
                                               (source.index() << 6) | target.index()));
    }

    [[nodiscard]] constexpr Square from() const noexcept { return static_cast<Square>((move_ >> 6) & 0x3F); }
    [[nodiscard]] constexpr Square to() const noexcept { return static_cast<Square>(move_ & 0x3F); }

    [[nodiscard]] constexpr std::uint16_t typeOf() const noexcept {
        return static_cast<std::uint16_t>(move_ & (3 << 14));
    }

    [[nodiscard]] constexpr PieceType promotionType() const noexcept {
        return static_cast<PieceType::underlying>(((move_ >> 12) & 3) + 1);
    }

    constexpr void setScore(std::int16_t score) noexcept { score_ = score; }

    [[nodiscard]] constexpr std::uint16_t move() const noexcept { return move_; }
    [[nodiscard]] constexpr std::int16_t score() const noexcept { return score_; }

    constexpr bool operator==(const Move& rhs) const noexcept { return move_ == rhs.move_; }
    constexpr bool operator!=(const Move& rhs) const noexcept { return move_ != rhs.move_; }

    static constexpr std::uint16_t NO_MOVE   = 0;
    static constexpr std::uint16_t NULL_MOVE = 65;
    static constexpr std::uint16_t NORMAL    = 0;
    static constexpr std::uint16_t PROMOTION = 1 << 14;
    static constexpr std::uint16_t ENPASSANT = 2 << 14;
    static constexpr std::uint16_t CASTLING  = 3 << 14;

   private:
    std::uint16_t move_;
    std::int16_t score_;
};

class movegen;

class Movelist {
   public:
    using value_type      = Move;
    using size_type       = int;
    using difference_type = std::ptrdiff_t;
    using reference       = value_type&;
    using const_reference = const value_type&;
    using pointer         = value_type*;
    using const_pointer   = const value_type*;

    using iterator       = value_type*;
    using const_iterator = const value_type*;

    using reverse_iterator       = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    Movelist() = default;

    Movelist(const Movelist& other) : size_(other.size_) {
        std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(Move));
    }

    Movelist& operator=(const Movelist& other) {
        if (this != &other) {
            size_ = other.size_;
            std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(Move));
        }
        return *this;
    }

    Movelist(Movelist&& other) noexcept : size_(other.size_) {
        std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(Move));
        other.size_ = 0;
    }

    Movelist& operator=(Movelist&& other) noexcept {
        if (this != &other) {
            size_ = other.size_;
            std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(Move));
            other.size_ = 0;
        }
        return *this;
    }

    [[nodiscard]] constexpr reference at(size_type pos) {
#ifndef CHESS_NO_EXCEPTIONS
        if (pos >= size_)
            throw std::out_of_range("Movelist::at: pos (which is " + std::to_string(pos) + ") >= size (which is " +
                                    std::to_string(size_) + ")");
#endif
        return moves_[pos];
    }

    [[nodiscard]] constexpr const_reference at(size_type pos) const {
#ifndef CHESS_NO_EXCEPTIONS
        if (pos >= size_)
            throw std::out_of_range("Movelist::at: pos (which is " + std::to_string(pos) + ") >= size (which is " +
                                    std::to_string(size_) + ")");
#endif
        return moves_[pos];
    }

    [[nodiscard]] constexpr reference operator[](size_type pos) noexcept { return moves_[pos]; }
    [[nodiscard]] constexpr const_reference operator[](size_type pos) const noexcept { return moves_[pos]; }

    [[nodiscard]] constexpr reference front() noexcept { return moves_[0]; }
    [[nodiscard]] constexpr const_reference front() const noexcept { return moves_[0]; }
    [[nodiscard]] constexpr reference back() noexcept { return moves_[size_ - 1]; }
    [[nodiscard]] constexpr const_reference back() const noexcept { return moves_[size_ - 1]; }

    [[nodiscard]] constexpr iterator begin() noexcept { return moves_.data(); }
    [[nodiscard]] constexpr const_iterator begin() const noexcept { return moves_.data(); }
    [[nodiscard]] constexpr iterator end() noexcept { return moves_.data() + size_; }
    [[nodiscard]] constexpr const_iterator end() const noexcept { return moves_.data() + size_; }

    [[nodiscard]] constexpr bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] constexpr size_type size() const noexcept { return size_; }

    constexpr void clear() noexcept { size_ = 0; }

    constexpr void add(const_reference move) noexcept {
        assert(size_ < constants::MAX_MOVES);
        moves_[size_++] = move;
    }
    constexpr void add(value_type&& move) noexcept {
        assert(size_ < constants::MAX_MOVES);
        moves_[size_++] = move;
    }

    [[nodiscard]] [[deprecated("Use std::find() instead.")]] constexpr size_type find(value_type move) const noexcept {
        for (size_type i = 0; i < size_; ++i)
            if (moves_[i] == move) return i;
        return -1;
    }

   private:
    std::array<value_type, constants::MAX_MOVES> moves_;
    size_type size_ = 0;

    friend class movegen;
};

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  compile time tables
// ---------------------------------------------------------------------------------------------
namespace chess::bk {

using U64 = std::uint64_t;

inline constexpr U64 FILE_A_BB = 0x0101010101010101ULL;
inline constexpr U64 FILE_H_BB = FILE_A_BB << 7;

// Reference ray walker: attacks of a rook (true) / bishop (false) on an arbitrary occupancy.
constexpr U64 slide(int sq, U64 occ, bool rook) {
    constexpr int dr[2][4] = {{1, 1, -1, -1}, {1, -1, 0, 0}};
    constexpr int df[2][4] = {{1, -1, 1, -1}, {0, 0, 1, -1}};
    U64 out = 0;
    for (int d = 0; d < 4; ++d) {
        int r = (sq >> 3) + dr[rook][d];
        int f = (sq & 7) + df[rook][d];
        while (r >= 0 && r < 8 && f >= 0 && f < 8) {
            const U64 b = 1ULL << (r * 8 + f);
            out |= b;
            if (occ & b) break;
            r += dr[rook][d];
            f += df[rook][d];
        }
    }
    return out;
}

struct StaticTables {
    U64 knight[64];
    U64 king[64];
    U64 pawn[2][64];
    U64 pseudoRook[64];
    U64 pseudoBishop[64];
    U64 between[64][64];  // squares strictly between a and b, PLUS b itself (b alone if not aligned)
};

constexpr StaticTables makeStaticTables() {
    StaticTables t{};
    constexpr int kn[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    constexpr int kg[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    for (int s = 0; s < 64; ++s) {
        const int r = s >> 3, f = s & 7;
        for (int i = 0; i < 8; ++i) {
            int rr = r + kn[i][0], ff = f + kn[i][1];
            if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) t.knight[s] |= 1ULL << (rr * 8 + ff);
            rr = r + kg[i][0];
            ff = f + kg[i][1];
            if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) t.king[s] |= 1ULL << (rr * 8 + ff);
        }
        for (int df = -1; df <= 1; df += 2) {
            if (f + df < 0 || f + df > 7) continue;
            if (r < 7) t.pawn[0][s] |= 1ULL << ((r + 1) * 8 + f + df);
            if (r > 0) t.pawn[1][s] |= 1ULL << ((r - 1) * 8 + f + df);
        }
        t.pseudoRook[s]   = slide(s, 0, true);
        t.pseudoBishop[s] = slide(s, 0, false);
    }
    for (int a = 0; a < 64; ++a) {
        for (int b = 0; b < 64; ++b) t.between[a][b] = 1ULL << b;
        for (int d = 0; d < 8; ++d) {
            constexpr int dr[8] = {1, 1, 1, 0, -1, -1, -1, 0};
            constexpr int df[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
            int r = (a >> 3) + dr[d], f = (a & 7) + df[d];
            U64 ray = 0;
            while (r >= 0 && r < 8 && f >= 0 && f < 8) {
                const int b = r * 8 + f;
                ray |= 1ULL << b;
                t.between[a][b] = ray;
                r += dr[d];
                f += df[d];
            }
        }
    }
    return t;
}

inline constexpr StaticTables ST = makeStaticTables();

}  // namespace chess::bk

// ---------------------------------------------------------------------------------------------
//  attacks
// ---------------------------------------------------------------------------------------------
namespace chess {

class attacks {
    using U64 = std::uint64_t;

#ifdef CHESS_USE_PEXT
    struct alignas(16) Magic {
        U64 mask;
        Bitboard* table;
        BK_INLINE std::size_t index(U64 occ) const noexcept { return _pext_u64(occ, mask); }
    };
#else
    struct alignas(32) Magic {
        U64 mask;
        U64 magic;
        Bitboard* table;
        unsigned shift;
        BK_INLINE std::size_t index(U64 occ) const noexcept {
            return static_cast<std::size_t>(((occ & mask) * magic) >> shift);
        }
    };
#endif

    // magic multipliers found with an independent search (xorshift* seeded, sparse candidates)
    static constexpr U64 BishopMagics[64] = {
0x9004101001003080ULL, 0x0802880101060400ULL, 0x0110040088200008ULL, 0x40022a0200010100ULL,
0x0814042202000220ULL, 0x4000821041100200ULL, 0x0b02011082105000ULL, 0x1202220850080802ULL,
0x0080049002020400ULL, 0x8801209614005088ULL, 0x0080081204003000ULL, 0x0541040408800000ULL,
0x0004a2021000c008ULL, 0x0080289044200600ULL, 0x0800040101b01002ULL, 0x0106003401045000ULL,
0x080400081004080cULL, 0x002080480200aa00ULL, 0x8202001000801100ULL, 0x8824020601220040ULL,
0x800a006412020020ULL, 0x0080801808010890ULL, 0x0004000062021000ULL, 0x4000449201009884ULL,
0x8420850020a42406ULL, 0x0028080002104100ULL, 0x0008320110108200ULL, 0x0881040008440080ULL,
0x0001001009004000ULL, 0x00180c4002011080ULL, 0x04080a1200422200ULL, 0x00008e0000250401ULL,
0x1018829004404401ULL, 0x0044440420021000ULL, 0x0000241000010102ULL, 0x0000400a00012200ULL,
0x1060020280040048ULL, 0x084a100040420808ULL, 0x4004410400820081ULL, 0x988200a101002c00ULL,
0x1400842049002180ULL, 0x0482021002000404ULL, 0x0006001404028200ULL, 0x0204204010484200ULL,
0x2000020822005010ULL, 0x4002140818085c80ULL, 0x0088084120400c02ULL, 0x8042241418248880ULL,
0x0180880802100120ULL, 0x0802009201102015ULL, 0x00002020a4100440ULL, 0x060a800210440880ULL,
0x00000a0670440001ULL, 0xc482040408820008ULL, 0x8004281024048002ULL, 0x40081820c0820214ULL,
0x0000420210220200ULL, 0xc144028208010480ULL, 0x8800140044044400ULL, 0x0050044003420204ULL,
0x0004200004904401ULL, 0x0102850410820200ULL, 0x1400c42002121605ULL, 0xc422080108060048ULL,
    };

    static constexpr U64 RookMagics[64] = {
0x4080001020400081ULL, 0xc140100020004000ULL, 0x0200104008220080ULL, 0x028028008030000cULL,
0x0600100200040860ULL, 0x03000a0804000100ULL, 0x0400080914029042ULL, 0x2080030002205480ULL,
0x0820800040082080ULL, 0x4002400020005000ULL, 0x0000801000200080ULL, 0x4040801000800800ULL,
0x1202800400800802ULL, 0x0005000804010002ULL, 0x2204002102901834ULL, 0x08010021000c4982ULL,
0x0400808000400028ULL, 0x0200250040008100ULL, 0x0106110020064100ULL, 0x0001010020081004ULL,
0x0800808004000801ULL, 0x4009010008020400ULL, 0x1200040032100148ULL, 0x0080020004008041ULL,
0x0000400280008020ULL, 0x0001008200402204ULL, 0x0000200100104104ULL, 0x2120100100082500ULL,
0xa008080080040080ULL, 0xc010040080020080ULL, 0x0020280c00120950ULL, 0x0040802880004900ULL,
0x4000400024800080ULL, 0x0001400484802008ULL, 0x2150040800200020ULL, 0x1008100081804800ULL,
0x011a800400800800ULL, 0x1840800400800200ULL, 0x0408221004000188ULL, 0x800080a042000104ULL,
0x0440008040208008ULL, 0x0018d0082000c000ULL, 0x0000200010008080ULL, 0x0088002010010100ULL,
0x208a0090200a0004ULL, 0x0106400410080120ULL, 0x2002220801440070ULL, 0x0008010080420004ULL,
0x0140004080003080ULL, 0x0040008040200080ULL, 0x4800200010008480ULL, 0x2020100008210100ULL,
0x0610280080040180ULL, 0x0860020004008080ULL, 0x0006003401884200ULL, 0x4081003203428100ULL,
0x1280001441002881ULL, 0x04258011020228c2ULL, 0xd09080104222000aULL, 0x4000201000050009ULL,
0x0002001008052002ULL, 0x1001000208040001ULL, 0x0082080210008144ULL, 0x800ac08104004032ULL,
    };

    static inline Bitboard RookAttacks[0x19000]  = {};
    static inline Bitboard BishopAttacks[0x1480] = {};
    static inline Magic RookTable[64]            = {};
    static inline Magic BishopTable[64]          = {};
    static inline bool initialised_              = false;

    template <bool ISROOK>
    static void buildSlider(Magic* tables, Bitboard* storage, const U64* magics) noexcept {
        Bitboard* cursor = storage;
        for (int sq = 0; sq < 64; ++sq) {
            const U64 edges = (((0xFFULL | (0xFFULL << 56)) & ~(0xFFULL << (8 * (sq >> 3)))) |
                               ((bk::FILE_A_BB | bk::FILE_H_BB) & ~(bk::FILE_A_BB << (sq & 7))));
            Magic& m   = tables[sq];
            m.mask     = bk::slide(sq, 0, ISROOK) & ~edges;
            m.table    = cursor;
#ifndef CHESS_USE_PEXT
            m.magic = magics[sq];
            m.shift = static_cast<unsigned>(64 - bk::pop64(m.mask));
#else
            (void)magics;
#endif
            U64 occ = 0;
            do {
                m.table[m.index(occ)] = Bitboard(bk::slide(sq, occ, ISROOK));
                occ                   = (occ - m.mask) & m.mask;
            } while (occ);
            cursor += (1ULL << bk::pop64(m.mask));
        }
    }

   public:
    static constexpr Bitboard MASK_RANK[8] = {0xff,         0xff00,         0xff0000,         0xff000000,
                                              0xff00000000, 0xff0000000000, 0xff000000000000, 0xff00000000000000};

    static constexpr Bitboard MASK_FILE[8] = {
        0x101010101010101,  0x202020202020202,  0x404040404040404,  0x808080808080808,
        0x1010101010101010, 0x2020202020202020, 0x4040404040404040, 0x8080808080808080,
    };

    template <Direction direction>
    [[nodiscard]] static constexpr Bitboard shift(const Bitboard b) {
        switch (direction) {
            case Direction::NORTH: return b << 8;
            case Direction::SOUTH: return b >> 8;
            case Direction::NORTH_WEST: return (b & ~bk::FILE_A_BB) << 7;
            case Direction::WEST: return (b & ~bk::FILE_A_BB) >> 1;
            case Direction::SOUTH_WEST: return (b & ~bk::FILE_A_BB) >> 9;
            case Direction::NORTH_EAST: return (b & ~bk::FILE_H_BB) << 9;
            case Direction::EAST: return (b & ~bk::FILE_H_BB) << 1;
            case Direction::SOUTH_EAST: return (b & ~bk::FILE_H_BB) >> 7;
        }
        return {};
    }

    template <Color::underlying c>
    [[nodiscard]] static BK_INLINE Bitboard pawnLeftAttacks(const Bitboard pawns) {
        return c == Color::WHITE ? (pawns << 7) & ~bk::FILE_H_BB : (pawns >> 7) & ~bk::FILE_A_BB;
    }

    template <Color::underlying c>
    [[nodiscard]] static BK_INLINE Bitboard pawnRightAttacks(const Bitboard pawns) {
        return c == Color::WHITE ? (pawns << 9) & ~bk::FILE_A_BB : (pawns >> 9) & ~bk::FILE_H_BB;
    }

    [[nodiscard]] static BK_INLINE Bitboard pawn(Color c, Square sq) noexcept {
        return Bitboard(bk::ST.pawn[static_cast<int>(c)][sq.index()]);
    }
    [[nodiscard]] static BK_INLINE Bitboard knight(Square sq) noexcept { return Bitboard(bk::ST.knight[sq.index()]); }
    [[nodiscard]] static BK_INLINE Bitboard king(Square sq) noexcept { return Bitboard(bk::ST.king[sq.index()]); }

    [[nodiscard]] static BK_INLINE Bitboard bishop(Square sq, Bitboard occupied) noexcept {
        const Magic& m = BishopTable[sq.index()];
        return m.table[m.index(occupied.getBits())];
    }
    [[nodiscard]] static BK_INLINE Bitboard rook(Square sq, Bitboard occupied) noexcept {
        const Magic& m = RookTable[sq.index()];
        return m.table[m.index(occupied.getBits())];
    }
    [[nodiscard]] static BK_INLINE Bitboard queen(Square sq, Bitboard occupied) noexcept {
        return bishop(sq, occupied) | rook(sq, occupied);
    }

    [[nodiscard]] static Bitboard attackers(const Board& board, Color color, Square square) noexcept;

    template <PieceType::underlying pt>
    [[nodiscard]] static BK_INLINE Bitboard slider(Square sq, Bitboard occupied) noexcept {
        static_assert(pt == PieceType::BISHOP || pt == PieceType::ROOK || pt == PieceType::QUEEN,
                      "PieceType must be a slider!");
        if constexpr (pt == PieceType::BISHOP) return bishop(sq, occupied);
        if constexpr (pt == PieceType::ROOK) return rook(sq, occupied);
        if constexpr (pt == PieceType::QUEEN) return queen(sq, occupied);
    }

    /// [Internal] builds the slider tables. Idempotent; runs automatically before main().
    static inline void initAttacks() {
        if (initialised_) return;
        buildSlider<false>(BishopTable, BishopAttacks, BishopMagics);
        buildSlider<true>(RookTable, RookAttacks, RookMagics);
        initialised_ = true;
    }
};

namespace bk {
// Runs attacks::initAttacks() during static initialisation of any TU that includes this header.
inline const bool g_attacks_ready = (attacks::initAttacks(), true);
}  // namespace bk

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  Zobrist (Polyglot compatible keys)
// ---------------------------------------------------------------------------------------------
namespace chess {

class Zobrist {
    using U64 = std::uint64_t;

    // The standard Polyglot random array (781 entries): 768 piece/square, 4 castling, 8 ep file, 1 side.
    static constexpr U64 RANDOM_ARRAY[781] = {
        0x9D39247E33776D41ULL, 0x2AF7398005AAA5C7ULL, 0x44DB015024623547ULL, 0x9C15F73E62A76AE2ULL,
        0x75834465489C0C89ULL, 0x3290AC3A203001BFULL, 0x0FBBAD1F61042279ULL, 0xE83A908FF2FB60CAULL,
        0x0D7E765D58755C10ULL, 0x1A083822CEAFE02DULL, 0x9605D5F0E25EC3B0ULL, 0xD021FF5CD13A2ED5ULL,
        0x40BDF15D4A672E32ULL, 0x011355146FD56395ULL, 0x5DB4832046F3D9E5ULL, 0x239F8B2D7FF719CCULL,
        0x05D1A1AE85B49AA1ULL, 0x679F848F6E8FC971ULL, 0x7449BBFF801FED0BULL, 0x7D11CDB1C3B7ADF0ULL,
        0x82C7709E781EB7CCULL, 0xF3218F1C9510786CULL, 0x331478F3AF51BBE6ULL, 0x4BB38DE5E7219443ULL,
        0xAA649C6EBCFD50FCULL, 0x8DBD98A352AFD40BULL, 0x87D2074B81D79217ULL, 0x19F3C751D3E92AE1ULL,
        0xB4AB30F062B19ABFULL, 0x7B0500AC42047AC4ULL, 0xC9452CA81A09D85DULL, 0x24AA6C514DA27500ULL,
        0x4C9F34427501B447ULL, 0x14A68FD73C910841ULL, 0xA71B9B83461CBD93ULL, 0x03488B95B0F1850FULL,
        0x637B2B34FF93C040ULL, 0x09D1BC9A3DD90A94ULL, 0x3575668334A1DD3BULL, 0x735E2B97A4C45A23ULL,
        0x18727070F1BD400BULL, 0x1FCBACD259BF02E7ULL, 0xD310A7C2CE9B6555ULL, 0xBF983FE0FE5D8244ULL,
        0x9F74D14F7454A824ULL, 0x51EBDC4AB9BA3035ULL, 0x5C82C505DB9AB0FAULL, 0xFCF7FE8A3430B241ULL,
        0x3253A729B9BA3DDEULL, 0x8C74C368081B3075ULL, 0xB9BC6C87167C33E7ULL, 0x7EF48F2B83024E20ULL,
        0x11D505D4C351BD7FULL, 0x6568FCA92C76A243ULL, 0x4DE0B0F40F32A7B8ULL, 0x96D693460CC37E5DULL,
        0x42E240CB63689F2FULL, 0x6D2BDCDAE2919661ULL, 0x42880B0236E4D951ULL, 0x5F0F4A5898171BB6ULL,
        0x39F890F579F92F88ULL, 0x93C5B5F47356388BULL, 0x63DC359D8D231B78ULL, 0xEC16CA8AEA98AD76ULL,
        0x5355F900C2A82DC7ULL, 0x07FB9F855A997142ULL, 0x5093417AA8A7ED5EULL, 0x7BCBC38DA25A7F3CULL,
        0x19FC8A768CF4B6D4ULL, 0x637A7780DECFC0D9ULL, 0x8249A47AEE0E41F7ULL, 0x79AD695501E7D1E8ULL,
        0x14ACBAF4777D5776ULL, 0xF145B6BECCDEA195ULL, 0xDABF2AC8201752FCULL, 0x24C3C94DF9C8D3F6ULL,
        0xBB6E2924F03912EAULL, 0x0CE26C0B95C980D9ULL, 0xA49CD132BFBF7CC4ULL, 0xE99D662AF4243939ULL,
        0x27E6AD7891165C3FULL, 0x8535F040B9744FF1ULL, 0x54B3F4FA5F40D873ULL, 0x72B12C32127FED2BULL,
        0xEE954D3C7B411F47ULL, 0x9A85AC909A24EAA1ULL, 0x70AC4CD9F04F21F5ULL, 0xF9B89D3E99A075C2ULL,
        0x87B3E2B2B5C907B1ULL, 0xA366E5B8C54F48B8ULL, 0xAE4A9346CC3F7CF2ULL, 0x1920C04D47267BBDULL,
        0x87BF02C6B49E2AE9ULL, 0x092237AC237F3859ULL, 0xFF07F64EF8ED14D0ULL, 0x8DE8DCA9F03CC54EULL,
        0x9C1633264DB49C89ULL, 0xB3F22C3D0B0B38EDULL, 0x390E5FB44D01144BULL, 0x5BFEA5B4712768E9ULL,
        0x1E1032911FA78984ULL, 0x9A74ACB964E78CB3ULL, 0x4F80F7A035DAFB04ULL, 0x6304D09A0B3738C4ULL,
        0x2171E64683023A08ULL, 0x5B9B63EB9CEFF80CULL, 0x506AACF489889342ULL, 0x1881AFC9A3A701D6ULL,
        0x6503080440750644ULL, 0xDFD395339CDBF4A7ULL, 0xEF927DBCF00C20F2ULL, 0x7B32F7D1E03680ECULL,
        0xB9FD7620E7316243ULL, 0x05A7E8A57DB91B77ULL, 0xB5889C6E15630A75ULL, 0x4A750A09CE9573F7ULL,
        0xCF464CEC899A2F8AULL, 0xF538639CE705B824ULL, 0x3C79A0FF5580EF7FULL, 0xEDE6C87F8477609DULL,
        0x799E81F05BC93F31ULL, 0x86536B8CF3428A8CULL, 0x97D7374C60087B73ULL, 0xA246637CFF328532ULL,
        0x043FCAE60CC0EBA0ULL, 0x920E449535DD359EULL, 0x70EB093B15B290CCULL, 0x73A1921916591CBDULL,
        0x56436C9FE1A1AA8DULL, 0xEFAC4B70633B8F81ULL, 0xBB215798D45DF7AFULL, 0x45F20042F24F1768ULL,
        0x930F80F4E8EB7462ULL, 0xFF6712FFCFD75EA1ULL, 0xAE623FD67468AA70ULL, 0xDD2C5BC84BC8D8FCULL,
        0x7EED120D54CF2DD9ULL, 0x22FE545401165F1CULL, 0xC91800E98FB99929ULL, 0x808BD68E6AC10365ULL,
        0xDEC468145B7605F6ULL, 0x1BEDE3A3AEF53302ULL, 0x43539603D6C55602ULL, 0xAA969B5C691CCB7AULL,
        0xA87832D392EFEE56ULL, 0x65942C7B3C7E11AEULL, 0xDED2D633CAD004F6ULL, 0x21F08570F420E565ULL,
        0xB415938D7DA94E3CULL, 0x91B859E59ECB6350ULL, 0x10CFF333E0ED804AULL, 0x28AED140BE0BB7DDULL,
        0xC5CC1D89724FA456ULL, 0x5648F680F11A2741ULL, 0x2D255069F0B7DAB3ULL, 0x9BC5A38EF729ABD4ULL,
        0xEF2F054308F6A2BCULL, 0xAF2042F5CC5C2858ULL, 0x480412BAB7F5BE2AULL, 0xAEF3AF4A563DFE43ULL,
        0x19AFE59AE451497FULL, 0x52593803DFF1E840ULL, 0xF4F076E65F2CE6F0ULL, 0x11379625747D5AF3ULL,
        0xBCE5D2248682C115ULL, 0x9DA4243DE836994FULL, 0x066F70B33FE09017ULL, 0x4DC4DE189B671A1CULL,
        0x51039AB7712457C3ULL, 0xC07A3F80C31FB4B4ULL, 0xB46EE9C5E64A6E7CULL, 0xB3819A42ABE61C87ULL,
        0x21A007933A522A20ULL, 0x2DF16F761598AA4FULL, 0x763C4A1371B368FDULL, 0xF793C46702E086A0ULL,
        0xD7288E012AEB8D31ULL, 0xDE336A2A4BC1C44BULL, 0x0BF692B38D079F23ULL, 0x2C604A7A177326B3ULL,
        0x4850E73E03EB6064ULL, 0xCFC447F1E53C8E1BULL, 0xB05CA3F564268D99ULL, 0x9AE182C8BC9474E8ULL,
        0xA4FC4BD4FC5558CAULL, 0xE755178D58FC4E76ULL, 0x69B97DB1A4C03DFEULL, 0xF9B5B7C4ACC67C96ULL,
        0xFC6A82D64B8655FBULL, 0x9C684CB6C4D24417ULL, 0x8EC97D2917456ED0ULL, 0x6703DF9D2924E97EULL,
        0xC547F57E42A7444EULL, 0x78E37644E7CAD29EULL, 0xFE9A44E9362F05FAULL, 0x08BD35CC38336615ULL,
        0x9315E5EB3A129ACEULL, 0x94061B871E04DF75ULL, 0xDF1D9F9D784BA010ULL, 0x3BBA57B68871B59DULL,
        0xD2B7ADEEDED1F73FULL, 0xF7A255D83BC373F8ULL, 0xD7F4F2448C0CEB81ULL, 0xD95BE88CD210FFA7ULL,
        0x336F52F8FF4728E7ULL, 0xA74049DAC312AC71ULL, 0xA2F61BB6E437FDB5ULL, 0x4F2A5CB07F6A35B3ULL,
        0x87D380BDA5BF7859ULL, 0x16B9F7E06C453A21ULL, 0x7BA2484C8A0FD54EULL, 0xF3A678CAD9A2E38CULL,
        0x39B0BF7DDE437BA2ULL, 0xFCAF55C1BF8A4424ULL, 0x18FCF680573FA594ULL, 0x4C0563B89F495AC3ULL,
        0x40E087931A00930DULL, 0x8CFFA9412EB642C1ULL, 0x68CA39053261169FULL, 0x7A1EE967D27579E2ULL,
        0x9D1D60E5076F5B6FULL, 0x3810E399B6F65BA2ULL, 0x32095B6D4AB5F9B1ULL, 0x35CAB62109DD038AULL,
        0xA90B24499FCFAFB1ULL, 0x77A225A07CC2C6BDULL, 0x513E5E634C70E331ULL, 0x4361C0CA3F692F12ULL,
        0xD941ACA44B20A45BULL, 0x528F7C8602C5807BULL, 0x52AB92BEB9613989ULL, 0x9D1DFA2EFC557F73ULL,
        0x722FF175F572C348ULL, 0x1D1260A51107FE97ULL, 0x7A249A57EC0C9BA2ULL, 0x04208FE9E8F7F2D6ULL,
        0x5A110C6058B920A0ULL, 0x0CD9A497658A5698ULL, 0x56FD23C8F9715A4CULL, 0x284C847B9D887AAEULL,
        0x04FEABFBBDB619CBULL, 0x742E1E651C60BA83ULL, 0x9A9632E65904AD3CULL, 0x881B82A13B51B9E2ULL,
        0x506E6744CD974924ULL, 0xB0183DB56FFC6A79ULL, 0x0ED9B915C66ED37EULL, 0x5E11E86D5873D484ULL,
        0xF678647E3519AC6EULL, 0x1B85D488D0F20CC5ULL, 0xDAB9FE6525D89021ULL, 0x0D151D86ADB73615ULL,
        0xA865A54EDCC0F019ULL, 0x93C42566AEF98FFBULL, 0x99E7AFEABE000731ULL, 0x48CBFF086DDF285AULL,
        0x7F9B6AF1EBF78BAFULL, 0x58627E1A149BBA21ULL, 0x2CD16E2ABD791E33ULL, 0xD363EFF5F0977996ULL,
        0x0CE2A38C344A6EEDULL, 0x1A804AADB9CFA741ULL, 0x907F30421D78C5DEULL, 0x501F65EDB3034D07ULL,
        0x37624AE5A48FA6E9ULL, 0x957BAF61700CFF4EULL, 0x3A6C27934E31188AULL, 0xD49503536ABCA345ULL,
        0x088E049589C432E0ULL, 0xF943AEE7FEBF21B8ULL, 0x6C3B8E3E336139D3ULL, 0x364F6FFA464EE52EULL,
        0xD60F6DCEDC314222ULL, 0x56963B0DCA418FC0ULL, 0x16F50EDF91E513AFULL, 0xEF1955914B609F93ULL,
        0x565601C0364E3228ULL, 0xECB53939887E8175ULL, 0xBAC7A9A18531294BULL, 0xB344C470397BBA52ULL,
        0x65D34954DAF3CEBDULL, 0xB4B81B3FA97511E2ULL, 0xB422061193D6F6A7ULL, 0x071582401C38434DULL,
        0x7A13F18BBEDC4FF5ULL, 0xBC4097B116C524D2ULL, 0x59B97885E2F2EA28ULL, 0x99170A5DC3115544ULL,
        0x6F423357E7C6A9F9ULL, 0x325928EE6E6F8794ULL, 0xD0E4366228B03343ULL, 0x565C31F7DE89EA27ULL,
        0x30F5611484119414ULL, 0xD873DB391292ED4FULL, 0x7BD94E1D8E17DEBCULL, 0xC7D9F16864A76E94ULL,
        0x947AE053EE56E63CULL, 0xC8C93882F9475F5FULL, 0x3A9BF55BA91F81CAULL, 0xD9A11FBB3D9808E4ULL,
        0x0FD22063EDC29FCAULL, 0xB3F256D8ACA0B0B9ULL, 0xB03031A8B4516E84ULL, 0x35DD37D5871448AFULL,
        0xE9F6082B05542E4EULL, 0xEBFAFA33D7254B59ULL, 0x9255ABB50D532280ULL, 0xB9AB4CE57F2D34F3ULL,
        0x693501D628297551ULL, 0xC62C58F97DD949BFULL, 0xCD454F8F19C5126AULL, 0xBBE83F4ECC2BDECBULL,
        0xDC842B7E2819E230ULL, 0xBA89142E007503B8ULL, 0xA3BC941D0A5061CBULL, 0xE9F6760E32CD8021ULL,
        0x09C7E552BC76492FULL, 0x852F54934DA55CC9ULL, 0x8107FCCF064FCF56ULL, 0x098954D51FFF6580ULL,
        0x23B70EDB1955C4BFULL, 0xC330DE426430F69DULL, 0x4715ED43E8A45C0AULL, 0xA8D7E4DAB780A08DULL,
        0x0572B974F03CE0BBULL, 0xB57D2E985E1419C7ULL, 0xE8D9ECBE2CF3D73FULL, 0x2FE4B17170E59750ULL,
        0x11317BA87905E790ULL, 0x7FBF21EC8A1F45ECULL, 0x1725CABFCB045B00ULL, 0x964E915CD5E2B207ULL,
        0x3E2B8BCBF016D66DULL, 0xBE7444E39328A0ACULL, 0xF85B2B4FBCDE44B7ULL, 0x49353FEA39BA63B1ULL,
        0x1DD01AAFCD53486AULL, 0x1FCA8A92FD719F85ULL, 0xFC7C95D827357AFAULL, 0x18A6A990C8B35EBDULL,
        0xCCCB7005C6B9C28DULL, 0x3BDBB92C43B17F26ULL, 0xAA70B5B4F89695A2ULL, 0xE94C39A54A98307FULL,
        0xB7A0B174CFF6F36EULL, 0xD4DBA84729AF48ADULL, 0x2E18BC1AD9704A68ULL, 0x2DE0966DAF2F8B1CULL,
        0xB9C11D5B1E43A07EULL, 0x64972D68DEE33360ULL, 0x94628D38D0C20584ULL, 0xDBC0D2B6AB90A559ULL,
        0xD2733C4335C6A72FULL, 0x7E75D99D94A70F4DULL, 0x6CED1983376FA72BULL, 0x97FCAACBF030BC24ULL,
        0x7B77497B32503B12ULL, 0x8547EDDFB81CCB94ULL, 0x79999CDFF70902CBULL, 0xCFFE1939438E9B24ULL,
        0x829626E3892D95D7ULL, 0x92FAE24291F2B3F1ULL, 0x63E22C147B9C3403ULL, 0xC678B6D860284A1CULL,
        0x5873888850659AE7ULL, 0x0981DCD296A8736DULL, 0x9F65789A6509A440ULL, 0x9FF38FED72E9052FULL,
        0xE479EE5B9930578CULL, 0xE7F28ECD2D49EECDULL, 0x56C074A581EA17FEULL, 0x5544F7D774B14AEFULL,
        0x7B3F0195FC6F290FULL, 0x12153635B2C0CF57ULL, 0x7F5126DBBA5E0CA7ULL, 0x7A76956C3EAFB413ULL,
        0x3D5774A11D31AB39ULL, 0x8A1B083821F40CB4ULL, 0x7B4A38E32537DF62ULL, 0x950113646D1D6E03ULL,
        0x4DA8979A0041E8A9ULL, 0x3BC36E078F7515D7ULL, 0x5D0A12F27AD310D1ULL, 0x7F9D1A2E1EBE1327ULL,
        0xDA3A361B1C5157B1ULL, 0xDCDD7D20903D0C25ULL, 0x36833336D068F707ULL, 0xCE68341F79893389ULL,
        0xAB9090168DD05F34ULL, 0x43954B3252DC25E5ULL, 0xB438C2B67F98E5E9ULL, 0x10DCD78E3851A492ULL,
        0xDBC27AB5447822BFULL, 0x9B3CDB65F82CA382ULL, 0xB67B7896167B4C84ULL, 0xBFCED1B0048EAC50ULL,
        0xA9119B60369FFEBDULL, 0x1FFF7AC80904BF45ULL, 0xAC12FB171817EEE7ULL, 0xAF08DA9177DDA93DULL,
        0x1B0CAB936E65C744ULL, 0xB559EB1D04E5E932ULL, 0xC37B45B3F8D6F2BAULL, 0xC3A9DC228CAAC9E9ULL,
        0xF3B8B6675A6507FFULL, 0x9FC477DE4ED681DAULL, 0x67378D8ECCEF96CBULL, 0x6DD856D94D259236ULL,
        0xA319CE15B0B4DB31ULL, 0x073973751F12DD5EULL, 0x8A8E849EB32781A5ULL, 0xE1925C71285279F5ULL,
        0x74C04BF1790C0EFEULL, 0x4DDA48153C94938AULL, 0x9D266D6A1CC0542CULL, 0x7440FB816508C4FEULL,
        0x13328503DF48229FULL, 0xD6BF7BAEE43CAC40ULL, 0x4838D65F6EF6748FULL, 0x1E152328F3318DEAULL,
        0x8F8419A348F296BFULL, 0x72C8834A5957B511ULL, 0xD7A023A73260B45CULL, 0x94EBC8ABCFB56DAEULL,
        0x9FC10D0F989993E0ULL, 0xDE68A2355B93CAE6ULL, 0xA44CFE79AE538BBEULL, 0x9D1D84FCCE371425ULL,
        0x51D2B1AB2DDFB636ULL, 0x2FD7E4B9E72CD38CULL, 0x65CA5B96B7552210ULL, 0xDD69A0D8AB3B546DULL,
        0x604D51B25FBF70E2ULL, 0x73AA8A564FB7AC9EULL, 0x1A8C1E992B941148ULL, 0xAAC40A2703D9BEA0ULL,
        0x764DBEAE7FA4F3A6ULL, 0x1E99B96E70A9BE8BULL, 0x2C5E9DEB57EF4743ULL, 0x3A938FEE32D29981ULL,
        0x26E6DB8FFDF5ADFEULL, 0x469356C504EC9F9DULL, 0xC8763C5B08D1908CULL, 0x3F6C6AF859D80055ULL,
        0x7F7CC39420A3A545ULL, 0x9BFB227EBDF4C5CEULL, 0x89039D79D6FC5C5CULL, 0x8FE88B57305E2AB6ULL,
        0xA09E8C8C35AB96DEULL, 0xFA7E393983325753ULL, 0xD6B6D0ECC617C699ULL, 0xDFEA21EA9E7557E3ULL,
        0xB67C1FA481680AF8ULL, 0xCA1E3785A9E724E5ULL, 0x1CFC8BED0D681639ULL, 0xD18D8549D140CAEAULL,
        0x4ED0FE7E9DC91335ULL, 0xE4DBF0634473F5D2ULL, 0x1761F93A44D5AEFEULL, 0x53898E4C3910DA55ULL,
        0x734DE8181F6EC39AULL, 0x2680B122BAA28D97ULL, 0x298AF231C85BAFABULL, 0x7983EED3740847D5ULL,
        0x66C1A2A1A60CD889ULL, 0x9E17E49642A3E4C1ULL, 0xEDB454E7BADC0805ULL, 0x50B704CAB602C329ULL,
        0x4CC317FB9CDDD023ULL, 0x66B4835D9EAFEA22ULL, 0x219B97E26FFC81BDULL, 0x261E4E4C0A333A9DULL,
        0x1FE2CCA76517DB90ULL, 0xD7504DFA8816EDBBULL, 0xB9571FA04DC089C8ULL, 0x1DDC0325259B27DEULL,
        0xCF3F4688801EB9AAULL, 0xF4F5D05C10CAB243ULL, 0x38B6525C21A42B0EULL, 0x36F60E2BA4FA6800ULL,
        0xEB3593803173E0CEULL, 0x9C4CD6257C5A3603ULL, 0xAF0C317D32ADAA8AULL, 0x258E5A80C7204C4BULL,
        0x8B889D624D44885DULL, 0xF4D14597E660F855ULL, 0xD4347F66EC8941C3ULL, 0xE699ED85B0DFB40DULL,
        0x2472F6207C2D0484ULL, 0xC2A1E7B5B459AEB5ULL, 0xAB4F6451CC1D45ECULL, 0x63767572AE3D6174ULL,
        0xA59E0BD101731A28ULL, 0x116D0016CB948F09ULL, 0x2CF9C8CA052F6E9FULL, 0x0B090A7560A968E3ULL,
        0xABEEDDB2DDE06FF1ULL, 0x58EFC10B06A2068DULL, 0xC6E57A78FBD986E0ULL, 0x2EAB8CA63CE802D7ULL,
        0x14A195640116F336ULL, 0x7C0828DD624EC390ULL, 0xD74BBE77E6116AC7ULL, 0x804456AF10F5FB53ULL,
        0xEBE9EA2ADF4321C7ULL, 0x03219A39EE587A30ULL, 0x49787FEF17AF9924ULL, 0xA1E9300CD8520548ULL,
        0x5B45E522E4B1B4EFULL, 0xB49C3B3995091A36ULL, 0xD4490AD526F14431ULL, 0x12A8F216AF9418C2ULL,
        0x001F837CC7350524ULL, 0x1877B51E57A764D5ULL, 0xA2853B80F17F58EEULL, 0x993E1DE72D36D310ULL,
        0xB3598080CE64A656ULL, 0x252F59CF0D9F04BBULL, 0xD23C8E176D113600ULL, 0x1BDA0492E7E4586EULL,
        0x21E0BD5026C619BFULL, 0x3B097ADAF088F94EULL, 0x8D14DEDB30BE846EULL, 0xF95CFFA23AF5F6F4ULL,
        0x3871700761B3F743ULL, 0xCA672B91E9E4FA16ULL, 0x64C8E531BFF53B55ULL, 0x241260ED4AD1E87DULL,
        0x106C09B972D2E822ULL, 0x7FBA195410E5CA30ULL, 0x7884D9BC6CB569D8ULL, 0x0647DFEDCD894A29ULL,
        0x63573FF03E224774ULL, 0x4FC8E9560F91B123ULL, 0x1DB956E450275779ULL, 0xB8D91274B9E9D4FBULL,
        0xA2EBEE47E2FBFCE1ULL, 0xD9F1F30CCD97FB09ULL, 0xEFED53D75FD64E6BULL, 0x2E6D02C36017F67FULL,
        0xA9AA4D20DB084E9BULL, 0xB64BE8D8B25396C1ULL, 0x70CB6AF7C2D5BCF0ULL, 0x98F076A4F7A2322EULL,
        0xBF84470805E69B5FULL, 0x94C3251F06F90CF3ULL, 0x3E003E616A6591E9ULL, 0xB925A6CD0421AFF3ULL,
        0x61BDD1307C66E300ULL, 0xBF8D5108E27E0D48ULL, 0x240AB57A8B888B20ULL, 0xFC87614BAF287E07ULL,
        0xEF02CDD06FFDB432ULL, 0xA1082C0466DF6C0AULL, 0x8215E577001332C8ULL, 0xD39BB9C3A48DB6CFULL,
        0x2738259634305C14ULL, 0x61CF4F94C97DF93DULL, 0x1B6BACA2AE4E125BULL, 0x758F450C88572E0BULL,
        0x959F587D507A8359ULL, 0xB063E962E045F54DULL, 0x60E8ED72C0DFF5D1ULL, 0x7B64978555326F9FULL,
        0xFD080D236DA814BAULL, 0x8C90FD9B083F4558ULL, 0x106F72FE81E2C590ULL, 0x7976033A39F7D952ULL,
        0xA4EC0132764CA04BULL, 0x733EA705FAE4FA77ULL, 0xB4D8F77BC3E56167ULL, 0x9E21F4F903B33FD9ULL,
        0x9D765E419FB69F6DULL, 0xD30C088BA61EA5EFULL, 0x5D94337FBFAF7F5BULL, 0x1A4E4822EB4D7A59ULL,
        0x6FFE73E81B637FB3ULL, 0xDDF957BC36D8B9CAULL, 0x64D0E29EEA8838B3ULL, 0x08DD9BDFD96B9F63ULL,
        0x087E79E5A57D1D13ULL, 0xE328E230E3E2B3FBULL, 0x1C2559E30F0946BEULL, 0x720BF5F26F4D2EAAULL,
        0xB0774D261CC609DBULL, 0x443F64EC5A371195ULL, 0x4112CF68649A260EULL, 0xD813F2FAB7F5C5CAULL,
        0x660D3257380841EEULL, 0x59AC2C7873F910A3ULL, 0xE846963877671A17ULL, 0x93B633ABFA3469F8ULL,
        0xC0C0F5A60EF4CDCFULL, 0xCAF21ECD4377B28CULL, 0x57277707199B8175ULL, 0x506C11B9D90E8B1DULL,
        0xD83CC2687A19255FULL, 0x4A29C6465A314CD1ULL, 0xED2DF21216235097ULL, 0xB5635C95FF7296E2ULL,
        0x22AF003AB672E811ULL, 0x52E762596BF68235ULL, 0x9AEBA33AC6ECC6B0ULL, 0x944F6DE09134DFB6ULL,
        0x6C47BEC883A7DE39ULL, 0x6AD047C430A12104ULL, 0xA5B1CFDBA0AB4067ULL, 0x7C45D833AFF07862ULL,
        0x5092EF950A16DA0BULL, 0x9338E69C052B8E7BULL, 0x455A4B4CFE30E3F5ULL, 0x6B02E63195AD0CF8ULL,
        0x6B17B224BAD6BF27ULL, 0xD1E0CCD25BB9C169ULL, 0xDE0C89A556B9AE70ULL, 0x50065E535A213CF6ULL,
        0x9C1169FA2777B874ULL, 0x78EDEFD694AF1EEDULL, 0x6DC93D9526A50E68ULL, 0xEE97F453F06791EDULL,
        0x32AB0EDB696703D3ULL, 0x3A6853C7E70757A7ULL, 0x31865CED6120F37DULL, 0x67FEF95D92607890ULL,
        0x1F2B1D1F15F6DC9CULL, 0xB69E38A8965C6B65ULL, 0xAA9119FF184CCCF4ULL, 0xF43C732873F24C13ULL,
        0xFB4A3D794A9A80D2ULL, 0x3550C2321FD6109CULL, 0x371F77E76BB8417EULL, 0x6BFA9AAE5EC05779ULL,
        0xCD04F3FF001A4778ULL, 0xE3273522064480CAULL, 0x9F91508BFFCFC14AULL, 0x049A7F41061A9E60ULL,
        0xFCB6BE43A9F2FE9BULL, 0x08DE8A1C7797DA9BULL, 0x8F9887E6078735A1ULL, 0xB5B4071DBFC73A66ULL,
        0x230E343DFBA08D33ULL, 0x43ED7F5A0FAE657DULL, 0x3A88A0FBBCB05C63ULL, 0x21874B8B4D2DBC4FULL,
        0x1BDEA12E35F6A8C9ULL, 0x53C065C6C8E63528ULL, 0xE34A1D250E7A8D6BULL, 0xD6B04D3B7651DD7EULL,
        0x5E90277E7CB39E2DULL, 0x2C046F22062DC67DULL, 0xB10BB459132D0A26ULL, 0x3FA9DDFB67E2F199ULL,
        0x0E09B88E1914F7AFULL, 0x10E8B35AF3EEAB37ULL, 0x9EEDECA8E272B933ULL, 0xD4C718BC4AE8AE5FULL,
        0x81536D601170FC20ULL, 0x91B534F885818A06ULL, 0xEC8177F83F900978ULL, 0x190E714FADA5156EULL,
        0xB592BF39B0364963ULL, 0x89C350C893AE7DC1ULL, 0xAC042E70F8B383F2ULL, 0xB49B52E587A1EE60ULL,
        0xFB152FE3FF26DA89ULL, 0x3E666E6F69AE2C15ULL, 0x3B544EBE544C19F9ULL, 0xE805A1E290CF2456ULL,
        0x24B33C9D7ED25117ULL, 0xE74733427B72F0C1ULL, 0x0A804D18B7097475ULL, 0x57E3306D881EDB4FULL,
        0x4AE7D6A36EB5DBCBULL, 0x2D8D5432157064C8ULL, 0xD1E649DE1E7F268BULL, 0x8A328A1CEDFE552CULL,
        0x07A3AEC79624C7DAULL, 0x84547DDC3E203C94ULL, 0x990A98FD5071D263ULL, 0x1A4FF12616EEFC89ULL,
        0xF6F7FD1431714200ULL, 0x30C05B1BA332F41CULL, 0x8D2636B81555A786ULL, 0x46C9FEB55D120902ULL,
        0xCCEC0A73B49C9921ULL, 0x4E9D2827355FC492ULL, 0x19EBB029435DCB0FULL, 0x4659D2B743848A2CULL,
        0x963EF2C96B33BE31ULL, 0x74F85198B05A2E7DULL, 0x5A0F544DD2B1FB18ULL, 0x03727073C2E134B1ULL,
        0xC7F6AA2DE59AEA61ULL, 0x352787BAA0D7C22FULL, 0x9853EAB63B5E0B35ULL, 0xABBDCDD7ED5C0860ULL,
        0xCF05DAF5AC8D77B0ULL, 0x49CAD48CEBF4A71EULL, 0x7A4C10EC2158C4A6ULL, 0xD9E92AA246BF719EULL,
        0x13AE978D09FE5557ULL, 0x730499AF921549FFULL, 0x4E4B705B92903BA4ULL, 0xFF577222C14F0A3AULL,
        0x55B6344CF97AAFAEULL, 0xB862225B055B6960ULL, 0xCAC09AFBDDD2CDB4ULL, 0xDAF8E9829FE96B5FULL,
        0xB5FDFC5D3132C498ULL, 0x310CB380DB6F7503ULL, 0xE87FBB46217A360EULL, 0x2102AE466EBB1148ULL,
        0xF8549E1A3AA5E00DULL, 0x07A69AFDCC42261AULL, 0xC4C118BFE78FEAAEULL, 0xF9F4892ED96BD438ULL,
        0x1AF3DBE25D8F45DAULL, 0xF5B4B0B0D2DEEEB4ULL, 0x962ACEEFA82E1C84ULL, 0x046E3ECAAF453CE9ULL,
        0xF05D129681949A4CULL, 0x964781CE734B3C84ULL, 0x9C2ED44081CE5FBDULL, 0x522E23F3925E319EULL,
        0x177E00F9FC32F791ULL, 0x2BC60A63A6F3B3F2ULL, 0x222BBFAE61725606ULL, 0x486289DDCC3D6780ULL,
        0x7DC7785B8EFDFC80ULL, 0x8AF38731C02BA980ULL, 0x1FAB64EA29A2DDF7ULL, 0xE4D9429322CD065AULL,
        0x9DA058C67844F20CULL, 0x24C0E332B70019B0ULL, 0x233003B5A6CFE6ADULL, 0xD586BD01C5C217F6ULL,
        0x5E5637885F29BC2BULL, 0x7EBA726D8C94094BULL, 0x0A56A5F0BFE39272ULL, 0xD79476A84EE20D06ULL,
        0x9E4C1269BAA4BF37ULL, 0x17EFEE45B0DEE640ULL, 0x1D95B0A5FCF90BC6ULL, 0x93CBE0B699C2585DULL,
        0x65FA4F227A2B6D79ULL, 0xD5F9E858292504D5ULL, 0xC2B5A03F71471A6FULL, 0x59300222B4561E00ULL,
        0xCE2F8642CA0712DCULL, 0x7CA9723FBB2E8988ULL, 0x2785338347F2BA08ULL, 0xC61BB3A141E50E8CULL,
        0x150F361DAB9DEC26ULL, 0x9F6A419D382595F4ULL, 0x64A53DC924FE7AC9ULL, 0x142DE49FFF7A7C3DULL,
        0x0C335248857FA9E7ULL, 0x0A9C32D5EAE45305ULL, 0xE6C42178C4BBB92EULL, 0x71F1CE2490D20B07ULL,
        0xF1BCC3D275AFE51AULL, 0xE728E8C83C334074ULL, 0x96FBF83A12884624ULL, 0x81A1549FD6573DA5ULL,
        0x5FA7867CAF35E149ULL, 0x56986E2EF3ED091BULL, 0x917F1DD5F8886C61ULL, 0xD20D8C88C8FFE65FULL,
        0x31D71DCE64B2C310ULL, 0xF165B587DF898190ULL, 0xA57E6339DD2CF3A0ULL, 0x1EF6E6DBB1961EC9ULL,
        0x70CC73D90BC26E24ULL, 0xE21A6B35DF0C3AD7ULL, 0x003A93D8B2806962ULL, 0x1C99DED33CB890A1ULL,
        0xCF3145DE0ADD4289ULL, 0xD0E4427A5514FB72ULL, 0x77C621CC9FB3A483ULL, 0x67A34DAC4356550BULL,
        0xF8D626AAAF278509ULL,
    };

    static constexpr std::array<U64, 16> castlingKey = [] {
        std::array<U64, 16> a{};
        for (int m = 0; m < 16; ++m)
            for (int b = 0; b < 4; ++b)
                if (m & (1 << b)) a[m] ^= RANDOM_ARRAY[768 + b];
        return a;
    }();

    // flat table indexed by (piece * 64 + square), piece in chess.hpp numbering (white 0..5, black 6..11)
    static constexpr std::array<U64, 768> pieceKey = [] {
        constexpr int polyglotKind[12] = {1, 3, 5, 7, 9, 11, 0, 2, 4, 6, 8, 10};
        std::array<U64, 768> a{};
        for (int p = 0; p < 12; ++p)
            for (int s = 0; s < 64; ++s) a[p * 64 + s] = RANDOM_ARRAY[64 * polyglotKind[p] + s];
        return a;
    }();

    [[nodiscard]] static BK_INLINE U64 piece(Piece p, Square s) noexcept {
        assert(static_cast<int>(p) < 12);
        return pieceKey[static_cast<int>(p) * 64 + s.index()];
    }
    [[nodiscard]] static BK_INLINE U64 pieceIdx(int p, int s) noexcept { return pieceKey[p * 64 + s]; }
    [[nodiscard]] static BK_INLINE U64 enpassant(File f) noexcept { return RANDOM_ARRAY[772 + static_cast<int>(f)]; }
    [[nodiscard]] static BK_INLINE U64 castling(int rights) noexcept { return castlingKey[rights]; }
    [[nodiscard]] static BK_INLINE U64 castlingIndex(int idx) noexcept { return RANDOM_ARRAY[768 + idx]; }
    [[nodiscard]] static BK_INLINE U64 sideToMove() noexcept { return RANDOM_ARRAY[780]; }

   public:
    friend class Board;
};

enum class GameResult { WIN, LOSE, DRAW, NONE };

enum class GameResultReason {
    CHECKMATE,
    STALEMATE,
    INSUFFICIENT_MATERIAL,
    FIFTY_MOVE_RULE,
    THREEFOLD_REPETITION,
    NONE
};

enum class CheckType { NO_CHECK, DIRECT_CHECK, DISCOVERY_CHECK };

enum PieceGenType {
    PAWN   = 1,
    KNIGHT = 2,
    BISHOP = 4,
    ROOK   = 8,
    QUEEN  = 16,
    KING   = 32,
};

using PackedBoard = std::array<std::uint8_t, 24>;

namespace detail {
inline std::optional<int> parseStringViewToInt(std::string_view sv) {
    if (sv.empty()) return std::nullopt;
    if (sv.back() == ';') sv.remove_suffix(1);
    if (sv.empty()) return std::nullopt;
#ifdef BACKENED_HAS_CHARCONV
    int value;
    const char* first = sv.data();
    const char* last  = first + sv.size();
    auto [ptr, ec]    = std::from_chars(first, last, value);
    if (ec == std::errc() && ptr == last) return value;
#else
    std::stringstream ss{std::string(sv)};
    ss.exceptions(std::ios::goodbit);
    int value;
    ss >> value;
    if (!ss.fail() && (ss.eof() || (ss >> std::ws).eof())) return value;
#endif
    return std::nullopt;
}
}  // namespace detail

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  movegen (declaration; bodies follow Board)
// ---------------------------------------------------------------------------------------------
namespace chess {

class movegen {
   public:
    enum class MoveGenType : std::uint8_t { ALL, CAPTURE, QUIET };
    enum class GenMode : std::uint8_t { ALL_MOVES, ONE_MOVE_ONLY };

    /// Generates all legal moves for a position (clears `movelist` first).
    template <MoveGenType mt = MoveGenType::ALL>
    static void legalmoves(Movelist& movelist, const Board& board,
                           int pieces = PieceGenType::PAWN | PieceGenType::KNIGHT | PieceGenType::BISHOP |
                                        PieceGenType::ROOK | PieceGenType::QUEEN | PieceGenType::KING);

    /// Returns true if there is at least one legal move.
    template <MoveGenType mt = MoveGenType::ALL>
    static bool anylegalmoves(const Board& board, int pieces = PieceGenType::PAWN | PieceGenType::KNIGHT |
                                                               PieceGenType::BISHOP | PieceGenType::ROOK |
                                                               PieceGenType::QUEEN | PieceGenType::KING);

    /// Check if a move is legal from the given position.
    [[nodiscard]] static bool isLegal(const Board& board, const Move move);

   private:
    template <Color::underlying c, MoveGenType mt, bool ONE>
    static void generate(Movelist& movelist, const Board& board, int pieces);

    [[nodiscard]] static Bitboard between(Square sq1, Square sq2) noexcept {
        return Bitboard(bk::ST.between[sq1.index()][sq2.index()]);
    }

    template <Color::underlying c>
    [[nodiscard]] static bool isLegalFor(const Board& board, const Move move);

    friend class Board;
};

// ---------------------------------------------------------------------------------------------
//  Board
// ---------------------------------------------------------------------------------------------

class Board {
    using U64 = std::uint64_t;

   public:
    class CastlingRights {
       public:
        enum class Side : std::uint8_t { KING_SIDE, QUEEN_SIDE };

        constexpr void setCastlingRight(Color color, Side castle, File rook_file) {
            files_[static_cast<int>(color)][static_cast<int>(castle)] = rook_file;
            bits_ = static_cast<std::uint8_t>(bits_ | (1u << slot(color, castle)));
        }

        constexpr void clear() {
            bits_ = 0;
            for (auto& row : files_)
                for (auto& f : row) f = File::NO_FILE;
        }

        constexpr int clear(Color color, Side castle) {
            const int s = slot(color, castle);
            bits_       = static_cast<std::uint8_t>(bits_ & ~(1u << s));
            return s;
        }

        constexpr void clear(Color color) { bits_ = static_cast<std::uint8_t>(bits_ & ~(3u << (2 * static_cast<int>(color)))); }

        constexpr bool has(Color color, Side castle) const { return (bits_ >> slot(color, castle)) & 1u; }
        constexpr bool has(Color color) const { return (bits_ >> (2 * static_cast<int>(color))) & 3u; }

        constexpr File getRookFile(Color color, Side castle) const {
            return has(color, castle) ? files_[static_cast<int>(color)][static_cast<int>(castle)] : File(File::NO_FILE);
        }

        constexpr int hashIndex() const { return bits_; }

        constexpr bool isEmpty() const { return bits_ == 0; }

        template <typename T>
        static constexpr Side closestSide(T sq, T pred) {
            return sq > pred ? Side::KING_SIDE : Side::QUEEN_SIDE;
        }

        bool operator==(const CastlingRights& other) const noexcept {
            if (bits_ != other.bits_) return false;
            for (int c = 0; c < 2; ++c)
                for (int s = 0; s < 2; ++s)
                    if (((bits_ >> (c * 2 + s)) & 1u) && files_[c][s] != other.files_[c][s]) return false;
            return true;
        }

        // internal helpers
        constexpr void keepOnly(int mask) { bits_ = static_cast<std::uint8_t>(bits_ & mask); }
        constexpr int bits() const { return bits_; }

       private:
        static constexpr int slot(Color c, Side s) { return static_cast<int>(c) * 2 + static_cast<int>(s); }

        File files_[2][2] = {{File(File::NO_FILE), File(File::NO_FILE)}, {File(File::NO_FILE), File(File::NO_FILE)}};
        std::uint8_t bits_ = 0;
    };

   private:
    struct State {
        U64 hash;
        CastlingRights castling;
        Square enpassant;
        std::uint8_t half_moves;
        Piece captured_piece;

        State(const U64& h, const CastlingRights& c, const Square& e, const std::uint8_t& hm, const Piece& cp)
            : hash(h), castling(c), enpassant(e), half_moves(hm), captured_piece(cp) {}

        bool operator==(const State& o) const noexcept {
            return hash == o.hash && castling == o.castling && enpassant == o.enpassant &&
                   half_moves == o.half_moves && captured_piece == o.captured_piece;
        }
    };

   protected:
    enum class ProtectedCtor { CREATE };

    Board(ProtectedCtor) {}

   public:
    explicit Board(std::string_view fen = constants::STARTPOS, bool chess960 = false) {
        prev_states_.reserve(256);
        chess960_ = chess960;
        setFenInternal<true>(fen);
    }

    Board(const Board&)            = default;
    Board(Board&&)                 = default;
    Board& operator=(const Board&) = default;
    Board& operator=(Board&&)      = default;
#ifndef BACKENED_NO_VIRTUAL
    virtual ~Board() = default;
#endif

    static Board fromFen(std::string_view fen) { return Board(fen); }
    static Board fromXfen(std::string_view xfen) {
        Board board;
        board.setXfen(xfen);
        return board;
    }
    static Board fromEpd(std::string_view epd) {
        Board board;
        board.setEpd(epd);
        return board;
    }

    BK_VIRTUAL bool setFen(std::string_view fen) { return setFenInternal(fen); }

    /// Parse a position in X-FEN (Chess960 style) notation.
    bool setXfen(std::string_view xfen) {
        const bool prev_960 = chess960_;
        chess960_           = true;
        const bool ok       = parseFen<false>(xfen, true, true);
        if (!ok) chess960_ = prev_960;
        return ok;
    }

    bool setEpd(const std::string_view epd) {
        auto parts = utils::splitString(epd, ' ');
        if (parts.size() < 4) return false;

        int hm = 0;
        int fm = 1;

        if (auto it = std::find(parts.begin(), parts.end(), "hmvc"); it != parts.end()) {
            if (std::distance(it, parts.end()) <= 1) return false;
            if (auto v = detail::parseStringViewToInt(*(it + 1))) hm = *v;
        }

        if (auto it = std::find(parts.begin(), parts.end(), "fmvn"); it != parts.end()) {
            if (std::distance(it, parts.end()) <= 1) return false;
            auto v = detail::parseStringViewToInt(*(it + 1));
            if (!v || *v <= 0) return false;
            fm = *v;
        }

        std::string fen;
        fen.reserve(96);
        fen.append(parts[0]).append(" ").append(parts[1]).append(" ").append(parts[2]).append(" ").append(parts[3]);
        fen.append(" ").append(std::to_string(hm)).append(" ").append(std::to_string(fm));

        return setFen(fen);
    }

    [[nodiscard]] std::string getFen(bool move_counters = true) const {
        return fenString(move_counters, false);
    }

    [[nodiscard]] std::string getXfen(bool move_counters = true) const {
        return fenString(move_counters, true);
    }

    [[nodiscard]] std::string getEpd() const {
        std::string out;
        out.reserve(100);
        out += getFen(false);
        out += " hmvc ";
        out += std::to_string(halfMoveClock()) + ";";
        out += " fmvn ";
        out += std::to_string(fullMoveNumber()) + ";";
        return out;
    }

    [[nodiscard]] bool isLegal(const Move move) const { return movegen::isLegal(*this, move); }

    // -----------------------------------------------------------------------------------------
    //  make / unmake
    // -----------------------------------------------------------------------------------------

    /**
     * Make a move. The move must be legal. With EXACT == true the en passant square (and its hash)
     * is only recorded if the opponent can legally capture; otherwise as soon as an enemy pawn
     * attacks the skipped square.
     */
    template <bool EXACT = false>
    void makeMove(const Move move) {
#ifdef BACKENED_NO_VIRTUAL
        makeMoveImpl<EXACT, false>(move);
#else
        if (BK_LIKELY(plainBoard()))
            makeMoveImpl<EXACT, false>(move);
        else
            makeMoveImpl<EXACT, true>(move);
#endif
    }

    void unmakeMove(const Move move) {
#ifdef BACKENED_NO_VIRTUAL
        unmakeMoveImpl<false>(move);
#else
        if (BK_LIKELY(plainBoard()))
            unmakeMoveImpl<false>(move);
        else
            unmakeMoveImpl<true>(move);
#endif
    }

   private:
    // HOOKS == true routes every piece change through the virtual placePiece/removePiece (for subclasses),
    // HOOKS == false uses the fused, non-virtual primitives.
    template <bool HOOKS>
    BK_INLINE void rmPiece(Piece p, Square s) {
        if constexpr (HOOKS)
            removePiece(p, s);
        else
            removePieceInternal(p, s);
    }
    template <bool HOOKS>
    BK_INLINE void plPiece(Piece p, Square s) {
        if constexpr (HOOKS)
            placePiece(p, s);
        else
            placePieceInternal(p, s);
    }
    template <bool HOOKS>
    BK_INLINE void mvPiece(Piece p, Square from, Square to) {
        if constexpr (HOOKS) {
            removePiece(p, from);
            placePiece(p, to);
        } else {
            moveInternal(p, from, to);
        }
    }

    template <bool EXACT, bool HOOKS>
    void makeMoveImpl(const Move move) {
        const int from_i = move.from().index();
        const int to_i   = move.to().index();
        const int pc     = static_cast<int>(board_[from_i]);
        const int us     = static_cast<int>(stm_);
        const auto mtype = move.typeOf();

        assert(pc != 12 && bk::PIECE_COLOR[pc] == us);

        const bool is_castle = mtype == Move::CASTLING;
        const int cap_piece  = is_castle ? 12 : static_cast<int>(board_[to_i]);

        prev_states_.emplace_back(key_, cr_, ep_sq_, hfm_, Piece(static_cast<Piece::underlying>(cap_piece)));

        U64 k = key_ ^ Zobrist::sideToMove();
        if (ep_sq_ != Square::NO_SQ) {
            k ^= Zobrist::enpassant(ep_sq_.file());
            ep_sq_ = Square::NO_SQ;
        }

        ++hfm_;
        ++plies_;

        // castling rights: one AND per square touched, one XOR into the key
        {
            const int before = cr_.bits();
            const int after  = before & castle_mask_[from_i] & castle_mask_[to_i];
            cr_.keepOnly(after);
            k ^= Zobrist::castling(before ^ after);
        }

        if (cap_piece != 12) {
            rmPiece<HOOKS>(Piece(static_cast<Piece::underlying>(cap_piece)), move.to());
            k ^= Zobrist::pieceIdx(cap_piece, to_i);
            hfm_ = 0;
        }

        const int pt = bk::PIECE_TYPE[pc];

        if (mtype == Move::NORMAL) {
            mvPiece<HOOKS>(Piece(static_cast<Piece::underlying>(pc)), move.from(), move.to());
            k ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(pc, to_i);
        } else if (mtype == Move::PROMOTION) {
            const int promo = static_cast<int>(move.promotionType()) + 6 * us;
            rmPiece<HOOKS>(Piece(static_cast<Piece::underlying>(pc)), move.from());
            plPiece<HOOKS>(Piece(static_cast<Piece::underlying>(promo)), move.to());
            k ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(promo, to_i);
        } else if (mtype == Move::ENPASSANT) {
            mvPiece<HOOKS>(Piece(static_cast<Piece::underlying>(pc)), move.from(), move.to());
            k ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(pc, to_i);
            const int cap_sq = to_i ^ 8;
            const int victim = 6 * (us ^ 1);  // enemy pawn
            rmPiece<HOOKS>(Piece(static_cast<Piece::underlying>(victim)), Square(cap_sq));
            k ^= Zobrist::pieceIdx(victim, cap_sq);
        } else {  // castling: king takes own rook notation
            const bool king_side = to_i > from_i;
            const int king_to    = (king_side ? 6 : 2) ^ (us * 56);
            const int rook_to    = (king_side ? 5 : 3) ^ (us * 56);
            const int rook       = static_cast<int>(board_[to_i]);
            rmPiece<HOOKS>(Piece(static_cast<Piece::underlying>(pc)), move.from());
            rmPiece<HOOKS>(Piece(static_cast<Piece::underlying>(rook)), move.to());
            plPiece<HOOKS>(Piece(static_cast<Piece::underlying>(pc)), Square(king_to));
            plPiece<HOOKS>(Piece(static_cast<Piece::underlying>(rook)), Square(rook_to));
            k ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(pc, king_to);
            k ^= Zobrist::pieceIdx(rook, to_i) ^ Zobrist::pieceIdx(rook, rook_to);
        }

        if (pt == 0) {
            hfm_ = 0;
            if (((from_i ^ to_i) == 16)) {
                const Square eps(to_i ^ 8);
                const Color them(us ^ 1);
                bool record;
                if constexpr (EXACT)
                    record = epCaptureExists(them, eps);
                else
                    record = (attacks::pawn(Color(us), eps) & pieces_bb_[0] & occ_bb_[us ^ 1]).getBits() != 0;
                if (record) {
                    ep_sq_ = eps;
                    k ^= Zobrist::enpassant(eps.file());
                }
            }
        }

        key_ = k;
        stm_ = Color(us ^ 1);
    }

    template <bool HOOKS>
    void unmakeMoveImpl(const Move move) {
        const State& prev = prev_states_.back();

        ep_sq_ = prev.enpassant;
        cr_    = prev.castling;
        hfm_   = prev.half_moves;
        stm_   = ~stm_;
        --plies_;

        const int us     = static_cast<int>(stm_);
        const auto mtype = move.typeOf();
        const Square from = move.from();
        const Square to   = move.to();

        if (mtype == Move::CASTLING) {
            const bool king_side = to.index() > from.index();
            const Square rook_from(king_side ? 5 ^ (us * 56) : 3 ^ (us * 56));
            const Square king_to(king_side ? 6 ^ (us * 56) : 2 ^ (us * 56));
            const Piece rook = board_[rook_from.index()];
            const Piece king = board_[king_to.index()];
            rmPiece<HOOKS>(rook, rook_from);
            rmPiece<HOOKS>(king, king_to);
            plPiece<HOOKS>(king, from);
            plPiece<HOOKS>(rook, to);
        } else if (mtype == Move::PROMOTION) {
            const Piece promoted = board_[to.index()];
            rmPiece<HOOKS>(promoted, to);
            plPiece<HOOKS>(Piece(static_cast<Piece::underlying>(6 * us)), from);
            if (prev.captured_piece != Piece::NONE) plPiece<HOOKS>(prev.captured_piece, to);
        } else {
            const Piece moved = board_[to.index()];
            mvPiece<HOOKS>(moved, to, from);
            if (mtype == Move::ENPASSANT) {
                plPiece<HOOKS>(Piece(static_cast<Piece::underlying>(6 * (us ^ 1))), Square(to.index() ^ 8));
            } else if (prev.captured_piece != Piece::NONE) {
                plPiece<HOOKS>(prev.captured_piece, to);
            }
        }

        key_ = prev.hash;
        prev_states_.pop_back();
    }


   public:
    void makeNullMove() {
        prev_states_.emplace_back(key_, cr_, ep_sq_, hfm_, Piece::NONE);

        key_ ^= Zobrist::sideToMove();
        if (ep_sq_ != Square::NO_SQ) key_ ^= Zobrist::enpassant(ep_sq_.file());
        ep_sq_ = Square::NO_SQ;
        stm_   = ~stm_;
        ++plies_;
    }

    void unmakeNullMove() {
        const State& prev = prev_states_.back();

        ep_sq_ = prev.enpassant;
        cr_    = prev.castling;
        hfm_   = prev.half_moves;
        key_   = prev.hash;
        --plies_;
        stm_ = ~stm_;

        prev_states_.pop_back();
    }

    /// Restore a previous state of this board directly (see chess.hpp docs: only valid if `previous`
    /// is an ancestor of the current position reached by makeMove calls).
    void backtrackTo(const Board& previous) {
        const std::size_t n = previous.prev_states_.size();
        assert(n <= prev_states_.size());

        prev_states_.erase(prev_states_.begin() + static_cast<std::ptrdiff_t>(n), prev_states_.end());

        pieces_bb_    = previous.pieces_bb_;
        occ_bb_       = previous.occ_bb_;
        board_        = previous.board_;
        key_          = previous.key_;
        cr_           = previous.cr_;
        plies_        = previous.plies_;
        stm_          = previous.stm_;
        ep_sq_        = previous.ep_sq_;
        hfm_          = previous.hfm_;
        chess960_     = previous.chess960_;
        castling_path = previous.castling_path;
        castle_mask_  = previous.castle_mask_;
    }

    // -----------------------------------------------------------------------------------------
    //  queries
    // -----------------------------------------------------------------------------------------

    [[nodiscard]] BK_INLINE Bitboard us(Color color) const noexcept { return occ_bb_[static_cast<int>(color)]; }
    [[nodiscard]] BK_INLINE Bitboard them(Color color) const noexcept { return occ_bb_[static_cast<int>(color) ^ 1]; }
    [[nodiscard]] BK_INLINE Bitboard occ() const noexcept { return occ_bb_[0] | occ_bb_[1]; }
    [[nodiscard]] BK_INLINE Bitboard all() const noexcept { return occ_bb_[0] | occ_bb_[1]; }

    [[nodiscard]] BK_INLINE Square kingSq(Color color) const noexcept {
        assert(pieces(PieceType::KING, color) != 0ull);
        return Square((pieces_bb_[5] & occ_bb_[static_cast<int>(color)]).lsb());
    }

    [[nodiscard]] BK_INLINE Bitboard pieces(PieceType type, Color color) const noexcept {
        return pieces_bb_[static_cast<int>(type)] & occ_bb_[static_cast<int>(color)];
    }

    [[nodiscard]] BK_INLINE Bitboard pieces(PieceType type) const noexcept {
        return pieces_bb_[static_cast<int>(type)];
    }

    template <typename... Pieces, typename = std::enable_if_t<(std::is_convertible_v<Pieces, PieceType> && ...)>>
    [[nodiscard]] BK_INLINE Bitboard pieces(Pieces... pcs) const noexcept {
        return (pieces_bb_[static_cast<int>(static_cast<PieceType>(pcs))] | ...);
    }

    template <typename T = Piece>
    [[nodiscard]] BK_INLINE T at(Square sq) const noexcept {
        assert(sq.is_valid());
        if constexpr (std::is_same_v<T, PieceType>)
            return static_cast<PieceType::underlying>(bk::PIECE_TYPE[static_cast<int>(board_[sq.index()])]);
        else
            return board_[sq.index()];
    }

    [[nodiscard]] bool isCapture(const Move move) const noexcept {
        return (at(move.to()) != Piece::NONE && move.typeOf() != Move::CASTLING) || move.typeOf() == Move::ENPASSANT;
    }

    template <typename T = Piece>
    [[nodiscard]] T getCapturing(const Move move) const noexcept {
        assert(at(move.from()) != Piece::NONE);
        const bool ep  = move.typeOf() == Move::ENPASSANT;
        const bool cap = !ep && at(move.to()) != Piece::NONE && move.typeOf() != Move::CASTLING;
        if constexpr (std::is_same_v<T, PieceType>) {
            if (ep) return PieceType::PAWN;
            if (cap) return at<PieceType>(move.to());
            return PieceType::NONE;
        } else {
            if (ep) return stm_ == Color::WHITE ? Piece(Piece::BLACKPAWN) : Piece(Piece::WHITEPAWN);
            if (cap) return at(move.to());
            return Piece::NONE;
        }
    }

    [[nodiscard]] U64 hash() const noexcept { return key_; }

    [[nodiscard]] Color sideToMove() const noexcept { return stm_; }
    [[nodiscard]] Square enpassantSq() const noexcept { return ep_sq_; }
    [[nodiscard]] CastlingRights castlingRights() const noexcept { return cr_; }
    [[nodiscard]] std::uint32_t halfMoveClock() const noexcept { return hfm_; }
    [[nodiscard]] std::uint32_t fullMoveNumber() const noexcept { return 1 + plies_ / 2; }

    void set960(bool is960) {
        chess960_ = is960;
        if (!original_fen_.empty()) setFen(original_fen_);
    }

    [[nodiscard]] bool chess960() const noexcept { return chess960_; }

    [[nodiscard]] std::string getCastleString() const {
        std::string out;
        if (chess960_) {
            for (auto color : {Color::WHITE, Color::BLACK})
                for (auto side : {CastlingRights::Side::KING_SIDE, CastlingRights::Side::QUEEN_SIDE})
                    if (cr_.has(color, side)) out += castlingFileToken(color, cr_.getRookFile(color, side));
            return out;
        }
        if (cr_.has(Color::WHITE, CastlingRights::Side::KING_SIDE)) out += 'K';
        if (cr_.has(Color::WHITE, CastlingRights::Side::QUEEN_SIDE)) out += 'Q';
        if (cr_.has(Color::BLACK, CastlingRights::Side::KING_SIDE)) out += 'k';
        if (cr_.has(Color::BLACK, CastlingRights::Side::QUEEN_SIDE)) out += 'q';
        return out;
    }

    /// True if the current position occurred `count` times before (2 = threefold, 1 = twofold).
    [[nodiscard]] bool isRepetition(int count = 2) const noexcept {
        const int size = static_cast<int>(prev_states_.size());
        const int low  = size - static_cast<int>(hfm_) - 1;
        int seen       = 0;
        const State* s = prev_states_.data();
        for (int i = size - 2; i >= 0 && i >= low; i -= 2) {
            if (s[i].hash == key_ && ++seen == count) return true;
        }
        return false;
    }

    /// A10: repetition test for SEARCH nodes. One prior occurrence that lies
    /// inside the search tree (i.e. reached within `searchPly` plies from the
    /// root) is already a draw by twofold; otherwise two prior occurrences
    /// (threefold against the game history) count. `searchPly` is the caller's
    /// distance from the search root (0 at the root).
    [[nodiscard]] bool isRepetitionSearch(int searchPly) const noexcept {
        const int size = static_cast<int>(prev_states_.size());
        const int low  = size - static_cast<int>(hfm_) - 1;
        int seen       = 0;
        const State* s = prev_states_.data();
        for (int i = size - 2; i >= 0 && i >= low; i -= 2) {
            if (s[i].hash == key_) {
                if (size - i <= searchPly) return true;   // occurred inside search tree
                if (++seen == 2) return true;             // threefold with game history
            }
        }
        return false;
    }

    [[nodiscard]] bool isHalfMoveDraw() const noexcept { return hfm_ >= 100; }

    [[nodiscard]] std::pair<GameResultReason, GameResult> getHalfMoveDrawType() const noexcept {
        if (!movegen::anylegalmoves(*this) && inCheck()) return {GameResultReason::CHECKMATE, GameResult::LOSE};
        return {GameResultReason::FIFTY_MOVE_RULE, GameResult::DRAW};
    }

    [[nodiscard]] bool isInsufficientMaterial() const noexcept {
        const int count = occ().count();

        if (count == 2) return true;

        if (count == 3) {
            if (pieces(PieceType::BISHOP) || pieces(PieceType::KNIGHT)) return true;
        }

        if (count == 4) {
            const Bitboard wb = pieces(PieceType::BISHOP, Color::WHITE);
            const Bitboard bb = pieces(PieceType::BISHOP, Color::BLACK);

            if (wb && bb && Square::same_color(Square(wb.lsb()), Square(bb.lsb()))) return true;

            if (wb.count() == 2) {
                if (Square::same_color(Square(wb.lsb()), Square(wb.msb()))) return true;
            } else if (bb.count() == 2) {
                if (Square::same_color(Square(bb.lsb()), Square(bb.msb()))) return true;
            }
        }

        return false;
    }

    [[nodiscard]] std::pair<GameResultReason, GameResult> isGameOver() const noexcept {
        if (isHalfMoveDraw()) return getHalfMoveDrawType();
        if (isInsufficientMaterial()) return {GameResultReason::INSUFFICIENT_MATERIAL, GameResult::DRAW};
        if (isRepetition()) return {GameResultReason::THREEFOLD_REPETITION, GameResult::DRAW};

        if (!movegen::anylegalmoves(*this)) {
            if (inCheck()) return {GameResultReason::CHECKMATE, GameResult::LOSE};
            return {GameResultReason::STALEMATE, GameResult::DRAW};
        }

        return {GameResultReason::NONE, GameResult::NONE};
    }

    /// Is `square` attacked by a piece of `color`?
    [[nodiscard]] BK_INLINE bool isAttacked(Square square, Color color) const noexcept {
        const int c      = static_cast<int>(color);
        const Bitboard o = occ();
        const Bitboard u = occ_bb_[c];
        if (attacks::pawn(Color(c ^ 1), square) & pieces_bb_[0] & u) return true;
        if (attacks::knight(square) & pieces_bb_[1] & u) return true;
        if (attacks::king(square) & pieces_bb_[5] & u) return true;
        if (attacks::bishop(square, o) & (pieces_bb_[2] | pieces_bb_[4]) & u) return true;
        if (attacks::rook(square, o) & (pieces_bb_[3] | pieces_bb_[4]) & u) return true;
        return false;
    }

    [[nodiscard]] BK_INLINE bool inCheck() const noexcept { return isAttacked(kingSq(stm_), ~stm_); }

    [[nodiscard]] CheckType givesCheck(const Move& m) const noexcept;

    [[nodiscard]] bool hasNonPawnMaterial(Color color) const noexcept {
        const Bitboard u = us(color);
        return bool(u & ~(pieces_bb_[0] | pieces_bb_[5]));
    }

    /// Full recomputation of the Zobrist key (expensive; prefer hash()).
    [[nodiscard]] U64 zobrist() const {
        U64 h       = 0ULL;
        Bitboard oc = occ();
        while (oc) {
            const int s = oc.pop();
            h ^= Zobrist::pieceIdx(static_cast<int>(board_[s]), s);
        }
        if (ep_sq_ != Square::NO_SQ) h ^= Zobrist::enpassant(ep_sq_.file());
        if (stm_ == Color::WHITE) h ^= Zobrist::sideToMove();
        h ^= Zobrist::castling(cr_.hashIndex());
        return h;
    }

    /// Hash after `move` without making it. EXACT == false skips ep/castling/promotion/castle-move updates.
    template <bool EXACT = true>
    [[nodiscard]] U64 zobristAfter(const Move& move) const {
        U64 key = key_ ^ Zobrist::sideToMove();
        if (EXACT && ep_sq_ != Square::NO_SQ) key ^= Zobrist::enpassant(ep_sq_.file());

        if (move == Move::NULL_MOVE) return key;

        const int from_i = move.from().index();
        const int to_i   = move.to().index();
        const int us_i   = static_cast<int>(stm_);
        const int pc     = static_cast<int>(board_[from_i]);
        const auto mtype = move.typeOf();
        const int target = static_cast<int>(board_[to_i]);

        if (target != 12 && mtype != Move::CASTLING) key ^= Zobrist::pieceIdx(target, to_i);

        if constexpr (EXACT) {
            const int before = cr_.bits();
            const int after  = before & castle_mask_[from_i] & castle_mask_[to_i];
            // a captured piece is never a "castling" target, castling moves touch only own pieces
            key ^= Zobrist::castling(before ^ after);

            if (bk::PIECE_TYPE[pc] == 0 && ((from_i ^ to_i) == 16)) {
                const Square eps(to_i ^ 8);
                if (attacks::pawn(Color(us_i), eps) & pieces_bb_[0] & occ_bb_[us_i ^ 1])
                    key ^= Zobrist::enpassant(eps.file());
            }
        }

        if (mtype != Move::CASTLING && mtype != Move::PROMOTION) {
            key ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(pc, to_i);
        } else if constexpr (EXACT) {
            if (mtype == Move::CASTLING) {
                const bool ks   = to_i > from_i;
                const int kto   = (ks ? 6 : 2) ^ (us_i * 56);
                const int rto   = (ks ? 5 : 3) ^ (us_i * 56);
                const int rook  = target;
                key ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(pc, kto);
                key ^= Zobrist::pieceIdx(rook, to_i) ^ Zobrist::pieceIdx(rook, rto);
            } else {
                const int promo = static_cast<int>(move.promotionType()) + 6 * us_i;
                key ^= Zobrist::pieceIdx(pc, from_i) ^ Zobrist::pieceIdx(promo, to_i);
            }
        }

        if (EXACT && mtype == Move::ENPASSANT) key ^= Zobrist::pieceIdx(6 * (us_i ^ 1), to_i ^ 8);

        return key;
    }

    [[nodiscard]] Bitboard getCastlingPath(Color c, bool isKingSide) const noexcept {
        return castling_path[static_cast<int>(c)][isKingSide];
    }

    friend std::ostream& operator<<(std::ostream& os, const Board& board);
    friend class movegen;

    // -----------------------------------------------------------------------------------------
    //  compact (24 byte) board encoding - format identical to chess.hpp's PackedBoard
    // -----------------------------------------------------------------------------------------
    class Compact {
        friend class Board;
        Compact() = default;

       public:
        static PackedBoard encode(const Board& board) { return encodeState(board); }

        static PackedBoard encode(std::string_view fen, bool chess960 = false) { return encodeState(fen, chess960); }

        static Board decode(const PackedBoard& compressed, bool chess960 = false) {
            Board board     = Board(ProtectedCtor::CREATE);
            board.chess960_ = chess960;
            decode(board, compressed);
            return board;
        }

       private:
        // bytes 0..7: occupancy (big endian, byte 0 = rank 8); then one nibble per piece in ascending
        // square order. Nibbles 12..15 carry extra info: 12 pawn that can be captured en passant,
        // 13/14 white/black rook with castling rights, 15 black king when black is to move.
        static void putNibble(PackedBoard& p, int idx, unsigned v) {
            p[idx / 2] = static_cast<std::uint8_t>(p[idx / 2] | (v << ((idx & 1) ? 0 : 4)));
        }

        static void putOccupancy(PackedBoard& p, std::uint64_t occ) {
            for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(occ >> (56 - 8 * i));
        }

        static PackedBoard encodeState(const Board& board) {
            PackedBoard packed{};
            putOccupancy(packed, board.occ().getBits());

            int idx     = 16;
            Bitboard oc = board.occ();
            while (oc) {
                const Square sq(oc.pop());
                putNibble(packed, idx++, meaning(board.cr_, board.stm_, board.ep_sq_, sq, board.at(sq)));
            }
            return packed;
        }

        static PackedBoard encodeState(std::string_view fen, bool chess960) {
            if (chess960) return encodeState(Board(fen, true));

            PackedBoard packed{};
            while (!fen.empty() && fen[0] == ' ') fen.remove_prefix(1);

            const auto tok        = splitTokens<4>(fen);
            const auto position   = tok[0].value_or(std::string_view(""));
            const auto move_right = tok[1].value_or(std::string_view("w"));
            const auto castling   = tok[2].value_or(std::string_view("-"));
            const auto en_passant = tok[3].value_or(std::string_view("-"));

            const Square ep = en_passant == "-" ? Square(Square::NO_SQ) : Square(en_passant);
            const Color stm = move_right == "w" ? Color(Color::WHITE) : Color(Color::BLACK);

            CastlingRights cr;
            for (char ch : castling) {
                if (ch == '-') break;
                if (ch == 'K') cr.setCastlingRight(Color::WHITE, CastlingRights::Side::KING_SIDE, File::FILE_H);
                if (ch == 'Q') cr.setCastlingRight(Color::WHITE, CastlingRights::Side::QUEEN_SIDE, File::FILE_A);
                if (ch == 'k') cr.setCastlingRight(Color::BLACK, CastlingRights::Side::KING_SIDE, File::FILE_H);
                if (ch == 'q') cr.setCastlingRight(Color::BLACK, CastlingRights::Side::QUEEN_SIDE, File::FILE_A);
            }

            // collect rank strings (rank 8 first), then walk them from rank 1 upwards
            std::array<std::string_view, 8> rows{};
            int n = 0;
            for (std::size_t b = 0; n < 8;) {
                const auto e = position.find('/', b);
                rows[n++]    = position.substr(b, e == std::string_view::npos ? e : e - b);
                if (e == std::string_view::npos) break;
                b = e + 1;
            }

            int idx = 16, square = 0;
            Bitboard occ;
            for (int r = n - 1; r >= 0; --r) {
                for (char ch : rows[r]) {
                    if (ch >= '0' && ch <= '9') {
                        square += ch - '0';
                    } else if (ch == '/') {
                        ++square;
                    } else {
                        const Piece p(std::string_view(&ch, 1));
                        putNibble(packed, idx++, meaning(cr, stm, ep, Square(square), p));
                        occ.set(square);
                        ++square;
                    }
                }
            }
            putOccupancy(packed, occ.getBits());
            return packed;
        }

        static void decode(Board& board, const PackedBoard& packed) {
            std::uint64_t occupied = 0;
            for (int i = 0; i < 8; ++i) occupied |= static_cast<std::uint64_t>(packed[i]) << (56 - 8 * i);

            File white_castle[2] = {File::NO_FILE, File::NO_FILE};
            File black_castle[2] = {File::NO_FILE, File::NO_FILE};
            int wi = 0, bi = 0;

            board.hfm_   = 0;
            board.plies_ = 0;
            board.stm_   = Color::WHITE;
            board.ep_sq_ = Square::NO_SQ;
            board.cr_.clear();
            board.prev_states_.clear();
            board.original_fen_.clear();
            board.occ_bb_.fill(Bitboard(0));
            board.pieces_bb_.fill(Bitboard(0));
            board.board_.fill(Piece::NONE);

            int idx = 16;
            Bitboard oc(occupied);
            while (oc) {
                const Square sq(oc.pop());
                const unsigned nib = (packed[idx / 2] >> ((idx & 1) ? 0 : 4)) & 0xF;
                ++idx;

                if (nib < 12) {
                    board.placePiece(Piece(static_cast<Piece::underlying>(nib)), sq);
                } else if (nib == 12) {
                    board.ep_sq_ = sq.ep_square();
                    board.placePiece(Piece(PieceType::PAWN, sq.rank() == Rank::RANK_4 ? Color::WHITE : Color::BLACK),
                                     sq);
                } else if (nib == 13) {
                    if (wi < 2) white_castle[wi++] = sq.file();
                    board.placePiece(Piece(PieceType::ROOK, Color::WHITE), sq);
                } else if (nib == 14) {
                    if (bi < 2) black_castle[bi++] = sq.file();
                    board.placePiece(Piece(PieceType::ROOK, Color::BLACK), sq);
                } else {
                    board.stm_ = Color::BLACK;
                    board.placePiece(Piece(PieceType::KING, Color::BLACK), sq);
                }
            }

            for (int i = 0; i < 2; ++i) {
                if (white_castle[i] != File::NO_FILE) {
                    const auto side = CastlingRights::closestSide(white_castle[i], board.kingSq(Color::WHITE).file());
                    board.cr_.setCastlingRight(Color::WHITE, side, white_castle[i]);
                }
                if (black_castle[i] != File::NO_FILE) {
                    const auto side = CastlingRights::closestSide(black_castle[i], board.kingSq(Color::BLACK).file());
                    board.cr_.setCastlingRight(Color::BLACK, side, black_castle[i]);
                }
            }

            if (board.stm_ == Color::BLACK) ++board.plies_;

            board.key_ = board.zobrist();
            board.refreshCastlingData();
        }

        static unsigned meaning(const CastlingRights& cr, Color stm, Square ep, Square sq, Piece piece) {
            const PieceType t = piece.type();
            if (t == PieceType::PAWN && ep != Square::NO_SQ && Square(sq.index() ^ 8) == ep) return 12;

            if (t == PieceType::ROOK) {
                const Color c = piece.color();
                if (Square::back_rank(sq, c) &&
                    (cr.getRookFile(c, CastlingRights::Side::KING_SIDE) == sq.file() ||
                     cr.getRookFile(c, CastlingRights::Side::QUEEN_SIDE) == sq.file()))
                    return c == Color::WHITE ? 13u : 14u;
            }

            if (t == PieceType::KING && piece.color() == Color::BLACK && stm == Color::BLACK) return 15;

            return static_cast<unsigned>(static_cast<int>(piece));
        }
    };

    bool operator==(const Board& other) const noexcept {
        return pieces_bb_ == other.pieces_bb_ && occ_bb_ == other.occ_bb_ && board_ == other.board_ &&
               key_ == other.key_ && cr_ == other.cr_ && plies_ == other.plies_ && stm_ == other.stm_ &&
               ep_sq_ == other.ep_sq_ && hfm_ == other.hfm_ && chess960_ == other.chess960_ &&
               castling_path == other.castling_path;
    }

    /// Like operator== but additionally compares the stored state history.
    bool fullyEquals(const Board& other) const noexcept { return prev_states_ == other.prev_states_ && *this == other; }

   protected:
    BK_VIRTUAL void placePiece(Piece piece, Square sq) { placePieceInternal(piece, sq); }
    BK_VIRTUAL void removePiece(Piece piece, Square sq) { removePieceInternal(piece, sq); }

    std::vector<State> prev_states_;

    std::array<Bitboard, 6> pieces_bb_ = {};
    std::array<Bitboard, 2> occ_bb_    = {};
    std::array<Piece, 64> board_       = {};

    U64 key_             = 0ULL;
    CastlingRights cr_   = {};
    std::uint16_t plies_ = 0;
    Color stm_           = Color::WHITE;
    Square ep_sq_        = Square::NO_SQ;
    std::uint8_t hfm_    = 0;

    bool chess960_ = false;

    std::array<std::array<Bitboard, 2>, 2> castling_path = {};

    // rights that survive a piece leaving / entering each square (4 bit mask, 15 = nothing lost)
    std::array<std::uint8_t, 64> castle_mask_ = [] {
        std::array<std::uint8_t, 64> a{};
        for (auto& v : a) v = 15;
        return a;
    }();

   private:
    // Remembers (per object) whether this is exactly a `Board`, i.e. nobody can have overridden the
    // piece hooks. Copies start unresolved: a Derived may be built from a plain Board and vice versa.
    struct HookProbe {
        mutable std::int8_t v = -1;
        HookProbe()           = default;
        HookProbe(const HookProbe&) noexcept : v(-1) {}
        HookProbe& operator=(const HookProbe&) noexcept { return *this; }
    };
    HookProbe probe_;

    [[nodiscard]] BK_INLINE bool plainBoard() const noexcept {
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
        if (BK_UNLIKELY(probe_.v < 0)) probe_.v = (typeid(*this) == typeid(Board)) ? 1 : 0;
        return probe_.v == 1;
#else
        return false;
#endif
    }

    BK_INLINE void moveInternal(Piece piece, Square from, Square to) {
        const int p      = static_cast<int>(piece);
        const U64 ft     = (1ULL << from.index()) | (1ULL << to.index());
        pieces_bb_[bk::PIECE_TYPE[p]] ^= Bitboard(ft);
        occ_bb_[bk::PIECE_COLOR[p]] ^= Bitboard(ft);
        board_[from.index()] = Piece::NONE;
        board_[to.index()]   = piece;
    }

    BK_INLINE void removePieceInternal(Piece piece, Square sq) {
        assert(board_[sq.index()] == piece && piece != Piece::NONE);
        const int p   = static_cast<int>(piece);
        const U64 bit = 1ULL << sq.index();
        pieces_bb_[bk::PIECE_TYPE[p]] ^= Bitboard(bit);
        occ_bb_[bk::PIECE_COLOR[p]] ^= Bitboard(bit);
        board_[sq.index()] = Piece::NONE;
    }

    BK_INLINE void placePieceInternal(Piece piece, Square sq) {
        assert(board_[sq.index()] == Piece::NONE && piece != Piece::NONE);
        const int p   = static_cast<int>(piece);
        const U64 bit = 1ULL << sq.index();
        pieces_bb_[bk::PIECE_TYPE[p]] |= Bitboard(bit);
        occ_bb_[bk::PIECE_COLOR[p]] |= Bitboard(bit);
        board_[sq.index()] = piece;
    }

    // Is there a legal en passant capture onto `ep` for `capturer`? (The pawn that double pushed is
    // already on ep^8, the capturer is to move.)
    [[nodiscard]] bool epCaptureExists(Color capturer, Square ep) const noexcept {
        const int c         = static_cast<int>(capturer);
        const Square victim = Square(ep.index() ^ 8);
        if (!(occ_bb_[c ^ 1] & pieces_bb_[0] & Bitboard::fromSquare(victim))) return false;

        Bitboard candidates = attacks::pawn(Color(c ^ 1), ep) & pieces_bb_[0] & occ_bb_[c];
        if (!candidates) return false;
        if (!(pieces_bb_[5] & occ_bb_[c])) return false;

        const Square ksq      = kingSq(capturer);
        const Bitboard vbb    = Bitboard::fromSquare(victim);
        const Bitboard theirs = occ_bb_[c ^ 1] ^ vbb;
        const Bitboard base   = occ() ^ vbb ^ Bitboard::fromSquare(ep);
        const Bitboard rq     = theirs & (pieces_bb_[3] | pieces_bb_[4]);
        const Bitboard bq     = theirs & (pieces_bb_[2] | pieces_bb_[4]);

        if ((attacks::knight(ksq) & theirs & pieces_bb_[1]) ||
            (attacks::pawn(capturer, ksq) & theirs & pieces_bb_[0] & ~vbb))
            return false;

        while (candidates) {
            const Bitboard o = base ^ Bitboard::fromSquare(candidates.pop());
            if (!(attacks::rook(ksq, o) & rq) && !(attacks::bishop(ksq, o) & bq)) return true;
        }
        return false;
    }

    [[nodiscard]] static char castlingFileToken(Color color, File file) {
        const char ch = static_cast<char>('a' + static_cast<int>(file));
        return color == Color::WHITE ? static_cast<char>(std::toupper(ch)) : ch;
    }

    [[nodiscard]] Square kingSqOrNone(Color c) const noexcept {
        const Bitboard k = pieces_bb_[5] & occ_bb_[static_cast<int>(c)];
        return k ? Square(k.lsb()) : Square(Square::NO_SQ);
    }

    [[nodiscard]] bool isRookOf(Color c, Square s) const noexcept {
        return s.is_valid() && board_[s.index()] == Piece(PieceType::ROOK, c);
    }

    [[nodiscard]] bool hasRookOnFile(Color color, File file) const {
        const Square k = kingSqOrNone(color);
        if (k == Square::NO_SQ || static_cast<int>(file) > 7) return false;
        return isRookOf(color, Square(file, k.rank()));
    }

    // nearest rook (to the king) on the given side, scanning the back rank
    [[nodiscard]] File findRookOnSide(Color color, CastlingRights::Side side) const {
        const Square k = kingSqOrNone(color);
        if (k == Square::NO_SQ || !Square::back_rank(k, color)) return File(File::NO_FILE);
        const bool ks = side == CastlingRights::Side::KING_SIDE;
        for (int f = k.file() + (ks ? 1 : -1); f >= 0 && f <= 7; f += ks ? 1 : -1)
            if (isRookOf(color, Square(File(f), k.rank()))) return File(f);
        return File(File::NO_FILE);
    }

    // outermost rook on the given side of the king (X-FEN semantics)
    [[nodiscard]] File outerRookFile(Color color, CastlingRights::Side side) const {
        const Square k = kingSqOrNone(color);
        if (k == Square::NO_SQ) return File(File::NO_FILE);
        const bool ks = side == CastlingRights::Side::KING_SIDE;
        for (int f = ks ? 7 : 0; ks ? f > k.file() : f < k.file(); f += ks ? -1 : 1)
            if (isRookOf(color, Square(File(f), k.rank()))) return File(f);
        return File(File::NO_FILE);
    }

    void applyCastlingRight(Color color, CastlingRights::Side side, File fallback) {
        File file = fallback;
        if (!hasRookOnFile(color, fallback)) {
            file = findRookOnSide(color, side);
            if (file == File::NO_FILE) return;
        }
        cr_.setCastlingRight(color, side, file);
    }

    void parseCastlingField(std::string_view field, bool xfen) {
        using Side = CastlingRights::Side;
        for (char ch : field) {
            if (ch == '-') break;

            if (!xfen && !chess960_) {
                if (ch == 'K') applyCastlingRight(Color::WHITE, Side::KING_SIDE, File::FILE_H);
                else if (ch == 'Q') applyCastlingRight(Color::WHITE, Side::QUEEN_SIDE, File::FILE_A);
                else if (ch == 'k') applyCastlingRight(Color::BLACK, Side::KING_SIDE, File::FILE_H);
                else if (ch == 'q') applyCastlingRight(Color::BLACK, Side::QUEEN_SIDE, File::FILE_A);
                continue;
            }

            const Color color = std::isupper(static_cast<unsigned char>(ch)) ? Color(Color::WHITE) : Color(Color::BLACK);
            const Square k    = kingSqOrNone(color);
            if (k == Square::NO_SQ) continue;

            const char lower = utils::tolower(ch);
            if (lower == 'k' || lower == 'q') {
                const Side side = lower == 'k' ? Side::KING_SIDE : Side::QUEEN_SIDE;
                const File file = xfen ? outerRookFile(color, side) : findRookOnSide(color, side);
                if (file != File::NO_FILE) cr_.setCastlingRight(color, side, file);
                continue;
            }

            if (lower < 'a' || lower > 'h') continue;
            const File file(lower - 'a');
            if (file == k.file() || !hasRookOnFile(color, file)) continue;
            cr_.setCastlingRight(color, CastlingRights::closestSide(file, k.file()), file);
        }
    }

    void appendXfenToken(std::string& out, Color color, CastlingRights::Side side) const {
        if (!cr_.has(color, side)) return;
        const File rook  = cr_.getRookFile(color, side);
        const File outer = outerRookFile(color, side);
        if (outer != File::NO_FILE && rook == outer) {
            const char t = side == CastlingRights::Side::KING_SIDE ? 'K' : 'Q';
            out += color == Color::WHITE ? t : utils::tolower(t);
            return;
        }
        out += castlingFileToken(color, rook);
    }

    // rebuild castling_path and castle_mask_ from cr_ and the current king squares
    void refreshCastlingData() {
        castling_path = {};
        for (auto& m : castle_mask_) m = 15;

        for (int c = 0; c < 2; ++c) {
            const Square k = kingSqOrNone(Color(c));
            if (k == Square::NO_SQ) continue;
            for (int s = 0; s < 2; ++s) {
                const auto side = static_cast<CastlingRights::Side>(s);
                if (!cr_.has(Color(c), side)) continue;

                const bool ks        = side == CastlingRights::Side::KING_SIDE;
                const Square rook_f  = Square(cr_.getRookFile(Color(c), side), k.rank());
                const Square king_to = Square::castling_king_square(ks, Color(c));
                const Square rook_to = Square::castling_rook_square(ks, Color(c));

                castling_path[c][ks] = (Bitboard(bk::ST.between[rook_f.index()][rook_to.index()]) |
                                        Bitboard(bk::ST.between[k.index()][king_to.index()])) &
                                       ~(Bitboard::fromSquare(k) | Bitboard::fromSquare(rook_f));

                castle_mask_[rook_f.index()] &= static_cast<std::uint8_t>(~(1u << (c * 2 + s)));
                castle_mask_[k.index()] &= static_cast<std::uint8_t>(~(3u << (2 * c)));
            }
        }
    }

    template <int N>
    static std::array<std::optional<std::string_view>, N> splitTokens(std::string_view s, char delim = ' ') {
        std::array<std::optional<std::string_view>, N> out = {};
        std::size_t start                                  = 0;
        for (int i = 0; i < N; ++i) {
            const std::size_t end = s.find(delim, start);
            if (end == std::string_view::npos) {
                out[i] = s.substr(start);
                break;
            }
            out[i] = s.substr(start, end - start);
            start  = end + 1;
        }
        return out;
    }

    template <bool ctor>
    bool parseFen(std::string_view fen, bool xfen, bool require_kings) {
        original_fen_ = fen;
        reset();

        while (!fen.empty() && fen[0] == ' ') fen.remove_prefix(1);
        if (fen.empty()) return false;

        const auto tok        = splitTokens<6>(fen);
        const auto position   = tok[0].value_or(std::string_view(""));
        const auto move_right = tok[1].value_or(std::string_view("w"));
        const auto castling   = tok[2].value_or(std::string_view("-"));
        const auto en_passant = tok[3].value_or(std::string_view("-"));
        const auto half_move  = tok[4].value_or(std::string_view("0"));
        const auto full_move  = tok[5].value_or(std::string_view("1"));

        if (position.empty()) return false;
        if (move_right != "w" && move_right != "b") return false;

        hfm_   = static_cast<std::uint8_t>(detail::parseStringViewToInt(half_move).value_or(0));
        plies_ = static_cast<std::uint16_t>(detail::parseStringViewToInt(full_move).value_or(1) * 2 - 2);

        if (en_passant != "-") {
            if (!Square::is_valid_string_sq(en_passant)) return false;
            ep_sq_ = Square(en_passant);
        }

        stm_ = move_right == "w" ? Color(Color::WHITE) : Color(Color::BLACK);
        if (stm_ == Color::BLACK)
            ++plies_;
        else
            key_ ^= Zobrist::sideToMove();

        int square = 56;
        for (char ch : position) {
            if (ch >= '0' && ch <= '9') {
                square += ch - '0';
            } else if (ch == '/') {
                square -= 16;
            } else {
                const Piece p(std::string_view(&ch, 1));
                if (p == Piece::NONE || !Square::is_valid_sq(square) || board_[square] != Piece::NONE) return false;
                if constexpr (ctor)
                    placePieceInternal(p, Square(square));
                else
                    placePiece(p, Square(square));
                key_ ^= Zobrist::piece(p, Square(square));
                ++square;
            }
        }

        if (require_kings && (!(pieces_bb_[5] & occ_bb_[0]) || !(pieces_bb_[5] & occ_bb_[1]))) return false;

        parseCastlingField(castling, xfen);

        if (ep_sq_ != Square::NO_SQ) {
            const bool rank_ok = (ep_sq_.rank() == Rank::RANK_3 && stm_ == Color::BLACK) ||
                                 (ep_sq_.rank() == Rank::RANK_6 && stm_ == Color::WHITE);
            if (rank_ok && epCaptureExists(stm_, ep_sq_))
                key_ ^= Zobrist::enpassant(ep_sq_.file());
            else
                ep_sq_ = Square::NO_SQ;
        }

        key_ ^= Zobrist::castling(cr_.hashIndex());

        refreshCastlingData();
        return true;
    }

    template <bool ctor = false>
    bool setFenInternal(std::string_view fen) {
        return parseFen<ctor>(fen, false, false);
    }

    [[nodiscard]] std::string fenString(bool move_counters, bool xfen) const {
        std::string out;
        out.reserve(100);

        for (int rank = 7; rank >= 0; --rank) {
            int gap = 0;
            for (int file = 0; file < 8; ++file) {
                const Piece p = board_[rank * 8 + file];
                if (p != Piece::NONE) {
                    if (gap) out += static_cast<char>('0' + gap);
                    gap = 0;
                    out += static_cast<std::string>(p);
                } else {
                    ++gap;
                }
            }
            if (gap) out += static_cast<char>('0' + gap);
            if (rank > 0) out += '/';
        }

        out += ' ';
        out += stm_ == Color::WHITE ? 'w' : 'b';
        out += ' ';

        if (cr_.isEmpty()) {
            out += '-';
        } else if (xfen) {
            appendXfenToken(out, Color::WHITE, CastlingRights::Side::KING_SIDE);
            appendXfenToken(out, Color::WHITE, CastlingRights::Side::QUEEN_SIDE);
            appendXfenToken(out, Color::BLACK, CastlingRights::Side::KING_SIDE);
            appendXfenToken(out, Color::BLACK, CastlingRights::Side::QUEEN_SIDE);
        } else {
            out += getCastleString();
        }

        out += ' ';
        out += ep_sq_ == Square::NO_SQ ? std::string("-") : static_cast<std::string>(ep_sq_);

        if (move_counters) {
            out += ' ';
            out += std::to_string(halfMoveClock());
            out += ' ';
            out += std::to_string(fullMoveNumber());
        }
        return out;
    }

    void reset() {
        occ_bb_.fill(Bitboard(0));
        pieces_bb_.fill(Bitboard(0));
        board_.fill(Piece::NONE);

        stm_   = Color::WHITE;
        ep_sq_ = Square::NO_SQ;
        hfm_   = 0;
        plies_ = 1;
        key_   = 0ULL;
        cr_.clear();
        prev_states_.clear();
    }

    // original FEN, so set960() can re-parse it
    std::string original_fen_;
};

inline std::ostream& operator<<(std::ostream& os, const Board& b) {
    for (int i = 63; i >= 0; i -= 8) {
        for (int j = 7; j >= 0; j--) os << " " << static_cast<std::string>(b.board_[i - j]);
        os << "\n";
    }

    os << "\n\n";
    os << "Side to move: " << static_cast<int>(b.stm_.internal()) << "\n";
    os << "Castling rights: " << b.getCastleString() << "\n";
    os << "Halfmoves: " << b.halfMoveClock() << "\n";
    os << "Fullmoves: " << b.fullMoveNumber() << "\n";
    os << "EP: " << b.ep_sq_.index() << "\n";
    os << "Hash: " << b.key_ << "\n";

    os << std::endl;

    return os;
}

[[nodiscard]] inline Bitboard attacks::attackers(const Board& board, Color color, Square square) noexcept {
    const Bitboard occupied = board.occ();
    const Bitboard queens   = board.pieces(PieceType::QUEEN, color);

    Bitboard atk = attacks::pawn(~color, square) & board.pieces(PieceType::PAWN, color);
    atk |= attacks::knight(square) & board.pieces(PieceType::KNIGHT, color);
    atk |= attacks::bishop(square, occupied) & (board.pieces(PieceType::BISHOP, color) | queens);
    atk |= attacks::rook(square, occupied) & (board.pieces(PieceType::ROOK, color) | queens);
    atk |= attacks::king(square) & board.pieces(PieceType::KING, color);
    return atk & occupied;
}

inline CheckType Board::givesCheck(const Move& m) const noexcept {
    assert(at(m.from()).color() == stm_);

    const int us_i       = static_cast<int>(stm_);
    const Square from    = m.from();
    const Square to      = m.to();
    const Square ksq     = kingSq(~stm_);
    const Bitboard fromB = Bitboard::fromSquare(from);
    const Bitboard toB   = Bitboard::fromSquare(to);
    const auto mtype     = m.typeOf();

    Bitboard o        = occ() ^ fromB;  // occupancy after the mover has left
    Bitboard ours     = occ_bb_[us_i] ^ fromB;
    PieceType landing = at<PieceType>(from);
    Square rook_to;
    bool castle = false;

    if (mtype == Move::CASTLING) {
        castle        = true;
        const bool ks = to > from;
        rook_to       = Square::castling_rook_square(ks, stm_);
        const Square kto = Square::castling_king_square(ks, stm_);
        o ^= toB;  // the rook leaves its square ...
        o |= Bitboard::fromSquare(kto) | Bitboard::fromSquare(rook_to);
        ours ^= toB;
        ours |= Bitboard::fromSquare(kto);  // king (not a slider) ; rook handled separately
        landing = PieceType::KING;
    } else {
        o |= toB;
        if (mtype == Move::ENPASSANT) o ^= Bitboard::fromSquare(Square(to.index() ^ 8));
        if (mtype == Move::PROMOTION) landing = m.promotionType();
    }

    // direct check from the piece that moved
    Bitboard direct;
    switch (static_cast<int>(landing)) {
        case 0: direct = attacks::pawn(~stm_, ksq) & toB; break;
        case 1: direct = attacks::knight(ksq) & toB; break;
        case 2: direct = attacks::bishop(ksq, o) & toB; break;
        case 3: direct = attacks::rook(ksq, o) & toB; break;
        case 4: direct = attacks::queen(ksq, o) & toB; break;
        default: break;
    }
    // chess.hpp convention: for a promotion an uncovered slider is reported before the new piece's own check
    if (direct && mtype != Move::PROMOTION) return CheckType::DIRECT_CHECK;

    if (castle) {
        if (attacks::rook(ksq, o) & Bitboard::fromSquare(rook_to)) return CheckType::DISCOVERY_CHECK;
    }

    // discovered check: a slider of ours whose line to the king ran through a square we just vacated
    Bitboard vacated = fromB;
    if (castle) vacated |= toB;
    if (mtype == Move::ENPASSANT) vacated |= Bitboard::fromSquare(Square(to.index() ^ 8));

    Bitboard others = ours;
    if (!castle) others &= ~toB;  // the mover itself was handled above

    if (vacated & Bitboard(bk::ST.pseudoRook[ksq.index()])) {
        if (attacks::rook(ksq, o) & others & (pieces_bb_[3] | pieces_bb_[4])) return CheckType::DISCOVERY_CHECK;
    }
    if (vacated & Bitboard(bk::ST.pseudoBishop[ksq.index()])) {
        if (attacks::bishop(ksq, o) & others & (pieces_bb_[2] | pieces_bb_[4])) return CheckType::DISCOVERY_CHECK;
    }

    return direct ? CheckType::DIRECT_CHECK : CheckType::NO_CHECK;
}

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  movegen implementation
// ---------------------------------------------------------------------------------------------
namespace chess {

namespace bk {
BK_INLINE Move mkMove(int from, int to) noexcept { return Move(static_cast<std::uint16_t>((from << 6) | to)); }
BK_INLINE Move mkMove(std::uint16_t type, int from, int to, int promo) noexcept {
    return Move(static_cast<std::uint16_t>(type | ((promo - 1) << 12) | (from << 6) | to));
}
BK_INLINE bool singleBit(std::uint64_t x) noexcept { return (x & (x - 1)) == 0; }

// is `sq` attacked by `atk` given an explicit occupancy?
BK_INLINE bool attackedBy(const Board& b, Color atk, Square sq, Bitboard occ) noexcept {
    if (attacks::pawn(~atk, sq) & b.pieces(PieceType::PAWN, atk)) return true;
    if (attacks::knight(sq) & b.pieces(PieceType::KNIGHT, atk)) return true;
    if (attacks::king(sq) & b.pieces(PieceType::KING, atk)) return true;
    if (attacks::bishop(sq, occ) & b.pieces(PieceType::BISHOP, PieceType::QUEEN) & b.us(atk)) return true;
    if (attacks::rook(sq, occ) & b.pieces(PieceType::ROOK, PieceType::QUEEN) & b.us(atk)) return true;
    return false;
}
}  // namespace bk

template <Color::underlying C, movegen::MoveGenType MT, bool ONE>
inline void movegen::generate(Movelist& ml, const Board& b, int pieces) {
    constexpr Color US   = C;
    constexpr Color THEM = ~C;

    Move* const start = ml.moves_.data() + ml.size_;
    Move* out         = start;

    const Bitboard ours   = b.us(US);
    const Bitboard theirs = b.us(THEM);
    const Bitboard occ    = ours | theirs;

    const Square ksq  = b.kingSq(US);
    const int ki      = ksq.index();
    const Bitboard kb = Bitboard::fromSquare(ksq);

    const Bitboard t_pawns   = b.pieces(PieceType::PAWN, THEM);
    const Bitboard t_knights = b.pieces(PieceType::KNIGHT, THEM);
    const Bitboard t_king    = b.pieces(PieceType::KING, THEM);
    const Bitboard t_queens  = b.pieces(PieceType::QUEEN, THEM);
    const Bitboard t_bq      = b.pieces(PieceType::BISHOP, THEM) | t_queens;
    const Bitboard t_rq      = b.pieces(PieceType::ROOK, THEM) | t_queens;

    // ---- checkers and pins, discovered through the king's rays ---------------------------------
    Bitboard checkmask = (attacks::knight(ksq) & t_knights) | (attacks::pawn(US, ksq) & t_pawns);
    int checks         = checkmask.count();
    Bitboard pin_hv, pin_d;

    {
        Bitboard snipers = Bitboard(bk::ST.pseudoRook[ki]) & t_rq;
        while (snipers) {
            const int s         = snipers.pop();
            const Bitboard line = Bitboard(bk::ST.between[ki][s]);  // king..sniper, sniper included
            const Bitboard blk  = line & occ & ~Bitboard::fromSquare(s);
            if (!blk) {
                ++checks;
                checkmask |= line;
            } else if (bk::singleBit(blk.getBits()) && (blk & ours)) {
                pin_hv |= line;
            }
        }
        snipers = Bitboard(bk::ST.pseudoBishop[ki]) & t_bq;
        while (snipers) {
            const int s         = snipers.pop();
            const Bitboard line = Bitboard(bk::ST.between[ki][s]);
            const Bitboard blk  = line & occ & ~Bitboard::fromSquare(s);
            if (!blk) {
                ++checks;
                checkmask |= line;
            } else if (bk::singleBit(blk.getBits()) && (blk & ours)) {
                pin_d |= line;
            }
        }
    }
    if (checks == 0) checkmask = constants::DEFAULT_CHECKMASK;

    auto commit = [&] { ml.size_ = static_cast<int>(out - ml.moves_.data()); };

    // ---- king ----------------------------------------------------------------------------------
    if (pieces & PieceGenType::KING) {
        const Bitboard occ_nk = occ ^ kb;

        auto unsafe = [&](int sq) -> bool {
            const Square s(sq);
            if (attacks::pawn(US, s) & t_pawns) return true;
            if (attacks::knight(s) & t_knights) return true;
            if (attacks::king(s) & t_king) return true;
            // a slider can only hit `s` if it is aligned with it on the empty board
            if ((Bitboard(bk::ST.pseudoBishop[sq]) & t_bq) && (attacks::bishop(s, occ_nk) & t_bq)) return true;
            if ((Bitboard(bk::ST.pseudoRook[sq]) & t_rq) && (attacks::rook(s, occ_nk) & t_rq)) return true;
            return false;
        };

        Bitboard targets;
        if constexpr (MT == MoveGenType::ALL)
            targets = ~ours;
        else if constexpr (MT == MoveGenType::CAPTURE)
            targets = theirs;
        else
            targets = ~occ;

        Bitboard cand            = attacks::king(ksq) & targets;
        const Bitboard evaluated = cand;  // squares whose safety is known after this loop ...
        Bitboard bad;                     // ... and the ones among them that are attacked
        while (cand) {
            const int to = cand.pop();
            if (unsafe(to))
                bad.set(to);
            else
                *out++ = bk::mkMove(ki, to);
        }

        if (MT != MoveGenType::CAPTURE && checks == 0 && ((b.cr_.bits() >> (2 * static_cast<int>(US))) & 3) &&
            Square::back_rank(ksq, US)) {
            Bitboard rooks_bb;
            for (int s = 0; s < 2; ++s) {
                const auto side = static_cast<Board::CastlingRights::Side>(s);
                if (!b.cr_.has(US, side)) continue;
                const bool ks = side == Board::CastlingRights::Side::KING_SIDE;

                if (occ & b.castling_path[static_cast<int>(US)][ks]) continue;

                const Square king_to = Square::castling_king_square(ks, US);
                Bitboard walk        = Bitboard(bk::ST.between[ki][king_to.index()]);
                bool ok              = true;
                while (walk) {
                    const int w = walk.pop();
                    if (evaluated.check(w) ? bad.check(w) : unsafe(w)) {
                        ok = false;
                        break;
                    }
                }
                if (!ok) continue;

                const Square rook_from(b.cr_.getRookFile(US, side), ksq.rank());

                if (b.chess960_) {
                    // the rook leaving the back rank must not expose the king on its destination
                    const Square rook_to = Square::castling_rook_square(ks, US);
                    const Bitboard fin   = ((occ ^ kb ^ Bitboard::fromSquare(rook_from)) |
                                          Bitboard::fromSquare(king_to) | Bitboard::fromSquare(rook_to));
                    if ((attacks::rook(king_to, fin) & t_rq) || (attacks::bishop(king_to, fin) & t_bq)) continue;
                }

                rooks_bb.set(rook_from.index());
            }
            while (rooks_bb) *out++ = bk::mkMove(Move::CASTLING, ki, rooks_bb.pop(), 1);
        }

        if constexpr (ONE) {
            if (out != start) return commit();
        }
    }

    if (checks == 2) return commit();

    Bitboard movable;
    if constexpr (MT == MoveGenType::ALL)
        movable = ~ours;
    else if constexpr (MT == MoveGenType::CAPTURE)
        movable = theirs;
    else
        movable = ~occ;
    movable &= checkmask;

    // ---- pawns ---------------------------------------------------------------------------------
    if (pieces & PieceGenType::PAWN) {
        constexpr auto UP         = make_direction(Direction::NORTH, C);
        constexpr auto UP_LEFT    = make_direction(Direction::NORTH_WEST, C);
        constexpr auto UP_RIGHT   = make_direction(Direction::NORTH_EAST, C);
        constexpr int D_UP        = static_cast<int>(UP);
        constexpr int D_LEFT      = static_cast<int>(UP_LEFT);
        constexpr int D_RIGHT     = static_cast<int>(UP_RIGHT);
        constexpr Bitboard R_FROM = Bitboard(Rank::rank(Rank::RANK_7, C).bb());
        constexpr Bitboard R_PROM = Bitboard(Rank::rank(Rank::RANK_8, C).bb());
        constexpr Bitboard R_DBL  = Bitboard(Rank::rank(Rank::RANK_3, C).bb());

        const Bitboard pawns = b.pieces(PieceType::PAWN, US);
        const Bitboard empty = ~occ;

        const Bitboard lr_pawns  = pawns & ~pin_hv;
        const Bitboard lr_free   = lr_pawns & ~pin_d;
        const Bitboard lr_pinned = lr_pawns & pin_d;

        Bitboard capL = (attacks::shift<UP_LEFT>(lr_free) | (attacks::shift<UP_LEFT>(lr_pinned) & pin_d)) & theirs &
                        checkmask;
        Bitboard capR = (attacks::shift<UP_RIGHT>(lr_free) | (attacks::shift<UP_RIGHT>(lr_pinned) & pin_d)) &
                        theirs & checkmask;

        const Bitboard fw_pawns  = pawns & ~pin_d;
        const Bitboard fw_free   = fw_pawns & ~pin_hv;
        const Bitboard fw_pinned = fw_pawns & pin_hv;

        Bitboard push1 = (attacks::shift<UP>(fw_free) & empty) | (attacks::shift<UP>(fw_pinned) & pin_hv & empty);
        Bitboard push2 = attacks::shift<UP>(push1 & R_DBL) & empty & checkmask;
        push1 &= checkmask;

        if (pawns & R_FROM) {
            Bitboard pl = capL & R_PROM;
            Bitboard pr = capR & R_PROM;
            Bitboard pp = push1 & R_PROM;

            if (MT != MoveGenType::QUIET) {
                while (pl) {
                    const int to = pl.pop(), from = to - D_LEFT;
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 4);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 3);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 2);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 1);
                }
                while (pr) {
                    const int to = pr.pop(), from = to - D_RIGHT;
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 4);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 3);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 2);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 1);
                }
            }
            if (MT != MoveGenType::CAPTURE) {
                while (pp) {
                    const int to = pp.pop(), from = to - D_UP;
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 4);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 3);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 2);
                    *out++ = bk::mkMove(Move::PROMOTION, from, to, 1);
                }
            }
        }

        push1 &= ~R_PROM;
        capL &= ~R_PROM;
        capR &= ~R_PROM;

        if (MT != MoveGenType::QUIET) {
            while (capL) {
                const int to = capL.pop();
                *out++       = bk::mkMove(to - D_LEFT, to);
            }
            while (capR) {
                const int to = capR.pop();
                *out++       = bk::mkMove(to - D_RIGHT, to);
            }
        }
        if (MT != MoveGenType::CAPTURE) {
            while (push1) {
                const int to = push1.pop();
                *out++       = bk::mkMove(to - D_UP, to);
            }
            while (push2) {
                const int to = push2.pop();
                *out++       = bk::mkMove(to - 2 * D_UP, to);
            }
        }

        if (MT != MoveGenType::QUIET && b.enpassantSq() != Square::NO_SQ) {
            const Square ep      = b.enpassantSq();
            const Bitboard epb   = Bitboard::fromSquare(ep);
            const Bitboard vict  = Bitboard::fromSquare(ep.index() - D_UP);
            if (checkmask & (epb | vict)) {
                Bitboard from_set = attacks::pawn(THEM, ep) & lr_pawns;
                while (from_set) {
                    const int from = from_set.pop();
                    const Bitboard fb = Bitboard::fromSquare(from);
                    if ((fb & pin_d) && !(pin_d & epb)) continue;
                    // both pawns vanish from the rank: a rook/queen on that rank may suddenly see the king
                    if ((kb & Bitboard(Rank(ksq.rank()))) && (t_rq & Bitboard(Rank(ksq.rank()))) &&
                        (attacks::rook(ksq, occ ^ vict ^ fb) & t_rq))
                        continue;
                    *out++ = bk::mkMove(Move::ENPASSANT, from, ep.index(), 1);
                }
            }
        }

        if constexpr (ONE) {
            if (out != start) return commit();
        }
    }

    // ---- knights -------------------------------------------------------------------------------
    if (pieces & PieceGenType::KNIGHT) {
        Bitboard kn = b.pieces(PieceType::KNIGHT, US) & ~(pin_d | pin_hv);
        while (kn) {
            const int from = kn.pop();
            Bitboard t     = attacks::knight(Square(from)) & movable;
            while (t) *out++ = bk::mkMove(from, t.pop());
        }
        if constexpr (ONE) {
            if (out != start) return commit();
        }
    }

    // ---- bishops -------------------------------------------------------------------------------
    if (pieces & PieceGenType::BISHOP) {
        Bitboard bs = b.pieces(PieceType::BISHOP, US) & ~pin_hv;
        while (bs) {
            const int from = bs.pop();
            Bitboard t     = attacks::bishop(Square(from), occ) & movable;
            if (pin_d.check(from)) t &= pin_d;
            while (t) *out++ = bk::mkMove(from, t.pop());
        }
        if constexpr (ONE) {
            if (out != start) return commit();
        }
    }

    // ---- rooks ---------------------------------------------------------------------------------
    if (pieces & PieceGenType::ROOK) {
        Bitboard rs = b.pieces(PieceType::ROOK, US) & ~pin_d;
        while (rs) {
            const int from = rs.pop();
            Bitboard t     = attacks::rook(Square(from), occ) & movable;
            if (pin_hv.check(from)) t &= pin_hv;
            while (t) *out++ = bk::mkMove(from, t.pop());
        }
        if constexpr (ONE) {
            if (out != start) return commit();
        }
    }

    // ---- queens --------------------------------------------------------------------------------
    if (pieces & PieceGenType::QUEEN) {
        Bitboard qs = b.pieces(PieceType::QUEEN, US);
        while (qs) {
            const int from = qs.pop();
            Bitboard t;
            if (pin_d.check(from))
                t = attacks::bishop(Square(from), occ) & pin_d;
            else if (pin_hv.check(from))
                t = attacks::rook(Square(from), occ) & pin_hv;
            else
                t = attacks::queen(Square(from), occ);
            t &= movable;
            while (t) *out++ = bk::mkMove(from, t.pop());
        }
    }

    commit();
}

template <movegen::MoveGenType mt>
inline void movegen::legalmoves(Movelist& movelist, const Board& board, int pieces) {
    movelist.clear();
    if (board.sideToMove() == Color::WHITE)
        generate<Color::WHITE, mt, false>(movelist, board, pieces);
    else
        generate<Color::BLACK, mt, false>(movelist, board, pieces);
}

template <movegen::MoveGenType mt>
inline bool movegen::anylegalmoves(const Board& board, int pieces) {
    Movelist movelist;
    if (board.sideToMove() == Color::WHITE)
        generate<Color::WHITE, mt, true>(movelist, board, pieces);
    else
        generate<Color::BLACK, mt, true>(movelist, board, pieces);
    return !movelist.empty();
}

// Exact membership test: true iff `move` (canonically encoded) is among the legal moves of `board`.
template <Color::underlying C>
inline bool movegen::isLegalFor(const Board& b, const Move m) {
    constexpr Color US   = C;
    constexpr Color THEM = ~C;

    const Square from = m.from();
    const Square to   = m.to();
    const auto mtype  = m.typeOf();
    const Piece pc    = b.at(from);

    if (pc == Piece::NONE || pc.color() != US) return false;

    const PieceType pt    = pc.type();
    const Piece tp        = b.at(to);
    const Bitboard ours   = b.us(US);
    const Bitboard theirs = b.us(THEM);
    const Bitboard occ    = ours | theirs;
    const Bitboard fb     = Bitboard::fromSquare(from);
    const Bitboard tb     = Bitboard::fromSquare(to);

    // bits 12-13 are only meaningful for promotions
    if (mtype != Move::PROMOTION && ((m.move() >> 12) & 3)) return false;

    constexpr int UPD = static_cast<int>(make_direction(Direction::NORTH, C));

    // can the enemy hit our king on `ksq` after the described change of occupancy?
    auto kingSafe = [&](Bitboard o, Bitboard t) {
        const Square ksq = b.kingSq(US);
        const Bitboard rq = t & b.pieces(PieceType::ROOK, PieceType::QUEEN);
        const Bitboard bq = t & b.pieces(PieceType::BISHOP, PieceType::QUEEN);
        if (attacks::rook(ksq, o) & rq) return false;
        if (attacks::bishop(ksq, o) & bq) return false;
        if (attacks::knight(ksq) & t & b.pieces(PieceType::KNIGHT)) return false;
        if (attacks::pawn(US, ksq) & t & b.pieces(PieceType::PAWN)) return false;
        return true;
    };

    if (mtype == Move::CASTLING) {
        if (pt != PieceType::KING || tp != Piece(PieceType::ROOK, US)) return false;
        if (!Square::back_rank(from, US) || !Square::back_rank(to, US)) return false;

        const auto side = to > from ? Board::CastlingRights::Side::KING_SIDE : Board::CastlingRights::Side::QUEEN_SIDE;
        const auto rights = b.castlingRights();
        if (!rights.has(US, side)) return false;
        if (rights.getRookFile(US, side) != to.file()) return false;

        const bool ks = side == Board::CastlingRights::Side::KING_SIDE;
        if (occ & b.getCastlingPath(US, ks)) return false;

        const Bitboard occ_nk = occ ^ fb;
        if (bk::attackedBy(b, THEM, from, occ_nk)) return false;

        const Square king_to = Square::castling_king_square(ks, US);
        Bitboard walk        = Bitboard(bk::ST.between[from.index()][king_to.index()]);
        while (walk)
            if (bk::attackedBy(b, THEM, Square(walk.pop()), occ_nk)) return false;

        if (b.chess960()) {
            const Square rook_to = Square::castling_rook_square(ks, US);
            const Bitboard fin   = (occ_nk ^ tb) | Bitboard::fromSquare(king_to) | Bitboard::fromSquare(rook_to);
            const Bitboard trq   = b.pieces(PieceType::ROOK, PieceType::QUEEN) & theirs;
            const Bitboard tbq   = b.pieces(PieceType::BISHOP, PieceType::QUEEN) & theirs;
            if ((attacks::rook(king_to, fin) & trq) || (attacks::bishop(king_to, fin) & tbq)) return false;
        }
        return true;
    }

    // everything else may not land on an own piece or on a king
    if (tp != Piece::NONE && (tp.color() == US || tp.type() == PieceType::KING)) return false;

    if (mtype == Move::ENPASSANT) {
        if (pt != PieceType::PAWN || to != b.enpassantSq() || tp != Piece::NONE) return false;
        if (!(attacks::pawn(US, from) & tb)) return false;
        const Bitboard vb = Bitboard::fromSquare(to.index() - UPD);
        return kingSafe((occ ^ fb ^ vb) | tb, theirs ^ vb);
    }

    if (pt == PieceType::KING) {
        if (mtype != Move::NORMAL) return false;
        if (!(attacks::king(from) & tb)) return false;
        return !bk::attackedBy(b, THEM, to, occ ^ fb);
    }

    const bool to_promo_rank = Rank::back_rank(to.rank(), THEM);

    if (pt == PieceType::PAWN) {
        if (to_promo_rank != (mtype == Move::PROMOTION)) return false;
        if (tp != Piece::NONE) {
            if (!(attacks::pawn(US, from) & tb)) return false;
        } else {
            const int d = to.index() - from.index();
            if (d == UPD) {
                // single push
            } else if (d == 2 * UPD && from.rank() == Rank::rank(Rank::RANK_2, US) &&
                       b.at(Square(from.index() + UPD)) == Piece::NONE) {
                // double push through an empty square
            } else {
                return false;
            }
        }
    } else {
        if (mtype != Move::NORMAL) return false;
        Bitboard reach;
        switch (static_cast<int>(pt)) {
            case 1: reach = attacks::knight(from); break;
            case 2: reach = attacks::bishop(from, occ); break;
            case 3: reach = attacks::rook(from, occ); break;
            default: reach = attacks::queen(from, occ); break;
        }
        if (!(reach & tb)) return false;
    }

    return kingSafe((occ ^ fb) | tb, theirs & ~tb);
}

inline bool movegen::isLegal(const Board& board, const Move move) {
    if (board.sideToMove() == Color::WHITE) return isLegalFor<Color::WHITE>(board, move);
    return isLegalFor<Color::BLACK>(board, move);
}

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  uci / san
// ---------------------------------------------------------------------------------------------
namespace chess {

class uci {
   public:
    [[nodiscard]] static std::string moveToUci(const Move& move, bool chess960 = false) noexcept(false) {
        const Square from_sq = move.from();
        Square to_sq         = move.to();

        // castling is stored as "king takes own rook"; plain UCI wants the king's destination
        if (!chess960 && move.typeOf() == Move::CASTLING)
            to_sq = Square(to_sq > from_sq ? File::FILE_G : File::FILE_C, from_sq.rank());

        std::string result = static_cast<std::string>(from_sq);
        result += static_cast<std::string>(to_sq);
        if (move.typeOf() == Move::PROMOTION) result += static_cast<std::string>(move.promotionType());
        return result;
    }

    [[nodiscard]] static Move uciToMove(const Board& board, std::string_view uci) noexcept(false) {
        if (uci.length() < 4) return Move::NO_MOVE;

        const Square source = Square(uci.substr(0, 2));
        Square target       = Square(uci.substr(2, 2));

        if (!source.is_valid() || !target.is_valid()) return Move::NO_MOVE;

        const auto pt = board.at(source).type();

        if (board.chess960() && pt == PieceType::KING && board.at(target).type() == PieceType::ROOK &&
            board.at(target).color() == board.sideToMove())
            return Move::make<Move::CASTLING>(source, target);

        if (!board.chess960() && pt == PieceType::KING && Square::distance(target, source) == 2) {
            target = Square(target > source ? File::FILE_H : File::FILE_A, source.rank());
            return Move::make<Move::CASTLING>(source, target);
        }

        if (pt == PieceType::PAWN && target == board.enpassantSq()) return Move::make<Move::ENPASSANT>(source, target);

        if (pt == PieceType::PAWN && uci.length() == 5 && Square::back_rank(target, ~board.sideToMove())) {
            const auto promotion = PieceType(uci.substr(4, 1));
            if (promotion == PieceType::NONE || promotion == PieceType::KING || promotion == PieceType::PAWN)
                return Move::NO_MOVE;
            return Move::make<Move::PROMOTION>(source, target, promotion);
        }

        return uci.length() == 4 ? Move::make<Move::NORMAL>(source, target) : Move(Move::NO_MOVE);
    }

    [[nodiscard]] static std::string moveToSan(const Board& board, const Move& move) noexcept(false) {
        std::string san;
        moveToRep<false>(board, move, san);
        return san;
    }

    [[nodiscard]] static std::string moveToLan(const Board& board, const Move& move) noexcept(false) {
        std::string lan;
        moveToRep<true>(board, move, lan);
        return lan;
    }

    class SanParseError : public std::exception {
       public:
        explicit SanParseError(const char* message) : msg_(message) {}
        explicit SanParseError(const std::string& message) : msg_(message) {}
        virtual ~SanParseError() noexcept {}
        virtual const char* what() const noexcept { return msg_.c_str(); }

       protected:
        std::string msg_;
    };

    class AmbiguousMoveError : public std::exception {
       public:
        explicit AmbiguousMoveError(const char* message) : msg_(message) {}
        explicit AmbiguousMoveError(const std::string& message) : msg_(message) {}
        virtual ~AmbiguousMoveError() noexcept {}
        virtual const char* what() const noexcept { return msg_.c_str(); }

       protected:
        std::string msg_;
    };

    [[nodiscard]] static Move parseSan(const Board& board, std::string_view san) noexcept(false) {
        Movelist moves;
        return parseSan(board, san, moves);
    }

    [[nodiscard]] static Move parseSan(const Board& board, std::string_view san, Movelist& moves) noexcept(false) {
        if (san.empty()) return Move::NO_MOVE;

        const SanInfo info = parseSanInfo(san);
        const int mask     = 1 << static_cast<int>(info.piece);

        if (info.capture)
            movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, board, mask);
        else
            movegen::legalmoves<movegen::MoveGenType::QUIET>(moves, board, mask);

        if (info.castling_short || info.castling_long) {
            for (const auto& mv : moves) {
                if (mv.typeOf() != Move::CASTLING) continue;
                if ((info.castling_short && mv.to() > mv.from()) || (info.castling_long && mv.to() < mv.from()))
                    return mv;
            }
#ifndef CHESS_NO_EXCEPTIONS
            throw SanParseError("Failed to parse san. At step 2: " + std::string(san) + " " + board.getFen());
#endif
        }

        Move found(Move::NO_MOVE);
        bool have = false;

        for (const auto& mv : moves) {
            if (mv.to() != info.to || mv.typeOf() == Move::CASTLING) continue;

            if (info.promotion != PieceType::NONE) {
                if (mv.typeOf() != Move::PROMOTION || info.promotion != mv.promotionType() ||
                    mv.from().file() != info.from_file)
                    continue;
            } else if (mv.typeOf() == Move::ENPASSANT) {
                if (mv.from().file() != info.from_file) continue;
            } else if (info.from != Square::NO_SQ) {
                if (mv.from() != info.from) continue;
            } else if (info.from_rank != Rank::NO_RANK || info.from_file != File::NO_FILE) {
                if ((info.from_file != File::NO_FILE && mv.from().file() != info.from_file) ||
                    (info.from_rank != Rank::NO_RANK && mv.from().rank() != info.from_rank))
                    continue;
            }

            if (have) {
#ifndef CHESS_NO_EXCEPTIONS
                throw AmbiguousMoveError("Ambiguous san: " + std::string(san) + " in " + board.getFen());
#endif
            }
            found = mv;
            have  = true;
        }

        if (!have) {
#ifndef CHESS_NO_EXCEPTIONS
            throw SanParseError("Failed to parse san, illegal move: " + std::string(san) + " " + board.getFen());
#endif
        }

        return found;
    }

    static bool isUciMove(std::string_view move) noexcept {
        auto file = [](char c) { return c >= 'a' && c <= 'h'; };
        auto rank = [](char c) { return c >= '1' && c <= '8'; };
        auto prom = [](char c) { return c == 'n' || c == 'b' || c == 'r' || c == 'q'; };

        if (move.size() > 5) return false;
        bool ok = false;
        if (move.size() >= 4) ok = file(move[0]) && rank(move[1]) && file(move[2]) && rank(move[3]);
        if (move.size() == 5) ok = ok && prom(move[4]);
        return ok;
    }

   private:
    struct SanInfo {
        File from_file      = File::NO_FILE;
        Rank from_rank      = Rank::NO_RANK;
        PieceType promotion = PieceType::NONE;
        Square from         = Square::NO_SQ;
        Square to           = Square::NO_SQ;
        PieceType piece     = PieceType::NONE;
        bool castling_short = false;
        bool castling_long  = false;
        bool capture        = false;
    };

    [[nodiscard]] static SanInfo parseSanInfo(std::string_view san) noexcept(false) {
#ifndef CHESS_NO_EXCEPTIONS
        if (san.length() < 2) throw SanParseError("Failed to parse san. At step 0: " + std::string(san));
#endif
        auto is_rank = [](char c) { return c >= '1' && c <= '8'; };
        auto is_file = [](char c) { return c >= 'a' && c <= 'h'; };
        auto sv      = [](const char& c) { return std::string_view(&c, 1); };

        SanInfo info;
        bool bad = false;

        if (san[0] == 'O' || san[0] == '0') {
            const char zero = san[0];
            info.piece      = PieceType::KING;
            san.remove_prefix(3);
            info.castling_short = san.empty() || san[0] != '-';
            info.castling_long  = san.length() >= 2 && san[0] == '-' && san[1] == zero;
            return info;
        }

        std::size_t i = 1;
        if (is_file(san[0])) {
            i          = 0;
            info.piece = PieceType::PAWN;
        } else {
            info.piece = PieceType(san);
            if (info.piece == PieceType::NONE) bad = true;
        }

        File file_to = File::NO_FILE;
        Rank rank_to = Rank::NO_RANK;

        if (i < san.size() && is_file(san[i])) info.from_file = File(sv(san[i++]));
        if (i < san.size() && is_rank(san[i])) info.from_rank = Rank(sv(san[i++]));
        if (i < san.size() && san[i] == 'x') {
            info.capture = true;
            ++i;
        }
        if (i < san.size() && is_file(san[i])) file_to = File(sv(san[i++]));
        if (i < san.size() && is_rank(san[i])) rank_to = Rank(sv(san[i++]));

        if (i < san.size() && san[i] == '=') {
            ++i;
            if (i < san.size()) info.promotion = PieceType(sv(san[i]));
            if (info.promotion == PieceType::KING || info.promotion == PieceType::PAWN ||
                info.promotion == PieceType::NONE)
                bad = true;
            ++i;
        }

        // "e4" / "Nf3": what we read as origin is really the destination
        if (file_to == File::NO_FILE && rank_to == Rank::NO_RANK) {
            file_to        = info.from_file;
            rank_to        = info.from_rank;
            info.from_file = File::NO_FILE;
            info.from_rank = Rank::NO_RANK;
        }

        if (info.piece == PieceType::PAWN && info.from_file == File::NO_FILE && !info.capture) info.from_file = file_to;

        if (file_to != File::NO_FILE && rank_to != Rank::NO_RANK)
            info.to = Square(file_to, rank_to);
        else
            bad = true;

#ifndef CHESS_NO_EXCEPTIONS
        if (bad) throw SanParseError("Failed to parse san. At step 1: " + std::string(san));
#endif
        (void)bad;

        if (info.from_file != File::NO_FILE && info.from_rank != Rank::NO_RANK)
            info.from = Square(info.from_file, info.from_rank);

        return info;
    }

    template <bool LAN = false>
    static void moveToRep(Board board, const Move& move, std::string& str) {
        if (move.typeOf() == Move::CASTLING) {
            str = move.to().file() > move.from().file() ? "O-O" : "O-O-O";
            board.makeMove(move);
            if (board.inCheck()) appendCheck(board, str);
            return;
        }

        const PieceType pt = board.at(move.from()).type();
        const bool capture = board.at(move.to()) != Piece::NONE || move.typeOf() == Move::ENPASSANT;

        if (pt != PieceType::PAWN) str += static_cast<char>(std::toupper(static_cast<std::string>(pt)[0]));

        if constexpr (LAN) {
            appendSquare(move.from(), str);
        } else if (pt == PieceType::PAWN) {
            if (capture) str += static_cast<std::string>(move.from().file());
        } else {
            disambiguate(board, move, pt, str);
        }

        if (capture) str += 'x';
        appendSquare(move.to(), str);

        if (move.typeOf() == Move::PROMOTION) {
            str += '=';
            str += static_cast<char>(std::toupper(static_cast<std::string>(move.promotionType())[0]));
        }

        board.makeMove(move);
        if (board.inCheck()) appendCheck(board, str);
    }

    static void appendSquare(Square sq, std::string& str) {
        str += static_cast<std::string>(sq.file());
        str += static_cast<std::string>(sq.rank());
    }

    static void appendCheck(Board& board, std::string& str) {
        str += board.isGameOver().second == GameResult::LOSE ? '#' : '+';
    }

    static void disambiguate(const Board& board, const Move& move, PieceType pt, std::string& str) {
        Movelist moves;
        movegen::legalmoves(moves, board, 1 << static_cast<int>(pt));

        bool clash = false, same_file = false, same_rank = false;
        for (const auto& m : moves) {
            if (m == move || m.to() != move.to()) continue;
            clash = true;
            if (m.from().file() == move.from().file()) same_file = true;
            if (m.from().rank() == move.from().rank()) same_rank = true;
        }
        if (!clash) return;

        if (!same_file)
            str += static_cast<std::string>(move.from().file());
        else if (!same_rank)
            str += static_cast<std::string>(move.from().rank());
        else
            appendSquare(move.from(), str);
    }
};

}  // namespace chess

// ---------------------------------------------------------------------------------------------
//  pgn
// ---------------------------------------------------------------------------------------------
namespace chess::pgn {

namespace detail {

/// Fixed capacity token buffer (PGN tokens are limited to 255 characters).
class StringBuffer {
   public:
    bool empty() const noexcept { return len_ == 0; }
    void clear() noexcept { len_ = 0; }
    std::string_view get() const noexcept { return std::string_view(data_.data(), len_); }

    bool add(char c) {
        if (len_ >= kCapacity) return false;
        data_[len_++] = c;
        return true;
    }

   private:
    static constexpr std::size_t kCapacity = 255;
    std::array<char, kCapacity> data_      = {};
    std::size_t len_                       = 0;
};

/// Chunked character source over an istream. Carriage returns are invisible to some().
template <std::size_t BUFFER_SIZE>
class StreamBuffer {
   public:
    explicit StreamBuffer(std::istream& stream) : stream_(stream), chunk_(BUFFER_SIZE * BUFFER_SIZE) {}

    std::optional<char> some() {
        for (;;) {
            if (pos_ < got_) {
                const char c = chunk_[static_cast<std::size_t>(pos_)];
                if (c == '\r') {
                    ++pos_;
                    continue;
                }
                return c;
            }
            if (!fill()) return std::nullopt;
        }
    }

    /// Consume up to and including the delimiter that closes the one we are standing on.
    bool skipUntil(char open_delim, char close_delim) {
        int depth = 0;
        for (;;) {
            const auto c = some();
            advance();
            if (!c) return false;
            if (*c == open_delim) {
                ++depth;
            } else if (*c == close_delim) {
                if (depth == 0) return false;
                if (--depth == 0) return true;
            }
        }
    }

    bool fill() {
        pos_ = 0;
        stream_.read(chunk_.data(), static_cast<std::streamsize>(chunk_.size()));
        got_ = stream_.gcount();
        return got_ > 0;
    }

    void advance() {
        if (pos_ >= got_) fill();
        ++pos_;
    }

    char peek() {
        if (pos_ + 1 >= got_) return static_cast<char>(stream_.peek());
        return chunk_[static_cast<std::size_t>(pos_ + 1)];
    }

    std::optional<char> current() {
        if (pos_ >= got_) return fill() ? std::optional<char>(chunk_[0]) : std::nullopt;
        return chunk_[static_cast<std::size_t>(pos_)];
    }

   private:
    std::istream& stream_;
    std::vector<char> chunk_;
    std::streamsize got_ = 0;
    std::streamsize pos_ = 0;
};

}  // namespace detail

/// Callback interface used by StreamParser.
class Visitor {
   public:
    virtual ~Visitor() {}

    /// When set (after startPgn) the rest of the current game is skipped; only endPgn is reported.
    void skipPgn(bool skip) { skip_ = skip; }
    bool skip() { return skip_; }

    virtual void startPgn()                                           = 0;
    virtual void header(std::string_view key, std::string_view value) = 0;
    virtual void startMoves()                                         = 0;
    virtual void move(std::string_view move, std::string_view comment) = 0;
    virtual void endPgn()                                             = 0;

   private:
    bool skip_ = false;
};

class StreamParserError {
   public:
    enum Code {
        None,
        ExceededMaxStringLength,
        InvalidHeaderMissingClosingBracket,
        InvalidHeaderMissingClosingQuote,
        NotEnoughData
    };

    StreamParserError() : code_(None) {}
    StreamParserError(Code code) : code_(code) {}

    Code code() const { return code_; }
    bool hasError() const { return code_ != None; }

    std::string message() const {
        switch (code_) {
            case None: return "No error";
            case ExceededMaxStringLength: return "Exceeded max string length";
            case InvalidHeaderMissingClosingBracket: return "Invalid header: missing closing bracket";
            case InvalidHeaderMissingClosingQuote: return "Invalid header: missing closing quote";
            case NotEnoughData: return "Not enough data";
        }
        return "Unknown error";
    }

    bool operator==(Code code) const { return code_ == code; }
    bool operator!=(Code code) const { return code_ != code; }
    bool operator==(const StreamParserError& other) const { return code_ == other.code_; }
    bool operator!=(const StreamParserError& other) const { return code_ != other.code_; }

    operator bool() const { return code_ != None; }

   private:
    Code code_;
};

template <std::size_t BUFFER_SIZE =
#if defined(__APPLE__) || defined(__MACH__)
              256
#elif defined(__unix__) || defined(__unix) || defined(unix)
              1024
#else
              256
#endif
          >
class StreamParser {
   public:
    explicit StreamParser(std::istream& stream) : in_(stream) {}

    StreamParserError readGames(Visitor& vis) {
        vis_ = &vis;

        if (!in_.fill()) return StreamParserError::NotEnoughData;

        while (auto c = in_.some()) {
            if (phase_ == Phase::Header) {
                vis_->skipPgn(false);
                if (*c == '[') {
                    vis_->startPgn();
                    game_closed_ = false;
                    readHeaders();
                    if (error_ != StreamParserError::None) return error_;
                }
            } else {
                readBody();
                if (error_ != StreamParserError::None) return error_;
            }

            if (!hold_position_) in_.advance();
            hold_position_ = false;
        }

        if (!game_closed_) finishGame();
        return error_;
    }

   private:
    enum class Phase { Header, Body };

    static bool isSpace(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
    static bool isDigit(char c) noexcept { return c >= '0' && c <= '9'; }

    void skipWhile(bool (*pred)(char)) {
        while (auto c = in_.some()) {
            if (!pred(*c)) break;
            in_.advance();
        }
    }
    void skipSpaces() { skipWhile(&isSpace); }

    void resetGameState() {
        tag_key_.clear();
        tag_value_.clear();
        san_.clear();
        comment_.clear();
        phase_ = Phase::Header;
    }

    void flushMove() {
        if (san_.empty()) return;
        if (!vis_->skip()) vis_->move(san_.get(), comment_);
        san_.clear();
        comment_.clear();
    }

    void finishGame() {
        flushMove();
        vis_->endPgn();
        vis_->skipPgn(false);
        resetGameState();
        game_closed_ = true;
    }

    void readHeaders() {
        bool escaped = false;

        while (auto c = in_.some()) {
            switch (*c) {
                case '[':
                    in_.advance();
                    while (auto k = in_.some()) {
                        if (isSpace(*k)) break;
                        if (!tag_key_.add(*k)) {
                            error_ = StreamParserError::ExceededMaxStringLength;
                            return;
                        }
                        in_.advance();
                    }
                    in_.advance();
                    break;

                case '"':
                    in_.advance();
                    while (auto k = in_.some()) {
                        if (*k == '\\') {
                            escaped = true;
                            in_.advance();
                        } else if (*k == '"' && !escaped) {
                            in_.advance();
                            if (in_.current().value_or('\0') != ']') {
                                error_ = StreamParserError::InvalidHeaderMissingClosingBracket;
                                return;
                            }
                            in_.advance();
                            break;
                        } else if (*k == '\n') {
                            error_ = StreamParserError::InvalidHeaderMissingClosingQuote;
                            return;
                        } else {
                            escaped = false;
                            if (!tag_value_.add(*k)) {
                                error_ = StreamParserError::ExceededMaxStringLength;
                                return;
                            }
                            in_.advance();
                        }
                    }

                    if (in_.current() == '\r') in_.advance();

                    if (!vis_->skip()) vis_->header(tag_key_.get(), tag_value_.get());
                    tag_key_.clear();
                    tag_value_.clear();
                    in_.advance();
                    break;

                default:
                    // blank line (or unexpected text): the movetext starts here
                    phase_ = Phase::Body;
                    if (!vis_->skip()) vis_->startMoves();
                    return;
            }
        }
    }

    // comment text up to the closing brace (the opening brace is already consumed)
    void readBraceComment() {
        while (auto k = in_.some()) {
            in_.advance();
            if (*k == '}') break;
            comment_ += *k;
        }
    }

    void readBody() {
        bool terminator = false;

        // leading move numbers, comments before the first move, or a bare game termination
        while (auto c = in_.some()) {
            if (*c == ' ' || isDigit(*c)) {
                in_.advance();
            } else if (*c == '-' || *c == '*' || *c == '/') {
                terminator = true;
                in_.advance();
            } else if (*c == '{') {
                in_.advance();
                readBraceComment();
                if (!vis_->skip()) {
                    vis_->move("", comment_);
                    comment_.clear();
                }
            } else {
                break;
            }
        }

        if (terminator) {
            finishGame();
            return;
        }

        skipSpaces();

        while (auto cd = in_.some()) {
            if (*cd == '[') {  // next game starts without a terminator
                finishGame();
                hold_position_ = true;
                break;
            }

            skipWhile([](char c) { return isSpace(c) || isDigit(c); });
            skipWhile([](char c) { return c == '.'; });
            skipSpaces();

            if (readSan()) break;

            skipSpaces();

            const auto curr = in_.current();
            if (!curr) {
                finishGame();
                break;
            }

            if (*curr == '*') {
                finishGame();
                in_.advance();
                break;
            }

            const char next = in_.peek();

            if (*curr == '1') {
                if (next == '-') {
                    in_.advance();
                    in_.advance();
                    finishGame();
                    break;
                }
                if (next == '/') {  // 1/2-1/2
                    for (int i = 0; i < 7; ++i) in_.advance();
                    finishGame();
                    break;
                }
            }

            // "0-1" (result) versus "0-0" / "0-0-0" (castling written with zeros)
            if (*curr == '0' && next == '-') {
                in_.advance();
                in_.advance();

                const auto after = in_.current();
                if (!after) {
                    finishGame();
                    break;
                }
                if (*after == '1') {
                    finishGame();
                    in_.advance();
                    break;
                }
                if (!san_.add('0') || !san_.add('-')) {
                    error_ = StreamParserError::ExceededMaxStringLength;
                    return;
                }
                if (readSan()) {
                    in_.advance();
                    break;
                }
            }
        }
    }

    // read one SAN token; returns true when the caller must stop (end of data / error)
    bool readSan() {
        while (auto c = in_.some()) {
            if (isSpace(*c)) break;
            if (!san_.add(*c)) {
                error_ = StreamParserError::ExceededMaxStringLength;
                return true;
            }
            in_.advance();
        }
        return readAnnotations();
    }

    // comments, variations, NAGs and blanks that trail a move; flushes the move when done
    bool readAnnotations() {
        for (;;) {
            const auto curr = in_.current();
            if (!curr) {
                finishGame();
                return true;
            }

            switch (*curr) {
                case '{':
                    in_.advance();
                    readBraceComment();
                    break;
                case '(':
                    in_.skipUntil('(', ')');
                    break;
                case '$':
                    while (auto c = in_.some()) {
                        if (isSpace(*c)) break;
                        in_.advance();
                    }
                    break;
                case ' ':
                    while (auto c = in_.some()) {
                        if (!isSpace(*c)) break;
                        in_.advance();
                    }
                    break;
                default:
                    flushMove();
                    return false;
            }
        }
    }

    detail::StreamBuffer<BUFFER_SIZE> in_;
    Visitor* vis_ = nullptr;

    detail::StringBuffer tag_key_;
    detail::StringBuffer tag_value_;
    detail::StringBuffer san_;
    std::string comment_;

    StreamParserError error_ = StreamParserError::None;

    Phase phase_        = Phase::Header;
    bool game_closed_   = true;
    bool hold_position_ = false;
};

}  // namespace chess::pgn
#endif  // BACKENED_HPP
