
#ifndef BACKENED_HPP
#define BACKENED_HPP
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
# define BACKENED_HAS_CHARCONV
# include <charconv>
#else
# include <sstream>
#endif
#if defined(_MSC_VER)
# include <intrin.h>
# include <nmmintrin.h>
#endif
#if defined(BACKENED_USE_PEXT) && !defined(CHESS_USE_PEXT)
# define CHESS_USE_PEXT
#endif
#ifdef CHESS_USE_PEXT
# include <immintrin.h>
#endif
#if defined(__GNUC__) || defined(__clang__)
# define BK_INLINE inline __attribute__((always_inline))
# define BK_LIKELY(x) __builtin_expect(!!(x), 1)
# define BK_UNLIKELY(x) __builtin_expect(!!(x), 0)
# define BK_PREFETCH(p) __builtin_prefetch(p)
#elif defined(_MSC_VER)
# define BK_INLINE __forceinline
# define BK_LIKELY(x) (x)
# define BK_UNLIKELY(x) (x)
# define BK_PREFETCH(p) ((void)0)
#else
# define BK_INLINE inline
# define BK_LIKELY(x) (x)
# define BK_UNLIKELY(x) (x)
# define BK_PREFETCH(p) ((void)0)
#endif
#ifdef BACKENED_NO_VIRTUAL
# define BK_VIRTUAL
#else
# define BK_VIRTUAL virtual
#endif
namespace _q156::_q14y {
#if defined(__GNUC__) || defined(__clang__)
# define BK_CONSTEXPR_BITS constexpr
BK_INLINE constexpr int _q15e(std::uint64_t x) noexcept { return __builtin_ctzll(x); }
BK_INLINE constexpr int _q15a(std::uint64_t x) noexcept { return __builtin_clzll(x); }
BK_INLINE constexpr int _q17t(std::uint64_t x) noexcept { return __builtin_popcountll(x); }
#elif defined(_MSC_VER)
# define BK_CONSTEXPR_BITS
BK_INLINE int _q15e(std::uint64_t x) noexcept {
unsigned long i;
_BitScanForward64(&i, x);
return static_cast<int>(i);
}
BK_INLINE int _q15a(std::uint64_t x) noexcept {
unsigned long i;
_BitScanReverse64(&i, x);
return 63 - static_cast<int>(i);
}
BK_INLINE int _q17t(std::uint64_t x) noexcept { return static_cast<int>(_mm_popcnt_u64(x)); }
#else
# error "Unsupported compiler"
#endif
}
namespace _q156 {
class _q10k {
public:
enum class _q18t : std::int8_t { _q148 = 0, _q103 = 1, _q121 = -1 };
constexpr _q10k() : _q15b(_q18t::_q121) {}
constexpr _q10k(_q18t c) : _q15b(c) {}
constexpr _q10k(int c) : _q15b(static_cast<_q18t>(c)) { assert(c == 0 || c == 1 || c == -1); }
constexpr _q10k(std::string_view str)
: _q15b(str == "w" ? _q18t::_q148 : str == "b" ? _q18t::_q103 : _q18t::_q121) {}
[[nodiscard]] std::string longStr() const {
if (_q15b == _q18t::_q148) return "White";
if (_q15b == _q18t::_q103) return "Black";
return "None";
}
constexpr _q10k operator~() const noexcept {
assert(_q15b != _q18t::_q121);
return _q10k(static_cast<_q18t>(static_cast<int>(_q15b) ^ 1));
}
constexpr bool operator==(const _q10k& rhs) const noexcept { return _q15b == rhs._q15b; }
constexpr bool operator!=(const _q10k& rhs) const noexcept { return _q15b != rhs._q15b; }
constexpr operator int() const noexcept { return static_cast<int>(_q15b); }
explicit operator std::string() const {
return _q15b == _q18t::_q148 ? "w" : _q15b == _q18t::_q103 ? "b" : "NONE";
}
[[nodiscard]] constexpr _q18t internal() const noexcept { return _q15b; }
friend std::ostream& operator<<(std::ostream& os, const _q10k& c) { return os << static_cast<std::string>(c); }
static constexpr _q18t _q148 = _q18t::_q148;
static constexpr _q18t _q103 = _q18t::_q103;
static constexpr _q18t _q121 = _q18t::_q121;
private:
_q18t _q15b;
};
constexpr _q10k::_q18t operator~(_q10k::_q18t c) {
return c == _q10k::_q18t::_q148 ? _q10k::_q18t::_q103
: c == _q10k::_q18t::_q103 ? _q10k::_q18t::_q148
: _q10k::_q18t::_q121;
}
namespace _q18y {
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
}
class _q11b {
public:
enum class _q18t : std::uint8_t { _q10z, _q111, _q112, _q113, _q114, _q115, _q116, _q117, _q127 };
constexpr _q11b() : file(_q18t::_q127) {}
constexpr _q11b(_q18t f) : file(f) {}
constexpr _q11b(int f) : file(static_cast<_q18t>(f)) {}
constexpr _q11b(std::string_view sw)
: file(static_cast<_q18t>(_q18y::tolower(static_cast<char>(sw[0])) - 'a')) {}
[[nodiscard]] constexpr _q18t internal() const noexcept { return file; }
constexpr bool operator==(const _q11b& rhs) const noexcept { return file == rhs.file; }
constexpr bool operator!=(const _q11b& rhs) const noexcept { return file != rhs.file; }
constexpr bool operator==(const _q18t& rhs) const noexcept { return file == rhs; }
constexpr bool operator!=(const _q18t& rhs) const noexcept { return file != rhs; }
constexpr bool operator>=(const _q11b& rhs) const noexcept { return int(*this) >= int(rhs); }
constexpr bool operator<=(const _q11b& rhs) const noexcept { return int(*this) <= int(rhs); }
constexpr bool operator>(const _q11b& rhs) const noexcept { return int(*this) > int(rhs); }
constexpr bool operator<(const _q11b& rhs) const noexcept { return int(*this) < int(rhs); }
constexpr _q11b& operator+=(int rhs) noexcept {
file = static_cast<_q18t>(static_cast<int>(file) + rhs);
return *this;
}
constexpr operator int() const noexcept { return static_cast<int>(file); }
explicit operator std::string() const { return std::string(1, static_cast<char>('a' + static_cast<int>(file))); }
static constexpr _q18t _q10z = _q18t::_q10z;
static constexpr _q18t _q111 = _q18t::_q111;
static constexpr _q18t _q112 = _q18t::_q112;
static constexpr _q18t _q113 = _q18t::_q113;
static constexpr _q18t _q114 = _q18t::_q114;
static constexpr _q18t _q115 = _q18t::_q115;
static constexpr _q18t _q116 = _q18t::_q116;
static constexpr _q18t _q117 = _q18t::_q117;
static constexpr _q18t _q127 = _q18t::_q127;
private:
_q18t file;
};
class _q13b {
public:
enum class _q18t { _q12z, _q130, _q131, _q132, _q133, _q134, _q135, _q136, _q129 };
constexpr _q13b() : rank_(_q18t::_q129) {}
constexpr _q13b(_q18t r) : rank_(r) {}
constexpr _q13b(int r) : rank_(static_cast<_q18t>(r)) {}
constexpr _q13b(std::string_view sw)
: rank_(static_cast<_q18t>(_q18y::tolower(static_cast<char>(sw[0])) - '1')) {}
[[nodiscard]] constexpr _q18t internal() const noexcept { return rank_; }
constexpr bool operator==(const _q13b& rhs) const noexcept { return rank_ == rhs.rank_; }
constexpr bool operator!=(const _q13b& rhs) const noexcept { return rank_ != rhs.rank_; }
constexpr bool operator==(const _q18t& rhs) const noexcept { return rank_ == rhs; }
constexpr bool operator!=(const _q18t& rhs) const noexcept { return rank_ != rhs; }
constexpr bool operator>=(const _q13b& rhs) const noexcept { return int(*this) >= int(rhs); }
constexpr bool operator<=(const _q13b& rhs) const noexcept { return int(*this) <= int(rhs); }
constexpr _q13b& operator+=(int rhs) noexcept {
rank_ = static_cast<_q18t>(static_cast<int>(rank_) + rhs);
return *this;
}
operator std::string() const { return std::string(1, static_cast<char>('1' + static_cast<int>(rank_))); }
constexpr operator int() const noexcept { return static_cast<int>(rank_); }
[[nodiscard]] constexpr std::uint64_t bb() const noexcept { return 0xffULL << (8 * static_cast<int>(rank_)); }
[[nodiscard]] static constexpr bool _q14v(_q13b r, _q10k _q15b) noexcept {
return static_cast<int>(r) == static_cast<int>(_q15b) * 7;
}
[[nodiscard]] static constexpr _q13b rank(_q13b r, _q10k _q15b) noexcept {
return _q13b(static_cast<int>(r) ^ (static_cast<int>(_q15b) * 7));
}
static constexpr _q18t _q12z = _q18t::_q12z;
static constexpr _q18t _q130 = _q18t::_q130;
static constexpr _q18t _q131 = _q18t::_q131;
static constexpr _q18t _q132 = _q18t::_q132;
static constexpr _q18t _q133 = _q18t::_q133;
static constexpr _q18t _q134 = _q18t::_q134;
static constexpr _q18t _q135 = _q18t::_q135;
static constexpr _q18t _q136 = _q18t::_q136;
static constexpr _q18t _q129 = _q18t::_q129;
private:
_q18t rank_;
};
#define BK_SQ_RANK(N) \
static constexpr auto SQ_A##N = _q18t::SQ_A##N; \
static constexpr auto SQ_B##N = _q18t::SQ_B##N; \
static constexpr auto SQ_C##N = _q18t::SQ_C##N; \
static constexpr auto SQ_D##N = _q18t::SQ_D##N; \
static constexpr auto SQ_E##N = _q18t::SQ_E##N; \
static constexpr auto SQ_F##N = _q18t::SQ_F##N; \
static constexpr auto SQ_G##N = _q18t::SQ_G##N; \
static constexpr auto SQ_H##N = _q18t::SQ_H##N;
class _q13n {
public:
enum class _q18t {
SQ_A1, SQ_B1, SQ_C1, SQ_D1, SQ_E1, SQ_F1, SQ_G1, SQ_H1,
SQ_A2, SQ_B2, SQ_C2, SQ_D2, SQ_E2, SQ_F2, SQ_G2, SQ_H2,
SQ_A3, SQ_B3, SQ_C3, SQ_D3, SQ_E3, SQ_F3, SQ_G3, SQ_H3,
SQ_A4, SQ_B4, SQ_C4, SQ_D4, SQ_E4, SQ_F4, SQ_G4, SQ_H4,
SQ_A5, SQ_B5, SQ_C5, SQ_D5, SQ_E5, SQ_F5, SQ_G5, SQ_H5,
SQ_A6, SQ_B6, SQ_C6, SQ_D6, SQ_E6, SQ_F6, SQ_G6, SQ_H6,
SQ_A7, SQ_B7, SQ_C7, SQ_D7, SQ_E7, SQ_F7, SQ_G7, SQ_H7,
SQ_A8, SQ_B8, SQ_C8, SQ_D8, SQ_E8, SQ_F8, SQ_G8, SQ_H8,
_q12a
};
BK_SQ_RANK(1) BK_SQ_RANK(2) BK_SQ_RANK(3) BK_SQ_RANK(4)
BK_SQ_RANK(5) BK_SQ_RANK(6) BK_SQ_RANK(7) BK_SQ_RANK(8)
static constexpr auto _q12a = _q18t::_q12a;
constexpr _q13n() : sq(_q18t::_q12a) {}
constexpr _q13n(int s) : sq(static_cast<_q18t>(s)) { assert(s <= 64 && s >= 0); }
constexpr _q13n(_q11b f, _q13b r) : sq(static_cast<_q18t>(int(f) + int(r) * 8)) {}
constexpr _q13n(_q13b r, _q11b f) : sq(static_cast<_q18t>(int(f) + int(r) * 8)) {}
constexpr _q13n(_q18t s) : sq(s) {}
constexpr _q13n(std::string_view str)
: sq(static_cast<_q18t>((str[0] - 'a') + (str[1] - '1') * 8)) {
assert(str.size() >= 2);
}
constexpr _q13n operator^(const _q13n& s) const noexcept {
return _q13n(static_cast<_q18t>(static_cast<int>(sq) ^ s.index()));
}
constexpr bool operator==(const _q13n& rhs) const noexcept { return sq == rhs.sq; }
constexpr bool operator!=(const _q13n& rhs) const noexcept { return sq != rhs.sq; }
constexpr bool operator>(const _q13n& rhs) const noexcept { return index() > rhs.index(); }
constexpr bool operator>=(const _q13n& rhs) const noexcept { return index() >= rhs.index(); }
constexpr bool operator<(const _q13n& rhs) const noexcept { return index() < rhs.index(); }
constexpr bool operator<=(const _q13n& rhs) const noexcept { return index() <= rhs.index(); }
constexpr _q13n operator+(const _q13n& rhs) const noexcept {
return _q13n(static_cast<_q18t>(index() + rhs.index()));
}
constexpr _q13n operator-(const _q13n& rhs) const noexcept {
return _q13n(static_cast<_q18t>(index() - rhs.index()));
}
constexpr _q13n& operator++() noexcept {
sq = static_cast<_q18t>(index() + 1);
return *this;
}
constexpr _q13n operator++(int) noexcept {
_q13n old(*this);
++*this;
return old;
}
constexpr _q13n& operator--() noexcept {
sq = static_cast<_q18t>(index() - 1);
return *this;
}
constexpr _q13n operator--(int) noexcept {
_q13n old(*this);
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
[[nodiscard]] constexpr _q11b file() const noexcept { return _q11b(index() & 7); }
[[nodiscard]] constexpr _q13b rank() const noexcept { return _q13b(index() >> 3); }
[[nodiscard]] constexpr bool is_light() const noexcept { return ((index() >> 3) + (index() & 7)) & 1; }
[[nodiscard]] constexpr bool is_dark() const noexcept { return !is_light(); }
[[nodiscard]] constexpr bool _q16k() const noexcept { return static_cast<std::int8_t>(sq) < 64; }
[[nodiscard]] constexpr static bool _q16k(_q13b r, _q11b f) noexcept {
return int(r) >= 0 && int(r) <= 7 && int(f) >= 0 && int(f) <= 7;
}
[[nodiscard]] static int distance(_q13n a, _q13n b) noexcept {
const int _q15h = std::abs((a.index() & 7) - (b.index() & 7));
const int _q15i = std::abs((a.index() >> 3) - (b.index() >> 3));
return _q15h > _q15i ? _q15h : _q15i;
}
[[nodiscard]] static int value_distance(_q13n a, _q13n b) noexcept { return std::abs(a.index() - b.index()); }
[[nodiscard]] static constexpr bool _q18b(_q13n a, _q13n b) noexcept {
return ((9 * (a ^ b).index()) & 8) == 0;
}
[[nodiscard]] static constexpr bool _q14v(_q13n s, _q10k c) noexcept {
return (s.index() >> 3) == static_cast<int>(c) * 7;
}
constexpr _q13n& flip() noexcept {
sq = static_cast<_q18t>(index() ^ 56);
return *this;
}
[[nodiscard]] constexpr _q13n _q187(_q10k c) const noexcept {
return _q13n(index() ^ (static_cast<int>(c) * 56));
}
[[nodiscard]] constexpr int diagonal_of() const noexcept { return 7 + (index() >> 3) - (index() & 7); }
[[nodiscard]] constexpr int antidiagonal_of() const noexcept { return (index() >> 3) + (index() & 7); }
[[nodiscard]] constexpr _q13n ep_square() const noexcept {
assert(rank() == _q13b::_q131 || rank() == _q13b::_q132 || rank() == _q13b::_q133 ||
rank() == _q13b::_q134);
return _q13n(index() ^ 8);
}
[[nodiscard]] static constexpr _q13n _q153(bool is_king_side, _q10k c) noexcept {
return _q13n(is_king_side ? 6 : 2)._q187(c);
}
[[nodiscard]] static constexpr _q13n _q154(bool is_king_side, _q10k c) noexcept {
return _q13n(is_king_side ? 5 : 3)._q187(c);
}
[[nodiscard]] static constexpr int max() noexcept { return 64; }
[[nodiscard]] static bool _q16m(std::string_view str) noexcept {
return str.size() == 2 && str[0] >= 'a' && str[0] <= 'h' && str[1] >= '1' && str[1] <= '8';
}
[[nodiscard]] static constexpr bool _q16l(int s) noexcept { return s >= 0 && s < 64; }
private:
_q18t sq;
};
#undef BK_SQ_RANK
inline std::ostream& operator<<(std::ostream& os, const _q13n& sq) { return os << static_cast<std::string>(sq); }
enum class _q10t : std::int8_t {
_q123 = 8,
_q147 = -1,
_q13d = -8,
_q10u = 1,
_q124 = 9,
_q125 = 7,
_q13f = -9,
_q13e = -7
};
[[nodiscard]] constexpr _q10t make_direction(_q10t dir, _q10k c) noexcept {
return c == _q10k::_q103 ? static_cast<_q10t>(-static_cast<std::int8_t>(dir)) : dir;
}
constexpr _q13n operator+(_q13n sq, _q10t dir) {
return static_cast<_q13n>(sq.index() + static_cast<std::int8_t>(dir));
}
}
namespace _q156 {
class _q10b {
public:
constexpr _q10b() : _q14x(0) {}
constexpr _q10b(std::uint64_t b) : _q14x(b) {}
constexpr _q10b(_q11b file) : _q14x(0x0101010101010101ULL << static_cast<int>(file.internal())) {
assert(file != _q11b::_q127);
}
constexpr _q10b(_q13b rank) : _q14x(0xFFULL << (8 * static_cast<int>(rank.internal()))) {
assert(rank != _q13b::_q129);
}
constexpr explicit operator bool() const noexcept { return _q14x != 0; }
explicit operator std::string() const {
std::bitset<64> b(_q14x);
std::string full = b.to_string();
std::string out;
for (int i = 0; i < 64; i += 8) {
std::string row = full.substr(i, 8);
std::reverse(row.begin(), row.end());
out += row + '\n';
}
return out;
}
constexpr _q10b operator&&(bool rhs) const noexcept { return _q10b(_q14x && rhs); }
constexpr _q10b operator&(std::uint64_t rhs) const noexcept { return _q10b(_q14x & rhs); }
constexpr _q10b operator|(std::uint64_t rhs) const noexcept { return _q10b(_q14x | rhs); }
constexpr _q10b operator^(std::uint64_t rhs) const noexcept { return _q10b(_q14x ^ rhs); }
constexpr _q10b operator<<(std::uint64_t rhs) const noexcept { return _q10b(_q14x << rhs); }
constexpr _q10b operator>>(std::uint64_t rhs) const noexcept { return _q10b(_q14x >> rhs); }
constexpr bool operator==(std::uint64_t rhs) const noexcept { return _q14x == rhs; }
constexpr bool operator!=(std::uint64_t rhs) const noexcept { return _q14x != rhs; }
constexpr _q10b operator&(const _q10b& rhs) const noexcept { return _q10b(_q14x & rhs._q14x); }
constexpr _q10b operator|(const _q10b& rhs) const noexcept { return _q10b(_q14x | rhs._q14x); }
constexpr _q10b operator^(const _q10b& rhs) const noexcept { return _q10b(_q14x ^ rhs._q14x); }
constexpr _q10b operator~() const noexcept { return _q10b(~_q14x); }
constexpr _q10b& operator&=(const _q10b& rhs) noexcept { _q14x &= rhs._q14x; return *this; }
constexpr _q10b& operator|=(const _q10b& rhs) noexcept { _q14x |= rhs._q14x; return *this; }
constexpr _q10b& operator^=(const _q10b& rhs) noexcept { _q14x ^= rhs._q14x; return *this; }
constexpr bool operator==(const _q10b& rhs) const noexcept { return _q14x == rhs._q14x; }
constexpr bool operator!=(const _q10b& rhs) const noexcept { return _q14x != rhs._q14x; }
constexpr bool operator||(const _q10b& rhs) const noexcept { return _q14x || rhs._q14x; }
constexpr bool operator&&(const _q10b& rhs) const noexcept { return _q14x && rhs._q14x; }
constexpr _q10b& set(int index) noexcept {
assert(index >= 0 && index < 64);
_q14x |= (1ULL << index);
return *this;
}
[[nodiscard]] constexpr bool _q155(int index) const noexcept {
assert(index >= 0 && index < 64);
return (_q14x >> index) & 1ULL;
}
constexpr _q10b& clear(int index) noexcept {
assert(index >= 0 && index < 64);
_q14x &= ~(1ULL << index);
return *this;
}
constexpr _q10b& clear() noexcept { _q14x = 0; return *this; }
[[nodiscard]] static constexpr _q10b _q15s(int index) noexcept {
assert(index >= 0 && index < 64);
return _q10b(1ULL << index);
}
[[nodiscard]] static constexpr _q10b _q15s(_q13n sq) noexcept {
assert(sq.index() >= 0 && sq.index() < 64);
return _q10b(1ULL << sq.index());
}
[[nodiscard]] constexpr bool empty() const noexcept { return _q14x == 0; }
[[nodiscard]] BK_CONSTEXPR_BITS int _q16y() const noexcept {
assert(_q14x != 0);
return _q14y::_q15e(_q14x);
}
[[nodiscard]] BK_CONSTEXPR_BITS int _q178() const noexcept {
assert(_q14x != 0);
return 63 ^ _q14y::_q15a(_q14x);
}
[[nodiscard]] BK_CONSTEXPR_BITS int count() const noexcept { return _q14y::_q17t(_q14x); }
[[nodiscard]] BK_CONSTEXPR_BITS std::uint8_t pop() noexcept {
assert(_q14x != 0);
const int i = _q14y::_q15e(_q14x);
_q14x &= _q14x - 1;
return static_cast<std::uint8_t>(i);
}
[[nodiscard]] constexpr std::uint64_t _q15v() const noexcept { return _q14x; }
friend std::ostream& operator<<(std::ostream& os, const _q10b& bb);
private:
std::uint64_t _q14x;
};
inline std::ostream& operator<<(std::ostream& os, const _q10b& bb) { return os << std::string(bb); }
constexpr _q10b operator&(std::uint64_t lhs, const _q10b& rhs) { return rhs & lhs; }
constexpr _q10b operator|(std::uint64_t lhs, const _q10b& rhs) { return rhs | lhs; }
class _q10c;
class _q12t {
public:
enum class _q18t : std::uint8_t { _q12j, _q11m, _q102, _q137, _q12v, _q11k, _q121 };
constexpr _q12t() : pt(_q18t::_q121) {}
constexpr _q12t(_q18t p) : pt(p) {}
constexpr explicit _q12t(std::string_view type) : pt(_q18t::_q121) {
assert(type.size() > 0);
switch (type[0]) {
case 'P': case 'p': pt = _q18t::_q12j; break;
case 'N': case 'n': pt = _q18t::_q11m; break;
case 'B': case 'b': pt = _q18t::_q102; break;
case 'R': case 'r': pt = _q18t::_q137; break;
case 'Q': case 'q': pt = _q18t::_q12v; break;
case 'K': case 'k': pt = _q18t::_q11k; break;
default: pt = _q18t::_q121; break;
}
}
explicit operator std::string() const {
constexpr const char* names[] = {"p", "n", "b", "r", "q", "k"};
return pt == _q18t::_q121 ? " " : names[static_cast<int>(pt)];
}
constexpr bool operator==(const _q12t& rhs) const noexcept { return pt == rhs.pt; }
constexpr bool operator!=(const _q12t& rhs) const noexcept { return pt != rhs.pt; }
constexpr operator int() const noexcept { return static_cast<int>(pt); }
[[nodiscard]] constexpr _q18t internal() const noexcept { return pt; }
static constexpr _q18t _q12j = _q18t::_q12j;
static constexpr _q18t _q11m = _q18t::_q11m;
static constexpr _q18t _q102 = _q18t::_q102;
static constexpr _q18t _q137 = _q18t::_q137;
static constexpr _q18t _q12v = _q18t::_q12v;
static constexpr _q18t _q11k = _q18t::_q11k;
static constexpr _q18t _q121 = _q18t::_q121;
private:
_q18t pt;
};
inline std::ostream& operator<<(std::ostream& os, const _q12t& pt) { return os << static_cast<std::string>(pt); }
namespace _q14y {
inline constexpr std::uint8_t _q12l[13] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6};
inline constexpr std::int8_t _q12k[13] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, -1};
}
class _q12s {
public:
enum class _q18t : std::uint8_t {
_q14c, _q14b, _q149, _q14e, _q14d, _q14a,
_q107, _q106, _q104, _q109, _q108, _q105,
_q121
};
constexpr _q12s() : _q17m(_q18t::_q121) {}
constexpr _q12s(_q18t p) : _q17m(p) {}
constexpr _q12s(_q12t type, _q10k _q15b)
: _q17m((_q15b == _q10k::_q121 || type == _q12t::_q121)
? _q18t::_q121
: static_cast<_q18t>(static_cast<int>(_q15b.internal()) * 6 + static_cast<int>(type))) {}
constexpr _q12s(_q10k _q15b, _q12t type) : _q12s(type, _q15b) {}
constexpr _q12s(std::string_view p) : _q17m(_q15r(p[0])) {}
constexpr bool operator<(const _q12s& rhs) const noexcept { return _q17m < rhs._q17m; }
constexpr bool operator>(const _q12s& rhs) const noexcept { return _q17m > rhs._q17m; }
constexpr bool operator==(const _q12s& rhs) const noexcept { return _q17m == rhs._q17m; }
constexpr bool operator!=(const _q12s& rhs) const noexcept { return _q17m != rhs._q17m; }
constexpr bool operator==(const _q18t& rhs) const noexcept { return _q17m == rhs; }
constexpr bool operator!=(const _q18t& rhs) const noexcept { return _q17m != rhs; }
constexpr bool operator==(const _q12t& rhs) const noexcept { return type() == rhs; }
constexpr bool operator!=(const _q12t& rhs) const noexcept { return type() != rhs; }
explicit operator std::string() const {
constexpr const char* names[] = {"P", "N", "B", "R", "Q", "K", "p", "n", "b", "r", "q", "k"};
return _q17m == _q18t::_q121 ? "." : names[static_cast<int>(_q17m)];
}
constexpr operator int() const noexcept { return static_cast<int>(_q17m); }
[[nodiscard]] constexpr _q12t type() const noexcept {
return static_cast<_q12t::_q18t>(_q14y::_q12l[static_cast<int>(_q17m)]);
}
[[nodiscard]] constexpr _q10k _q15b() const noexcept {
return _q10k(static_cast<int>(_q14y::_q12k[static_cast<int>(_q17m)]));
}
[[nodiscard]] constexpr _q18t internal() const noexcept { return _q17m; }
static constexpr _q18t _q121 = _q18t::_q121;
static constexpr _q18t _q14c = _q18t::_q14c;
static constexpr _q18t _q14b = _q18t::_q14b;
static constexpr _q18t _q149 = _q18t::_q149;
static constexpr _q18t _q14e = _q18t::_q14e;
static constexpr _q18t _q14d = _q18t::_q14d;
static constexpr _q18t _q14a = _q18t::_q14a;
static constexpr _q18t _q107 = _q18t::_q107;
static constexpr _q18t _q106 = _q18t::_q106;
static constexpr _q18t _q104 = _q18t::_q104;
static constexpr _q18t _q109 = _q18t::_q109;
static constexpr _q18t _q108 = _q18t::_q108;
static constexpr _q18t _q105 = _q18t::_q105;
private:
_q18t _q17m;
[[nodiscard]] static constexpr _q18t _q15r(char c) {
constexpr char _q17d[] = "PNBRQKpnbrqk";
for (int i = 0; i < 12; ++i)
if (_q17d[i] == c) return static_cast<_q18t>(i);
return _q18t::_q121;
}
};
namespace _q15d {
constexpr _q10b _q10m = _q10b(0xFFFFFFFFFFFFFFFFull);
constexpr auto _q13i = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
constexpr auto _q11v = 256;
}
class _q11w {
public:
_q11w() = default;
constexpr _q11w(std::uint16_t move) : move_(move), score_(0) {}
template <std::uint16_t MoveType = 0>
[[nodiscard]] static constexpr _q11w _q171(_q13n source, _q13n target, _q12t pt = _q12t::_q11m) noexcept {
assert(pt >= _q12t(_q12t::_q11m) && pt <= _q12t(_q12t::_q12v));
return _q11w(static_cast<std::uint16_t>(MoveType | ((static_cast<int>(pt) - 1) << 12) |
(source.index() << 6) | target.index()));
}
[[nodiscard]] constexpr _q13n from() const noexcept { return static_cast<_q13n>((move_ >> 6) & 0x3F); }
[[nodiscard]] constexpr _q13n to() const noexcept { return static_cast<_q13n>(move_ & 0x3F); }
[[nodiscard]] constexpr std::uint16_t _q18q() const noexcept {
return static_cast<std::uint16_t>(move_ & (3 << 14));
}
[[nodiscard]] constexpr _q12t _q17w() const noexcept {
return static_cast<_q12t::_q18t>(((move_ >> 12) & 3) + 1);
}
constexpr void setScore(std::int16_t score) noexcept { score_ = score; }
[[nodiscard]] constexpr std::uint16_t move() const noexcept { return move_; }
[[nodiscard]] constexpr std::int16_t score() const noexcept { return score_; }
constexpr bool operator==(const _q11w& rhs) const noexcept { return move_ == rhs.move_; }
constexpr bool operator!=(const _q11w& rhs) const noexcept { return move_ != rhs.move_; }
static constexpr std::uint16_t _q128 = 0;
static constexpr std::uint16_t _q12b = 65;
static constexpr std::uint16_t _q122 = 0;
static constexpr std::uint16_t _q12m = 1 << 14;
static constexpr std::uint16_t _q10v = 2 << 14;
static constexpr std::uint16_t _q10f = 3 << 14;
private:
std::uint16_t move_;
std::int16_t score_;
};
class _q177;
class _q11z {
public:
using value_type = _q11w;
using size_type = int;
using difference_type = std::ptrdiff_t;
using reference = value_type&;
using const_reference = const value_type&;
using pointer = value_type*;
using const_pointer = const value_type*;
using iterator = value_type*;
using const_iterator = const value_type*;
using reverse_iterator = std::reverse_iterator<iterator>;
using const_reverse_iterator = std::reverse_iterator<const_iterator>;
_q11z() = default;
_q11z(const _q11z& other) : size_(other.size_) {
std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(_q11w));
}
_q11z& operator=(const _q11z& other) {
if (this != &other) {
size_ = other.size_;
std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(_q11w));
}
return *this;
}
_q11z(_q11z&& other) noexcept : size_(other.size_) {
std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(_q11w));
other.size_ = 0;
}
_q11z& operator=(_q11z&& other) noexcept {
if (this != &other) {
size_ = other.size_;
std::memcpy(moves_.data(), other.moves_.data(), static_cast<std::size_t>(size_) * sizeof(_q11w));
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
assert(size_ < _q15d::_q11v);
moves_[size_++] = move;
}
constexpr void add(value_type&& move) noexcept {
assert(size_ < _q15d::_q11v);
moves_[size_++] = move;
}
[[nodiscard]] [[deprecated("Use std::find() instead.")]] constexpr size_type find(value_type move) const noexcept {
for (size_type i = 0; i < size_; ++i)
if (moves_[i] == move) return i;
return -1;
}
private:
std::array<value_type, _q15d::_q11v> moves_;
size_type size_ = 0;
friend class _q177;
};
}
namespace _q156::_q14y {
using _q13z = std::uint64_t;
inline constexpr _q13z _q110 = 0x0101010101010101ULL;
inline constexpr _q13z _q118 = _q110 << 7;
constexpr _q13z _q18m(int sq, _q13z _q17c, bool _q18a) {
constexpr int _q15i[2][4] = {{1, 1, -1, -1}, {1, -1, 0, 0}};
constexpr int _q15h[2][4] = {{1, -1, 1, -1}, {0, 0, 1, -1}};
_q13z out = 0;
for (int d = 0; d < 4; ++d) {
int r = (sq >> 3) + _q15i[_q18a][d];
int f = (sq & 7) + _q15h[_q18a][d];
while (r >= 0 && r < 8 && f >= 0 && f < 8) {
const _q13z b = 1ULL << (r * 8 + f);
out |= b;
if (_q17c & b) break;
r += _q15i[_q18a][d];
f += _q15h[_q18a][d];
}
}
return out;
}
struct _q13q {
_q13z _q16u[64];
_q13z _q16q[64];
_q13z _q17i[2][64];
_q13z pseudoRook[64];
_q13z pseudoBishop[64];
_q13z between[64][64];
};
constexpr _q13q makeStaticTables() {
_q13q t{};
constexpr int _q16t[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
constexpr int _q16p[8][2] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
for (int s = 0; s < 64; ++s) {
const int r = s >> 3, f = s & 7;
for (int i = 0; i < 8; ++i) {
int rr = r + _q16t[i][0], ff = f + _q16t[i][1];
if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) t._q16u[s] |= 1ULL << (rr * 8 + ff);
rr = r + _q16p[i][0];
ff = f + _q16p[i][1];
if (rr >= 0 && rr < 8 && ff >= 0 && ff < 8) t._q16q[s] |= 1ULL << (rr * 8 + ff);
}
for (int _q15h = -1; _q15h <= 1; _q15h += 2) {
if (f + _q15h < 0 || f + _q15h > 7) continue;
if (r < 7) t._q17i[0][s] |= 1ULL << ((r + 1) * 8 + f + _q15h);
if (r > 0) t._q17i[1][s] |= 1ULL << ((r - 1) * 8 + f + _q15h);
}
t.pseudoRook[s] = _q18m(s, 0, true);
t.pseudoBishop[s] = _q18m(s, 0, false);
}
for (int a = 0; a < 64; ++a) {
for (int b = 0; b < 64; ++b) t.between[a][b] = 1ULL << b;
for (int d = 0; d < 8; ++d) {
constexpr int _q15i[8] = {1, 1, 1, 0, -1, -1, -1, 0};
constexpr int _q15h[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
int r = (a >> 3) + _q15i[d], f = (a & 7) + _q15h[d];
_q13z ray = 0;
while (r >= 0 && r < 8 && f >= 0 && f < 8) {
const int b = r * 8 + f;
ray |= 1ULL << b;
t.between[a][b] = ray;
r += _q15i[d];
f += _q15h[d];
}
}
}
return t;
}
inline constexpr _q13q _q13g = makeStaticTables();
}
namespace _q156 {
class _q14u {
using _q13z = std::uint64_t;
#ifdef CHESS_USE_PEXT
struct alignas(16) Magic {
_q13z mask;
_q10b* table;
BK_INLINE std::size_t index(_q13z _q17c) const noexcept { return _pext_u64(_q17c, mask); }
};
#else
struct alignas(32) Magic {
_q13z mask;
_q13z _q16z;
_q10b* table;
unsigned shift;
BK_INLINE std::size_t index(_q13z _q17c) const noexcept {
return static_cast<std::size_t>(((_q17c & mask) * _q16z) >> shift);
}
};
#endif
static constexpr _q13z _q10a[64] = {
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
static constexpr _q13z _q13c[64] = {
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
static inline _q10b RookAttacks[0x19000] = {};
static inline _q10b BishopAttacks[0x1480] = {};
static inline Magic RookTable[64] = {};
static inline Magic BishopTable[64] = {};
static inline bool initialised_ = false;
template <bool ISROOK>
static void _q14z(Magic* tables, _q10b* storage, const _q13z* magics) noexcept {
_q10b* cursor = storage;
for (int sq = 0; sq < 64; ++sq) {
const _q13z edges = (((0xFFULL | (0xFFULL << 56)) & ~(0xFFULL << (8 * (sq >> 3)))) |
((_q14y::_q110 | _q14y::_q118) & ~(_q14y::_q110 << (sq & 7))));
Magic& m = tables[sq];
m.mask = _q14y::_q18m(sq, 0, ISROOK) & ~edges;
m.table = cursor;
#ifndef CHESS_USE_PEXT
m._q16z = magics[sq];
m.shift = static_cast<unsigned>(64 - _q14y::_q17t(m.mask));
#else
(void)magics;
#endif
_q13z _q17c = 0;
do {
m.table[m.index(_q17c)] = _q10b(_q14y::_q18m(sq, _q17c, ISROOK));
_q17c = (_q17c - m.mask) & m.mask;
} while (_q17c);
cursor += (1ULL << _q14y::_q17t(m.mask));
}
}
public:
static constexpr _q10b _q11u[8] = {0xff, 0xff00, 0xff0000, 0xff000000,
0xff00000000, 0xff0000000000, 0xff000000000000, 0xff00000000000000};
static constexpr _q10b _q11t[8] = {
0x101010101010101, 0x202020202020202, 0x404040404040404, 0x808080808080808,
0x1010101010101010, 0x2020202020202020, 0x4040404040404040, 0x8080808080808080,
};
template <_q10t direction>
[[nodiscard]] static constexpr _q10b shift(const _q10b b) {
switch (direction) {
case _q10t::_q123: return b << 8;
case _q10t::_q13d: return b >> 8;
case _q10t::_q125: return (b & ~_q14y::_q110) << 7;
case _q10t::_q147: return (b & ~_q14y::_q110) >> 1;
case _q10t::_q13f: return (b & ~_q14y::_q110) >> 9;
case _q10t::_q124: return (b & ~_q14y::_q118) << 9;
case _q10t::_q10u: return (b & ~_q14y::_q118) << 1;
case _q10t::_q13e: return (b & ~_q14y::_q118) >> 7;
}
return {};
}
template <_q10k::_q18t c>
[[nodiscard]] static BK_INLINE _q10b pawnLeftAttacks(const _q10b pawns) {
return c == _q10k::_q148 ? (pawns << 7) & ~_q14y::_q118 : (pawns >> 7) & ~_q14y::_q110;
}
template <_q10k::_q18t c>
[[nodiscard]] static BK_INLINE _q10b pawnRightAttacks(const _q10b pawns) {
return c == _q10k::_q148 ? (pawns << 9) & ~_q14y::_q110 : (pawns >> 9) & ~_q14y::_q118;
}
[[nodiscard]] static BK_INLINE _q10b _q17i(_q10k c, _q13n sq) noexcept {
return _q10b(_q14y::_q13g._q17i[static_cast<int>(c)][sq.index()]);
}
[[nodiscard]] static BK_INLINE _q10b _q16u(_q13n sq) noexcept { return _q10b(_q14y::_q13g._q16u[sq.index()]); }
[[nodiscard]] static BK_INLINE _q10b _q16q(_q13n sq) noexcept { return _q10b(_q14y::_q13g._q16q[sq.index()]); }
[[nodiscard]] static BK_INLINE _q10b _q14w(_q13n sq, _q10b occupied) noexcept {
const Magic& m = BishopTable[sq.index()];
return m.table[m.index(occupied._q15v())];
}
[[nodiscard]] static BK_INLINE _q10b _q18a(_q13n sq, _q10b occupied) noexcept {
const Magic& m = RookTable[sq.index()];
return m.table[m.index(occupied._q15v())];
}
[[nodiscard]] static BK_INLINE _q10b _q17y(_q13n sq, _q10b occupied) noexcept {
return _q14w(sq, occupied) | _q18a(sq, occupied);
}
[[nodiscard]] static _q10b attackers(const _q10c& board, _q10k _q15b, _q13n square) noexcept;
template <_q12t::_q18t pt>
[[nodiscard]] static BK_INLINE _q10b slider(_q13n sq, _q10b occupied) noexcept {
static_assert(pt == _q12t::_q102 || pt == _q12t::_q137 || pt == _q12t::_q12v,
"PieceType must be a slider!");
if constexpr (pt == _q12t::_q102) return _q14w(sq, occupied);
if constexpr (pt == _q12t::_q137) return _q18a(sq, occupied);
if constexpr (pt == _q12t::_q12v) return _q17y(sq, occupied);
}
static inline void _q16a() {
if (initialised_) return;
_q14z<false>(BishopTable, BishopAttacks, _q10a);
_q14z<true>(RookTable, RookAttacks, _q13c);
initialised_ = true;
}
};
namespace _q14y {
inline const bool g_attacks_ready = (_q14u::_q16a(), true);
}
}
namespace _q156 {
class _q14i {
using _q13z = std::uint64_t;
static constexpr _q13z _q12y[781] = {
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
static constexpr std::array<_q13z, 16> castlingKey = [] {
std::array<_q13z, 16> a{};
for (int m = 0; m < 16; ++m)
for (int b = 0; b < 4; ++b)
if (m & (1 << b)) a[m] ^= _q12y[768 + b];
return a;
}();
static constexpr std::array<_q13z, 768> pieceKey = [] {
constexpr int _q17s[12] = {1, 3, 5, 7, 9, 11, 0, 2, 4, 6, 8, 10};
std::array<_q13z, 768> a{};
for (int p = 0; p < 12; ++p)
for (int s = 0; s < 64; ++s) a[p * 64 + s] = _q12y[64 * _q17s[p] + s];
return a;
}();
public:
[[nodiscard]] static BK_INLINE _q13z _q17m(_q12s p, _q13n s) noexcept {
assert(static_cast<int>(p) < 12);
return pieceKey[static_cast<int>(p) * 64 + s.index()];
}
[[nodiscard]] static BK_INLINE _q13z _q17n(int p, int s) noexcept { return pieceKey[p * 64 + s]; }
[[nodiscard]] static BK_INLINE _q13z _q15j(_q11b f) noexcept { return _q12y[772 + static_cast<int>(f)]; }
[[nodiscard]] static BK_INLINE _q13z _q150(int rights) noexcept { return castlingKey[rights]; }
[[nodiscard]] static BK_INLINE _q13z castlingIndex(int idx) noexcept { return _q12y[768 + idx]; }
[[nodiscard]] static BK_INLINE _q13z _q18j() noexcept { return _q12y[780]; }
public:
friend class _q10c;
};
enum class _q11c { _q14f, _q11r, _q10p, _q121 };
enum class _q11d {
_q10g,
_q13h,
_q11j,
_q10y,
_q13w,
_q121
};
enum class _q10j { _q126, _q10n, _q10o };
enum PieceGenType {
_q12j = 1,
_q11m = 2,
_q102 = 4,
_q137 = 8,
_q12v = 16,
_q11k = 32,
};
using _q12p = std::array<std::uint8_t, 24>;
namespace _q15g {
inline std::optional<int> _q17h(std::string_view sv) {
if (sv.empty()) return std::nullopt;
if (sv.back() == ';') sv.remove_suffix(1);
if (sv.empty()) return std::nullopt;
#ifdef BACKENED_HAS_CHARCONV
int value;
const char* first = sv.data();
const char* last = first + sv.size();
auto [ptr, ec] = std::from_chars(first, last, value);
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
}
}
namespace _q156 {
class _q177 {
public:
enum class _q11x : std::uint8_t { _qzy, _q10e, _q12x };
enum class _q11e : std::uint8_t { _qzz, _q12h };
template <_q11x mt = _q11x::_qzy>
static void _q16x(_q11z& movelist, const _q10c& board,
int _q17o = PieceGenType::_q12j | PieceGenType::_q11m | PieceGenType::_q102 |
PieceGenType::_q137 | PieceGenType::_q12v | PieceGenType::_q11k);
template <_q11x mt = _q11x::_qzy>
static bool _q14q(const _q10c& board, int _q17o = PieceGenType::_q12j | PieceGenType::_q11m |
PieceGenType::_q102 | PieceGenType::_q137 |
PieceGenType::_q12v | PieceGenType::_q11k);
[[nodiscard]] static bool _q16f(const _q10c& board, const _q11w move);
private:
template <_q10k::_q18t c, _q11x mt, bool ONE>
static void generate(_q11z& movelist, const _q10c& board, int _q17o);
[[nodiscard]] static _q10b between(_q13n sq1, _q13n sq2) noexcept {
return _q10b(_q14y::_q13g.between[sq1.index()][sq2.index()]);
}
template <_q10k::_q18t c>
[[nodiscard]] static bool _q16g(const _q10c& board, const _q11w move);
friend class _q10c;
};
class _q10c {
using _q13z = std::uint64_t;
public:
class _q10i {
public:
enum class _q13m : std::uint8_t { _q11l, _q12w };
constexpr void _q18f(_q10k _q15b, _q13m castle, _q11b rook_file) {
files_[static_cast<int>(_q15b)][static_cast<int>(castle)] = rook_file;
bits_ = static_cast<std::uint8_t>(bits_ | (1u << _q18n(_q15b, castle)));
}
constexpr void clear() {
bits_ = 0;
for (auto& row : files_)
for (auto& f : row) f = _q11b::_q127;
}
constexpr int clear(_q10k _q15b, _q13m castle) {
const int s = _q18n(_q15b, castle);
bits_ = static_cast<std::uint8_t>(bits_ & ~(1u << s));
return s;
}
constexpr void clear(_q10k _q15b) { bits_ = static_cast<std::uint8_t>(bits_ & ~(3u << (2 * static_cast<int>(_q15b)))); }
constexpr bool _q162(_q10k _q15b, _q13m castle) const { return (bits_ >> _q18n(_q15b, castle)) & 1u; }
constexpr bool _q162(_q10k _q15b) const { return (bits_ >> (2 * static_cast<int>(_q15b))) & 3u; }
constexpr _q11b _q15y(_q10k _q15b, _q13m castle) const {
return _q162(_q15b, castle) ? files_[static_cast<int>(_q15b)][static_cast<int>(castle)] : _q11b(_q11b::_q127);
}
constexpr int _q166() const { return bits_; }
constexpr bool _q16d() const { return bits_ == 0; }
template <typename T>
static constexpr _q13m _q159(T sq, T pred) {
return sq > pred ? _q13m::_q11l : _q13m::_q12w;
}
bool operator==(const _q10i& other) const noexcept {
if (bits_ != other.bits_) return false;
for (int c = 0; c < 2; ++c)
for (int s = 0; s < 2; ++s)
if (((bits_ >> (c * 2 + s)) & 1u) && files_[c][s] != other.files_[c][s]) return false;
return true;
}
constexpr void _q16o(int mask) { bits_ = static_cast<std::uint8_t>(bits_ & mask); }
constexpr int _q14x() const { return bits_; }
private:
static constexpr int _q18n(_q10k c, _q13m s) { return static_cast<int>(c) * 2 + static_cast<int>(s); }
_q11b files_[2][2] = {{_q11b(_q11b::_q127), _q11b(_q11b::_q127)}, {_q11b(_q11b::_q127), _q11b(_q11b::_q127)}};
std::uint8_t bits_ = 0;
};
private:
struct _q13p {
_q13z hash;
_q10i _q150;
_q13n _q15j;
std::uint8_t half_moves;
_q12s captured_piece;
bool null_move = false;
_q13p(const _q13z& h, const _q10i& c, const _q13n& e, const std::uint8_t& hm, const _q12s& cp, const bool nm = false)
: hash(h), _q150(c), _q15j(e), half_moves(hm), captured_piece(cp), null_move(nm) {}
bool operator==(const _q13p& o) const noexcept {
return hash == o.hash && _q150 == o._q150 && _q15j == o._q15j &&
half_moves == o.half_moves && captured_piece == o.captured_piece;
}
};
protected:
enum class _q12u { _q10h };
_q10c(_q12u) {}
public:
explicit _q10c(std::string_view fen = _q15d::_q13i, bool _q157 = false) {
prev_states_.reserve(256);
chess960_ = _q157;
_q18h<true>(fen);
}
_q10c(const _q10c&) = default;
_q10c(_q10c&&) = default;
_q10c& operator=(const _q10c&) = default;
_q10c& operator=(_q10c&&) = default;
#ifndef BACKENED_NO_VIRTUAL
virtual ~_q10c() = default;
#endif
static _q10c fromFen(std::string_view fen) { return _q10c(fen); }
static _q10c fromXfen(std::string_view xfen) {
_q10c board;
board.setXfen(xfen);
return board;
}
static _q10c fromEpd(std::string_view epd) {
_q10c board;
board.setEpd(epd);
return board;
}
BK_VIRTUAL bool _q18g(std::string_view fen) { return _q18h(fen); }
bool setXfen(std::string_view xfen) {
const bool prev_960 = chess960_;
chess960_ = true;
const bool ok = _q17g<false>(xfen, true, true);
if (!ok) chess960_ = prev_960;
return ok;
}
bool setEpd(const std::string_view epd) {
auto parts = _q18y::splitString(epd, ' ');
if (parts.size() < 4) return false;
int hm = 0;
int fm = 1;
if (auto it = std::find(parts.begin(), parts.end(), "hmvc"); it != parts.end()) {
if (std::distance(it, parts.end()) <= 1) return false;
if (auto v = _q15g::_q17h(*(it + 1))) hm = *v;
}
if (auto it = std::find(parts.begin(), parts.end(), "fmvn"); it != parts.end()) {
if (std::distance(it, parts.end()) <= 1) return false;
auto v = _q15g::_q17h(*(it + 1));
if (!v || *v <= 0) return false;
fm = *v;
}
std::string fen;
fen.reserve(96);
fen.append(parts[0]).append(" ").append(parts[1]).append(" ").append(parts[2]).append(" ").append(parts[3]);
fen.append(" ").append(std::to_string(hm)).append(" ").append(std::to_string(fm));
return _q18g(fen);
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
out += std::to_string(_q161()) + ";";
out += " fmvn ";
out += std::to_string(_q15t()) + ";";
return out;
}
[[nodiscard]] bool _q16f(const _q11w move) const { return _q177::_q16f(*this, move); }
template <bool EXACT = false>
void _q172(const _q11w move) {
#ifdef BACKENED_NO_VIRTUAL
_q173<EXACT, false>(move);
#else
if (BK_LIKELY(plainBoard()))
_q173<EXACT, false>(move);
else
_q173<EXACT, true>(move);
#endif
}
void _q18u(const _q11w move) {
#ifdef BACKENED_NO_VIRTUAL
_q18v<false>(move);
#else
if (BK_LIKELY(plainBoard()))
_q18v<false>(move);
else
_q18v<true>(move);
#endif
}
private:
template <bool HOOKS>
BK_INLINE void _q189(_q12s p, _q13n s) {
if constexpr (HOOKS)
removePiece(p, s);
else
_q188(p, s);
}
template <bool HOOKS>
BK_INLINE void _q17p(_q12s p, _q13n s) {
if constexpr (HOOKS)
_q17q(p, s);
else
_q17r(p, s);
}
template <bool HOOKS>
BK_INLINE void _q17a(_q12s p, _q13n from, _q13n to) {
if constexpr (HOOKS) {
removePiece(p, from);
_q17q(p, to);
} else {
_q176(p, from, to);
}
}
template <bool EXACT, bool HOOKS>
void _q173(const _q11w move) {
const int from_i = move.from().index();
const int to_i = move.to().index();
const int pc = static_cast<int>(board_[from_i]);
const int us = static_cast<int>(stm_);
const auto mtype = move._q18q();
assert(pc != 12 && _q14y::_q12k[pc] == us);
const bool is_castle = mtype == _q11w::_q10f;
const int cap_piece = is_castle ? 12 : static_cast<int>(board_[to_i]);
prev_states_.emplace_back(key_, cr_, ep_sq_, hfm_, _q12s(static_cast<_q12s::_q18t>(cap_piece)));
_q13z k = key_ ^ _q14i::_q18j();
if (ep_sq_ != _q13n::_q12a) {
k ^= _q14i::_q15j(ep_sq_.file());
ep_sq_ = _q13n::_q12a;
}
++hfm_;
++plies_;
{
const int before = cr_._q14x();
const int after = before & castle_mask_[from_i] & castle_mask_[to_i];
cr_._q16o(after);
k ^= _q14i::_q150(before ^ after);
}
if (cap_piece != 12) {
_q189<HOOKS>(_q12s(static_cast<_q12s::_q18t>(cap_piece)), move.to());
k ^= _q14i::_q17n(cap_piece, to_i);
hfm_ = 0;
}
const int pt = _q14y::_q12l[pc];
if (mtype == _q11w::_q122) {
_q17a<HOOKS>(_q12s(static_cast<_q12s::_q18t>(pc)), move.from(), move.to());
k ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(pc, to_i);
} else if (mtype == _q11w::_q12m) {
const int promo = static_cast<int>(move._q17w()) + 6 * us;
_q189<HOOKS>(_q12s(static_cast<_q12s::_q18t>(pc)), move.from());
_q17p<HOOKS>(_q12s(static_cast<_q12s::_q18t>(promo)), move.to());
k ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(promo, to_i);
} else if (mtype == _q11w::_q10v) {
_q17a<HOOKS>(_q12s(static_cast<_q12s::_q18t>(pc)), move.from(), move.to());
k ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(pc, to_i);
const int cap_sq = to_i ^ 8;
const int victim = 6 * (us ^ 1);
_q189<HOOKS>(_q12s(static_cast<_q12s::_q18t>(victim)), _q13n(cap_sq));
k ^= _q14i::_q17n(victim, cap_sq);
} else {
const bool king_side = to_i > from_i;
const int king_to = (king_side ? 6 : 2) ^ (us * 56);
const int rook_to = (king_side ? 5 : 3) ^ (us * 56);
const int _q18a = static_cast<int>(board_[to_i]);
_q189<HOOKS>(_q12s(static_cast<_q12s::_q18t>(pc)), move.from());
_q189<HOOKS>(_q12s(static_cast<_q12s::_q18t>(_q18a)), move.to());
_q17p<HOOKS>(_q12s(static_cast<_q12s::_q18t>(pc)), _q13n(king_to));
_q17p<HOOKS>(_q12s(static_cast<_q12s::_q18t>(_q18a)), _q13n(rook_to));
k ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(pc, king_to);
k ^= _q14i::_q17n(_q18a, to_i) ^ _q14i::_q17n(_q18a, rook_to);
}
if (pt == 0) {
hfm_ = 0;
if (((from_i ^ to_i) == 16)) {
const _q13n eps(to_i ^ 8);
const _q10k them(us ^ 1);
bool record;
if constexpr (EXACT)
record = _q15l(them, eps);
else
record = (_q14u::_q17i(_q10k(us), eps) & pieces_bb_[0] & occ_bb_[us ^ 1])._q15v() != 0;
if (record) {
ep_sq_ = eps;
k ^= _q14i::_q15j(eps.file());
}
}
}
key_ = k;
stm_ = _q10k(us ^ 1);
}
template <bool HOOKS>
void _q18v(const _q11w move) {
const _q13p& prev = prev_states_.back();
ep_sq_ = prev._q15j;
cr_ = prev._q150;
hfm_ = prev.half_moves;
stm_ = ~stm_;
--plies_;
const int us = static_cast<int>(stm_);
const auto mtype = move._q18q();
const _q13n from = move.from();
const _q13n to = move.to();
if (mtype == _q11w::_q10f) {
const bool king_side = to.index() > from.index();
const _q13n rook_from(king_side ? 5 ^ (us * 56) : 3 ^ (us * 56));
const _q13n king_to(king_side ? 6 ^ (us * 56) : 2 ^ (us * 56));
const _q12s _q18a = board_[rook_from.index()];
const _q12s _q16q = board_[king_to.index()];
_q189<HOOKS>(_q18a, rook_from);
_q189<HOOKS>(_q16q, king_to);
_q17p<HOOKS>(_q16q, from);
_q17p<HOOKS>(_q18a, to);
} else if (mtype == _q11w::_q12m) {
const _q12s promoted = board_[to.index()];
_q189<HOOKS>(promoted, to);
_q17p<HOOKS>(_q12s(static_cast<_q12s::_q18t>(6 * us)), from);
if (prev.captured_piece != _q12s::_q121) _q17p<HOOKS>(prev.captured_piece, to);
} else {
const _q12s moved = board_[to.index()];
_q17a<HOOKS>(moved, to, from);
if (mtype == _q11w::_q10v) {
_q17p<HOOKS>(_q12s(static_cast<_q12s::_q18t>(6 * (us ^ 1))), _q13n(to.index() ^ 8));
} else if (prev.captured_piece != _q12s::_q121) {
_q17p<HOOKS>(prev.captured_piece, to);
}
}
key_ = prev.hash;
prev_states_.pop_back();
}
public:
void _q174() {
prev_states_.emplace_back(key_, cr_, ep_sq_, hfm_, _q12s::_q121, true);
key_ ^= _q14i::_q18j();
if (ep_sq_ != _q13n::_q12a) key_ ^= _q14i::_q15j(ep_sq_.file());
ep_sq_ = _q13n::_q12a;
stm_ = ~stm_;
++plies_;
}
void _q18w() {
const _q13p& prev = prev_states_.back();
ep_sq_ = prev._q15j;
cr_ = prev._q150;
hfm_ = prev.half_moves;
key_ = prev.hash;
--plies_;
stm_ = ~stm_;
prev_states_.pop_back();
}
void backtrackTo(const _q10c& previous) {
const std::size_t n = previous.prev_states_.size();
assert(n <= prev_states_.size());
prev_states_.erase(prev_states_.begin() + static_cast<std::ptrdiff_t>(n), prev_states_.end());
pieces_bb_ = previous.pieces_bb_;
occ_bb_ = previous.occ_bb_;
board_ = previous.board_;
key_ = previous.key_;
cr_ = previous.cr_;
plies_ = previous.plies_;
stm_ = previous.stm_;
ep_sq_ = previous.ep_sq_;
hfm_ = previous.hfm_;
chess960_ = previous.chess960_;
castling_path = previous.castling_path;
castle_mask_ = previous.castle_mask_;
}
[[nodiscard]] BK_INLINE _q10b us(_q10k _q15b) const noexcept { return occ_bb_[static_cast<int>(_q15b)]; }
[[nodiscard]] BK_INLINE _q10b them(_q10k _q15b) const noexcept { return occ_bb_[static_cast<int>(_q15b) ^ 1]; }
[[nodiscard]] BK_INLINE _q10b _q17c() const noexcept { return occ_bb_[0] | occ_bb_[1]; }
[[nodiscard]] BK_INLINE _q10b all() const noexcept { return occ_bb_[0] | occ_bb_[1]; }
[[nodiscard]] BK_INLINE _q13n _q16r(_q10k _q15b) const noexcept {
assert(_q17o(_q12t::_q11k, _q15b) != 0ull);
return _q13n((pieces_bb_[5] & occ_bb_[static_cast<int>(_q15b)])._q16y());
}
[[nodiscard]] BK_INLINE _q10b _q17o(_q12t type, _q10k _q15b) const noexcept {
return pieces_bb_[static_cast<int>(type)] & occ_bb_[static_cast<int>(_q15b)];
}
[[nodiscard]] BK_INLINE _q10b _q17o(_q12t type) const noexcept {
return pieces_bb_[static_cast<int>(type)];
}
template <typename... Pieces, typename = std::enable_if_t<(std::is_convertible_v<Pieces, _q12t> && ...)>>
[[nodiscard]] BK_INLINE _q10b _q17o(Pieces... pcs) const noexcept {
return (pieces_bb_[static_cast<int>(static_cast<_q12t>(pcs))] | ...);
}
template <typename T = _q12s>
[[nodiscard]] BK_INLINE T at(_q13n sq) const noexcept {
assert(sq._q16k());
if constexpr (std::is_same_v<T, _q12t>)
return static_cast<_q12t::_q18t>(_q14y::_q12l[static_cast<int>(board_[sq.index()])]);
else
return board_[sq.index()];
}
[[nodiscard]] bool _q16c(const _q11w move) const noexcept {
return (at(move.to()) != _q12s::_q121 && move._q18q() != _q11w::_q10f) || move._q18q() == _q11w::_q10v;
}
template <typename T = _q12s>
[[nodiscard]] T getCapturing(const _q11w move) const noexcept {
assert(at(move.from()) != _q12s::_q121);
const bool ep = move._q18q() == _q11w::_q10v;
const bool cap = !ep && at(move.to()) != _q12s::_q121 && move._q18q() != _q11w::_q10f;
if constexpr (std::is_same_v<T, _q12t>) {
if (ep) return _q12t::_q12j;
if (cap) return at<_q12t>(move.to());
return _q12t::_q121;
} else {
if (ep) return stm_ == _q10k::_q148 ? _q12s(_q12s::_q107) : _q12s(_q12s::_q14c);
if (cap) return at(move.to());
return _q12s::_q121;
}
}
[[nodiscard]] _q13z hash() const noexcept { return key_; }
[[nodiscard]] _q10k _q18j() const noexcept { return stm_; }
[[nodiscard]] _q13n _q15k() const noexcept { return ep_sq_; }
[[nodiscard]] _q10i _q152() const noexcept { return cr_; }
[[nodiscard]] std::uint32_t _q161() const noexcept { return hfm_; }
[[nodiscard]] std::uint32_t _q15t() const noexcept { return 1 + plies_ / 2; }
void _q18e(bool is960) {
chess960_ = is960;
if (!original_fen_.empty()) _q18g(original_fen_);
}
[[nodiscard]] bool _q157() const noexcept { return chess960_; }
[[nodiscard]] std::string getCastleString() const {
std::string out;
if (chess960_) {
for (auto _q15b : {_q10k::_q148, _q10k::_q103})
for (auto side : {_q10i::_q13m::_q11l, _q10i::_q13m::_q12w})
if (cr_._q162(_q15b, side)) out += _q151(_q15b, cr_._q15y(_q15b, side));
return out;
}
if (cr_._q162(_q10k::_q148, _q10i::_q13m::_q11l)) out += 'K';
if (cr_._q162(_q10k::_q148, _q10i::_q13m::_q12w)) out += 'Q';
if (cr_._q162(_q10k::_q103, _q10i::_q13m::_q11l)) out += 'k';
if (cr_._q162(_q10k::_q103, _q10i::_q13m::_q12w)) out += 'q';
return out;
}
[[nodiscard]] bool _q16h(int count = 2) const noexcept {
const int size = static_cast<int>(prev_states_.size());
const int low = size - static_cast<int>(hfm_) - 1;
int seen = 0;
const _q13p* s = prev_states_.data();
for (int i = size - 2; i >= 0 && i >= low; i -= 2) {
if (s[i].hash == key_ && ++seen == count) return true;
}
return false;
}
[[nodiscard]] bool _q16i(int searchPly) const noexcept {
const int size = static_cast<int>(prev_states_.size());
const int low = size - static_cast<int>(hfm_) - 1;
int seen = 0;
const _q13p* s = prev_states_.data();
for (int i = size - 2; i >= 0 && i >= low; i -= 2) {
if (s[i].hash == key_) {
if (size - i <= searchPly) return true;
if (++seen == 2) return true;
}
}
return false;
}
[[nodiscard]] bool isHalfMoveDraw() const noexcept { return hfm_ >= 100; }
[[nodiscard]] bool _q165(int ply, const _q13z cuckoo[8192], const _q11w cuckooMove[8192]) const noexcept {
const int size = static_cast<int>(prev_states_.size());
int end = static_cast<int>(hfm_);
{
for (int i = size - 1; i >= 0 && i >= size - end - 1; --i)
if (prev_states_[i].null_move) { end = size - 1 - i; break; }
}
if (end < 3) return false;
const _q13z originalKey = key_;
const _q13p* stp = prev_states_.data();
const _q10b occ_bb = _q17c();
for (int i = 3; i <= end && i <= size; i += 2)
{
const _q13p& prev = stp[size - i];
const _q13z moveKey = originalKey ^ prev.hash;
int j = static_cast<int>(moveKey & 0x1fff);
if (cuckoo[j] != moveKey)
{
j = static_cast<int>((moveKey >> 16) & 0x1fff);
if (cuckoo[j] != moveKey) continue;
}
const _q11w move = cuckooMove[j];
const _q13n s1 = move.from();
const _q13n s2 = move.to();
if (!((_q10b(_q14y::_q13g.between[s1.index()][s2.index()])
^ _q10b::_q15s(s2)) & occ_bb))
{
if (ply > i)
return true;
const _q13n fromSq = (occ_bb._q15v() & _q10b::_q15s(s1)._q15v())
? s1 : s2;
if (!(occ_bb._q15v() & _q10b::_q15s(fromSq)._q15v()))
continue;
const _q12s pc = board_[fromSq.index()];
if (pc._q15b() != stm_ || pc.type() == _q12t::_q12j || pc.type() == _q12t::_q11k)
continue;
int seen = 0;
for (int k = size - i - 2; k >= 0 && k >= size - static_cast<int>(hfm_) - 1; k -= 2)
if (stp[k].hash == prev.hash && ++seen >= 1) return true;
}
}
return false;
}
[[nodiscard]] std::pair<_q11d, _q11c> _q15x() const noexcept {
if (!_q177::_q14q(*this) && _q167()) return {_q11d::_q10g, _q11c::_q11r};
return {_q11d::_q10y, _q11c::_q10p};
}
[[nodiscard]] bool _q16e() const noexcept {
const int count = _q17c().count();
if (count == 2) return true;
if (count == 3) {
if (_q17o(_q12t::_q102) || _q17o(_q12t::_q11m)) return true;
}
if (count == 4) {
const _q10b wb = _q17o(_q12t::_q102, _q10k::_q148);
const _q10b bb = _q17o(_q12t::_q102, _q10k::_q103);
if (wb && bb && _q13n::_q18b(_q13n(wb._q16y()), _q13n(bb._q16y()))) return true;
if (wb.count() == 2) {
if (_q13n::_q18b(_q13n(wb._q16y()), _q13n(wb._q178()))) return true;
} else if (bb.count() == 2) {
if (_q13n::_q18b(_q13n(bb._q16y()), _q13n(bb._q178()))) return true;
}
}
return false;
}
[[nodiscard]] std::pair<_q11d, _q11c> isGameOver() const noexcept {
if (isHalfMoveDraw()) return _q15x();
if (_q16e()) return {_q11d::_q11j, _q11c::_q10p};
if (_q16h()) return {_q11d::_q13w, _q11c::_q10p};
if (!_q177::_q14q(*this)) {
if (_q167()) return {_q11d::_q10g, _q11c::_q11r};
return {_q11d::_q13h, _q11c::_q10p};
}
return {_q11d::_q121, _q11c::_q121};
}
[[nodiscard]] BK_INLINE bool _q16b(_q13n square, _q10k _q15b) const noexcept {
const int c = static_cast<int>(_q15b);
const _q10b o = _q17c();
const _q10b u = occ_bb_[c];
if (_q14u::_q17i(_q10k(c ^ 1), square) & pieces_bb_[0] & u) return true;
if (_q14u::_q16u(square) & pieces_bb_[1] & u) return true;
if (_q14u::_q16q(square) & pieces_bb_[5] & u) return true;
if (_q14u::_q14w(square, o) & (pieces_bb_[2] | pieces_bb_[4]) & u) return true;
if (_q14u::_q18a(square, o) & (pieces_bb_[3] | pieces_bb_[4]) & u) return true;
return false;
}
[[nodiscard]] BK_INLINE bool _q167() const noexcept { return _q16b(_q16r(stm_), ~stm_); }
[[nodiscard]] _q10j _q15z(const _q11w& m) const noexcept;
[[nodiscard]] bool _q163(_q10k _q15b) const noexcept {
const _q10b u = us(_q15b);
return bool(u & ~(pieces_bb_[0] | pieces_bb_[5]));
}
[[nodiscard]] _q13z zobrist() const {
_q13z h = 0ULL;
_q10b oc = _q17c();
while (oc) {
const int s = oc.pop();
h ^= _q14i::_q17n(static_cast<int>(board_[s]), s);
}
if (ep_sq_ != _q13n::_q12a) h ^= _q14i::_q15j(ep_sq_.file());
if (stm_ == _q10k::_q148) h ^= _q14i::_q18j();
h ^= _q14i::_q150(cr_._q166());
return h;
}
template <bool EXACT = true>
[[nodiscard]] _q13z _q18z(const _q11w& move) const {
_q13z key = key_ ^ _q14i::_q18j();
if (EXACT && ep_sq_ != _q13n::_q12a) key ^= _q14i::_q15j(ep_sq_.file());
if (move == _q11w::_q12b) return key;
const int from_i = move.from().index();
const int to_i = move.to().index();
const int us_i = static_cast<int>(stm_);
const int pc = static_cast<int>(board_[from_i]);
const auto mtype = move._q18q();
const int target = static_cast<int>(board_[to_i]);
if (target != 12 && mtype != _q11w::_q10f) key ^= _q14i::_q17n(target, to_i);
if constexpr (EXACT) {
const int before = cr_._q14x();
const int after = before & castle_mask_[from_i] & castle_mask_[to_i];
key ^= _q14i::_q150(before ^ after);
if (_q14y::_q12l[pc] == 0 && ((from_i ^ to_i) == 16)) {
const _q13n eps(to_i ^ 8);
if (_q14u::_q17i(_q10k(us_i), eps) & pieces_bb_[0] & occ_bb_[us_i ^ 1])
key ^= _q14i::_q15j(eps.file());
}
}
if (mtype != _q11w::_q10f && mtype != _q11w::_q12m) {
key ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(pc, to_i);
} else if constexpr (EXACT) {
if (mtype == _q11w::_q10f) {
const bool ks = to_i > from_i;
const int kto = (ks ? 6 : 2) ^ (us_i * 56);
const int rto = (ks ? 5 : 3) ^ (us_i * 56);
const int _q18a = target;
key ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(pc, kto);
key ^= _q14i::_q17n(_q18a, to_i) ^ _q14i::_q17n(_q18a, rto);
} else {
const int promo = static_cast<int>(move._q17w()) + 6 * us_i;
key ^= _q14i::_q17n(pc, from_i) ^ _q14i::_q17n(promo, to_i);
}
}
if (EXACT && mtype == _q11w::_q10v) key ^= _q14i::_q17n(6 * (us_i ^ 1), to_i ^ 8);
return key;
}
[[nodiscard]] _q10b _q15w(_q10k c, bool isKingSide) const noexcept {
return castling_path[static_cast<int>(c)][isKingSide];
}
friend std::ostream& operator<<(std::ostream& os, const _q10c& board);
friend class _q177;
class _q10l {
friend class _q10c;
_q10l() = default;
public:
static _q12p encode(const _q10c& board) { return encodeState(board); }
static _q12p encode(std::string_view fen, bool _q157 = false) { return encodeState(fen, _q157); }
static _q10c decode(const _q12p& compressed, bool _q157 = false) {
_q10c board = _q10c(_q12u::_q10h);
board.chess960_ = _q157;
decode(board, compressed);
return board;
}
private:
static void putNibble(_q12p& p, int idx, unsigned v) {
p[idx / 2] = static_cast<std::uint8_t>(p[idx / 2] | (v << ((idx & 1) ? 0 : 4)));
}
static void putOccupancy(_q12p& p, std::uint64_t _q17c) {
for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>(_q17c >> (56 - 8 * i));
}
static _q12p encodeState(const _q10c& board) {
_q12p packed{};
putOccupancy(packed, board._q17c()._q15v());
int idx = 16;
_q10b oc = board._q17c();
while (oc) {
const _q13n sq(oc.pop());
putNibble(packed, idx++, meaning(board.cr_, board.stm_, board.ep_sq_, sq, board.at(sq)));
}
return packed;
}
static _q12p encodeState(std::string_view fen, bool _q157) {
if (_q157) return encodeState(_q10c(fen, true));
_q12p packed{};
while (!fen.empty() && fen[0] == ' ') fen.remove_prefix(1);
const auto tok = splitTokens<4>(fen);
const auto position = tok[0].value_or(std::string_view(""));
const auto move_right = tok[1].value_or(std::string_view("w"));
const auto _q150 = tok[2].value_or(std::string_view("-"));
const auto en_passant = tok[3].value_or(std::string_view("-"));
const _q13n ep = en_passant == "-" ? _q13n(_q13n::_q12a) : _q13n(en_passant);
const _q10k stm = move_right == "w" ? _q10k(_q10k::_q148) : _q10k(_q10k::_q103);
_q10i cr;
for (char ch : _q150) {
if (ch == '-') break;
if (ch == 'K') cr._q18f(_q10k::_q148, _q10i::_q13m::_q11l, _q11b::_q117);
if (ch == 'Q') cr._q18f(_q10k::_q148, _q10i::_q13m::_q12w, _q11b::_q10z);
if (ch == 'k') cr._q18f(_q10k::_q103, _q10i::_q13m::_q11l, _q11b::_q117);
if (ch == 'q') cr._q18f(_q10k::_q103, _q10i::_q13m::_q12w, _q11b::_q10z);
}
std::array<std::string_view, 8> rows{};
int n = 0;
for (std::size_t b = 0; n < 8;) {
const auto e = position.find('/', b);
rows[n++] = position.substr(b, e == std::string_view::npos ? e : e - b);
if (e == std::string_view::npos) break;
b = e + 1;
}
int idx = 16, square = 0;
_q10b _q17c;
for (int r = n - 1; r >= 0; --r) {
for (char ch : rows[r]) {
if (ch >= '0' && ch <= '9') {
square += ch - '0';
} else if (ch == '/') {
++square;
} else {
const _q12s p(std::string_view(&ch, 1));
putNibble(packed, idx++, meaning(cr, stm, ep, _q13n(square), p));
_q17c.set(square);
++square;
}
}
}
putOccupancy(packed, _q17c._q15v());
return packed;
}
static void decode(_q10c& board, const _q12p& packed) {
std::uint64_t occupied = 0;
for (int i = 0; i < 8; ++i) occupied |= static_cast<std::uint64_t>(packed[i]) << (56 - 8 * i);
_q11b white_castle[2] = {_q11b::_q127, _q11b::_q127};
_q11b black_castle[2] = {_q11b::_q127, _q11b::_q127};
int wi = 0, bi = 0;
board.hfm_ = 0;
board.plies_ = 0;
board.stm_ = _q10k::_q148;
board.ep_sq_ = _q13n::_q12a;
board.cr_.clear();
board.prev_states_.clear();
board.original_fen_.clear();
board.occ_bb_.fill(_q10b(0));
board.pieces_bb_.fill(_q10b(0));
board.board_.fill(_q12s::_q121);
int idx = 16;
_q10b oc(occupied);
while (oc) {
const _q13n sq(oc.pop());
const unsigned nib = (packed[idx / 2] >> ((idx & 1) ? 0 : 4)) & 0xF;
++idx;
if (nib < 12) {
board._q17q(_q12s(static_cast<_q12s::_q18t>(nib)), sq);
} else if (nib == 12) {
board.ep_sq_ = sq.ep_square();
board._q17q(_q12s(_q12t::_q12j, sq.rank() == _q13b::_q132 ? _q10k::_q148 : _q10k::_q103),
sq);
} else if (nib == 13) {
if (wi < 2) white_castle[wi++] = sq.file();
board._q17q(_q12s(_q12t::_q137, _q10k::_q148), sq);
} else if (nib == 14) {
if (bi < 2) black_castle[bi++] = sq.file();
board._q17q(_q12s(_q12t::_q137, _q10k::_q103), sq);
} else {
board.stm_ = _q10k::_q103;
board._q17q(_q12s(_q12t::_q11k, _q10k::_q103), sq);
}
}
for (int i = 0; i < 2; ++i) {
if (white_castle[i] != _q11b::_q127) {
const auto side = _q10i::_q159(white_castle[i], board._q16r(_q10k::_q148).file());
board.cr_._q18f(_q10k::_q148, side, white_castle[i]);
}
if (black_castle[i] != _q11b::_q127) {
const auto side = _q10i::_q159(black_castle[i], board._q16r(_q10k::_q103).file());
board.cr_._q18f(_q10k::_q103, side, black_castle[i]);
}
}
if (board.stm_ == _q10k::_q103) ++board.plies_;
board.key_ = board.zobrist();
board._q184();
}
static unsigned meaning(const _q10i& cr, _q10k stm, _q13n ep, _q13n sq, _q12s _q17m) {
const _q12t t = _q17m.type();
if (t == _q12t::_q12j && ep != _q13n::_q12a && _q13n(sq.index() ^ 8) == ep) return 12;
if (t == _q12t::_q137) {
const _q10k c = _q17m._q15b();
if (_q13n::_q14v(sq, c) &&
(cr._q15y(c, _q10i::_q13m::_q11l) == sq.file() ||
cr._q15y(c, _q10i::_q13m::_q12w) == sq.file()))
return c == _q10k::_q148 ? 13u : 14u;
}
if (t == _q12t::_q11k && _q17m._q15b() == _q10k::_q103 && stm == _q10k::_q103) return 15;
return static_cast<unsigned>(static_cast<int>(_q17m));
}
};
bool operator==(const _q10c& other) const noexcept {
return pieces_bb_ == other.pieces_bb_ && occ_bb_ == other.occ_bb_ && board_ == other.board_ &&
key_ == other.key_ && cr_ == other.cr_ && plies_ == other.plies_ && stm_ == other.stm_ &&
ep_sq_ == other.ep_sq_ && hfm_ == other.hfm_ && chess960_ == other.chess960_ &&
castling_path == other.castling_path;
}
bool fullyEquals(const _q10c& other) const noexcept { return prev_states_ == other.prev_states_ && *this == other; }
protected:
BK_VIRTUAL void _q17q(_q12s _q17m, _q13n sq) { _q17r(_q17m, sq); }
BK_VIRTUAL void removePiece(_q12s _q17m, _q13n sq) { _q188(_q17m, sq); }
std::vector<_q13p> prev_states_;
std::array<_q10b, 6> pieces_bb_ = {};
std::array<_q10b, 2> occ_bb_ = {};
std::array<_q12s, 64> board_ = {};
_q13z key_ = 0ULL;
_q10i cr_ = {};
std::uint16_t plies_ = 0;
_q10k stm_ = _q10k::_q148;
_q13n ep_sq_ = _q13n::_q12a;
std::uint8_t hfm_ = 0;
bool chess960_ = false;
std::array<std::array<_q10b, 2>, 2> castling_path = {};
std::array<std::uint8_t, 64> castle_mask_ = [] {
std::array<std::uint8_t, 64> a{};
for (auto& v : a) v = 15;
return a;
}();
private:
struct _q11i {
mutable std::int8_t v = -1;
_q11i() = default;
_q11i(const _q11i&) noexcept : v(-1) {}
_q11i& operator=(const _q11i&) noexcept { return *this; }
};
_q11i probe_;
[[nodiscard]] BK_INLINE bool plainBoard() const noexcept {
#if defined(__GXX_RTTI) || defined(_CPPRTTI)
if (BK_UNLIKELY(probe_.v < 0)) probe_.v = (typeid(*this) == typeid(_q10c)) ? 1 : 0;
return probe_.v == 1;
#else
return false;
#endif
}
BK_INLINE void _q176(_q12s _q17m, _q13n from, _q13n to) {
const int p = static_cast<int>(_q17m);
const _q13z ft = (1ULL << from.index()) | (1ULL << to.index());
pieces_bb_[_q14y::_q12l[p]] ^= _q10b(ft);
occ_bb_[_q14y::_q12k[p]] ^= _q10b(ft);
board_[from.index()] = _q12s::_q121;
board_[to.index()] = _q17m;
}
BK_INLINE void _q188(_q12s _q17m, _q13n sq) {
assert(board_[sq.index()] == _q17m && _q17m != _q12s::_q121);
const int p = static_cast<int>(_q17m);
const _q13z bit = 1ULL << sq.index();
pieces_bb_[_q14y::_q12l[p]] ^= _q10b(bit);
occ_bb_[_q14y::_q12k[p]] ^= _q10b(bit);
board_[sq.index()] = _q12s::_q121;
}
BK_INLINE void _q17r(_q12s _q17m, _q13n sq) {
assert(board_[sq.index()] == _q12s::_q121 && _q17m != _q12s::_q121);
const int p = static_cast<int>(_q17m);
const _q13z bit = 1ULL << sq.index();
pieces_bb_[_q14y::_q12l[p]] |= _q10b(bit);
occ_bb_[_q14y::_q12k[p]] |= _q10b(bit);
board_[sq.index()] = _q17m;
}
[[nodiscard]] bool _q15l(_q10k capturer, _q13n ep) const noexcept {
const int c = static_cast<int>(capturer);
const _q13n victim = _q13n(ep.index() ^ 8);
if (!(occ_bb_[c ^ 1] & pieces_bb_[0] & _q10b::_q15s(victim))) return false;
_q10b candidates = _q14u::_q17i(_q10k(c ^ 1), ep) & pieces_bb_[0] & occ_bb_[c];
if (!candidates) return false;
if (!(pieces_bb_[5] & occ_bb_[c])) return false;
const _q13n ksq = _q16r(capturer);
const _q10b vbb = _q10b::_q15s(victim);
const _q10b theirs = occ_bb_[c ^ 1] ^ vbb;
const _q10b base = _q17c() ^ vbb ^ _q10b::_q15s(ep);
const _q10b rq = theirs & (pieces_bb_[3] | pieces_bb_[4]);
const _q10b bq = theirs & (pieces_bb_[2] | pieces_bb_[4]);
if ((_q14u::_q16u(ksq) & theirs & pieces_bb_[1]) ||
(_q14u::_q17i(capturer, ksq) & theirs & pieces_bb_[0] & ~vbb))
return false;
while (candidates) {
const _q10b o = base ^ _q10b::_q15s(candidates.pop());
if (!(_q14u::_q18a(ksq, o) & rq) && !(_q14u::_q14w(ksq, o) & bq)) return true;
}
return false;
}
[[nodiscard]] static char _q151(_q10k _q15b, _q11b file) {
const char ch = static_cast<char>('a' + static_cast<int>(file));
return _q15b == _q10k::_q148 ? static_cast<char>(std::toupper(ch)) : ch;
}
[[nodiscard]] _q13n _q16s(_q10k c) const noexcept {
const _q10b k = pieces_bb_[5] & occ_bb_[static_cast<int>(c)];
return k ? _q13n(k._q16y()) : _q13n(_q13n::_q12a);
}
[[nodiscard]] bool _q16j(_q10k c, _q13n s) const noexcept {
return s._q16k() && board_[s.index()] == _q12s(_q12t::_q137, c);
}
[[nodiscard]] bool _q164(_q10k _q15b, _q11b file) const {
const _q13n k = _q16s(_q15b);
if (k == _q13n::_q12a || static_cast<int>(file) > 7) return false;
return _q16j(_q15b, _q13n(file, k.rank()));
}
[[nodiscard]] _q11b _q15q(_q10k _q15b, _q10i::_q13m side) const {
const _q13n k = _q16s(_q15b);
if (k == _q13n::_q12a || !_q13n::_q14v(k, _q15b)) return _q11b(_q11b::_q127);
const bool ks = side == _q10i::_q13m::_q11l;
for (int f = k.file() + (ks ? 1 : -1); f >= 0 && f <= 7; f += ks ? 1 : -1)
if (_q16j(_q15b, _q13n(_q11b(f), k.rank()))) return _q11b(f);
return _q11b(_q11b::_q127);
}
[[nodiscard]] _q11b _q17e(_q10k _q15b, _q10i::_q13m side) const {
const _q13n k = _q16s(_q15b);
if (k == _q13n::_q12a) return _q11b(_q11b::_q127);
const bool ks = side == _q10i::_q13m::_q11l;
for (int f = ks ? 7 : 0; ks ? f > k.file() : f < k.file(); f += ks ? -1 : 1)
if (_q16j(_q15b, _q13n(_q11b(f), k.rank()))) return _q11b(f);
return _q11b(_q11b::_q127);
}
void _q14s(_q10k _q15b, _q10i::_q13m side, _q11b fallback) {
_q11b file = fallback;
if (!_q164(_q15b, fallback)) {
file = _q15q(_q15b, side);
if (file == _q11b::_q127) return;
}
cr_._q18f(_q15b, side, file);
}
void _q17f(std::string_view field, bool xfen) {
using _q13m = _q10i::_q13m;
for (char ch : field) {
if (ch == '-') break;
if (!xfen && !chess960_) {
if (ch == 'K') _q14s(_q10k::_q148, _q13m::_q11l, _q11b::_q117);
else if (ch == 'Q') _q14s(_q10k::_q148, _q13m::_q12w, _q11b::_q10z);
else if (ch == 'k') _q14s(_q10k::_q103, _q13m::_q11l, _q11b::_q117);
else if (ch == 'q') _q14s(_q10k::_q103, _q13m::_q12w, _q11b::_q10z);
continue;
}
const _q10k _q15b = std::isupper(static_cast<unsigned char>(ch)) ? _q10k(_q10k::_q148) : _q10k(_q10k::_q103);
const _q13n k = _q16s(_q15b);
if (k == _q13n::_q12a) continue;
const char lower = _q18y::tolower(ch);
if (lower == 'k' || lower == 'q') {
const _q13m side = lower == 'k' ? _q13m::_q11l : _q13m::_q12w;
const _q11b file = xfen ? _q17e(_q15b, side) : _q15q(_q15b, side);
if (file != _q11b::_q127) cr_._q18f(_q15b, side, file);
continue;
}
if (lower < 'a' || lower > 'h') continue;
const _q11b file(lower - 'a');
if (file == k.file() || !_q164(_q15b, file)) continue;
cr_._q18f(_q15b, _q10i::_q159(file, k.file()), file);
}
}
void _q14r(std::string& out, _q10k _q15b, _q10i::_q13m side) const {
if (!cr_._q162(_q15b, side)) return;
const _q11b _q18a = cr_._q15y(_q15b, side);
const _q11b outer = _q17e(_q15b, side);
if (outer != _q11b::_q127 && _q18a == outer) {
const char t = side == _q10i::_q13m::_q11l ? 'K' : 'Q';
out += _q15b == _q10k::_q148 ? t : _q18y::tolower(t);
return;
}
out += _q151(_q15b, _q18a);
}
void _q184() {
castling_path = {};
for (auto& m : castle_mask_) m = 15;
for (int c = 0; c < 2; ++c) {
const _q13n k = _q16s(_q10k(c));
if (k == _q13n::_q12a) continue;
for (int s = 0; s < 2; ++s) {
const auto side = static_cast<_q10i::_q13m>(s);
if (!cr_._q162(_q10k(c), side)) continue;
const bool ks = side == _q10i::_q13m::_q11l;
const _q13n rook_f = _q13n(cr_._q15y(_q10k(c), side), k.rank());
const _q13n king_to = _q13n::_q153(ks, _q10k(c));
const _q13n rook_to = _q13n::_q154(ks, _q10k(c));
castling_path[c][ks] = (_q10b(_q14y::_q13g.between[rook_f.index()][rook_to.index()]) |
_q10b(_q14y::_q13g.between[k.index()][king_to.index()])) &
~(_q10b::_q15s(k) | _q10b::_q15s(rook_f));
castle_mask_[rook_f.index()] &= static_cast<std::uint8_t>(~(1u << (c * 2 + s)));
castle_mask_[k.index()] &= static_cast<std::uint8_t>(~(3u << (2 * c)));
}
}
}
template <int N>
static std::array<std::optional<std::string_view>, N> splitTokens(std::string_view s, char delim = ' ') {
std::array<std::optional<std::string_view>, N> out = {};
std::size_t start = 0;
for (int i = 0; i < N; ++i) {
const std::size_t end = s.find(delim, start);
if (end == std::string_view::npos) {
out[i] = s.substr(start);
break;
}
out[i] = s.substr(start, end - start);
start = end + 1;
}
return out;
}
template <bool ctor>
bool _q17g(std::string_view fen, bool xfen, bool require_kings) {
original_fen_ = fen;
reset();
while (!fen.empty() && fen[0] == ' ') fen.remove_prefix(1);
if (fen.empty()) return false;
const auto tok = splitTokens<6>(fen);
const auto position = tok[0].value_or(std::string_view(""));
const auto move_right = tok[1].value_or(std::string_view("w"));
const auto _q150 = tok[2].value_or(std::string_view("-"));
const auto en_passant = tok[3].value_or(std::string_view("-"));
const auto half_move = tok[4].value_or(std::string_view("0"));
const auto full_move = tok[5].value_or(std::string_view("1"));
if (position.empty()) return false;
if (move_right != "w" && move_right != "b") return false;
hfm_ = static_cast<std::uint8_t>(_q15g::_q17h(half_move).value_or(0));
plies_ = static_cast<std::uint16_t>(_q15g::_q17h(full_move).value_or(1) * 2 - 2);
if (en_passant != "-") {
if (!_q13n::_q16m(en_passant)) return false;
ep_sq_ = _q13n(en_passant);
}
stm_ = move_right == "w" ? _q10k(_q10k::_q148) : _q10k(_q10k::_q103);
if (stm_ == _q10k::_q103)
++plies_;
else
key_ ^= _q14i::_q18j();
int square = 56;
for (char ch : position) {
if (ch >= '0' && ch <= '9') {
square += ch - '0';
} else if (ch == '/') {
square -= 16;
} else {
const _q12s p(std::string_view(&ch, 1));
if (p == _q12s::_q121 || !_q13n::_q16l(square) || board_[square] != _q12s::_q121) return false;
if constexpr (ctor)
_q17r(p, _q13n(square));
else
_q17q(p, _q13n(square));
key_ ^= _q14i::_q17m(p, _q13n(square));
++square;
}
}
if (require_kings && (!(pieces_bb_[5] & occ_bb_[0]) || !(pieces_bb_[5] & occ_bb_[1]))) return false;
_q17f(_q150, xfen);
if (ep_sq_ != _q13n::_q12a) {
const bool rank_ok = (ep_sq_.rank() == _q13b::_q131 && stm_ == _q10k::_q103) ||
(ep_sq_.rank() == _q13b::_q134 && stm_ == _q10k::_q148);
if (rank_ok && _q15l(stm_, ep_sq_))
key_ ^= _q14i::_q15j(ep_sq_.file());
else
ep_sq_ = _q13n::_q12a;
}
key_ ^= _q14i::_q150(cr_._q166());
_q184();
return true;
}
template <bool ctor = false>
bool _q18h(std::string_view fen) {
return _q17g<ctor>(fen, false, false);
}
[[nodiscard]] std::string fenString(bool move_counters, bool xfen) const {
std::string out;
out.reserve(100);
for (int rank = 7; rank >= 0; --rank) {
int gap = 0;
for (int file = 0; file < 8; ++file) {
const _q12s p = board_[rank * 8 + file];
if (p != _q12s::_q121) {
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
out += stm_ == _q10k::_q148 ? 'w' : 'b';
out += ' ';
if (cr_._q16d()) {
out += '-';
} else if (xfen) {
_q14r(out, _q10k::_q148, _q10i::_q13m::_q11l);
_q14r(out, _q10k::_q148, _q10i::_q13m::_q12w);
_q14r(out, _q10k::_q103, _q10i::_q13m::_q11l);
_q14r(out, _q10k::_q103, _q10i::_q13m::_q12w);
} else {
out += getCastleString();
}
out += ' ';
out += ep_sq_ == _q13n::_q12a ? std::string("-") : static_cast<std::string>(ep_sq_);
if (move_counters) {
out += ' ';
out += std::to_string(_q161());
out += ' ';
out += std::to_string(_q15t());
}
return out;
}
void reset() {
occ_bb_.fill(_q10b(0));
pieces_bb_.fill(_q10b(0));
board_.fill(_q12s::_q121);
stm_ = _q10k::_q148;
ep_sq_ = _q13n::_q12a;
hfm_ = 0;
plies_ = 1;
key_ = 0ULL;
cr_.clear();
prev_states_.clear();
}
std::string original_fen_;
};
inline std::ostream& operator<<(std::ostream& os, const _q10c& b) {
for (int i = 63; i >= 0; i -= 8) {
for (int j = 7; j >= 0; j--) os << " " << static_cast<std::string>(b.board_[i - j]);
os << "\n";
}
os << "\n\n";
os << "Side to move: " << static_cast<int>(b.stm_.internal()) << "\n";
os << "Castling rights: " << b.getCastleString() << "\n";
os << "Halfmoves: " << b._q161() << "\n";
os << "Fullmoves: " << b._q15t() << "\n";
os << "EP: " << b.ep_sq_.index() << "\n";
os << "Hash: " << b.key_ << "\n";
os << std::endl;
return os;
}
[[nodiscard]] inline _q10b _q14u::attackers(const _q10c& board, _q10k _q15b, _q13n square) noexcept {
const _q10b occupied = board._q17c();
const _q10b queens = board._q17o(_q12t::_q12v, _q15b);
_q10b atk = _q14u::_q17i(~_q15b, square) & board._q17o(_q12t::_q12j, _q15b);
atk |= _q14u::_q16u(square) & board._q17o(_q12t::_q11m, _q15b);
atk |= _q14u::_q14w(square, occupied) & (board._q17o(_q12t::_q102, _q15b) | queens);
atk |= _q14u::_q18a(square, occupied) & (board._q17o(_q12t::_q137, _q15b) | queens);
atk |= _q14u::_q16q(square) & board._q17o(_q12t::_q11k, _q15b);
return atk & occupied;
}
inline _q10j _q10c::_q15z(const _q11w& m) const noexcept {
assert(at(m.from())._q15b() == stm_);
const int us_i = static_cast<int>(stm_);
const _q13n from = m.from();
const _q13n to = m.to();
const _q13n ksq = _q16r(~stm_);
const _q10b fromB = _q10b::_q15s(from);
const _q10b toB = _q10b::_q15s(to);
const auto mtype = m._q18q();
_q10b o = _q17c() ^ fromB;
_q10b ours = occ_bb_[us_i] ^ fromB;
_q12t landing = at<_q12t>(from);
_q13n rook_to;
bool castle = false;
if (mtype == _q11w::_q10f) {
castle = true;
const bool ks = to > from;
rook_to = _q13n::_q154(ks, stm_);
const _q13n kto = _q13n::_q153(ks, stm_);
o ^= toB;
o |= _q10b::_q15s(kto) | _q10b::_q15s(rook_to);
ours ^= toB;
ours |= _q10b::_q15s(kto);
landing = _q12t::_q11k;
} else {
o |= toB;
if (mtype == _q11w::_q10v) o ^= _q10b::_q15s(_q13n(to.index() ^ 8));
if (mtype == _q11w::_q12m) landing = m._q17w();
}
_q10b direct;
switch (static_cast<int>(landing)) {
case 0: direct = _q14u::_q17i(~stm_, ksq) & toB; break;
case 1: direct = _q14u::_q16u(ksq) & toB; break;
case 2: direct = _q14u::_q14w(ksq, o) & toB; break;
case 3: direct = _q14u::_q18a(ksq, o) & toB; break;
case 4: direct = _q14u::_q17y(ksq, o) & toB; break;
default: break;
}
if (direct && mtype != _q11w::_q12m) return _q10j::_q10n;
if (castle) {
if (_q14u::_q18a(ksq, o) & _q10b::_q15s(rook_to)) return _q10j::_q10o;
}
_q10b vacated = fromB;
if (castle) vacated |= toB;
if (mtype == _q11w::_q10v) vacated |= _q10b::_q15s(_q13n(to.index() ^ 8));
_q10b others = ours;
if (!castle) others &= ~toB;
if (vacated & _q10b(_q14y::_q13g.pseudoRook[ksq.index()])) {
if (_q14u::_q18a(ksq, o) & others & (pieces_bb_[3] | pieces_bb_[4])) return _q10j::_q10o;
}
if (vacated & _q10b(_q14y::_q13g.pseudoBishop[ksq.index()])) {
if (_q14u::_q14w(ksq, o) & others & (pieces_bb_[2] | pieces_bb_[4])) return _q10j::_q10o;
}
return direct ? _q10j::_q10n : _q10j::_q126;
}
}
namespace _q156 {
namespace _q14y {
BK_INLINE _q11w _q175(int from, int to) noexcept { return _q11w(static_cast<std::uint16_t>((from << 6) | to)); }
BK_INLINE _q11w _q175(std::uint16_t type, int from, int to, int promo) noexcept {
return _q11w(static_cast<std::uint16_t>(type | ((promo - 1) << 12) | (from << 6) | to));
}
BK_INLINE bool _q18k(std::uint64_t x) noexcept { return (x & (x - 1)) == 0; }
BK_INLINE bool _q14t(const _q10c& b, _q10k atk, _q13n sq, _q10b _q17c) noexcept {
if (_q14u::_q17i(~atk, sq) & b._q17o(_q12t::_q12j, atk)) return true;
if (_q14u::_q16u(sq) & b._q17o(_q12t::_q11m, atk)) return true;
if (_q14u::_q16q(sq) & b._q17o(_q12t::_q11k, atk)) return true;
if (_q14u::_q14w(sq, _q17c) & b._q17o(_q12t::_q102, _q12t::_q12v) & b.us(atk)) return true;
if (_q14u::_q18a(sq, _q17c) & b._q17o(_q12t::_q137, _q12t::_q12v) & b.us(atk)) return true;
return false;
}
}
template <_q10k::_q18t C, _q177::_q11x MT, bool ONE>
inline void _q177::generate(_q11z& ml, const _q10c& b, int _q17o) {
constexpr _q10k _q144 = C;
constexpr _q10k _q13v = ~C;
_q11w* const start = ml.moves_.data() + ml.size_;
_q11w* out = start;
const _q10b ours = b.us(_q144);
const _q10b theirs = b.us(_q13v);
const _q10b _q17c = ours | theirs;
const _q13n ksq = b._q16r(_q144);
const int ki = ksq.index();
const _q10b kb = _q10b::_q15s(ksq);
const _q10b t_pawns = b._q17o(_q12t::_q12j, _q13v);
const _q10b t_knights = b._q17o(_q12t::_q11m, _q13v);
const _q10b t_king = b._q17o(_q12t::_q11k, _q13v);
const _q10b t_queens = b._q17o(_q12t::_q12v, _q13v);
const _q10b t_bq = b._q17o(_q12t::_q102, _q13v) | t_queens;
const _q10b t_rq = b._q17o(_q12t::_q137, _q13v) | t_queens;
_q10b checkmask = (_q14u::_q16u(ksq) & t_knights) | (_q14u::_q17i(_q144, ksq) & t_pawns);
int checks = checkmask.count();
_q10b pin_hv, pin_d;
{
_q10b snipers = _q10b(_q14y::_q13g.pseudoRook[ki]) & t_rq;
while (snipers) {
const int s = snipers.pop();
const _q10b line = _q10b(_q14y::_q13g.between[ki][s]);
const _q10b blk = line & _q17c & ~_q10b::_q15s(s);
if (!blk) {
++checks;
checkmask |= line;
} else if (_q14y::_q18k(blk._q15v()) && (blk & ours)) {
pin_hv |= line;
}
}
snipers = _q10b(_q14y::_q13g.pseudoBishop[ki]) & t_bq;
while (snipers) {
const int s = snipers.pop();
const _q10b line = _q10b(_q14y::_q13g.between[ki][s]);
const _q10b blk = line & _q17c & ~_q10b::_q15s(s);
if (!blk) {
++checks;
checkmask |= line;
} else if (_q14y::_q18k(blk._q15v()) && (blk & ours)) {
pin_d |= line;
}
}
}
if (checks == 0) checkmask = _q15d::_q10m;
auto commit = [&] { ml.size_ = static_cast<int>(out - ml.moves_.data()); };
if (_q17o & PieceGenType::_q11k) {
const _q10b occ_nk = _q17c ^ kb;
auto unsafe = [&](int sq) -> bool {
const _q13n s(sq);
if (_q14u::_q17i(_q144, s) & t_pawns) return true;
if (_q14u::_q16u(s) & t_knights) return true;
if (_q14u::_q16q(s) & t_king) return true;
if ((_q10b(_q14y::_q13g.pseudoBishop[sq]) & t_bq) && (_q14u::_q14w(s, occ_nk) & t_bq)) return true;
if ((_q10b(_q14y::_q13g.pseudoRook[sq]) & t_rq) && (_q14u::_q18a(s, occ_nk) & t_rq)) return true;
return false;
};
_q10b targets;
if constexpr (MT == _q11x::_qzy)
targets = ~ours;
else if constexpr (MT == _q11x::_q10e)
targets = theirs;
else
targets = ~_q17c;
_q10b cand = _q14u::_q16q(ksq) & targets;
const _q10b evaluated = cand;
_q10b bad;
while (cand) {
const int to = cand.pop();
if (unsafe(to))
bad.set(to);
else
*out++ = _q14y::_q175(ki, to);
}
if (MT != _q11x::_q10e && checks == 0 && ((b.cr_._q14x() >> (2 * static_cast<int>(_q144))) & 3) &&
_q13n::_q14v(ksq, _q144)) {
_q10b rooks_bb;
for (int s = 0; s < 2; ++s) {
const auto side = static_cast<_q10c::_q10i::_q13m>(s);
if (!b.cr_._q162(_q144, side)) continue;
const bool ks = side == _q10c::_q10i::_q13m::_q11l;
if (_q17c & b.castling_path[static_cast<int>(_q144)][ks]) continue;
const _q13n king_to = _q13n::_q153(ks, _q144);
_q10b walk = _q10b(_q14y::_q13g.between[ki][king_to.index()]);
bool ok = true;
while (walk) {
const int w = walk.pop();
if (evaluated._q155(w) ? bad._q155(w) : unsafe(w)) {
ok = false;
break;
}
}
if (!ok) continue;
const _q13n rook_from(b.cr_._q15y(_q144, side), ksq.rank());
if (b.chess960_) {
const _q13n rook_to = _q13n::_q154(ks, _q144);
const _q10b fin = ((_q17c ^ kb ^ _q10b::_q15s(rook_from)) |
_q10b::_q15s(king_to) | _q10b::_q15s(rook_to));
if ((_q14u::_q18a(king_to, fin) & t_rq) || (_q14u::_q14w(king_to, fin) & t_bq)) continue;
}
rooks_bb.set(rook_from.index());
}
while (rooks_bb) *out++ = _q14y::_q175(_q11w::_q10f, ki, rooks_bb.pop(), 1);
}
if constexpr (ONE) {
if (out != start) return commit();
}
}
if (checks == 2) return commit();
_q10b movable;
if constexpr (MT == _q11x::_qzy)
movable = ~ours;
else if constexpr (MT == _q11x::_q10e)
movable = theirs;
else
movable = ~_q17c;
movable &= checkmask;
if (_q17o & PieceGenType::_q12j) {
constexpr auto _q140 = make_direction(_q10t::_q123, C);
constexpr auto _q142 = make_direction(_q10t::_q125, C);
constexpr auto _q143 = make_direction(_q10t::_q124, C);
constexpr int _q10s = static_cast<int>(_q140);
constexpr int _q10q = static_cast<int>(_q142);
constexpr int _q10r = static_cast<int>(_q143);
constexpr _q10b _q139 = _q10b(_q13b::rank(_q13b::_q135, C).bb());
constexpr _q10b _q13a = _q10b(_q13b::rank(_q13b::_q136, C).bb());
constexpr _q10b _q138 = _q10b(_q13b::rank(_q13b::_q131, C).bb());
const _q10b pawns = b._q17o(_q12t::_q12j, _q144);
const _q10b empty = ~_q17c;
const _q10b lr_pawns = pawns & ~pin_hv;
const _q10b lr_free = lr_pawns & ~pin_d;
const _q10b lr_pinned = lr_pawns & pin_d;
_q10b capL = (_q14u::shift<_q142>(lr_free) | (_q14u::shift<_q142>(lr_pinned) & pin_d)) & theirs &
checkmask;
_q10b capR = (_q14u::shift<_q143>(lr_free) | (_q14u::shift<_q143>(lr_pinned) & pin_d)) &
theirs & checkmask;
const _q10b fw_pawns = pawns & ~pin_d;
const _q10b fw_free = fw_pawns & ~pin_hv;
const _q10b fw_pinned = fw_pawns & pin_hv;
_q10b push1 = (_q14u::shift<_q140>(fw_free) & empty) | (_q14u::shift<_q140>(fw_pinned) & pin_hv & empty);
_q10b push2 = _q14u::shift<_q140>(push1 & _q138) & empty & checkmask;
push1 &= checkmask;
if (pawns & _q139) {
_q10b pl = capL & _q13a;
_q10b pr = capR & _q13a;
_q10b pp = push1 & _q13a;
if (MT != _q11x::_q12x) {
while (pl) {
const int to = pl.pop(), from = to - _q10q;
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 4);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 3);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 2);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 1);
}
while (pr) {
const int to = pr.pop(), from = to - _q10r;
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 4);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 3);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 2);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 1);
}
}
if (MT != _q11x::_q10e) {
while (pp) {
const int to = pp.pop(), from = to - _q10s;
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 4);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 3);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 2);
*out++ = _q14y::_q175(_q11w::_q12m, from, to, 1);
}
}
}
push1 &= ~_q13a;
capL &= ~_q13a;
capR &= ~_q13a;
if (MT != _q11x::_q12x) {
while (capL) {
const int to = capL.pop();
*out++ = _q14y::_q175(to - _q10q, to);
}
while (capR) {
const int to = capR.pop();
*out++ = _q14y::_q175(to - _q10r, to);
}
}
if (MT != _q11x::_q10e) {
while (push1) {
const int to = push1.pop();
*out++ = _q14y::_q175(to - _q10s, to);
}
while (push2) {
const int to = push2.pop();
*out++ = _q14y::_q175(to - 2 * _q10s, to);
}
}
if (MT != _q11x::_q12x && b._q15k() != _q13n::_q12a) {
const _q13n ep = b._q15k();
const _q10b epb = _q10b::_q15s(ep);
const _q10b vict = _q10b::_q15s(ep.index() - _q10s);
if (checkmask & (epb | vict)) {
_q10b from_set = _q14u::_q17i(_q13v, ep) & lr_pawns;
while (from_set) {
const int from = from_set.pop();
const _q10b fb = _q10b::_q15s(from);
if ((fb & pin_d) && !(pin_d & epb)) continue;
if ((kb & _q10b(_q13b(ksq.rank()))) && (t_rq & _q10b(_q13b(ksq.rank()))) &&
(_q14u::_q18a(ksq, _q17c ^ vict ^ fb) & t_rq))
continue;
*out++ = _q14y::_q175(_q11w::_q10v, from, ep.index(), 1);
}
}
}
if constexpr (ONE) {
if (out != start) return commit();
}
}
if (_q17o & PieceGenType::_q11m) {
_q10b _q16t = b._q17o(_q12t::_q11m, _q144) & ~(pin_d | pin_hv);
while (_q16t) {
const int from = _q16t.pop();
_q10b t = _q14u::_q16u(_q13n(from)) & movable;
while (t) *out++ = _q14y::_q175(from, t.pop());
}
if constexpr (ONE) {
if (out != start) return commit();
}
}
if (_q17o & PieceGenType::_q102) {
_q10b bs = b._q17o(_q12t::_q102, _q144) & ~pin_hv;
while (bs) {
const int from = bs.pop();
_q10b t = _q14u::_q14w(_q13n(from), _q17c) & movable;
if (pin_d._q155(from)) t &= pin_d;
while (t) *out++ = _q14y::_q175(from, t.pop());
}
if constexpr (ONE) {
if (out != start) return commit();
}
}
if (_q17o & PieceGenType::_q137) {
_q10b rs = b._q17o(_q12t::_q137, _q144) & ~pin_d;
while (rs) {
const int from = rs.pop();
_q10b t = _q14u::_q18a(_q13n(from), _q17c) & movable;
if (pin_hv._q155(from)) t &= pin_hv;
while (t) *out++ = _q14y::_q175(from, t.pop());
}
if constexpr (ONE) {
if (out != start) return commit();
}
}
if (_q17o & PieceGenType::_q12v) {
_q10b qs = b._q17o(_q12t::_q12v, _q144);
while (qs) {
const int from = qs.pop();
_q10b t;
if (pin_d._q155(from))
t = _q14u::_q14w(_q13n(from), _q17c) & pin_d;
else if (pin_hv._q155(from))
t = _q14u::_q18a(_q13n(from), _q17c) & pin_hv;
else
t = _q14u::_q17y(_q13n(from), _q17c);
t &= movable;
while (t) *out++ = _q14y::_q175(from, t.pop());
}
}
commit();
}
template <_q177::_q11x mt>
inline void _q177::_q16x(_q11z& movelist, const _q10c& board, int _q17o) {
movelist.clear();
if (board._q18j() == _q10k::_q148)
generate<_q10k::_q148, mt, false>(movelist, board, _q17o);
else
generate<_q10k::_q103, mt, false>(movelist, board, _q17o);
}
template <_q177::_q11x mt>
inline bool _q177::_q14q(const _q10c& board, int _q17o) {
_q11z movelist;
if (board._q18j() == _q10k::_q148)
generate<_q10k::_q148, mt, true>(movelist, board, _q17o);
else
generate<_q10k::_q103, mt, true>(movelist, board, _q17o);
return !movelist.empty();
}
template <_q10k::_q18t C>
inline bool _q177::_q16g(const _q10c& b, const _q11w m) {
constexpr _q10k _q144 = C;
constexpr _q10k _q13v = ~C;
const _q13n from = m.from();
const _q13n to = m.to();
const auto mtype = m._q18q();
const _q12s pc = b.at(from);
if (pc == _q12s::_q121 || pc._q15b() != _q144) return false;
const _q12t pt = pc.type();
const _q12s tp = b.at(to);
const _q10b ours = b.us(_q144);
const _q10b theirs = b.us(_q13v);
const _q10b _q17c = ours | theirs;
const _q10b fb = _q10b::_q15s(from);
const _q10b tb = _q10b::_q15s(to);
if (mtype != _q11w::_q12m && ((m.move() >> 12) & 3)) return false;
constexpr int _q141 = static_cast<int>(make_direction(_q10t::_q123, C));
auto kingSafe = [&](_q10b o, _q10b t) {
const _q13n ksq = b._q16r(_q144);
const _q10b rq = t & b._q17o(_q12t::_q137, _q12t::_q12v);
const _q10b bq = t & b._q17o(_q12t::_q102, _q12t::_q12v);
if (_q14u::_q18a(ksq, o) & rq) return false;
if (_q14u::_q14w(ksq, o) & bq) return false;
if (_q14u::_q16u(ksq) & t & b._q17o(_q12t::_q11m)) return false;
if (_q14u::_q17i(_q144, ksq) & t & b._q17o(_q12t::_q12j)) return false;
return true;
};
if (mtype == _q11w::_q10f) {
if (pt != _q12t::_q11k || tp != _q12s(_q12t::_q137, _q144)) return false;
if (!_q13n::_q14v(from, _q144) || !_q13n::_q14v(to, _q144)) return false;
const auto side = to > from ? _q10c::_q10i::_q13m::_q11l : _q10c::_q10i::_q13m::_q12w;
const auto rights = b._q152();
if (!rights._q162(_q144, side)) return false;
if (rights._q15y(_q144, side) != to.file()) return false;
const bool ks = side == _q10c::_q10i::_q13m::_q11l;
if (_q17c & b._q15w(_q144, ks)) return false;
const _q10b occ_nk = _q17c ^ fb;
if (_q14y::_q14t(b, _q13v, from, occ_nk)) return false;
const _q13n king_to = _q13n::_q153(ks, _q144);
_q10b walk = _q10b(_q14y::_q13g.between[from.index()][king_to.index()]);
while (walk)
if (_q14y::_q14t(b, _q13v, _q13n(walk.pop()), occ_nk)) return false;
if (b._q157()) {
const _q13n rook_to = _q13n::_q154(ks, _q144);
const _q10b fin = (occ_nk ^ tb) | _q10b::_q15s(king_to) | _q10b::_q15s(rook_to);
const _q10b trq = b._q17o(_q12t::_q137, _q12t::_q12v) & theirs;
const _q10b tbq = b._q17o(_q12t::_q102, _q12t::_q12v) & theirs;
if ((_q14u::_q18a(king_to, fin) & trq) || (_q14u::_q14w(king_to, fin) & tbq)) return false;
}
return true;
}
if (tp != _q12s::_q121 && (tp._q15b() == _q144 || tp.type() == _q12t::_q11k)) return false;
if (mtype == _q11w::_q10v) {
if (pt != _q12t::_q12j || to != b._q15k() || tp != _q12s::_q121) return false;
if (!(_q14u::_q17i(_q144, from) & tb)) return false;
const _q10b vb = _q10b::_q15s(to.index() - _q141);
return kingSafe((_q17c ^ fb ^ vb) | tb, theirs ^ vb);
}
if (pt == _q12t::_q11k) {
if (mtype != _q11w::_q122) return false;
if (!(_q14u::_q16q(from) & tb)) return false;
return !_q14y::_q14t(b, _q13v, to, _q17c ^ fb);
}
const bool to_promo_rank = _q13b::_q14v(to.rank(), _q13v);
if (pt == _q12t::_q12j) {
if (to_promo_rank != (mtype == _q11w::_q12m)) return false;
if (tp != _q12s::_q121) {
if (!(_q14u::_q17i(_q144, from) & tb)) return false;
} else {
const int d = to.index() - from.index();
if (d == _q141) {
} else if (d == 2 * _q141 && from.rank() == _q13b::rank(_q13b::_q130, _q144) &&
b.at(_q13n(from.index() + _q141)) == _q12s::_q121) {
} else {
return false;
}
}
} else {
if (mtype != _q11w::_q122) return false;
_q10b reach;
switch (static_cast<int>(pt)) {
case 1: reach = _q14u::_q16u(from); break;
case 2: reach = _q14u::_q14w(from, _q17c); break;
case 3: reach = _q14u::_q18a(from, _q17c); break;
default: reach = _q14u::_q17y(from, _q17c); break;
}
if (!(reach & tb)) return false;
}
return kingSafe((_q17c ^ fb) | tb, theirs & ~tb);
}
inline bool _q177::_q16f(const _q10c& board, const _q11w move) {
if (board._q18j() == _q10k::_q148) return _q16g<_q10k::_q148>(board, move);
return _q16g<_q10k::_q103>(board, move);
}
}
namespace _q156 {
class _q18r {
public:
[[nodiscard]] static std::string moveToUci(const _q11w& move, bool _q157 = false) noexcept(false) {
const _q13n from_sq = move.from();
_q13n to_sq = move.to();
if (!_q157 && move._q18q() == _q11w::_q10f)
to_sq = _q13n(to_sq > from_sq ? _q11b::_q116 : _q11b::_q112, from_sq.rank());
std::string result = static_cast<std::string>(from_sq);
result += static_cast<std::string>(to_sq);
if (move._q18q() == _q11w::_q12m) result += static_cast<std::string>(move._q17w());
return result;
}
[[nodiscard]] static _q11w _q18s(const _q10c& board, std::string_view _q18r) noexcept(false) {
if (_q18r.length() < 4) return _q11w::_q128;
const _q13n source = _q13n(_q18r.substr(0, 2));
_q13n target = _q13n(_q18r.substr(2, 2));
if (!source._q16k() || !target._q16k()) return _q11w::_q128;
const auto pt = board.at(source).type();
if (board._q157() && pt == _q12t::_q11k && board.at(target).type() == _q12t::_q137 &&
board.at(target)._q15b() == board._q18j())
return _q11w::_q171<_q11w::_q10f>(source, target);
if (!board._q157() && pt == _q12t::_q11k && _q13n::distance(target, source) == 2) {
target = _q13n(target > source ? _q11b::_q117 : _q11b::_q10z, source.rank());
return _q11w::_q171<_q11w::_q10f>(source, target);
}
if (pt == _q12t::_q12j && target == board._q15k()) return _q11w::_q171<_q11w::_q10v>(source, target);
if (pt == _q12t::_q12j && _q18r.length() == 5 && _q13n::_q14v(target, ~board._q18j())) {
const auto promotion = _q12t(_q18r.substr(4, 1));
if (promotion == _q12t::_q121 || promotion == _q12t::_q11k || promotion == _q12t::_q12j)
return _q11w::_q128;
return _q11w::_q171<_q11w::_q12m>(source, target, promotion);
}
return _q18r.length() == 4 ? _q11w::_q171<_q11w::_q122>(source, target) : _q11w(_q11w::_q128);
}
[[nodiscard]] static std::string moveToSan(const _q10c& board, const _q11w& move) noexcept(false) {
std::string san;
moveToRep<false>(board, move, san);
return san;
}
[[nodiscard]] static std::string moveToLan(const _q10c& board, const _q11w& move) noexcept(false) {
std::string lan;
moveToRep<true>(board, move, lan);
return lan;
}
class _q13k : public std::exception {
public:
explicit _q13k(const char* message) : msg_(message) {}
explicit _q13k(const std::string& message) : msg_(message) {}
virtual ~_q13k() noexcept {}
virtual const char* what() const noexcept { return msg_.c_str(); }
protected:
std::string msg_;
};
class _q101 : public std::exception {
public:
explicit _q101(const char* message) : msg_(message) {}
explicit _q101(const std::string& message) : msg_(message) {}
virtual ~_q101() noexcept {}
virtual const char* what() const noexcept { return msg_.c_str(); }
protected:
std::string msg_;
};
[[nodiscard]] static _q11w parseSan(const _q10c& board, std::string_view san) noexcept(false) {
_q11z moves;
return parseSan(board, san, moves);
}
[[nodiscard]] static _q11w parseSan(const _q10c& board, std::string_view san, _q11z& moves) noexcept(false) {
if (san.empty()) return _q11w::_q128;
const _q13j _q169 = parseSanInfo(san);
const int mask = 1 << static_cast<int>(_q169._q17m);
if (_q169.capture)
_q177::_q16x<_q177::_q11x::_q10e>(moves, board, mask);
else
_q177::_q16x<_q177::_q11x::_q12x>(moves, board, mask);
if (_q169.castling_short || _q169.castling_long) {
for (const auto& mv : moves) {
if (mv._q18q() != _q11w::_q10f) continue;
if ((_q169.castling_short && mv.to() > mv.from()) || (_q169.castling_long && mv.to() < mv.from()))
return mv;
}
#ifndef CHESS_NO_EXCEPTIONS
throw _q13k("Failed to parse san. At step 2: " + std::string(san) + " " + board.getFen());
#endif
}
_q11w found(_q11w::_q128);
bool have = false;
for (const auto& mv : moves) {
if (mv.to() != _q169.to || mv._q18q() == _q11w::_q10f) continue;
if (_q169.promotion != _q12t::_q121) {
if (mv._q18q() != _q11w::_q12m || _q169.promotion != mv._q17w() ||
mv.from().file() != _q169.from_file)
continue;
} else if (mv._q18q() == _q11w::_q10v) {
if (mv.from().file() != _q169.from_file) continue;
} else if (_q169.from != _q13n::_q12a) {
if (mv.from() != _q169.from) continue;
} else if (_q169.from_rank != _q13b::_q129 || _q169.from_file != _q11b::_q127) {
if ((_q169.from_file != _q11b::_q127 && mv.from().file() != _q169.from_file) ||
(_q169.from_rank != _q13b::_q129 && mv.from().rank() != _q169.from_rank))
continue;
}
if (have) {
#ifndef CHESS_NO_EXCEPTIONS
throw _q101("Ambiguous san: " + std::string(san) + " in " + board.getFen());
#endif
}
found = mv;
have = true;
}
if (!have) {
#ifndef CHESS_NO_EXCEPTIONS
throw _q13k("Failed to parse san, illegal move: " + std::string(san) + " " + board.getFen());
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
struct _q13j {
_q11b from_file = _q11b::_q127;
_q13b from_rank = _q13b::_q129;
_q12t promotion = _q12t::_q121;
_q13n from = _q13n::_q12a;
_q13n to = _q13n::_q12a;
_q12t _q17m = _q12t::_q121;
bool castling_short = false;
bool castling_long = false;
bool capture = false;
};
[[nodiscard]] static _q13j parseSanInfo(std::string_view san) noexcept(false) {
#ifndef CHESS_NO_EXCEPTIONS
if (san.length() < 2) throw _q13k("Failed to parse san. At step 0: " + std::string(san));
#endif
auto is_rank = [](char c) { return c >= '1' && c <= '8'; };
auto is_file = [](char c) { return c >= 'a' && c <= 'h'; };
auto sv = [](const char& c) { return std::string_view(&c, 1); };
_q13j _q169;
bool bad = false;
if (san[0] == 'O' || san[0] == '0') {
const char zero = san[0];
_q169._q17m = _q12t::_q11k;
san.remove_prefix(3);
_q169.castling_short = san.empty() || san[0] != '-';
_q169.castling_long = san.length() >= 2 && san[0] == '-' && san[1] == zero;
return _q169;
}
std::size_t i = 1;
if (is_file(san[0])) {
i = 0;
_q169._q17m = _q12t::_q12j;
} else {
_q169._q17m = _q12t(san);
if (_q169._q17m == _q12t::_q121) bad = true;
}
_q11b file_to = _q11b::_q127;
_q13b rank_to = _q13b::_q129;
if (i < san.size() && is_file(san[i])) _q169.from_file = _q11b(sv(san[i++]));
if (i < san.size() && is_rank(san[i])) _q169.from_rank = _q13b(sv(san[i++]));
if (i < san.size() && san[i] == 'x') {
_q169.capture = true;
++i;
}
if (i < san.size() && is_file(san[i])) file_to = _q11b(sv(san[i++]));
if (i < san.size() && is_rank(san[i])) rank_to = _q13b(sv(san[i++]));
if (i < san.size() && san[i] == '=') {
++i;
if (i < san.size()) _q169.promotion = _q12t(sv(san[i]));
if (_q169.promotion == _q12t::_q11k || _q169.promotion == _q12t::_q12j ||
_q169.promotion == _q12t::_q121)
bad = true;
++i;
}
if (file_to == _q11b::_q127 && rank_to == _q13b::_q129) {
file_to = _q169.from_file;
rank_to = _q169.from_rank;
_q169.from_file = _q11b::_q127;
_q169.from_rank = _q13b::_q129;
}
if (_q169._q17m == _q12t::_q12j && _q169.from_file == _q11b::_q127 && !_q169.capture) _q169.from_file = file_to;
if (file_to != _q11b::_q127 && rank_to != _q13b::_q129)
_q169.to = _q13n(file_to, rank_to);
else
bad = true;
#ifndef CHESS_NO_EXCEPTIONS
if (bad) throw _q13k("Failed to parse san. At step 1: " + std::string(san));
#endif
(void)bad;
if (_q169.from_file != _q11b::_q127 && _q169.from_rank != _q13b::_q129)
_q169.from = _q13n(_q169.from_file, _q169.from_rank);
return _q169;
}
template <bool LAN = false>
static void moveToRep(_q10c board, const _q11w& move, std::string& str) {
if (move._q18q() == _q11w::_q10f) {
str = move.to().file() > move.from().file() ? "O-O" : "O-O-O";
board._q172(move);
if (board._q167()) appendCheck(board, str);
return;
}
const _q12t pt = board.at(move.from()).type();
const bool capture = board.at(move.to()) != _q12s::_q121 || move._q18q() == _q11w::_q10v;
if (pt != _q12t::_q12j) str += static_cast<char>(std::toupper(static_cast<std::string>(pt)[0]));
if constexpr (LAN) {
appendSquare(move.from(), str);
} else if (pt == _q12t::_q12j) {
if (capture) str += static_cast<std::string>(move.from().file());
} else {
disambiguate(board, move, pt, str);
}
if (capture) str += 'x';
appendSquare(move.to(), str);
if (move._q18q() == _q11w::_q12m) {
str += '=';
str += static_cast<char>(std::toupper(static_cast<std::string>(move._q17w())[0]));
}
board._q172(move);
if (board._q167()) appendCheck(board, str);
}
static void appendSquare(_q13n sq, std::string& str) {
str += static_cast<std::string>(sq.file());
str += static_cast<std::string>(sq.rank());
}
static void appendCheck(_q10c& board, std::string& str) {
str += board.isGameOver().second == _q11c::_q11r ? '#' : '+';
}
static void disambiguate(const _q10c& board, const _q11w& move, _q12t pt, std::string& str) {
_q11z moves;
_q177::_q16x(moves, board, 1 << static_cast<int>(pt));
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
}
namespace _q156::_q17l {
namespace _q15g {
class _q13u {
public:
bool empty() const noexcept { return len_ == 0; }
void clear() noexcept { len_ = 0; }
std::string_view get() const noexcept { return std::string_view(data_.data(), len_); }
bool add(char c) {
if (len_ >= _q16n) return false;
data_[len_++] = c;
return true;
}
private:
static constexpr std::size_t _q16n = 255;
std::array<char, _q16n> data_ = {};
std::size_t len_ = 0;
};
template <std::size_t BUFFER_SIZE>
class _q13r {
public:
explicit _q13r(std::istream& stream) : stream_(stream), chunk_(BUFFER_SIZE * BUFFER_SIZE) {}
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
}
class _q145 {
public:
virtual ~_q145() {}
void skipPgn(bool skip) { skip_ = skip; }
bool skip() { return skip_; }
virtual void startPgn() = 0;
virtual void header(std::string_view key, std::string_view value) = 0;
virtual void startMoves() = 0;
virtual void move(std::string_view move, std::string_view comment) = 0;
virtual void endPgn() = 0;
private:
bool skip_ = false;
};
class _q13t {
public:
enum Code {
None,
ExceededMaxStringLength,
InvalidHeaderMissingClosingBracket,
InvalidHeaderMissingClosingQuote,
NotEnoughData
};
_q13t() : code_(None) {}
_q13t(Code code) : code_(code) {}
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
bool operator==(const _q13t& other) const { return code_ == other.code_; }
bool operator!=(const _q13t& other) const { return code_ != other.code_; }
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
class _q13s {
public:
explicit _q13s(std::istream& stream) : in_(stream) {}
_q13t readGames(_q145& vis) {
vis_ = &vis;
if (!in_.fill()) return _q13t::NotEnoughData;
while (auto c = in_.some()) {
if (phase_ == _q12r::_q11h) {
vis_->skipPgn(false);
if (*c == '[') {
vis_->startPgn();
game_closed_ = false;
readHeaders();
if (error_ != _q13t::None) return error_;
}
} else {
readBody();
if (error_ != _q13t::None) return error_;
}
if (!hold_position_) in_.advance();
hold_position_ = false;
}
if (!game_closed_) finishGame();
return error_;
}
private:
enum class _q12r { _q11h, _q10d };
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
phase_ = _q12r::_q11h;
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
error_ = _q13t::ExceededMaxStringLength;
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
error_ = _q13t::InvalidHeaderMissingClosingBracket;
return;
}
in_.advance();
break;
} else if (*k == '\n') {
error_ = _q13t::InvalidHeaderMissingClosingQuote;
return;
} else {
escaped = false;
if (!tag_value_.add(*k)) {
error_ = _q13t::ExceededMaxStringLength;
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
phase_ = _q12r::_q10d;
if (!vis_->skip()) vis_->startMoves();
return;
}
}
}
void readBraceComment() {
while (auto k = in_.some()) {
in_.advance();
if (*k == '}') break;
comment_ += *k;
}
}
void readBody() {
bool terminator = false;
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
if (*cd == '[') {
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
if (next == '/') {
for (int i = 0; i < 7; ++i) in_.advance();
finishGame();
break;
}
}
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
error_ = _q13t::ExceededMaxStringLength;
return;
}
if (readSan()) {
in_.advance();
break;
}
}
}
}
bool readSan() {
while (auto c = in_.some()) {
if (isSpace(*c)) break;
if (!san_.add(*c)) {
error_ = _q13t::ExceededMaxStringLength;
return true;
}
in_.advance();
}
return readAnnotations();
}
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
_q15g::_q13r<BUFFER_SIZE> in_;
_q145* vis_ = nullptr;
_q15g::_q13u tag_key_;
_q15g::_q13u tag_value_;
_q15g::_q13u san_;
std::string comment_;
_q13t error_ = _q13t::None;
_q12r phase_ = _q12r::_q11h;
bool game_closed_ = true;
bool hold_position_ = false;
};
}
#endif
