#pragma once
#include "backened.hpp"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#ifdef _WIN32
#include <malloc.h>
#endif
inline void* _q14o(size_t align, size_t size) {
#ifdef _WIN32
return _aligned_malloc(size, align);
#else
return std::aligned_alloc(align, size);
#endif
}
inline void _q14p(void* p) {
#ifdef _WIN32
_aligned_free(p);
#else
std::free(p);
#endif
}
namespace nnue {
#ifndef NNUE_ARCH_L1
#define NNUE_ARCH_L1 512
#endif
#ifndef NNUE_ARCH_L2
#define NNUE_ARCH_L2 16
#endif
#ifndef NNUE_ARCH_L3
#define NNUE_ARCH_L3 32
#endif
constexpr int _q11o = NNUE_ARCH_L1;
constexpr int _q11p = NNUE_ARCH_L2;
constexpr int _q11q = NNUE_ARCH_L3;
static_assert(_q11o % 2 == 0 && _q11p >= 2, "bad architecture");
constexpr int _q12n = 8;
constexpr int _q11s = 8;
constexpr int _q12f = 64;
constexpr int _q12d = 32;
constexpr int _q12c = 11;
constexpr int _q12o = _q12c * _q12f;
constexpr int _q12e = _q12o * _q12d;
constexpr uint32_t _q119 = 0x6A448AFAu;
constexpr uint32_t _q10x = 0x7F234CB8u;
constexpr int _q12i = 16;
constexpr int _q146 = 6;
constexpr int _q11a = 255;
constexpr int _q11g = 128;
constexpr int _q120 = 600;
constexpr int _q11n[_q12f] = {
-1, -1, -1, -1, 31, 30, 29, 28,
-1, -1, -1, -1, 27, 26, 25, 24,
-1, -1, -1, -1, 23, 22, 21, 20,
-1, -1, -1, -1, 19, 18, 17, 16,
-1, -1, -1, -1, 15, 14, 13, 12,
-1, -1, -1, -1, 11, 10, 9, 8,
-1, -1, -1, -1, 7, 6, 5, 4,
-1, -1, -1, -1, 3, 2, 1, 0,
};
namespace _q15g {
inline bool _q17z(std::ifstream& f, void* dst, size_t n) {
f.read(reinterpret_cast<char*>(dst), (std::streamsize)n);
return (bool)f;
}
inline bool _q181(std::ifstream& f, uint32_t& out) {
uint8_t b[4];
if (!_q17z(f, b, 4)) return false;
out = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
return true;
}
inline bool _q17k(std::ifstream& f) {
static constexpr char _q16z[] = "COMPRESSED_LEB128";
constexpr size_t _q170 = sizeof(_q16z) - 1;
std::streampos pos = f.tellg();
char buf[_q170];
f.read(buf, (std::streamsize)_q170);
bool ok = (f.gcount() == (std::streamsize)_q170) && std::memcmp(buf, _q16z, _q170) == 0;
f.clear();
f.seekg(pos);
return ok;
}
inline bool _q15f(const std::vector<uint8_t>& bytes, size_t count, std::vector<int64_t>& out) {
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
template <typename T>
inline bool _q180(std::ifstream& f, std::vector<T>& out, size_t count, int width) {
out.resize(count);
if (_q17k(f)) {
char _q16z[17];
if (!_q17z(f, _q16z, 17)) return false;
uint32_t byteLen;
if (!_q181(f, byteLen)) return false;
std::vector<uint8_t> buf(byteLen);
if (byteLen && !_q17z(f, buf.data(), byteLen)) return false;
std::vector<int64_t> decoded;
if (!_q15f(buf, count, decoded)) return false;
for (size_t i = 0; i < count; ++i) out[i] = (T)decoded[i];
return true;
} else {
for (size_t i = 0; i < count; ++i) {
if (width == 1) {
int8_t v;
if (!_q17z(f, &v, 1)) return false;
out[i] = (T)v;
} else if (width == 2) {
uint8_t b[2];
if (!_q17z(f, b, 2)) return false;
int16_t v = (int16_t)((uint16_t)b[0] | ((uint16_t)b[1] << 8));
out[i] = (T)v;
} else {
uint8_t b[4];
if (!_q17z(f, b, 4)) return false;
int32_t v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
((uint32_t)b[3] << 24));
out[i] = (T)v;
}
}
return true;
}
}
}
struct _q12q {
alignas(32) std::array<int16_t, _q11o> acc{};
std::array<int32_t, _q12n> psqt{};
};
struct _q100 {
_q12q side[2];
};
struct _q10w {
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
uint8_t b[4];
if (!_q15g::_q17z(f, b, 4)) return false;
bias[i] = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) |
((uint32_t)b[3] << 24));
}
weight.resize((size_t)out_features * padded_in);
for (size_t i = 0; i < weight.size(); ++i) {
int8_t v;
if (!_q15g::_q17z(f, &v, 1)) return false;
weight[i] = v;
}
return true;
}
};
struct _q12g {
std::vector<int16_t> ftBias;
std::vector<int16_t> ftWeight;
std::vector<int32_t> ftPsqtWeight;
_q10w l1[_q11s];
_q10w l2[_q11s];
_q10w l3[_q11s];
bool loaded = false;
static uint32_t _q15c() {
uint32_t prev = 0xEC42E90Du ^ (uint32_t)(_q11o * 2);
const int outs[3] = {_q11p, _q11q, 1};
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
std::string lastError;
bool load(const std::string& path) {
loaded = false;
lastError.clear();
std::ifstream f(path, std::ios::binary);
if (!f) { lastError = "cannot open file"; return false; }
const uint32_t fcHash = _q15c();
uint32_t version;
if (!_q15g::_q181(f, version)) { lastError = "file too short"; return false; }
if (version != _q119) { lastError = "unsupported file version (not an nnue-pytorch export)"; return false; }
uint32_t headerHash;
if (!_q15g::_q181(f, headerHash)) { lastError = "file too short"; return false; }
if (headerHash != (fcHash ^ _q10x ^ (uint32_t)(_q11o * 2))) {
lastError = "architecture hash mismatch (net must be HalfKAv2_hm^ with the l1/l2/l3 this binary was built for)";
return false;
}
uint32_t descLen;
if (!_q15g::_q181(f, descLen)) { lastError = "file too short"; return false; }
f.seekg(descLen, std::ios::cur);
if (!f) { lastError = "file too short"; return false; }
uint32_t ftHash;
if (!_q15g::_q181(f, ftHash)) { lastError = "file too short"; return false; }
if (ftHash != (_q10x ^ (uint32_t)(_q11o * 2))) { lastError = "feature transformer hash mismatch"; return false; }
if (!_q15g::_q180(f, ftBias, (size_t)_q11o, 2) ||
!_q15g::_q180(f, ftWeight, (size_t)_q12e * _q11o, 2) ||
!_q15g::_q180(f, ftPsqtWeight, (size_t)_q12e * _q12n, 4)) {
lastError = "truncated or corrupt feature transformer";
return false;
}
for (int b = 0; b < _q11s; ++b) {
uint32_t bucketHash;
if (!_q15g::_q181(f, bucketHash) || bucketHash != fcHash ||
!l1[b].load(f, _q11o, _q11p) || !l2[b].load(f, 2 * _q11p, _q11q) ||
!l3[b].load(f, 2 * _q11p + 2 * _q11q, 1)) {
lastError = "truncated or corrupt layer stack";
return false;
}
}
loaded = true;
return true;
}
static inline int _q15p(_q156::_q10k perspective, _q156::_q13n ksq, _q156::_q12t pt,
_q156::_q10k _q15b, _q156::_q13n sq) {
const bool whitePov = (perspective == _q156::_q10k::_q148);
const int kfile = ksq.index() & 7;
const int flip = (kfile < 4 ? 7 : 0) ^ (whitePov ? 0 : 56);
const int oksq = ksq.index() ^ flip;
const int bucket = _q11n[oksq];
const int plane =
(pt == _q156::_q12t::_q11k) ? 10 : ((int)pt) * 2 + ((_q15b != perspective) ? 1 : 0);
const int osq = sq.index() ^ flip;
return bucket * _q12o + plane * _q12f + osq;
}
inline void _q18p(_q100& acc, _q156::_q10k perspective, _q156::_q13n ksq,
_q156::_q12t pt, _q156::_q10k _q15b, _q156::_q13n sq, bool add) const {
const int row = _q15p(perspective, ksq, pt, _q15b, sq);
_q12q& pa = acc.side[(int)perspective];
const int16_t* wrow = &ftWeight[(size_t)row * _q11o];
const int32_t* prow = &ftPsqtWeight[(size_t)row * _q12n];
if (add) {
for (int i = 0; i < _q11o; ++i) pa.acc[i] = (int16_t)(pa.acc[i] + wrow[i]);
for (int i = 0; i < _q12n; ++i) pa.psqt[i] += prow[i];
} else {
for (int i = 0; i < _q11o; ++i) pa.acc[i] = (int16_t)(pa.acc[i] - wrow[i]);
for (int i = 0; i < _q12n; ++i) pa.psqt[i] -= prow[i];
}
}
void _q185(_q156::_q10k perspective, const _q156::_q10c& board, _q100& acc) const {
using namespace _q156;
_q12q& pa = acc.side[(int)perspective];
for (int i = 0; i < _q11o; ++i) pa.acc[i] = ftBias[i];
pa.psqt.fill(0);
const _q13n ksq = board._q16r(perspective);
_q10b _q17c = board._q17c();
while (_q17c) {
_q13n sq = _q13n(_q17c.pop());
_q12s p = board.at(sq);
if (p == _q12s::_q121) continue;
_q18p(acc, perspective, ksq, p.type(), p._q15b(), sq, true);
}
}
void _q183(const _q156::_q10c& board, _q100& acc) const {
_q185(_q156::_q10k::_q148, board, acc);
_q185(_q156::_q10k::_q103, board, acc);
}
void _q186(_q156::_q10k perspective, const _q156::_q10c& board,
const _q156::_q11w& move, _q100& acc) const {
using namespace _q156;
_q12q& pa = acc.side[(int)perspective];
for (int i = 0; i < _q11o; ++i) pa.acc[i] = ftBias[i];
pa.psqt.fill(0);
const _q13n newKsq = move.to();
const _q13n fromSq = move.from();
const _q13n toSq = move.to();
_q10b _q17c = board._q17c();
while (_q17c) {
_q13n sq = _q13n(_q17c.pop());
if (sq == toSq) continue;
_q12s p = board.at(sq);
if (p == _q12s::_q121) continue;
_q13n actualSq = (sq == fromSq) ? newKsq : sq;
_q18p(acc, perspective, newKsq, p.type(), p._q15b(), actualSq, true);
}
}
void _q18x(_q100& acc, const _q156::_q10c& board, const _q156::_q11w& move) const {
using namespace _q156;
assert(move._q18q() != _q11w::_q10f);
_q13n from = move.from(), to = move.to();
_q12s moved = board.at(from);
if (moved == _q12s::_q121) return;
const bool isEP = (move._q18q() == _q11w::_q10v);
const bool hasNormalCapture = !isEP && board.at(to) != _q12s::_q121;
_q12s normalCaptured = hasNormalCapture ? board.at(to) : _q12s::_q121;
const _q12t destPT =
(move._q18q() == _q11w::_q12m) ? move._q17w() : moved.type();
for (int side = 0; side < 2; ++side) {
_q10k persp = _q10k(side);
if (moved.type() == _q12t::_q11k && moved._q15b() == persp) {
_q186(persp, board, move, acc);
continue;
}
const _q13n ksq = board._q16r(persp);
_q18p(acc, persp, ksq, moved.type(), moved._q15b(), from, false);
if (isEP) {
_q13n epCapSq = _q13n(to.index() + ((moved._q15b() == _q10k::_q148) ? -8 : 8));
_q18p(acc, persp, ksq, _q12t::_q12j, ~moved._q15b(), epCapSq, false);
} else if (hasNormalCapture) {
_q18p(acc, persp, ksq, normalCaptured.type(), normalCaptured._q15b(), to, false);
}
_q18p(acc, persp, ksq, destPT, moved._q15b(), to, true);
}
}
void update_for_null_move(_q100& acc, const _q156::_q10c& board) const {
(void)acc;
(void)board;
}
void _q15n(const _q100& acc, _q156::_q10k stm, int pieceCount, int& psqtOut, int& positionalOut) const {
int bucket = (pieceCount - 1) / 4;
if (bucket < 0) bucket = 0;
if (bucket >= _q11s) bucket = _q11s - 1;
const _q156::_q10k persp0 = stm;
const _q156::_q10k persp1 = ~stm;
std::array<uint8_t, _q11o> ftOut{};
constexpr int _q160 = _q11o / 2;
for (int p = 0; p < 2; ++p) {
const auto& a = acc.side[(int)(p == 0 ? persp0 : persp1)].acc;
const int offset = _q160 * p;
for (int i = 0; i < _q160; ++i) {
int32_t v0 = std::clamp<int32_t>(a[i], 0, _q11a);
int32_t v1 = std::clamp<int32_t>(a[i + _q160], 0, _q11a);
ftOut[offset + i] = (uint8_t)std::min(127, (v0 * v1) >> 9);
}
}
int32_t psqt =
(acc.side[(int)persp0].psqt[bucket] - acc.side[(int)persp1].psqt[bucket]) / 2;
const _q10w& fc0 = l1[bucket];
int32_t fc0_out[_q11p];
for (int j = 0; j < _q11p; ++j) {
int32_t s = fc0.bias[j];
const int8_t* w = &fc0.weight[(size_t)j * fc0.padded_in];
for (int i = 0; i < _q11o; ++i) s += (int32_t)w[i] * (int32_t)ftOut[i];
fc0_out[j] = s;
}
constexpr int _q14g = _q146 + 1;
uint8_t concat1[2 * _q11p];
for (int j = 0; j < _q11p; ++j) {
int64_t sq = (int64_t)fc0_out[j] * (int64_t)fc0_out[j];
concat1[j] = (uint8_t)std::min<int64_t>(127, sq >> (2 * _q14g + 7));
concat1[_q11p + j] = (uint8_t)std::clamp(fc0_out[j] >> _q14g, 0, 127);
}
const _q10w& fc1 = l2[bucket];
int32_t fc1_out[_q11q];
for (int k = 0; k < _q11q; ++k) {
int32_t s = fc1.bias[k];
const int8_t* w = &fc1.weight[(size_t)k * fc1.padded_in];
for (int i = 0; i < 2 * _q11p; ++i) s += (int32_t)w[i] * (int32_t)concat1[i];
fc1_out[k] = s;
}
constexpr int _q14h = _q146;
uint8_t concatFinal[2 * _q11p + 2 * _q11q];
std::memcpy(concatFinal, concat1, sizeof(concat1));
for (int k = 0; k < _q11q; ++k) {
int64_t sq = (int64_t)fc1_out[k] * (int64_t)fc1_out[k];
concatFinal[2 * _q11p + k] = (uint8_t)std::min<int64_t>(127, sq >> (2 * _q14h + 7));
concatFinal[2 * _q11p + _q11q + k] = (uint8_t)std::clamp(fc1_out[k] >> _q14h, 0, 127);
}
const _q10w& fc2 = l3[bucket];
int32_t fwdOut = fc2.bias[0];
{
const int8_t* w = &fc2.weight[0];
const int n = 2 * _q11p + 2 * _q11q;
for (int i = 0; i < n; ++i) fwdOut += (int32_t)w[i] * (int32_t)concatFinal[i];
}
static_assert(_q11p >= 2, "L2 must be at least 2 for the skip connection");
fwdOut += fc0_out[_q11p - 2] - fc0_out[_q11p - 1];
constexpr int64_t _q179 = (int64_t)_q120 * _q12i;
constexpr int64_t denominator = (int64_t)_q11g * (1 << _q146) * 2;
int32_t positional = (int32_t)(((int64_t)fwdOut * _q179) / denominator);
psqtOut = psqt / _q12i;
positionalOut = positional / _q12i;
}
int evaluate_from_accumulator(const _q100& acc, _q156::_q10k stm, int pieceCount) const {
int p, q;
_q15n(acc, stm, pieceCount, p, q);
return p + q;
}
int _q15o(const _q156::_q10c& board) const {
_q100 acc;
_q183(board, acc);
return evaluate_from_accumulator(acc, board._q18j(), board._q17c().count());
}
};
}
