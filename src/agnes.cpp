
#include "backened.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#ifndef _WIN32
#include <unistd.h>
#include <sys/mman.h>
#include <dirent.h>
#endif
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
using namespace chess;
#include "nnue.h"
struct Tunable {
const char* name;
int* value;
int def, lo, hi, step;
};
std::vector<Tunable>& _q4m() {
static std::vector<Tunable> t;
return t;
}
#define TUNE(name, def, lo, hi, step) \
static int name = (def); \
namespace { \
struct Reg_##name { \
Reg_##name() { _q4m().push_back({#name, &name, def, lo, hi, step}); } \
}; \
static Reg_##name reg_##name##_inst; \
}
constexpr int _q8 = 32001;
constexpr int _qd = 32000;
constexpr int _qc = _qd - 128;
constexpr int TB_WIN = _qc - 1;
constexpr int _qf = 128;
constexpr int _qe = 64;
constexpr int _qg = -32768;
constexpr int _q6 = 0;
inline bool is_decisive(int s) { return std::abs(s) >= _qc; }
inline bool is_win(int s) { return s >= _qc; }
inline bool is_loss(int s) { return s <= -_qc; }
inline int mated_in(int ply) { return -(_qd - ply); }
#include "tb/tbprobe.h"
namespace syzygy {
inline constexpr unsigned LOSS = TB_LOSS;
inline constexpr unsigned BLESSED_LOSS = TB_BLESSED_LOSS;
inline constexpr unsigned DRAW = TB_DRAW;
inline constexpr unsigned CURSED_WIN = TB_CURSED_WIN;
inline constexpr unsigned WIN = TB_WIN;
inline constexpr unsigned RESULT_FAILED = TB_RESULT_FAILED;
}
#undef TB_WIN
#undef TB_LOSS
#undef TB_DRAW
#undef TB_BLESSED_LOSS
#undef TB_CURSED_WIN
namespace syzygy {
inline bool ready() { return TB_LARGEST > 0; }
inline unsigned largest() { return TB_LARGEST; }
inline unsigned ep_arg(const chess::Board& b) {
return b.enpassantSq() == chess::Square::NO_SQ
? 0u
: static_cast<unsigned>(b.enpassantSq().index());
}
inline bool probe_wdl(const chess::Board& b, int ply, int& score) {
const unsigned v = tb_probe_wdl_impl(
b.us(chess::Color::WHITE).getBits(), b.us(chess::Color::BLACK).getBits(),
b.pieces(chess::PieceType::KING).getBits(), b.pieces(chess::PieceType::QUEEN).getBits(),
b.pieces(chess::PieceType::ROOK).getBits(), b.pieces(chess::PieceType::BISHOP).getBits(),
b.pieces(chess::PieceType::KNIGHT).getBits(), b.pieces(chess::PieceType::PAWN).getBits(),
ep_arg(b), b.sideToMove() == chess::Color::WHITE);
switch (v) {
case WIN:
case CURSED_WIN: score = _qd - ply - 1; return true;
case DRAW: score = _q6; return true;
case LOSS:
case BLESSED_LOSS: score = -(_qd - ply - 1); return true;
default: return false;
}
}
struct RootProbe {
chess::Move move;
int cp_score;
};
inline RootProbe probe_root(const chess::Board& b, bool has_repeated) {
const RootProbe none{chess::Move::NO_MOVE, 0};
if (!ready() || !b.castlingRights().isEmpty()) return none;
if (b.occ().count() > static_cast<int>(TB_LARGEST)) return none;
const std::uint64_t w = b.us(chess::Color::WHITE).getBits();
const std::uint64_t bl = b.us(chess::Color::BLACK).getBits();
const std::uint64_t k = b.pieces(chess::PieceType::KING).getBits();
const std::uint64_t q = b.pieces(chess::PieceType::QUEEN).getBits();
const std::uint64_t r = b.pieces(chess::PieceType::ROOK).getBits();
const std::uint64_t bi = b.pieces(chess::PieceType::BISHOP).getBits();
const std::uint64_t n = b.pieces(chess::PieceType::KNIGHT).getBits();
const std::uint64_t p = b.pieces(chess::PieceType::PAWN).getBits();
const unsigned rule50 = static_cast<unsigned>(b.halfMoveClock());
const unsigned ep = ep_arg(b);
const bool white = b.sideToMove() == chess::Color::WHITE;
struct TbRootMoves results;
int ok = tb_probe_root_dtz(w, bl, k, q, r, bi, n, p, rule50, 0, ep, white,
has_repeated, true, &results);
if (!ok)
ok = tb_probe_root_wdl(w, bl, k, q, r, bi, n, p, rule50, 0, ep, white,
true, &results);
if (!ok || results.size == 0) return none;
unsigned best = 0;
for (unsigned i = 1; i < results.size; i++)
if (results.moves[i].tbRank > results.moves[best].tbRank) best = i;
const TbMove tm = results.moves[best].move;
const int from = TB_MOVE_FROM(tm);
const int to = TB_MOVE_TO(tm);
const int promo = TB_MOVE_PROMOTES(tm);
chess::Movelist legal;
chess::movegen::legalmoves<chess::movegen::MoveGenType::ALL>(legal, b);
for (const chess::Move& m : legal) {
if (m.from().index() != from || m.to().index() != to) continue;
if (promo == 0) {
if (m.typeOf() == chess::Move::PROMOTION) continue;
return {m, std::clamp(results.moves[best].tbScore, -30000, 30000)};
}
if (m.typeOf() != chess::Move::PROMOTION) continue;
const int want = promo == TB_PROMOTES_QUEEN ? static_cast<int>(chess::PieceType::QUEEN)
: promo == TB_PROMOTES_ROOK ? static_cast<int>(chess::PieceType::ROOK)
: promo == TB_PROMOTES_BISHOP ? static_cast<int>(chess::PieceType::BISHOP)
: static_cast<int>(chess::PieceType::KNIGHT);
if (static_cast<int>(m.promotionType()) == want)
return {m, std::clamp(results.moves[best].tbScore, -30000, 30000)};
}
return none;
}
}
TUNE(_ql, 10, 5, 40, 1)
TUNE(_qm, 3, 2, 8, 1)
TUNE(_q38, 450, 200, 900, 20)
TUNE(_q39, 270, 100, 600, 20)
TUNE(_q3c, 80, 40, 180, 4)
TUNE(_q3b, 60, 20, 120, 4)
TUNE(_q3d, 9, 4, 14, 1)
TUNE(_q2r, 4, 2, 8, 1)
TUNE(_q2s, 3, 2, 6, 1)
TUNE(_q2u, 180, 80, 400, 10)
TUNE(_q2t, 3, 1, 6, 1)
TUNE(_q2v, 390, 100, 800, 20)
TUNE(_q2w, 18, 8, 40, 2)
TUNE(_q2y, 14, 8, 20, 1)
TUNE(_q24, 6, 3, 10, 1)
TUNE(_q35, 200, 100, 400, 10)
TUNE(_q34, 60, 20, 120, 5)
TUNE(_q36, 5, 3, 8, 1)
TUNE(_q33, 4, 2, 6, 1)
TUNE(_q27, 3, 1, 8, 1)
TUNE(_q23, 4000, 1500, 8000, 250)
TUNE(_q19, 120, 50, 300, 10)
TUNE(_q1c, 100, 50, 250, 10)
TUNE(_qq, 230, 100, 500, 15)
TUNE(_qr, 200, 80, 400, 10)
TUNE(_q3h, -25, -60, -10, 5)
TUNE(_q3e, -150, -300, -60, 10)
TUNE(_q1b, 8000, 2048, 32768, 512)
TUNE(_q3g, 32, 8, 256, 4)
TUNE(_q3f, 50, 20, 100, 5)
TUNE(_qo, 6, 2, 12, 1)
TUNE(_qn, 100, 0, 300, 10)
TUNE(_q3p, 6, 4, 10, 1)
TUNE(_q3k, 52, 20, 100, 5)
TUNE(_q3l, 74, 30, 150, 5)
TUNE(_q3n, 20, 10, 60, 5)
TUNE(_q3r, 30, 0, 80, 5)
TUNE(_q3o, 85, 70, 100, 5)
TUNE(_q2n, 110, 0, 250, 10)
TUNE(_q3q, -3, -6, -1, 1)
TUNE(_q13, 8, 4, 16, 1)
TUNE(_q1x, 170, 60, 350, 10)
TUNE(_q1y, 90, 20, 250, 10)
TUNE(_q1z, 1700, 800, 3200, 100)
TUNE(_q20, 450, 200, 800, 25)
TUNE(_q21, 150, 20, 400, 10)
TUNE(_q22, 1500, 700, 3000, 100)
TUNE(_q3w, 900, 0, 4000, 100)
TUNE(_q3v, 700, 0, 4000, 100)
TUNE(_qs, 1200, 0, 8000, 200)
TUNE(_qt, -75, -200, 0, 25)
TUNE(_q1w, 71, 25, 100, 1)
TUNE(_q32, 3, 2, 6, 1)
TUNE(_q10, 8, 1, 20, 1)
TUNE(_qy, 6, 1, 20, 1)
TUNE(_qz, 6, 1, 20, 1)
TUNE(_qw, 5, 1, 20, 1)
TUNE(_qx, 4, 1, 20, 1)
TUNE(_qu, 16, 4, 32, 2)
TUNE(_qv, 1024, 256, 4096, 64)
TUNE(_q14, 8, 4, 16, 1)
TUNE(_q2c, 1024, 0, 2048, 64)
TUNE(_q2d, 1024, 0, 2048, 64)
TUNE(_q2g, 900, 0, 2048, 64)
TUNE(_q2j, 1024, 0, 2048, 64)
TUNE(_q2m, 1024, 0, 2048, 64)
TUNE(_q29, 800, 0, 2048, 64)
TUNE(_q2l, 1024, 0, 2048, 64)
TUNE(_q2b, 700, 0, 2048, 64)
TUNE(_q2h, 1024, 0, 2048, 64)
TUNE(_q28, 1024, 0, 2048, 64)
TUNE(_q2i, 35, 0, 128, 5)
TUNE(_q2k, 32, 0, 256, 8)
TUNE(_q2f, 9000, 4000, 24000, 500)
TUNE(_q2e, 2048, 512, 4096, 128)
TUNE(_q15, 40, 10, 120, 5)
TUNE(_q16, 1, 1, 6, 1)
TUNE(_q37, 200, 100, 400, 10)
TUNE(_q4a, 125, 80, 160, 5)
TUNE(_q4b, 7, 3, 12, 1)
TUNE(_q3z, 60, 30, 120, 5)
TUNE(_q3y, 170, 100, 300, 10)
TUNE(_q44, 150, 50, 300, 10)
TUNE(_q45, 90, 30, 150, 5)
TUNE(_q40, 800, 600, 950, 10)
TUNE(_q43, 600, 300, 1000, 25)
TUNE(_q2o, 630, 300, 1000, 25)
TUNE(_q2p, 11, 3, 30, 1)
TUNE(_q46, 45, 20, 100, 5)
TUNE(_q4p, 14, 1, 60, 1)
TUNE(_q3x, 250, 100, 400, 10)
TUNE(_q49, 93, 80, 99, 1)
TUNE(_q48, 300, 100, 1000, 25)
TUNE(_q47, 6, 3, 10, 1)
TUNE(_q41, 1000, 500, 3000, 50)
TUNE(_q42, 50, 0, 100, 5)
TUNE(_q3a, 2, 1, 8, 1)
TUNE(_q1a, 2, 1, 8, 1)
TUNE(_q2a, 128, 0, 512, 16)
TUNE(_q3m, 4, 1, 16, 1)
TUNE(_q2x, 100, 0, 300, 10)
static const int _q9 = 64;
static int _qb[_q9][_q9];
static int _qa[_q9][_q9];
void _q26() {
for (int d = 1; d < _q9; ++d)
for (int m = 1; m < _q9; ++m) {
_qb[d][m] = (int)(1024.0 * (0.80 + std::log((double)d) * std::log((double)m) / 2.20));
_qa[d][m] = (int)(1024.0 * (0.20 + std::log((double)d) * std::log((double)m) / 3.20));
}
for (int m = 0; m < _q9; ++m) { _qb[0][m] = _qa[0][m] = 0; }
for (int d = 0; d < _q9; ++d) { _qb[d][0] = _qa[d][0] = 0; }
}
inline int lmr_q(int d, int m) { return _qb[std::min(d, _q9 - 1)][std::min(m, _q9 - 1)]; }
inline int lmr_n(int d, int m) { return _qa[std::min(d, _q9 - 1)][std::min(m, _q9 - 1)]; }
static std::uint64_t _q1e[8192];
static Move _q1f[8192];
static inline int _q11(std::uint64_t h) { return (int)(h & 0x1fff); }
static inline int _q12(std::uint64_t h) { return (int)((h >> 16) & 0x1fff); }
void _q25() {
std::memset(_q1e, 0, sizeof(_q1e));
for (int i = 0; i < 8192; ++i) _q1f[i] = Move::NO_MOVE;
static const int pcs[10] = {1, 2, 3, 4, 5, 7, 8, 9, 10, 11};
for (int pc : pcs)
for (int s1 = 0; s1 < 64; ++s1)
for (int s2 = s1 + 1; s2 < 64; ++s2) {
const PieceType pt = Piece(static_cast<Piece::underlying>(pc)).type();
Bitboard att;
switch ((int)pt) {
case 1: att = attacks::knight(Square(s1)); break;
case 2: att = attacks::bishop(Square(s1), Bitboard(0ULL)); break;
case 3: att = attacks::rook(Square(s1), Bitboard(0ULL)); break;
case 4: att = attacks::queen(Square(s1), Bitboard(0ULL)); break;
default: att = attacks::king(Square(s1)); break;
}
if (!((att.getBits() >> s2) & 1)) continue;
Move mv = Move::make<0>(Square(s1), Square(s2));
std::uint64_t key = Zobrist::pieceIdx(pc, s1) ^ Zobrist::pieceIdx(pc, s2)
^ Zobrist::sideToMove();
int i = _q11(key);
while (true) {
std::swap(_q1e[i], key);
std::swap(_q1f[i], mv);
if (mv == Move::NO_MOVE) break;
i = (i == _q11(key)) ? _q12(key) : _q11(key);
}
}
}
enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };
struct TTEntry {
uint16_t key16;
uint16_t move16;
int16_t score;
int16_t eval;
uint8_t depth;
uint8_t gen_bound;
uint8_t pad[6];
};
static_assert(sizeof(TTEntry) == 16, "TTEntry must be exactly 16 bytes");
struct alignas(64) TTCluster { TTEntry e[4]; };
static inline uint16_t move_to_u16(const Move& m) { return m.move(); }
static inline Move _q4n(uint16_t r) { return Move(r); }
static inline void _q4l(TTEntry& dst, const TTEntry& src) {
#ifdef __SSE2__
_mm_store_si128(reinterpret_cast<__m128i*>(&dst),
_mm_load_si128(reinterpret_cast<const __m128i*>(&src)));
#else
dst = src;
#endif
}
static inline TTEntry _q4j(const TTEntry& src) {
TTEntry out;
#ifdef __SSE2__
_mm_store_si128(reinterpret_cast<__m128i*>(&out),
_mm_load_si128(reinterpret_cast<const __m128i*>(&src)));
#else
out = src;
#endif
return out;
}
static inline uint64_t _q2q(uint64_t a, uint64_t b) {
return (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
}
static TTCluster* _q4d = nullptr;
static size_t _q1u = 1 << 16;
static uint8_t _q4g = 0;
inline size_t tt_cluster_of(uint64_t key) { return _q2q(key, _q1u); }
inline void tt_prefetch(uint64_t key) { __builtin_prefetch(&_q4d[tt_cluster_of(key)]); }
inline int score_to_tt(int s, int ply) {
if (s >= _qc) return s + ply;
if (s <= -_qc) return s - ply;
return s;
}
inline int score_from_tt(int s, int ply) {
if (s >= _qc) return s - ply;
if (s <= -_qc) return s + ply;
return s;
}
void _q4e();
void _q4f() {
if (_q4d) { aligned_free_raw(_q4d); _q4d = nullptr; }
}
void _q4k(size_t mb) {
size_t want = std::max<size_t>(mb, 1) * 1024 * 1024 / sizeof(TTCluster);
TTCluster* p = static_cast<TTCluster*>(aligned_alloc_raw(64, want * sizeof(TTCluster)));
if (!p) return;
#if defined(__linux__)
madvise(p, want * sizeof(TTCluster), MADV_HUGEPAGE);
#endif
_q4f();
_q4d = p;
_q1u = want;
_q4e();
}
void _q4e() {
if (!_q4d) return;
unsigned hw = std::thread::hardware_concurrency();
int nth = (int)std::clamp(hw ? hw : 1u, 1u, 16u);
auto worker = [&](int id) {
size_t chunk = (_q1u + nth - 1) / nth;
size_t lo = std::min<size_t>(id * chunk, _q1u);
size_t hi = std::min(lo + chunk, _q1u);
std::memset(_q4d + lo, 0, (hi - lo) * sizeof(TTCluster));
};
std::vector<std::thread> th;
for (int i = 1; i < nth; ++i) th.emplace_back(worker, i);
worker(0);
for (auto& t : th) t.join();
_q4g = 0;
}
struct TTData {
bool hit;
Move move;
int score;
int eval;
int depth;
Bound bound;
bool pv;
};
struct TTWriter {
TTEntry* e;
bool found;
};
TTData _q4i(uint64_t key) {
TTCluster& c = _q4d[tt_cluster_of(key)];
const uint16_t k16 = (uint16_t)key;
for (int i = 0; i < 4; ++i) {
TTEntry e = _q4j(c.e[i]);
if (e.key16 == k16 && e.gen_bound != 0) {
TTData d;
d.hit = true;
d.move = _q4n(e.move16);
d.score = e.score;
d.eval = e.eval;
d.depth = e.depth;
d.bound = (Bound)(e.gen_bound & 0x03);
d.pv = (e.gen_bound >> 2) & 1;
return d;
}
}
int best = 0, bestv = 1 << 30;
for (int i = 0; i < 4; ++i) {
TTEntry e = _q4j(c.e[i]);
int age = (int)((_q4g - (e.gen_bound >> 3)) & 31);
int v = (int)e.depth - 8 * age;
if (v < bestv) { bestv = v; best = i; }
}
TTData d;
d.hit = false;
d.move = Move::NO_MOVE;
d.score = 0;
d.eval = _qg;
d.depth = 0;
d.bound = BOUND_NONE;
d.pv = false;
(void)best;
return d;
}
void tt_store(uint64_t key, Move move, int score, int eval, int depth, Bound bound, bool pv, int ply) {
TTCluster& c = _q4d[tt_cluster_of(key)];
const uint16_t k16 = (uint16_t)key;
int slot = -1;
for (int i = 0; i < 4; ++i) {
TTEntry e = _q4j(c.e[i]);
if (e.key16 == k16 || (e.gen_bound & 0x03) == BOUND_NONE) { slot = i; break; }
}
if (slot < 0) {
int best = 0, bestv = 1 << 30;
for (int i = 0; i < 4; ++i) {
TTEntry e = _q4j(c.e[i]);
int age = (int)((_q4g - (e.gen_bound >> 3)) & 31);
int v = (int)e.depth - 8 * age;
if (v < bestv) { bestv = v; best = i; }
}
slot = best;
}
TTEntry old = _q4j(c.e[slot]);
bool key_ok = (old.key16 == k16);
bool replace = !key_ok
|| bound == BOUND_EXACT
|| depth + 2 + 2 * pv > (int)old.depth
|| (old.gen_bound >> 3) != _q4g
|| (old.gen_bound & 0x03) == BOUND_NONE;
if (!replace) return;
TTEntry n;
n.key16 = k16;
uint16_t mv16 = move_to_u16(move);
if (key_ok && mv16 == 0 && old.move16 != 0) mv16 = old.move16;
n.move16 = mv16;
n.score = (int16_t)std::clamp(score_to_tt(score, ply), -32767, 32767);
n.eval = (int16_t)std::clamp(eval, -32768, 32767);
n.depth = (uint8_t)std::clamp(depth, 0, 255);
n.gen_bound = (uint8_t)((_q4g << 3) | (pv ? 4 : 0) | (uint8_t)bound);
_q4l(c.e[slot], n);
}
int _q4h() {
if (!_q4d) return 0;
int used = 0;
size_t stride = _q1u / 1000;
if (!stride) stride = 1;
for (int i = 0; i < 1000; ++i) {
TTCluster& c = _q4d[(size_t)i * stride];
for (int j = 0; j < 4; ++j) {
TTEntry e = _q4j(c.e[j]);
if ((e.gen_bound & 0x03) != BOUND_NONE && (e.gen_bound >> 3) == _q4g) ++used;
}
}
return used / 4;
}
static nnue::Network _q1j;
static std::string _q1k = "acherontia.nnue";
static int _q1l = 40;
struct Stack {
Move move = Move::NO_MOVE;
PieceType moved_pt = PieceType::NONE;
int to = -1;
int static_eval = _qg;
int raw_eval = _qg;
bool in_check = false;
bool was_capture = false;
bool tt_pv = false;
bool tt_capture = false;
Bitboard threat = Bitboard(0ULL);
int cutoff_cnt = 0;
int reduction = 0;
int dext = 0;
int move_count = 0;
Move excluded = Move::NO_MOVE;
Move killers[2] = {Move::NO_MOVE, Move::NO_MOVE};
Move pv[_qf];
int pv_len = 0;
};
constexpr int _q4 = 16384;
struct AccEntry {
nnue::Accumulator acc;
bool computed = false;
};
struct ThreadState {
Stack stacks[_qf + 8];
int16_t mainHist[2][2][2][64][64];
int16_t pieceToHist[2][2][2][6][64];
int16_t pawnHist[2][1024][6][64];
int16_t contHist[2][2][6][64][6][64];
int16_t capHist[7][64][7];
int16_t capContHist[6][64][6][64];
Move counterMoves[2][6][64];
int16_t contCorr2[6][64][6][64];
AccEntry accStack[_qf + 8];
Move accMoves[_qf + 8];
int accDepth = 0;
uint64_t nodes = 0;
int seldepth = 0;
int thread_id = 0;
int nmp_min_ply = 0;
int root_depth = 1;
uint64_t root_nodes[64][64];
Move root_best = Move::NO_MOVE;
int root_score = -_q8;
bool root_best_updated = false;
void clear_history() {
std::memset(mainHist, 0, sizeof(mainHist));
std::memset(pieceToHist, 0, sizeof(pieceToHist));
std::memset(pawnHist, 0, sizeof(pawnHist));
std::memset(contHist, 0, sizeof(contHist));
std::memset(capHist, 0, sizeof(capHist));
std::memset(capContHist, 0, sizeof(capContHist));
std::memset(counterMoves, 0, sizeof(counterMoves));
std::memset(contCorr2, 0, sizeof(contCorr2));
}
void age_histories() {
const int pct = _q1w;
auto age16 = [pct](int16_t* p, size_t n) {
for (size_t i = 0; i < n; ++i) p[i] = (int16_t)((int32_t)p[i] * pct / 100);
};
age16(&mainHist[0][0][0][0][0], sizeof(mainHist)/sizeof(int16_t));
age16(&pieceToHist[0][0][0][0][0], sizeof(pieceToHist)/sizeof(int16_t));
age16(&pawnHist[0][0][0][0], sizeof(pawnHist)/sizeof(int16_t));
age16(&contHist[0][0][0][0][0][0], sizeof(contHist)/sizeof(int16_t));
age16(&capHist[0][0][0], sizeof(capHist)/sizeof(int16_t));
age16(&capContHist[0][0][0][0], sizeof(capContHist)/sizeof(int16_t));
age16(&contCorr2[0][0][0][0], sizeof(contCorr2)/sizeof(int16_t));
}
};
struct SharedCorr {
int16_t pawnCorr[2][2][_q4];
int16_t minorCorr[2][2][_q4];
int16_t nonPawnCorr[2][2][2][_q4];
int16_t contCorr[6][64];
void clear() {
std::memset(pawnCorr, 0, sizeof(pawnCorr));
std::memset(minorCorr, 0, sizeof(minorCorr));
std::memset(nonPawnCorr, 0, sizeof(nonPawnCorr));
std::memset(contCorr, 0, sizeof(contCorr));
}
};
static SharedCorr _q1d;
static std::vector<std::unique_ptr<ThreadState>> _q1t;
static int _q1m = 1;
static const int _qk[7] = { 100, 320, 330, 500, 900, 20000, 0 };
constexpr int _q7 = 16384;
inline void add_hist(int16_t& h, int bonus) {
h = (int16_t)std::clamp((int)h + bonus - (int)h * std::abs(bonus) / _q7,
-_q7, _q7);
}
inline int history_bonus(int d) { return std::min(_q1x * d - _q1y, _q1z); }
inline int history_malus(int d) { return std::min(_q20 * d - _q21, _q22); }
inline int mvv_lva(PieceType a, PieceType v) { return _qk[(int)v] * 10 - _qk[(int)a]; }
constexpr int _q1[5] = {1, 2, 4, 6, 99};
constexpr int _q2[5] = {16, 16, 8, 4, 0};
inline int16_t* cont_slot(ThreadState& ts, int back, const Stack* ss, int ply, int pt, int to) {
int p = ply - back;
if (p < 0) return nullptr;
const Stack& ps = ss[p];
if (ps.moved_pt == PieceType::NONE || ps.to < 0) return nullptr;
return &ts.contHist[ps.in_check ? 1 : 0][ps.was_capture ? 1 : 0][(int)ps.moved_pt][ps.to][pt][to];
}
inline int cont_hist_at(ThreadState& ts, int back, const Stack* ss, int ply, int pt, int to) {
int16_t* s = cont_slot(ts, back, ss, ply, pt, to);
return s ? *s : 0;
}
inline int cont_hist_sum(ThreadState& ts, const Stack* ss, int ply, int pt, int to) {
int h = 0;
for (int i = 0; i < 5; ++i) h += _q2[i] * cont_hist_at(ts, _q1[i], ss, ply, pt, to);
return h / 16;
}
inline void cont_hist_update(ThreadState& ts, const Stack* ss, int ply, int pt, int to,
int bonus, bool with_weights) {
for (int i = 0; i < 5; ++i) {
int16_t* s = cont_slot(ts, _q1[i], ss, ply, pt, to);
if (s) add_hist(*s, with_weights ? bonus * _q2[i] / 16 : bonus);
}
}
constexpr int _q3 = 8 * 1024;
inline uint64_t mix64(uint64_t x) {
x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
x ^= x >> 27; x *= 0x94d049bb133111ebULL;
x ^= x >> 31;
return x;
}
inline int corr_idx(uint64_t key) { return (int)(mix64(key) >> 50); }
inline uint64_t pawn_corr_key(const Board& b) {
return b.pieces(PieceType::PAWN, Color::WHITE).getBits() * 0x9E3779B97F4A7C15ULL
^ b.pieces(PieceType::PAWN, Color::BLACK).getBits() * 0xC2B2AE3D27D4EB4FULL;
}
inline uint64_t minor_corr_key(const Board& b) {
Bitboard m = b.pieces(PieceType::KNIGHT) | b.pieces(PieceType::BISHOP);
return m.getBits() * 0xD6E8FEB86659FD93ULL;
}
inline uint64_t nonpawn_corr_key(const Board& b, Color c) {
Bitboard m = b.us(c) & ~b.pieces(PieceType::PAWN, c);
return m.getBits() * (c == Color::WHITE ? 0xA24BAED4963EE407ULL : 0x9FB21C651E98DF25ULL);
}
inline int get_correction(ThreadState& ts, const Board& b, const Stack* ss, int ply) {
if (!_q1j.loaded) return 0;
const int stm = (int)b.sideToMove();
const int hb = b.halfMoveClock() >= 20 ? 1 : 0;
int pawn = _q1d.pawnCorr[hb][stm][corr_idx(pawn_corr_key(b))];
int minor = _q1d.minorCorr[hb][stm][corr_idx(minor_corr_key(b))];
int npW = _q1d.nonPawnCorr[hb][stm][0][corr_idx(nonpawn_corr_key(b, Color::WHITE))];
int npB = _q1d.nonPawnCorr[hb][stm][1][corr_idx(nonpawn_corr_key(b, Color::BLACK))];
int cont = 0;
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
cont = _q1d.contCorr[(int)ss[ply - 1].moved_pt][ss[ply - 1].to];
int cont2 = 0;
if (ply >= 4 && ss[ply - 2].moved_pt != PieceType::NONE && ss[ply - 2].to >= 0 &&
ss[ply - 4].moved_pt != PieceType::NONE && ss[ply - 4].to >= 0)
cont2 = ts.contCorr2[(int)ss[ply - 2].moved_pt][ss[ply - 2].to]
[(int)ss[ply - 4].moved_pt][ss[ply - 4].to];
int sum = _q10 * pawn + _qy * minor
+ _qz * npW + _qz * npB + _qw * cont
+ _qx * cont2;
return sum / (_qu * 32);
}
inline void update_corr_entry(int16_t& e, int diff, int depth) {
int target = std::clamp(diff * depth * _qu / 8, -_qv / 4, _qv / 4);
e = (int16_t)std::clamp((int)e + target - (int)e * std::abs(target) / _qv,
-_q3, _q3);
}
inline void update_correction(ThreadState& ts, const Board& b, const Stack* ss, int ply,
int best, int static_eval, int depth, int alpha, int beta,
bool best_is_capture) {
if (!_q1j.loaded || std::abs(best) >= _qc || best_is_capture) return;
if ((best >= beta && best <= static_eval) || (best <= alpha && best >= static_eval)) return;
const int stm = (int)b.sideToMove();
const int hb = b.halfMoveClock() >= 20 ? 1 : 0;
int diff = best - static_eval;
update_corr_entry(_q1d.pawnCorr[hb][stm][corr_idx(pawn_corr_key(b))], diff, depth);
update_corr_entry(_q1d.minorCorr[hb][stm][corr_idx(minor_corr_key(b))], diff, depth);
update_corr_entry(_q1d.nonPawnCorr[hb][stm][0][corr_idx(nonpawn_corr_key(b, Color::WHITE))], diff, depth);
update_corr_entry(_q1d.nonPawnCorr[hb][stm][1][corr_idx(nonpawn_corr_key(b, Color::BLACK))], diff, depth);
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
update_corr_entry(_q1d.contCorr[(int)ss[ply - 1].moved_pt][ss[ply - 1].to], diff, depth);
if (ply >= 4 && ss[ply - 2].moved_pt != PieceType::NONE && ss[ply - 2].to >= 0 &&
ss[ply - 4].moved_pt != PieceType::NONE && ss[ply - 4].to >= 0)
update_corr_entry(ts.contCorr2[(int)ss[ply - 2].moved_pt][ss[ply - 2].to]
[(int)ss[ply - 4].moved_pt][ss[ply - 4].to], diff, depth);
}
struct SeePins {
Bitboard blockers[2], pinners[2];
};
inline void compute_pins(const Board& b, Bitboard blockers[2], Bitboard pinners[2]);
bool see_ge(const Board& b, Move move, int threshold, const SeePins* pins = nullptr);
inline bool root_filter_ok(const Move& m);
inline Bitboard threat_mask(const Board& b) {
Color stm = b.sideToMove(), them = ~stm;
std::uint64_t t = 0;
Bitboard p = b.pieces(PieceType::PAWN, them);
while (p) t |= attacks::pawn(them, Square(p.pop())).getBits();
Bitboard n = b.pieces(PieceType::KNIGHT, them);
while (n) t |= attacks::knight(Square(n.pop())).getBits();
Bitboard bi = b.pieces(PieceType::BISHOP, them);
while (bi) t |= attacks::bishop(Square(bi.pop()), b.occ()).getBits();
Bitboard r = b.pieces(PieceType::ROOK, them);
while (r) t |= attacks::rook(Square(r.pop()), b.occ()).getBits();
return Bitboard(t);
}
inline void threat_masks_by_class(const Board& b, std::uint64_t& pawnAtt,
std::uint64_t& minorAtt, std::uint64_t& rookAtt) {
Color stm = b.sideToMove(), them = ~stm;
pawnAtt = minorAtt = rookAtt = 0;
Bitboard p = b.pieces(PieceType::PAWN, them);
while (p) pawnAtt |= attacks::pawn(them, Square(p.pop())).getBits();
Bitboard n = b.pieces(PieceType::KNIGHT, them);
while (n) minorAtt |= attacks::knight(Square(n.pop())).getBits();
Bitboard bi = b.pieces(PieceType::BISHOP, them);
const std::uint64_t occ = b.occ().getBits();
while (bi) minorAtt |= attacks::bishop(Square(bi.pop()), b.occ()).getBits();
Bitboard r = b.pieces(PieceType::ROOK, them);
while (r) rookAtt |= attacks::rook(Square(r.pop()), b.occ()).getBits();
(void)occ;
}
inline int threat_quiet_term(PieceType pt, int fromSq, int toSq,
std::uint64_t pawnAtt, std::uint64_t minorAtt,
std::uint64_t rookAtt) {
std::uint64_t mask;
int pti = (int)pt;
if (pti == (int)PieceType::KNIGHT || pti == (int)PieceType::BISHOP)
mask = pawnAtt;
else if (pti == (int)PieceType::ROOK)
mask = pawnAtt | minorAtt;
else if (pti == (int)PieceType::QUEEN)
mask = pawnAtt | minorAtt | rookAtt;
else
return 0;
int t = 0;
if ((mask >> fromSq) & 1) t += _q3w;
if ((mask >> toSq) & 1) t -= _q3v;
return t;
}
inline int main_hist(ThreadState& ts, const Board& b, const Move& m, const Bitboard& threat) {
int c = (int)b.sideToMove();
int f = m.from().index(), t = m.to().index();
bool ft = (threat.getBits() >> f) & 1, tt2 = (threat.getBits() >> t) & 1;
int pt = (int)b.at<PieceType>(m.from());
return (ts.mainHist[c][ft][tt2][f][t] + ts.pieceToHist[c][ft][tt2][pt][t]) / 2;
}
inline void main_hist_update(ThreadState& ts, const Board& b, const Move& m,
const Bitboard& threat, int bonus) {
int c = (int)b.sideToMove();
int f = m.from().index(), t = m.to().index();
bool ft = (threat.getBits() >> f) & 1, tt2 = (threat.getBits() >> t) & 1;
int pt = (int)b.at<PieceType>(m.from());
add_hist(ts.mainHist[c][ft][tt2][f][t], bonus);
add_hist(ts.pieceToHist[c][ft][tt2][pt][t], bonus);
}
struct MovePicker {
enum Stage { ST_TT = 0, ST_NOISY, ST_KILLERS, ST_QUIET, ST_BAD_NOISY, ST_DONE };
static constexpr int TT_MARK = 1 << 30;
static int qslot(int j) { return 255 - j; }
MovePicker(Board& bd, ThreadState& t, const Stack* s, int pl, Move ttm,
bool qs, bool evades, Bitboard thr)
: b(bd), ts(t), ss(s), ply(pl), ttmove(ttm), qs_mode(qs), evasion(evades), threat(thr) {
compute_pins(b, pins.blockers, pins.pinners);
threat_masks_by_class(b, threatPawn, threatMinor, threatRook);
#if defined(AGNES_LAZY_QUIETS)
if (evasion) {
movegen::legalmoves(moves, b);
for (const Move& m : moves) {
bool cap = b.isCapture(m);
bool qp = (m.typeOf() == Move::PROMOTION && m.promotionType() == PieceType::QUEEN);
if (cap || qp) { mbuf[n_noisy] = m.move(); sc[n_noisy] = 0; ++n_noisy; }
else { mbuf[qslot(n_quiet)] = m.move(); sc[qslot(n_quiet)] = 0; ++n_quiet; }
}
score_noisy();
} else {
movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, b);
for (const Move& m : moves) { mbuf[n_noisy] = m.move(); sc[n_noisy] = 0; ++n_noisy; }
score_noisy();
if (ttmove.typeOf() == Move::CASTLING) gen_quiets();
}
quiet_gen = false;
#else
if (qs_mode && !evasion) {
movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, b);
for (const Move& m : moves) { mbuf[n_noisy] = m.move(); sc[n_noisy] = 0; ++n_noisy; }
{
Movelist pquiet;
movegen::legalmoves<movegen::MoveGenType::QUIET>(pquiet, b, PieceGenType::PAWN);
for (const Move& m : pquiet)
if (m.typeOf() == Move::PROMOTION && m.promotionType() == PieceType::QUEEN) {
mbuf[n_noisy] = m.move(); sc[n_noisy] = 0; ++n_noisy;
}
}
score_noisy();
quiet_gen = true;
qs_capgen = true;
} else {
movegen::legalmoves(moves, b);
for (const Move& m : moves) {
bool cap = b.isCapture(m);
bool qp = (m.typeOf() == Move::PROMOTION && m.promotionType() == PieceType::QUEEN);
if (cap || qp) { mbuf[n_noisy] = m.move(); sc[n_noisy] = 0; ++n_noisy; }
else { mbuf[qslot(n_quiet)] = m.move(); sc[qslot(n_quiet)] = 0; ++n_quiet; }
}
score_noisy();
if (!evasion && !qs_mode) score_quiets();
quiet_gen = true;
}
#endif
noisy_done = quiet_done = killers_done = 0;
stage = ST_TT;
}
int pawn_hist_idx(const Board& bd) const {
return (int)(mix64(pawn_corr_key(bd)) >> 54);
}
void score_noisy() {
for (int i = 0; i < n_noisy; ++i) {
const Move m(mbuf[i]);
if (m == ttmove) { sc[i] = TT_MARK; continue; }
PieceType att = b.at<PieceType>(m.from());
PieceType vic = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: ((m.typeOf() == Move::PROMOTION && b.at(m.to()) == Piece::NONE)
? PieceType::PAWN : b.at<PieceType>(m.to()));
int ch = ts.capHist[(int)att][m.to().index()][(int)vic];
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
ch += ts.capContHist[(int)ss[ply - 1].moved_pt][ss[ply - 1].to][(int)att][m.to().index()];
sc[i] = ch + mvv_lva(att, vic)
+ ((m.typeOf() == Move::PROMOTION) ? 800 + (int)_qk[(int)m.promotionType()] : 0);
}
}
void score_quiets() {
for (int j = 0; j < n_quiet; ++j) {
const int k = qslot(j);
const Move m(mbuf[k]);
if (m == ttmove) { sc[k] = TT_MARK; continue; }
PieceType pt2 = b.at<PieceType>(m.from());
int h = main_hist(ts, b, m, threat)
+ ts.pawnHist[(int)b.sideToMove()][pawn_hist_idx(b)][(int)pt2][m.to().index()]
+ cont_hist_sum(ts, ss, ply, (int)pt2, m.to().index());
h += threat_quiet_term(pt2, m.from().index(), m.to().index(),
threatPawn, threatMinor, threatRook);
if (b.givesCheck(m) == CheckType::DIRECT_CHECK &&
see_ge(b, m, _qt, &pins)) h += _qs;
if (m.typeOf() == Move::PROMOTION) h -= 8000;
sc[k] = h;
}
}
void gen_quiets() {
if (quiet_gen) return;
quiet_gen = true;
Movelist all;
movegen::legalmoves(all, b);
for (const Move& m : all) {
const bool cap = b.isCapture(m);
const bool promo = (m.typeOf() == Move::PROMOTION);
const bool qpromo = promo && m.promotionType() == PieceType::QUEEN;
if (cap) continue;
if (m == ttmove || m == k0 || m == k1 || m == kc) continue;
PieceType pt2 = b.at<PieceType>(m.from());
int h = main_hist(ts, b, m, threat)
+ ts.pawnHist[(int)b.sideToMove()][pawn_hist_idx(b)][(int)pt2][m.to().index()]
+ cont_hist_sum(ts, ss, ply, (int)pt2, m.to().index());
h += threat_quiet_term(pt2, m.from().index(), m.to().index(),
threatPawn, threatMinor, threatRook);
if (b.givesCheck(m) == CheckType::DIRECT_CHECK &&
see_ge(b, m, _qt, &pins)) h += _qs;
if (promo && !qpromo) h -= 8000;
if (qpromo) h += 8000;
mbuf[qslot(n_quiet)] = m.move();
sc[qslot(n_quiet)] = h;
++n_quiet;
}
}
void skip_quiets() { skip_q = true; }
bool in_bad_noisy() const { return stage == ST_BAD_NOISY; }
bool legal_contains(const Move& m) const {
for (const Move& x : moves) if (x == m) return true;
return false;
}
bool probe_legal(const Move& m) {
const int f = m.from().index(), t = m.to().index();
if ((unsigned)f > 63u || (unsigned)t > 63u) return false;
if (m.typeOf() == Move::CASTLING) return false;
Piece p = b.at(Square(f));
if (p == Piece::NONE || p.color() != b.sideToMove()) return false;
Piece q = b.at(Square(t));
if (q != Piece::NONE && q.color() == b.sideToMove()) return false;
if (m.typeOf() == Move::PROMOTION) {
PieceType pr = m.promotionType();
if (pr != PieceType::KNIGHT && pr != PieceType::BISHOP &&
pr != PieceType::ROOK && pr != PieceType::QUEEN) return false;
}
b.makeMove(m);
bool ok = !b.isAttacked(b.kingSq(~b.sideToMove()), b.sideToMove());
b.unmakeMove(m);
return ok;
}
bool quiet_contains(const Move& m) const {
for (int j = 0; j < n_quiet; ++j) if (Move(mbuf[qslot(j)]) == m) return true;
return false;
}
Move next_move() {
while (true) {
switch (stage) {
case ST_TT: {
stage = ST_NOISY;
if (ttmove != Move::NO_MOVE) {
#if defined(AGNES_LAZY_QUIETS)
if (legal_contains(ttmove)) return ttmove;
gen_quiets();
if (quiet_contains(ttmove)) return ttmove;
#else
if (qs_capgen ? movegen::isLegal(b, ttmove)
: legal_contains(ttmove)) return ttmove;
#endif
}
break;
}
case ST_NOISY: {
if (evasion) {
if (!qs_scored) {
qs_scored = true;
for (int j = 0; j < n_quiet; ++j) {
const int k = qslot(j);
const Move m(mbuf[k]);
PieceType pt2 = b.at<PieceType>(m.from());
sc[k] = main_hist(ts, b, m, threat)
+ cont_hist_sum(ts, ss, ply, (int)pt2, m.to().index());
}
}
int bi = -1, bv = -1 << 30; int bk = -1;
for (int i = noisy_done; i < n_noisy; ++i)
if (sc[i] > bv) { bv = sc[i]; bi = i; }
for (int j = quiet_done; j < n_quiet; ++j) {
int k = qslot(j);
if (sc[k] > bv) { bv = sc[k]; bk = k; }
}
if (bi < 0 && bk < 0) { stage = qs_mode ? ST_DONE : ST_BAD_NOISY; break; }
if (bi >= 0 && (bk < 0 || sc[bi] >= sc[bk])) {
std::swap(mbuf[bi], mbuf[noisy_done]); std::swap(sc[bi], sc[noisy_done]);
return Move(mbuf[noisy_done++]);
}
int hq = qslot(quiet_done);
std::swap(mbuf[bk], mbuf[hq]); std::swap(sc[bk], sc[hq]);
++quiet_done;
return Move(mbuf[hq]);
}
if (qs_mode && noisy_done >= n_noisy) { stage = ST_DONE; break; }
while (noisy_done < n_noisy) {
int bi = noisy_done;
for (int i = noisy_done + 1; i < n_noisy; ++i) if (sc[i] > sc[bi]) bi = i;
std::swap(mbuf[bi], mbuf[noisy_done]); std::swap(sc[bi], sc[noisy_done]);
Move m(mbuf[noisy_done]);
if (m == ttmove) { ++noisy_done; continue; }
int thr = -sc[noisy_done] / 18;
if (see_ge(b, m, thr, &pins)) return Move(mbuf[noisy_done++]);
mbuf[n_bad++] = m.move();
++noisy_done;
}
stage = qs_mode ? ST_DONE : ST_KILLERS;
break;
}
case ST_KILLERS: {
if (skip_q) { stage = ST_BAD_NOISY; break; }
if (killers_done < 3) {
cands[0] = ss[ply].killers[0];
cands[1] = ss[ply].killers[1];
cands[2] = (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
? ts.counterMoves[(int)b.sideToMove()][(int)ss[ply - 1].moved_pt][ss[ply - 1].to]
: Move::NO_MOVE;
while (killers_done < 3) {
Move m = cands[killers_done++];
if (m == Move::NO_MOVE || m == ttmove) continue;
if (m == k0 || m == k1) continue;
bool iscap = b.isCapture(m);
#if defined(AGNES_LAZY_QUIETS)
if (iscap || !probe_legal(m)) continue;
#else
if (iscap || !legal_contains(m)) continue;
#endif
if (killers_done == 1) k0 = m;
else if (killers_done == 2) k1 = m;
else kc = m;
return m;
}
}
stage = ST_QUIET;
break;
}
case ST_QUIET: {
if (skip_q) { stage = ST_BAD_NOISY; break; }
if (!quiet_gen) gen_quiets();
while (quiet_done < n_quiet) {
int bj = quiet_done;
int bk = qslot(bj);
for (int j = quiet_done + 1; j < n_quiet; ++j) {
int k = qslot(j);
if (sc[k] > sc[bk]) { bj = j; bk = k; }
}
int hk = qslot(quiet_done);
std::swap(mbuf[bk], mbuf[hk]); std::swap(sc[bk], sc[hk]);
Move m(mbuf[hk]);
++quiet_done;
if (m == ttmove || m == k0 || m == k1 || m == kc) continue;
return m;
}
stage = ST_BAD_NOISY;
break;
}
case ST_BAD_NOISY: {
if (bad_i < n_bad) return Move(mbuf[bad_i++]);
stage = ST_DONE;
break;
}
default:
return Move::NO_MOVE;
}
}
}
Board& b;
ThreadState& ts;
const Stack* ss;
int ply;
Move ttmove;
bool qs_mode, evasion;
bool skip_q = false;
bool qs_scored = false;
bool quiet_gen = false;
Move k0 = Move::NO_MOVE, k1 = Move::NO_MOVE, kc = Move::NO_MOVE;
Move cands[3];
Stage stage;
Movelist moves;
SeePins pins;
Bitboard threat;
std::uint64_t threatPawn = 0, threatMinor = 0, threatRook = 0;
bool qs_capgen = false;
uint16_t mbuf[256];
int sc[256];
int n_noisy = 0, noisy_done = 0;
int n_quiet = 0, quiet_done = 0;
int n_bad = 0, bad_i = 0;
int killers_done = 0;
};
inline void compute_pins(const Board& b, Bitboard blockers[2], Bitboard pinners[2]) {
blockers[0] = blockers[1] = pinners[0] = pinners[1] = Bitboard(0ULL);
const std::uint64_t occ = b.occ().getBits();
for (Color c : {Color::WHITE, Color::BLACK}) {
Square ksq = b.kingSq(c);
std::uint64_t snipers = (bk::ST.pseudoBishop[ksq.index()] & (b.pieces(PieceType::BISHOP, ~c) | b.pieces(PieceType::QUEEN, ~c)).getBits())
| (bk::ST.pseudoRook[ksq.index()] & (b.pieces(PieceType::ROOK, ~c) | b.pieces(PieceType::QUEEN, ~c)).getBits());
std::uint64_t sn = snipers;
while (sn) {
int s = bk::ctz64(sn); sn &= sn - 1;
std::uint64_t between = bk::ST.between[ksq.index()][s] & occ & ~(1ULL << s);
if (bk::pop64(between) == 1) {
int bsq = bk::ctz64(between);
if ((b.at(Square(bsq)) != Piece::NONE) && (int)b.at(Square(bsq)).color() == (int)c) {
blockers[(int)c] |= Bitboard(1ULL << bsq);
pinners[(int)c] |= Bitboard(1ULL << s);
}
}
}
}
}
bool see_ge(const Board& b, Move move, int threshold, const SeePins* pins) {
if (move.typeOf() == Move::CASTLING) return true;
Square from = move.from(), to = move.to();
Piece movedPiece = b.at(from);
if (movedPiece == Piece::NONE) return true;
PieceType attackerPT = movedPiece.type();
PieceType capturedPT = (move.typeOf() == Move::ENPASSANT) ? PieceType::PAWN : b.at<PieceType>(to);
int balance = ((capturedPT == PieceType::NONE) ? 0 : _qk[(int)capturedPT]) - threshold;
if (balance < 0) return false;
PieceType nextVictim = attackerPT;
if (move.typeOf() == Move::PROMOTION) {
balance += _qk[(int)move.promotionType()] - _qk[(int)PieceType::PAWN];
nextVictim = move.promotionType();
}
balance = _qk[(int)nextVictim] - balance;
if (balance <= 0) return true;
std::uint64_t occRaw = b.occ().getBits();
occRaw &= ~(1ULL << from.index());
occRaw |= (1ULL << to.index());
if (move.typeOf() == Move::ENPASSANT) {
int capSq = to.index() + ((b.sideToMove() == Color::WHITE) ? -8 : 8);
occRaw &= ~(1ULL << capSq);
}
const std::uint64_t origOcc = b.occ().getBits();
Bitboard occ(occRaw);
Bitboard blockers[2], pinners[2];
if (pins) {
blockers[0] = pins->blockers[0]; blockers[1] = pins->blockers[1];
pinners[0] = pins->pinners[0]; pinners[1] = pins->pinners[1];
} else {
compute_pins(b, blockers, pinners);
}
Color side = ~b.sideToMove();
const Bitboard bq = b.pieces(PieceType::BISHOP) | b.pieces(PieceType::QUEEN);
const Bitboard rq = b.pieces(PieceType::ROOK) | b.pieces(PieceType::QUEEN);
auto get_atk = [&](Bitboard o) {
Bitboard a(0ULL);
a |= attacks::pawn(Color::BLACK, to) & b.pieces(PieceType::PAWN, Color::WHITE);
a |= attacks::pawn(Color::WHITE, to) & b.pieces(PieceType::PAWN, Color::BLACK);
a |= attacks::knight(to) & b.pieces(PieceType::KNIGHT);
a |= attacks::bishop(to, o) & bq;
a |= attacks::rook(to, o) & rq;
a |= attacks::king(to) & b.pieces(PieceType::KING);
return a & o;
};
Bitboard atk = get_atk(occ);
bool relStm = true;
while (true) {
Bitboard sa = atk & b.us(side);
if (!(pinners[(int)side].getBits() & ~occ.getBits()))
sa &= ~blockers[(int)side];
if (!sa) break;
PieceType pt = PieceType::NONE;
Square asq(0);
for (PieceType c : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
Bitboard cbb = sa & b.pieces(c, side);
if (cbb) { pt = c; asq = Square(cbb.lsb()); break; }
}
if (pt == PieceType::NONE) break;
occ &= ~Bitboard(1ULL << asq.index());
atk = get_atk(occ);
relStm = !relStm;
balance = -balance - 1 - _qk[(int)nextVictim];
nextVictim = pt;
if (balance >= 0) {
if (nextVictim == PieceType::KING && (atk & b.us(~side)))
relStm = !relStm;
break;
}
side = ~side;
}
return relStm;
}
uint64_t _q4c();
constexpr int _q3t[20] = {2, 2, 2, 3, 2, 2, 3, 4, 2, 2, 3, 2, 4, 2, 3, 2, 5, 2, 3, 2};
constexpr int _q3s[20] = {0, 1, 0, 1, 0, 1, 2, 1, 0, 1, 0, 1, 2, 0, 1, 0, 3, 1, 2, 0};
static const int _qj[7] = { 100, 320, 330, 500, 900, 20000, 0 };
static const int _qi[7] = { 120, 300, 320, 550, 950, 20000, 0 };
inline int game_phase(const Board& b) {
constexpr int MAX_PHASE = 24;
int p = MAX_PHASE;
for (Color c : {Color::WHITE, Color::BLACK}) {
p -= (int)b.pieces(PieceType::KNIGHT, c).count() * 1;
p -= (int)b.pieces(PieceType::BISHOP, c).count() * 1;
p -= (int)b.pieces(PieceType::ROOK, c).count() * 2;
p -= (int)b.pieces(PieceType::QUEEN, c).count() * 4;
}
p = std::max(0, p);
return (p * 256 + MAX_PHASE / 2) / MAX_PHASE;
}
int eval_material_simple(const Board& b) {
int ph = game_phase(b);
int mg = 0, eg = 0;
for (Color c : {Color::WHITE, Color::BLACK}) {
int s = (c == Color::WHITE) ? 1 : -1;
for (PieceType pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
PieceType::ROOK, PieceType::QUEEN}) {
int cnt = (int)b.pieces(pt, c).count();
mg += s * cnt * _qj[(int)pt];
eg += s * cnt * _qi[(int)pt];
}
}
int sc = (mg * (256 - ph) + eg * ph) / 256;
return (b.sideToMove() == Color::WHITE) ? sc : -sc;
}
inline int nnue_raw_from_acc(ThreadState& ts, const Board& b, int d) {
int psqt, positional;
_q1j.eval_parts(ts.accStack[d].acc, b.sideToMove(), (int)b.occ().count(), psqt, positional);
int raw = (125 * psqt + 131 * positional) / 128;
int e = raw * _q1l / 100;
int hfm = (int)b.halfMoveClock();
if (hfm > 90) e = e * std::max(0, 200 - hfm) / 200;
return std::clamp(e, -(_qc - 1), _qc - 1);
}
int evaluate_tree(Board& b, ThreadState& ts);
int evaluate(const Board& b);
inline void do_move(Board& b, const Move& m, ThreadState& ts) {
if (_q1j.loaded) {
++ts.accDepth;
ts.accMoves[ts.accDepth] = m;
ts.accStack[ts.accDepth].computed = false;
}
b.makeMove(m);
}
inline void undo_move(Board& b, const Move& m, ThreadState& ts) {
b.unmakeMove(m);
if (_q1j.loaded) --ts.accDepth;
}
inline void do_null_move(Board& b, ThreadState& ts) {
if (_q1j.loaded) {
++ts.accDepth;
ts.accMoves[ts.accDepth] = Move::NO_MOVE;
ts.accStack[ts.accDepth].computed = false;
}
b.makeNullMove();
}
inline void undo_null_move(Board& b, ThreadState& ts) {
b.unmakeNullMove();
if (_q1j.loaded) --ts.accDepth;
}
#if defined(NNUE_VERIFY)
#include <cassert>
static void nnue_verify_diagnose(Board& b, ThreadState& ts, int d) {
std::cout << "info string NNUE_VERIFY diagnose: stack walk (d=" << d << ")\n";
int first_bad = -1;
for (int p = d; p >= 0; --p) {
if (p < d) {
Move m = ts.accMoves[p + 1];
if (m == Move::NO_MOVE) b.unmakeNullMove();
else b.unmakeMove(m);
}
nnue::Accumulator fresh;
_q1j.refresh(b, fresh);
const auto& a = ts.accStack[p].acc;
int nd = 0, bad_side = -1;
for (int s = 0; s < 2; ++s) {
int nds = 0;
for (int i = 0; i < nnue::L1; ++i)
if (fresh.side[s].acc.data()[i] != a.side[s].acc.data()[i]) ++nds;
if (nds > 0 && bad_side < 0) bad_side = s;
nd += nds;
}
if (ts.accStack[p].computed && nd > 0 && first_bad < 0) first_bad = p;
std::cout << "info string   ply " << p << " move "
<< (ts.accMoves[p] == Move::NO_MOVE
? std::string("null")
: uci::moveToUci(ts.accMoves[p]))
<< " computed=" << (int)ts.accStack[p].computed
<< " diff-lanes=" << nd;
if (ts.accStack[p].computed && nd > 0 && bad_side >= 0)
std::cout << " BADSIDE=" << bad_side;
std::cout << "\n";
}
if (first_bad >= 1) {
for (int p = 1; p < first_bad; ++p) b.makeMove(ts.accMoves[p]);
const Move m = ts.accMoves[first_bad];
const Piece moved = b.at(m.from());
std::cout << "info string first-corrupt ply " << first_bad
<< " move " << uci::moveToUci(m)
<< " moved-piece-type " << (int)moved.type() << "\n";
if (m != Move::NO_MOVE && m.typeOf() != Move::CASTLING) {
nnue::Accumulator inc = ts.accStack[first_bad - 1].acc;
_q1j.update_for_move(inc, b, m);
Board post = b; post.makeMove(m);
nnue::Accumulator gt;
_q1j.refresh(post, gt);
for (int s = 0; s < 2; ++s) {
int nd = 0;
for (int i = 0; i < nnue::L1; ++i)
if (inc.side[s].acc.data()[i] != gt.side[s].acc.data()[i]) ++nd;
int npd = 0;
for (int i = 0; i < nnue::PSQT_BUCKETS; ++i)
if (inc.side[s].psqt[i] != gt.side[s].psqt[i]) ++npd;
std::cout << "info string   incremental-vs-refresh side " << s
<< " acc-diff-lanes " << nd
<< " psqt-diff-buckets " << npd << "\n";
}
}
}
std::cout.flush();
}
static void nnue_verify_node(Board& b, ThreadState& ts, int d) {
nnue::Accumulator fresh;
_q1j.refresh(b, fresh);
const auto& a = ts.accStack[d].acc;
bool acc_bad = std::memcmp(fresh.side[0].acc.data(), a.side[0].acc.data(), sizeof(int16_t) * nnue::L1) != 0 ||
std::memcmp(fresh.side[1].acc.data(), a.side[1].acc.data(), sizeof(int16_t) * nnue::L1) != 0;
bool psqt_bad = false;
for (int s = 0; s < 2 && !psqt_bad; ++s)
for (int i = 0; i < nnue::PSQT_BUCKETS; ++i)
if (fresh.side[s].psqt[i] != a.side[s].psqt[i]) { psqt_bad = true; break; }
if (acc_bad || psqt_bad) {
std::cout << "info string NNUE_VERIFY accumulator mismatch at accDepth " << d
<< (psqt_bad && !acc_bad ? " (psqt only)" : "") << "\n";
std::cout << "fen " << b.getFen() << "\n";
for (int side2 = 0; side2 < 2; ++side2) {
int nd = 0;
for (int i = 0; i < nnue::L1; ++i)
if (fresh.side[side2].acc.data()[i] != a.side[side2].acc.data()[i]) ++nd;
int npd = 0;
for (int i = 0; i < nnue::PSQT_BUCKETS; ++i)
if (fresh.side[side2].psqt[i] != a.side[side2].psqt[i]) ++npd;
std::cout << "info string side " << side2 << " diff-lanes " << nd
<< " psqt-diff-buckets " << npd << "\n";
}
std::cout << "info string path:";
for (int p2 = 0; p2 <= d; ++p2) {
Move mm = ts.accMoves[p2];
if (mm != Move::NO_MOVE) std::cout << " " << uci::moveToUci(mm);
else std::cout << " null";
}
std::cout << "\n";
std::cout.flush();
nnue_verify_diagnose(b, ts, d);
std::abort();
}
int p1, q1;
_q1j.eval_parts(a, b.sideToMove(), (int)b.occ().count(), p1, q1);
int tot = _q1j.evaluate_from_accumulator(a, b.sideToMove(), (int)b.occ().count());
if (p1 + q1 != tot) {
std::cout << "info string NNUE_VERIFY eval_parts(" << p1 << "," << q1
<< ") != evaluate_from_accumulator(" << tot << ")\n";
std::cout << "fen " << b.getFen() << "\n";
std::cout.flush();
std::abort();
}
}
#else
inline void nnue_verify_node(Board&, ThreadState&, int) {}
#endif
int evaluate_tree(Board& b, ThreadState& ts) {
if (!_q1j.loaded) return eval_material_simple(b);
const int d = ts.accDepth;
if (!ts.accStack[d].computed) {
int k = d;
while (k > 0 && !ts.accStack[k].computed) --k;
for (int p = d; p > k; --p) {
Move m = ts.accMoves[p];
if (m == Move::NO_MOVE) b.unmakeNullMove();
else b.unmakeMove(m);
}
if (!ts.accStack[k].computed) {
_q1j.refresh(b, ts.accStack[0].acc);
ts.accStack[0].computed = true;
}
for (int p = k + 1; p <= d; ++p) {
Move m = ts.accMoves[p];
nnue::Accumulator& dst = ts.accStack[p].acc;
if (m == Move::NO_MOVE) {
dst = ts.accStack[p - 1].acc;
b.makeNullMove();
} else if (m.typeOf() == Move::CASTLING) {
b.makeMove(m);
_q1j.refresh(b, dst);
} else {
dst = ts.accStack[p - 1].acc;
_q1j.update_for_move(dst, b, m);
b.makeMove(m);
}
ts.accStack[p].computed = true;
}
}
nnue_verify_node(b, ts, d);
return nnue_raw_from_acc(ts, b, d);
}
int evaluate(const Board& b) {
if (_q1j.loaded) {
nnue::Accumulator acc;
_q1j.refresh(b, acc);
int psqt, positional;
_q1j.eval_parts(acc, b.sideToMove(), (int)b.occ().count(), psqt, positional);
int raw = (125 * psqt + 131 * positional) / 128;
int e = raw * _q1l / 100;
int hfm = (int)b.halfMoveClock();
if (hfm > 90) e = e * std::max(0, 200 - hfm) / 200;
return std::clamp(e, -(_qc - 1), _qc - 1);
}
return eval_material_simple(b);
}
struct SearchInfo {
std::atomic<bool> stop{false};
std::atomic<uint64_t> node_limit{0};
std::atomic<int> soft_time{0};
std::atomic<int> hard_time{0};
std::atomic<bool> fixed_movetime{false};
std::atomic<bool> infinite{false};
std::atomic<int64_t> start_epoch_ms{0};
void set_start_now() {
start_epoch_ms.store(std::chrono::duration_cast<std::chrono::milliseconds>(
std::chrono::steady_clock::now().time_since_epoch()).count(),
std::memory_order_relaxed);
}
bool time_up() const {
const int ht = hard_time.load(std::memory_order_relaxed);
if (ht <= 0) return false;
const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
std::chrono::steady_clock::now().time_since_epoch()).count();
return now_ms - start_epoch_ms.load(std::memory_order_relaxed) >= ht;
}
};
static SearchInfo info;
static std::thread _q1s;
static int _q1p = 0, _q1o = 0;
static std::atomic<bool> _q1r{false};
void _q4q() {
info.stop.store(true, std::memory_order_relaxed);
if (_q1s.joinable()) _q1s.join();
}
inline int elapsed_ms() {
const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
std::chrono::steady_clock::now().time_since_epoch()).count();
return (int)(now_ms - info.start_epoch_ms.load(std::memory_order_relaxed));
}
int qsearch(Board& b, int alpha, int beta, int ply, ThreadState& ts, Stack* ss) {
ts.nodes++;
if (ts.thread_id == 0 && (ts.nodes & 1023) == 0) {
if (info.time_up() ||
(info.node_limit > 0 &&
_q4c() >= info.node_limit.load(std::memory_order_relaxed)))
info.stop.store(true, std::memory_order_relaxed);
}
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (ply > ts.seldepth) ts.seldepth = ply;
if (ply >= _qf - 1) return evaluate_tree(b, ts);
const uint64_t key = b.hash();
tt_prefetch(key);
const bool is_pv = (beta - alpha > 1);
ss[ply].pv_len = 0;
ss[ply].static_eval = _qg;
if (ply > 0 && alpha < _q6 && b.has_game_cycle(ply, _q1e, _q1f)) {
alpha = _q6 + 2 - (int)(ts.nodes & 3);
if (alpha >= beta) return alpha;
}
if (ply > 0 && (b.isRepetitionSearch(ply) || b.isInsufficientMaterial() ||
(b.halfMoveClock() >= 100 && !b.inCheck()))) {
return _q6 + (int)(ts.nodes & 3) - 1;
}
Move qtt = Move::NO_MOVE;
int raw_eval = _qg;
bool have_raw = false;
{
TTData td = _q4i(key);
ss[ply].tt_pv = is_pv || td.pv;
if (td.hit) {
qtt = td.move;
if (!is_pv) {
int s = score_from_tt(td.score, ply);
if (td.bound == BOUND_EXACT) return s;
if (td.bound == BOUND_UPPER && s <= alpha) return s;
if (td.bound == BOUND_LOWER && s >= beta) return s;
}
if (td.eval != _qg) { raw_eval = td.eval; have_raw = true; }
}
}
const bool in_check = b.inCheck();
if (!in_check && !have_raw) raw_eval = evaluate_tree(b, ts);
const int corr = (!in_check && _q1j.loaded) ? get_correction(ts, b, ss, ply) : 0;
const int stand_pat = in_check ? -_q8 : raw_eval + corr;
ss[ply].static_eval = in_check ? _qg : raw_eval;
if (!in_check) {
if (stand_pat >= beta)
return is_decisive(stand_pat) ? stand_pat : (stand_pat + beta) / 2;
if (stand_pat > alpha) alpha = stand_pat;
}
MovePicker mp(b, ts, ss, ply, qtt, true, in_check, Bitboard(0ULL));
if (in_check && mp.moves.empty()) return mated_in(ply);
int best_score = stand_pat;
Move best_move = Move::NO_MOVE;
const int orig_alpha = alpha;
const int futility_base = stand_pat + _q37;
int move_count = 0;
Move m;
while ((m = mp.next_move()) != Move::NO_MOVE) {
const bool is_cap = b.isCapture(m);
const bool is_promo = m.typeOf() == Move::PROMOTION;
if (!in_check) {
if (!is_promo && best_score > -_qc) {
PieceType vic = is_cap ? ((m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: b.at<PieceType>(m.to()))
: PieceType::NONE;
if (futility_base + _qk[(int)vic] <= alpha) {
best_score = std::max(best_score, futility_base + _qk[(int)vic]);
continue;
}
if (futility_base <= alpha && !see_ge(b, m, 1, &mp.pins)) continue;
if (!see_ge(b, m, 0, &mp.pins)) continue;
} else if (!see_ge(b, m, 0, &mp.pins)) {
continue;
}
} else if (!is_cap && !is_promo && move_count > 2 && best_score > -_qc) {
break;
}
++move_count;
ss[ply].move_count = move_count;
ss[ply].moved_pt = b.at<PieceType>(m.from());
ss[ply].to = m.to().index();
ss[ply].in_check = in_check;
ss[ply].was_capture = is_cap;
tt_prefetch(b.zobristAfter(m));
do_move(b, m, ts);
ss[ply].move = m;
int s = -qsearch(b, -beta, -alpha, ply + 1, ts, ss);
undo_move(b, m, ts);
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (s > best_score) {
best_score = s;
best_move = m;
if (s > alpha) {
alpha = s;
if (is_pv) {
ss[ply].pv[0] = m;
std::memcpy(ss[ply].pv + 1, ss[ply + 1].pv, ss[ply + 1].pv_len * sizeof(Move));
ss[ply].pv_len = ss[ply + 1].pv_len + 1;
}
}
if (alpha >= beta) break;
}
}
if (!info.stop.load(std::memory_order_relaxed)) {
Bound f = (best_score >= beta) ? BOUND_LOWER
: (alpha > orig_alpha) ? BOUND_EXACT
: BOUND_UPPER;
tt_store(key, best_move, best_score, in_check ? _qg : raw_eval, 0, f,
ss[ply].tt_pv, ply);
}
return best_score;
}
int negamax(Board& b, int depth, int alpha, int beta, int ply, bool null_ok, bool cut_node,
ThreadState& ts, Stack* ss, Move excluded = Move::NO_MOVE) {
ts.nodes++;
if (ts.thread_id == 0 && (ts.nodes & 1023) == 0) {
if (info.time_up() ||
(info.node_limit > 0 &&
_q4c() >= info.node_limit.load(std::memory_order_relaxed)))
info.stop.store(true, std::memory_order_relaxed);
}
if (info.stop.load(std::memory_order_relaxed)) return 0;
const bool is_pv = (beta - alpha > 1);
const bool is_root = (ply == 0);
if (!is_root) {
if (b.isRepetitionSearch(ply) || b.isInsufficientMaterial()) {
return _q6 + 2 - (int)(ts.nodes & 3);
}
if (b.halfMoveClock() >= 100) {
auto [reason, result] = b.getHalfMoveDrawType();
if (reason == GameResultReason::CHECKMATE) return mated_in(ply);
return _q6 + 2 - (int)(ts.nodes & 3);
}
}
if (!is_root && alpha < _q6 && b.has_game_cycle(ply, _q1e, _q1f)) {
alpha = _q6 + 2 - (int)(ts.nodes & 3);
if (alpha >= beta) return alpha;
}
if (syzygy::ready() && !is_root && b.castlingRights().isEmpty() &&
b.occ().count() <= static_cast<int>(syzygy::largest())) {
int tbs;
if (syzygy::probe_wdl(b, ply, tbs)) return tbs;
}
if (ply >= _qf - 1)
return b.inCheck() ? evaluate_tree(b, ts) : qsearch(b, alpha, beta, ply, ts, ss);
if (depth <= 0)
return qsearch(b, alpha, beta, ply, ts, ss);
if (!is_root) {
int mate_alpha = mated_in(ply);
int mate_beta = _qd - ply - 1;
if (alpha < mate_alpha) alpha = mate_alpha;
if (beta > mate_beta) beta = mate_beta;
if (alpha >= beta) return alpha;
}
if (ply + 2 < _qf) ss[ply + 2].cutoff_cnt = 0;
const bool in_check = b.inCheck();
ss[ply].in_check = in_check;
ss[ply].move_count = 0;
ss[ply].pv_len = 0;
ss[ply].dext = ss[ply - 1].dext;
ss[ply].reduction = 0;
ss[ply].excluded = excluded;
const uint64_t key = b.hash();
tt_prefetch(key);
const bool tt_ok = (excluded == Move::NO_MOVE) && b.halfMoveClock() < 90;
Move hm = Move::NO_MOVE;
int tt_depth = 0, tt_score = 0, tt_eval = _qg;
Bound tt_bound = BOUND_NONE;
bool tt_hit = false, tt_pv = false;
if (tt_ok) {
TTData td = _q4i(key);
if (td.hit) {
tt_hit = true;
hm = td.move;
tt_depth = td.depth;
tt_score = score_from_tt(td.score, ply);
tt_eval = td.eval;
tt_bound = td.bound;
tt_pv = td.pv;
if (!is_pv && tt_depth > depth - (tt_score <= beta ? 1 : 0)) {
if (tt_bound == BOUND_EXACT) return tt_score;
if (tt_bound == BOUND_LOWER && tt_score >= beta) return tt_score;
if (tt_bound == BOUND_UPPER && tt_score <= alpha) return tt_score;
}
}
}
ss[ply].tt_pv = is_pv || tt_pv;
ss[ply].tt_capture = tt_hit && hm != Move::NO_MOVE && b.isCapture(hm);
const Bitboard threat = threat_mask(b);
if (tt_hit && tt_score >= beta && hm != Move::NO_MOVE && !b.isCapture(hm) && !in_check) {
int bonus = history_bonus(depth) / 2;
main_hist_update(ts, b, hm, threat, bonus);
}
int raw_eval;
if (in_check) {
raw_eval = _qg;
} else if (tt_hit && tt_eval != _qg) {
raw_eval = tt_eval;
} else {
raw_eval = evaluate_tree(b, ts);
if (tt_ok) tt_store(key, Move::NO_MOVE, 0, raw_eval, 0, BOUND_NONE, ss[ply].tt_pv, ply);
}
ss[ply].raw_eval = raw_eval;
const int corr = (!in_check && _q1j.loaded) ? get_correction(ts, b, ss, ply) : 0;
const int static_eval = in_check ? _qg : raw_eval + corr;
ss[ply].static_eval = static_eval;
int eval_p = static_eval;
if (tt_hit) {
if (tt_bound == BOUND_EXACT) eval_p = tt_score;
else if (tt_bound == BOUND_LOWER && tt_score > static_eval) eval_p = tt_score;
else if (tt_bound == BOUND_UPPER && tt_score < static_eval) eval_p = tt_score;
}
bool improving = false;
if (!in_check) {
if (ply >= 2 && !ss[ply - 2].in_check && ss[ply - 2].static_eval != _qg)
improving = static_eval > ss[ply - 2].static_eval;
else if (ply >= 4 && ss[ply - 4].static_eval != _qg)
improving = static_eval > ss[ply - 4].static_eval;
}
const bool opp_worsening = ply >= 1 && ss[ply - 1].static_eval != _qg &&
static_eval + ss[ply - 1].static_eval > 0;
if (ply >= 2 && ss[ply - 1].reduction >= 3 && !in_check &&
ss[ply - 1].static_eval != _qg && static_eval + ss[ply - 1].static_eval < 0)
depth += 1;
else if (ply >= 2 && ss[ply - 1].reduction >= 2 && depth >= 2 && !in_check && opp_worsening)
depth -= 1;
if (ply >= 1 && !in_check && !ss[ply - 1].in_check && ss[ply - 1].static_eval != _qg &&
ss[ply - 1].move != Move::NO_MOVE && !ss[ply - 1].was_capture &&
ss[ply - 1].moved_pt != PieceType::NONE) {
int bonus = std::clamp(-13 * (ss[ply - 1].static_eval + static_eval), -1000, 1000);
Move pm = ss[ply - 1].move;
int c2 = 1 - (int)b.sideToMove();
int f2 = pm.from().index(), t2 = pm.to().index();
const Bitboard& thr = ss[ply - 1].threat;
bool ft = (thr.getBits() >> f2) & 1, tt2 = (thr.getBits() >> t2) & 1;
add_hist(ts.mainHist[c2][ft][tt2][f2][t2], bonus);
add_hist(ts.pieceToHist[c2][ft][tt2][(int)ss[ply - 1].moved_pt][t2], bonus);
}
const bool prune_ok = !is_pv && !in_check && excluded == Move::NO_MOVE && !is_root;
const bool has_np_material = b.hasNonPawnMaterial(b.sideToMove());
const int complexity = std::abs(corr);
bool opp_easy = false;
if (prune_ok) {
const Color us = b.sideToMove(), them = ~us;
const Bitboard our_nob = b.pieces(PieceType::KNIGHT, us) | b.pieces(PieceType::BISHOP, us)
| b.pieces(PieceType::ROOK, us) | b.pieces(PieceType::QUEEN, us);
Bitboard pa;
for (Bitboard p = b.pieces(PieceType::PAWN, them); p;)
pa |= attacks::pawn(them, Square(p.pop()));
if (pa & our_nob) opp_easy = true;
else {
const Bitboard our_heavy = b.pieces(PieceType::ROOK, us) | b.pieces(PieceType::QUEEN, us);
Bitboard na;
for (Bitboard n = b.pieces(PieceType::KNIGHT, them); n;)
na |= attacks::knight(Square(n.pop()));
if (na & our_heavy) opp_easy = true;
else {
Bitboard ba;
for (Bitboard bi = b.pieces(PieceType::BISHOP, them); bi;)
ba |= attacks::bishop(Square(bi.pop()), b.occ());
if (ba & our_heavy) opp_easy = true;
else if (b.pieces(PieceType::QUEEN, us)) {
Bitboard ra;
for (Bitboard r = b.pieces(PieceType::ROOK, them); r;)
ra |= attacks::rook(Square(r.pop()), b.occ());
if (ra & b.pieces(PieceType::QUEEN, us)) opp_easy = true;
}
}
}
}
const bool improving_rfp = improving && !opp_easy;
if (prune_ok && depth <= 5 && eval_p < alpha - (_q38 + _q39 * depth * depth)) {
int r = qsearch(b, alpha, alpha + 1, ply, ts, ss);
if (r <= alpha) return r;
}
if (prune_ok && depth <= _q3d && std::abs(beta) < _qc &&
!ss[ply].tt_capture && eval_p >= beta &&
eval_p - (_q3c * depth - _q3b * improving_rfp)
- complexity / _q3a >= beta) {
return (eval_p + beta) / 2;
}
if (depth >= _q24 && hm == Move::NO_MOVE && (is_pv || cut_node) &&
excluded == Move::NO_MOVE)
depth -= 1;
if (prune_ok && depth >= _q36 && std::abs(beta) < _qc &&
(!tt_hit || tt_score >= beta + _q35 - _q34 * improving ||
tt_depth < depth - 3)) {
const int beta_cut = beta + _q35 - _q34 * improving;
const int see_thr = beta_cut - static_eval;
SeePins ppins;
compute_pins(b, ppins.blockers, ppins.pinners);
Movelist caps;
movegen::legalmoves<movegen::MoveGenType::CAPTURE>(caps, b);
for (const Move& m : caps) {
if (!see_ge(b, m, see_thr, &ppins)) continue;
ss[ply].move = m;
ss[ply].moved_pt = b.at<PieceType>(m.from());
ss[ply].to = m.to().index();
ss[ply].was_capture = true;
tt_prefetch(b.zobristAfter(m));
do_move(b, m, ts);
int v = -qsearch(b, -beta_cut, -beta_cut + 1, ply + 1, ts, ss);
if (v >= beta_cut && depth > _q33)
v = -negamax(b, depth - _q33, -beta_cut, -beta_cut + 1, ply + 1,
true, !cut_node, ts, ss);
undo_move(b, m, ts);
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (v >= beta_cut) {
if (tt_ok)
tt_store(key, m, v - (beta_cut - beta), raw_eval, depth - 3, BOUND_LOWER,
ss[ply].tt_pv, ply);
return v - (beta_cut - beta);
}
}
}
if (prune_ok && null_ok && depth >= 3 && has_np_material && ply >= ts.nmp_min_ply &&
eval_p >= beta - (_q2w * depth - _q2v) &&
!opp_easy &&
!(tt_hit && tt_bound == BOUND_LOWER && tt_depth >= depth - 3 &&
tt_score >= beta + _q2x)) {
int R = _q2r + depth / _q2s
+ std::min((eval_p - beta) / _q2u, _q2t)
+ (cut_node ? 1 : 0);
R = std::clamp(R, 1, depth - 1);
ss[ply].move = Move::NO_MOVE; ss[ply].moved_pt = PieceType::NONE; ss[ply].to = -1;
do_null_move(b, ts);
int ns = -negamax(b, depth - 1 - R, -beta, -beta + 1, ply + 1, false, !cut_node, ts, ss);
undo_null_move(b, ts);
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (ns >= beta) {
if (is_win(ns)) ns = beta;
if (ts.nmp_min_ply != 0 || depth < _q2y) return ns;
ts.nmp_min_ply = ply + 3 * (depth - R) / 4;
int v = negamax(b, depth - R, beta - 1, beta, ply, false, cut_node, ts, ss);
ts.nmp_min_ply = 0;
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (v >= beta) return ns;
return v;
}
}
int ext = 0;
const bool shuffling = (int)b.halfMoveClock() >= 80;
if (!is_root && excluded == Move::NO_MOVE && hm != Move::NO_MOVE &&
ply < 2 * ts.root_depth &&
depth >= _q3p + (tt_pv ? 0 : 2) &&
std::abs(tt_score) < _qc &&
(tt_bound == BOUND_LOWER || tt_bound == BOUND_EXACT) && tt_depth >= depth - 3) {
int sg_units = _q3k + _q3l * (tt_pv && !is_pv);
if (ply >= ts.root_depth + 8)
sg_units = sg_units * _q3o / 100;
const int sing_beta = tt_score - sg_units * depth / 64;
const int sing_depth = (depth - 1) / 2;
const bool save_tt_pv = ss[ply].tt_pv;
const bool save_tt_capture = ss[ply].tt_capture;
const int save_static = ss[ply].static_eval;
const int save_raw = ss[ply].raw_eval;
int s = negamax(b, sing_depth, sing_beta - 1, sing_beta, ply, null_ok, cut_node, ts, ss, hm);
ss[ply].tt_pv = save_tt_pv;
ss[ply].tt_capture = save_tt_capture;
ss[ply].static_eval = save_static;
ss[ply].raw_eval = save_raw;
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (s < sing_beta) {
if (!shuffling) {
ext = 1;
if (!is_pv && s < sing_beta - _q3n) ext = 2;
if (!is_pv && s < sing_beta - 2 * _q3n - _q3r) ext = 3;
ss[ply].dext = ss[ply - 1].dext + (ext >= 2 ? ext - 1 : 0);
if (ss[ply].dext > _q14) ext = std::min(ext, 1);
if (!b.isCapture(hm))
cont_hist_update(ts, ss, ply, (int)b.at<PieceType>(hm.from()),
hm.to().index(), history_bonus(depth) / 2, true);
}
} else if (s >= beta) {
return s;
} else if (s >= tt_score) {
hm = Move::NO_MOVE;
} else if (tt_score >= beta) {
ext = _q3q;
} else if (cut_node) {
ext = -2;
} else if (tt_score <= alpha) {
ext = -1;
}
}
if (ext == 0 && !is_root && excluded == Move::NO_MOVE && cut_node &&
depth <= 7 && !in_check && !shuffling &&
static_eval != _qg && static_eval <= alpha - _q2n)
ext = 1;
MovePicker mp(b, ts, ss, ply, hm, false, in_check, threat);
int best_score = -_q8;
Move best_move = Move::NO_MOVE;
const int orig_alpha = alpha;
bool raised_alpha = false;
int raises_cnt = 0;
int moves_searched = 0;
Move quiets_tried[64]; int n_quiets = 0;
Move caps_tried[64]; int n_caps = 0;
Move m;
while ((m = mp.next_move()) != Move::NO_MOVE) {
if (m == excluded) continue;
if (is_root && !root_filter_ok(m)) continue;
const bool is_cap = b.isCapture(m);
const bool is_promo = m.typeOf() == Move::PROMOTION;
const bool is_quiet = !is_cap && !is_promo;
const int mt_idx = (m.typeOf() == Move::CASTLING)
? Square::castling_king_square(m.to().index() > m.from().index(),
b.sideToMove()).index()
: m.to().index();
++moves_searched;
ss[ply].move_count = moves_searched;
if (!is_root && best_score > -_qc && has_np_material) {
const int lmr_d = std::max(0, depth - 1 - lmr_q(depth, moves_searched) / 1024);
const bool pre_check = b.givesCheck(m) != CheckType::NO_CHECK;
if (is_quiet && !is_pv && !in_check) {
int h = main_hist(ts, b, m, threat)
+ ts.pawnHist[(int)b.sideToMove()]
[(int)(mix64(pawn_corr_key(b)) >> 54)][(int)b.at<PieceType>(m.from())][mt_idx]
+ cont_hist_sum(ts, ss, ply, (int)b.at<PieceType>(m.from()), mt_idx);
int lmp = (_q27 + depth * depth) / (2 - improving);
if (!pre_check && moves_searched >= lmp && h < 0) { mp.skip_quiets(); continue; }
if (lmr_d < 7 && h < -_q23 * depth) continue;
if (!pre_check && lmr_d <= 10 &&
eval_p + _q19 + _q1c * lmr_d + h / _q1b
+ complexity / _q1a <= alpha) {
best_score = std::max(best_score,
eval_p + _q19 + _q1c * lmr_d);
mp.skip_quiets();
continue;
}
const int see_thr = _q3h * lmr_d * lmr_d * (pre_check ? _q3f : 100) / 100
+ h / _q3g;
if ((threat.getBits() >> mt_idx) & 1) {
if (!see_ge(b, m, see_thr, &mp.pins)) continue;
}
}
if (is_cap) {
PieceType att2 = b.at<PieceType>(m.from());
PieceType vic2 = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: b.at<PieceType>(m.to());
int ch = ts.capHist[(int)att2][mt_idx][(int)vic2];
if (!pre_check && lmr_d <= 8 &&
eval_p + _qq + _qr * lmr_d + _qk[(int)vic2] + ch / 32 <= alpha) {
best_score = std::max(best_score,
eval_p + _qq + _qr * lmr_d + _qk[(int)vic2] + ch / 32);
continue;
}
if (!see_ge(b, m, _q3e * depth + ch / 32, &mp.pins)) continue;
}
if (mp.in_bad_noisy() && !is_pv && !in_check && depth <= _qo &&
eval_p + _qn <= alpha)
continue;
}
ss[ply].move = m;
ss[ply].moved_pt = b.at<PieceType>(m.from());
ss[ply].to = mt_idx;
ss[ply].was_capture = is_cap;
tt_prefetch(b.zobristAfter(m));
do_move(b, m, ts);
const bool gives_check = b.inCheck();
const uint64_t nodes_before = ts.nodes;
int nd = depth - 1 + ((m == hm) ? ext : 0);
int score;
if (moves_searched == 1) {
int r1 = 0;
if (!is_pv && !is_root && !in_check && !gives_check && depth >= 2) {
r1 = lmr_q(depth, 1) / 1024;
if (cut_node) r1 += (_q2c + (hm == Move::NO_MOVE ? _q2d : 0)) / 1024;
if (hm == Move::NO_MOVE && !cut_node) r1 += _q28 / 1024;
if (!improving) r1 += _q2g / 1024;
}
if (r1 > 3 && nd >= 2) {
score = -negamax(b, nd - 1, -beta, -alpha, ply + 1, true, true, ts, ss);
if (score > alpha && !is_pv)
score = -negamax(b, nd, -beta, -alpha, ply + 1, true,
!cut_node, ts, ss);
} else {
score = -negamax(b, nd, -beta, -alpha, ply + 1, true,
is_pv ? false : !cut_node, ts, ss);
}
} else {
int r = 0;
if (depth >= 2) {
r = is_quiet ? lmr_q(depth, moves_searched) : lmr_n(depth, moves_searched);
r += _q2i * (moves_searched - 1);
if (cut_node) r += _q2c + (hm == Move::NO_MOVE ? _q2d : 0);
if (!improving) r += _q2g;
if (is_pv) r -= _q2j;
if (tt_hit && tt_pv && tt_score > alpha && tt_depth >= depth) r -= _q2m;
if (gives_check) r -= _q29;
if (ss[ply].tt_capture && is_quiet) r += _q2l;
if (ss[ply + 1].cutoff_cnt > 2) r += _q2b;
if (is_quiet) {
int h = main_hist(ts, b, m, threat)
+ ts.pawnHist[(int)b.sideToMove()]
[(int)(mix64(pawn_corr_key(b)) >> 54)][(int)ss[ply].moved_pt][mt_idx]
+ cont_hist_sum(ts, ss, ply, (int)ss[ply].moved_pt, mt_idx);
r -= std::clamp(h * 1024 / _q2f, -_q2e, _q2e);
r -= complexity * _q2a / 64;
} else {
PieceType att2 = ss[ply].moved_pt;
PieceType vic2 = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: b.at<PieceType>(mt_idx);
int ch = ts.capHist[(int)att2][mt_idx][(int)vic2];
r -= std::clamp(ch * 1024 / _q2f, -_q2e, _q2e);
}
if ((m == ss[ply].killers[0] || m == ss[ply].killers[1]) && is_quiet) r -= _q2h;
if (hm == Move::NO_MOVE && !cut_node && !is_pv) r += _q28;
if (is_quiet) r += 3 * std::clamp(alpha - eval_p, -64, 96);
r += _q2k * std::min(raises_cnt, 3);
if (is_root) r -= 1024;
r = std::max(0, r);
}
const int ext_allow = (r / 1024 <= -2) ? 2 : 1;
const int d_red = std::clamp(nd - r / 1024, 1, nd + ext_allow);
ss[ply].reduction = nd - d_red;
score = -negamax(b, d_red, -alpha - 1, -alpha, ply + 1, true, true, ts, ss);
if (score > alpha && d_red < nd) {
const bool deeper = score > best_score + _q15 + 2 * nd;
const bool shallower = score < best_score + _q16 * 10;
nd += (deeper ? 1 : 0) - (shallower ? 1 : 0);
nd = std::clamp(nd, 1, depth - 1 + std::max(ext, 0));
score = -negamax(b, nd, -alpha - 1, -alpha, ply + 1, true, !cut_node, ts, ss);
}
if (is_pv && score > alpha && score < beta)
score = -negamax(b, nd, -beta, -alpha, ply + 1, true, false, ts, ss);
}
undo_move(b, m, ts);
if (is_root)
ts.root_nodes[m.from().index()][mt_idx] += ts.nodes - nodes_before;
if (info.stop.load(std::memory_order_relaxed)) return 0;
if (score > best_score) { best_score = score; best_move = m; }
if (score > alpha) {
alpha = score;
raised_alpha = true;
++raises_cnt;
if (is_pv) {
ss[ply].pv[0] = m;
std::memcpy(ss[ply].pv + 1, ss[ply + 1].pv, ss[ply + 1].pv_len * sizeof(Move));
ss[ply].pv_len = ss[ply + 1].pv_len + 1;
}
if (is_root) {
ts.root_best = m;
ts.root_score = score;
ts.root_best_updated = true;
}
}
if (is_quiet) { if (n_quiets < 64) quiets_tried[n_quiets++] = m; }
else { if (n_caps < 64) caps_tried[n_caps++] = m; }
if (alpha >= beta) {
ss[ply].cutoff_cnt++;
int bonus = history_bonus(depth);
if (best_score > beta + 200) bonus = history_bonus(depth + 2);
if (is_quiet) {
main_hist_update(ts, b, m, threat, bonus);
int pidx = (int)(mix64(pawn_corr_key(b)) >> 54);
add_hist(ts.pawnHist[(int)b.sideToMove()][pidx][(int)ss[ply].moved_pt][mt_idx], bonus);
cont_hist_update(ts, ss, ply, (int)ss[ply].moved_pt, mt_idx, bonus, true);
if (ply >= 0) {
if (m != ss[ply].killers[0]) {
ss[ply].killers[1] = ss[ply].killers[0];
ss[ply].killers[0] = m;
}
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
ts.counterMoves[(int)b.sideToMove()][(int)ss[ply - 1].moved_pt][ss[ply - 1].to] = m;
}
const int malus = -history_malus(depth);
for (int k = 0; k < n_quiets; ++k) {
if (quiets_tried[k] == m) continue;
Move q2 = quiets_tried[k];
main_hist_update(ts, b, q2, threat, malus);
int f2 = q2.from().index();
int t2 = (q2.typeOf() == Move::CASTLING)
? Square::castling_king_square(q2.to().index() > q2.from().index(),
b.sideToMove()).index()
: q2.to().index();
int pt2 = (int)b.at<PieceType>(q2.from());
int pi2 = (int)(mix64(pawn_corr_key(b)) >> 54);
add_hist(ts.pawnHist[(int)b.sideToMove()][pi2][pt2][t2], malus);
cont_hist_update(ts, ss, ply, pt2, t2, malus, true);
}
} else {
PieceType att2 = ss[ply].moved_pt;
PieceType vic2 = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: b.at<PieceType>(mt_idx);
add_hist(ts.capHist[(int)att2][mt_idx][(int)vic2], bonus);
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
add_hist(ts.capContHist[(int)ss[ply - 1].moved_pt][ss[ply - 1].to]
[(int)att2][mt_idx], bonus);
}
const int cap_malus = -history_malus(depth);
for (int k = 0; k < n_caps; ++k) {
if (!is_quiet && caps_tried[k] == m) continue;
Move c2 = caps_tried[k];
PieceType att2 = b.at<PieceType>(c2.from());
int t2 = c2.to().index();
PieceType vic2 = (c2.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
: b.at<PieceType>(t2);
add_hist(ts.capHist[(int)att2][t2][(int)vic2], cap_malus);
if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
add_hist(ts.capContHist[(int)ss[ply - 1].moved_pt][ss[ply - 1].to]
[(int)att2][t2], cap_malus);
}
if (!is_decisive(best_score) && !is_decisive(beta))
best_score = (best_score * depth + beta) / (depth + 1);
if (tt_ok)
tt_store(key, best_move, best_score, raw_eval, depth, BOUND_LOWER,
ss[ply].tt_pv, ply);
update_correction(ts, b, ss, ply, best_score, static_eval, depth, orig_alpha, beta,
!is_quiet);
return best_score;
}
}
if (moves_searched == 0) {
if (excluded != Move::NO_MOVE) return alpha;
return in_check ? mated_in(ply) : _q6;
}
if (best_move == Move::NO_MOVE && depth >= _q32 && ply >= 1 &&
ss[ply - 1].moved_pt != PieceType::NONE && !ss[ply - 1].was_capture &&
ss[ply - 1].move != Move::NO_MOVE) {
int16_t* slot = cont_slot(ts, 1, ss, ply - 1, (int)ss[ply - 1].moved_pt, ss[ply - 1].to);
if (slot) add_hist(*slot, history_bonus(depth) / 2);
}
Bound f = raised_alpha ? BOUND_EXACT : BOUND_UPPER;
if (tt_ok)
tt_store(key, best_move, best_score, raw_eval, depth, f, ss[ply].tt_pv, ply);
update_correction(ts, b, ss, ply, best_score, static_eval, depth, orig_alpha, beta,
best_move != Move::NO_MOVE && b.isCapture(best_move));
return best_score;
}
uint64_t _q4c() {
uint64_t n = 0;
for (size_t i = 0; i < _q1t.size(); ++i)
if (_q1t[i]) n += _q1t[i]->nodes;
return n;
}
std::string score_str(int score) {
std::ostringstream oss;
if (is_win(score)) oss << "score mate " << (_qd - score + 1) / 2;
else if (is_loss(score)) oss << "score mate -" << (_qd + score + 1) / 2;
else oss << "score cp " << score;
return oss.str();
}
std::string pv_to_uci(const Move* pv, int len, const Board& root) {
std::string s;
Board b2 = root;
for (int i = 0; i < len && pv[i] != Move::NO_MOVE; ++i) {
Movelist ml;
movegen::legalmoves(ml, b2);
bool ok = false;
for (const auto& mm : ml) if (mm == pv[i]) { ok = true; break; }
if (!ok) break;
s += " " + uci::moveToUci(pv[i]);
b2.makeMove(pv[i]);
if (b2.isRepetition()) break;
}
return s;
}
struct GoParams {
int wtime = 0, btime = 0, winc = 0, binc = 0;
int movetime = 0, movestogo = 0, depth = 0, nodes = 0, mate = 0;
bool infinite = false, ponder = false;
std::vector<Move> searchmoves;
};
static GoParams _q1g;
static int _q1h = 10;
static bool _q1n = false;
static void compute_tm(int& soft, int& hard, const GoParams& g, const Board& b) {
soft = hard = 0;
_q1n = false;
if (g.movetime > 0) {
soft = hard = std::max(1, g.movetime - 2 * _q1h);
return;
}
int myT = (b.sideToMove() == Color::WHITE) ? g.wtime : g.btime;
int myI = (b.sideToMove() == Color::WHITE) ? g.winc : g.binc;
if (myT <= 0) return;
const int mtg = g.movestogo > 0 ? std::min(g.movestogo, 50) : 50;
const int time_left = std::max(1, myT + myI * (mtg - 1) - 2 * _q1h);
const int game_ply = 2 * ((int)b.fullMoveNumber() - 1) + (b.sideToMove() == Color::BLACK ? 1 : 0);
double opt_scale, max_scale;
if (g.movestogo > 0) {
opt_scale = std::min((0.88 + game_ply / 116.4) / mtg, 0.88 * myT / time_left);
max_scale = std::min(_q2o / 100.0, 1.5 + _q2p / 100.0 * mtg);
} else {
opt_scale = std::min(_q46 / 1000.0, 0.20 * myT / time_left);
max_scale = _q43 / 100.0;
}
const double optimum = opt_scale * time_left;
hard = std::min((int)(_q40 / 100.0 * myT), (int)(max_scale * optimum))
- 2 * _q1h;
hard = std::max(hard, 1);
soft = std::clamp((int)optimum, 1, hard);
if (myT < _q48) {
_q1n = true;
hard = std::max(1, std::min(hard, myT / 2 - _q1h));
soft = std::clamp(soft, 1, std::max(1, hard / 3));
}
}
struct ThreadResult {
bool valid = false;
Move move = Move::NO_MOVE;
int score = -_q8;
int depth = 0;
Move pv[_qf];
int pv_len = 0;
};
static std::vector<std::string> _q1v;
static std::vector<Move> _q1q;
inline bool root_filter_ok(const Move& m) {
if (_q1q.empty()) return true;
for (const Move& f : _q1q) if (f == m) return true;
return false;
}
void thread_worker(Board board, int thread_id, int max_depth, ThreadResult* out) {
ThreadState& ts = *_q1t[thread_id];
ts.nodes = 0;
ts.seldepth = 0;
ts.thread_id = thread_id;
ts.nmp_min_ply = 0;
ts.root_best = Move::NO_MOVE;
ts.root_score = -_q8;
ts.root_best_updated = false;
std::memset(ts.root_nodes, 0, sizeof(ts.root_nodes));
ts.age_histories();
for (int i = 0; i < _qf + 8; ++i) {
Stack& st = ts.stacks[i];
st.move = Move::NO_MOVE; st.moved_pt = PieceType::NONE; st.to = -1;
st.static_eval = _qg; st.in_check = false; st.was_capture = false;
st.tt_pv = false; st.tt_capture = false;
st.threat = Bitboard(0ULL);
st.cutoff_cnt = 0; st.reduction = 0; st.dext = 0; st.move_count = 0;
st.excluded = Move::NO_MOVE;
st.killers[0] = st.killers[1] = Move::NO_MOVE;
st.pv_len = 0;
}
Stack* ss = ts.stacks + 8;
if (_q1j.loaded) {
ts.accDepth = 0;
_q1j.refresh(board, ts.accStack[0].acc);
ts.accStack[0].computed = true;
}
Movelist ml;
movegen::legalmoves(ml, board);
if (ml.empty()) { out->valid = false; return; }
Move best_move = ml[0];
if (ml.size() == 1 && info.hard_time > 0) {
out->valid = true; out->move = best_move; out->depth = 0;
out->score = 0; out->pv_len = 1; out->pv[0] = best_move;
return;
}
int prev_score = 0, score2 = 0;
double prev_avg = 0.0;
int iters = 0;
Move prev_best = Move::NO_MOVE;
int best_stable = 0;
int completed_depth = 0;
int last_iter_ms = 1;
for (int depth = 1; depth <= max_depth; ++depth) {
const int ms_before = elapsed_ms();
if (info.hard_time > 0 && ms_before >= info.hard_time) {
info.stop.store(true, std::memory_order_relaxed);
break;
}
if (thread_id != 0 && depth < 20) {
const int sidx = (thread_id - 1) % 20;
if (((depth - _q3s[sidx]) % _q3t[sidx] + _q3t[sidx]) % _q3t[sidx] != 0)
continue;
}
ts.root_depth = depth;
int score = 0;
bool completed = false;
int fhc = 0;
ts.root_best_updated = false;
const bool use_asp = depth >= 4 && std::abs(prev_score) < _qc;
if (use_asp) {
int delta = _ql + (ts.thread_id % 4) +
std::abs(prev_score) * std::abs(prev_score) / 12000;
int a = std::max(prev_score - delta, -_q8);
int bt = std::min(prev_score + delta, _q8);
while (true) {
const int adj_depth = std::max(1, depth - fhc);
score = negamax(board, adj_depth, a, bt, 0, true, false, ts, ss);
if (info.stop.load(std::memory_order_relaxed)) break;
if (score <= a) {
bt = (a + bt) / 2;
a = std::max(score - delta, -_q8);
fhc = 0;
} else if (score >= bt) {
bt = std::min(score + delta, _q8);
++fhc;
} else {
completed = true;
break;
}
delta += delta / _qm;
}
} else {
score = negamax(board, depth, -_q8, _q8, 0, true, false, ts, ss);
if (!info.stop.load(std::memory_order_relaxed)) completed = true;
}
if (!completed) {
if (ts.root_best_updated) {
best_move = ts.root_best;
out->valid = true;
out->move = best_move;
out->score = ts.root_score;
out->depth = depth;
out->pv_len = ss[0].pv_len;
std::memcpy(out->pv, ss[0].pv, ss[0].pv_len * sizeof(Move));
}
break;
}
completed_depth = depth;
best_move = ts.root_best != Move::NO_MOVE ? ts.root_best : best_move;
if (best_move == prev_best) ++best_stable;
else { best_stable = 0; prev_best = best_move; }
out->valid = true;
out->move = best_move;
out->score = score;
out->depth = depth;
out->pv_len = ss[0].pv_len;
std::memcpy(out->pv, ss[0].pv, ss[0].pv_len * sizeof(Move));
score2 = prev_score;
prev_score = score;
prev_avg = (prev_avg * iters + score) / (iters + 1);
++iters;
if (thread_id == 0) {
const int ms_now = elapsed_ms();
const int ms_depth = ms_now - ms_before;
last_iter_ms = std::max(1, ms_depth);
const uint64_t total = _q4c();
const int nps = (ms_now > 0) ? (int)(total * 1000ULL / ms_now) : 0;
std::ostringstream oss;
oss << "info depth " << depth
<< " seldepth " << ts.seldepth
<< " " << score_str(score)
<< " nodes " << total
<< " nps " << nps
<< " hashfull " << _q4h()
<< " time " << ms_now
<< " pv" << pv_to_uci(ss[0].pv, ss[0].pv_len, board)
<< "\n";
std::cout << oss.str();
std::cout.flush();
if (info.hard_time > 0) {
if (info.fixed_movetime) {
if (ms_now >= info.hard_time) {
info.stop.store(true, std::memory_order_relaxed);
break;
}
} else if (info.soft_time > 0) {
if (is_win(score) && completed_depth >= (_qd - score) + 4) {
info.stop.store(true, std::memory_order_relaxed);
break;
}
uint64_t best_nodes = 0, iter_nodes = 0;
for (int f2 = 0; f2 < 64; ++f2)
for (int t2 = 0; t2 < 64; ++t2) {
iter_nodes += ts.root_nodes[f2][t2];
if (f2 == best_move.from().index()) {
int kt = best_move.to().index();
if (best_move.typeOf() == Move::CASTLING)
kt = Square::castling_king_square(kt > f2,
board.sideToMove()).index();
if (t2 == kt) best_nodes += ts.root_nodes[f2][t2];
}
}
const double frac = iter_nodes ? (double)best_nodes / (double)iter_nodes : 0.0;
const double stability = std::clamp(
(_q4a - _q4b * std::min(best_stable, 8)) / 100.0,
0.55, 1.4);
const double falling = std::clamp(
(12 + _q3x / 100.0 * 2.2 * (prev_avg - score)
+ _q3x / 100.0 * 0.9 * (score2 - score)) / 100.0,
_q3z / 100.0, _q3y / 100.0);
const double node_tm = std::clamp(
(_q44 - _q45 * frac) / 100.0, 0.5, 1.5);
const double instability = 1.0 + std::min(
_q3x / 100.0 * std::abs(score2 - score) / (double)_q41,
_q42 / 100.0);
const int target = (int)(info.soft_time * stability * falling * node_tm * instability);
const int limit = std::min(target, info.hard_time.load(std::memory_order_relaxed));
const bool obvious = frac * 100.0 >= _q49
&& ms_now >= info.soft_time / 2;
if (ms_now >= limit || obvious ||
ms_now + 18LL * last_iter_ms / 10 > info.hard_time) {
info.stop.store(true, std::memory_order_relaxed);
break;
}
}
}
}
}
if (thread_id == 0 && out->move == Move::NO_MOVE && best_move != Move::NO_MOVE) {
out->valid = true;
out->move = best_move;
out->pv_len = 1;
out->pv[0] = best_move;
}
}
Move _q30(ThreadResult* res, int n) {
int valid = 0;
for (int i = 0; i < n; ++i) if (res[i].valid) ++valid;
if (valid == 0) return Move::NO_MOVE;
if (valid == 1) {
for (int i = 0; i < n; ++i) if (res[i].valid) return res[i].move;
}
int mate_thread = -1, best_mate = -_q8;
for (int i = 0; i < n; ++i)
if (res[i].valid && is_win(res[i].score) && res[i].score > best_mate) {
best_mate = res[i].score; mate_thread = i;
}
if (mate_thread >= 0) return res[mate_thread].move;
int min_score = _q8;
for (int i = 0; i < n; ++i) if (res[i].valid) min_score = std::min(min_score, res[i].score);
static std::vector<int64_t> votes;
static std::vector<int> maxd;
static std::vector<Move> mv;
votes.assign(1 << 16, 0);
maxd.assign(1 << 16, 0);
mv.assign(1 << 16, Move::NO_MOVE);
for (int i = 0; i < n; ++i) {
if (!res[i].valid) continue;
const uint16_t k = res[i].move.move();
votes[k] += (int64_t)(res[i].score - min_score + _q4p) * res[i].depth;
if (res[i].depth > maxd[k]) { maxd[k] = res[i].depth; mv[k] = res[i].move; }
}
int64_t bestv = -1;
Move best = Move::NO_MOVE;
int bestd = -1;
for (size_t k = 0; k < votes.size(); ++k) {
if (votes[k] == 0) continue;
if (votes[k] > bestv || (votes[k] == bestv && maxd[k] > bestd)) {
bestv = votes[k]; best = mv[k]; bestd = maxd[k];
}
}
if (best == Move::NO_MOVE) {
int bd = -1;
for (int i = 0; i < n; ++i)
if (res[i].valid && res[i].depth > bd) { bd = res[i].depth; best = res[i].move; }
}
return best;
}
Move search(Board& board, int max_depth) {
info.stop.store(false, std::memory_order_relaxed);
info.set_start_now();
_q4g = (uint8_t)((_q4g + 1) & 31);
while ((int)_q1t.size() < _q1m)
_q1t.push_back(std::make_unique<ThreadState>());
std::vector<ThreadResult> res(_q1m);
std::vector<std::thread> threads;
for (int i = 0; i < _q1m; ++i)
threads.emplace_back(thread_worker, board, i, max_depth, &res[i]);
for (auto& t : threads) t.join();
Move best = _q30(res.data(), _q1m);
if (best != Move::NO_MOVE) {
Movelist ml;
movegen::legalmoves(ml, board);
for (const auto& mm : ml) if (mm == best) return best;
}
Movelist ml;
movegen::legalmoves(ml, board);
return ml.empty() ? Move::NO_MOVE : ml[0];
}
uint64_t perft(Board& b, int depth) {
Movelist ml;
movegen::legalmoves(ml, b);
if (depth <= 1) return ml.size();
uint64_t n = 0;
for (const auto& m : ml) {
b.makeMove(m);
n += perft(b, depth - 1);
b.unmakeMove(m);
}
return n;
}
void perft_divide(Board& b, int depth) {
Movelist ml;
movegen::legalmoves(ml, b);
uint64_t total = 0;
for (const auto& m : ml) {
b.makeMove(m);
uint64_t n = (depth <= 1) ? 1 : perft(b, depth - 1);
b.unmakeMove(m);
std::cout << uci::moveToUci(m) << ": " << n << "\n";
total += n;
}
std::cout << "total: " << total << "\n";
}
struct PerftPos { const char* fen; int depth; uint64_t nodes; };
static const PerftPos _qh[] = {
{"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL},
{"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL},
{"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083ULL},
{"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15833292ULL},
{"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL},
{"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL},
{"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888ULL},
{"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133ULL},
{"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467ULL},
{"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072ULL},
{"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711ULL},
{"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206ULL},
{"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476ULL},
{"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001ULL},
{"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658ULL},
{"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342ULL},
{"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683ULL},
{"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217ULL},
{"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584ULL},
{"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527ULL},
{"R6R/3Q4/1Q4Q1/4Q3/2Q4Q/Q4Q2/pp1Q4/kBNN1KB1 w - - 0 1", 3, 19073ULL},
};
int _q2z() {
int fails = 0;
for (const auto& p : _qh) {
Board b;
b.setFen(p.fen);
uint64_t n = perft(b, p.depth);
bool ok = (n == p.nodes);
if (!ok) ++fails;
std::cout << (ok ? "PASS" : "FAIL") << " perft " << p.depth << " = " << n
<< (ok ? "" : " (expected " + std::to_string(p.nodes) + ")")
<< "  [" << p.fen << "]\n";
}
std::cout << (fails == 0 ? "ALL PASSED" : "FAILED") << " (" << fails << " failures)\n";
return fails;
}
static const char* _q0[] = {
"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
"4r1k1/5ppp/1p1p4/2bP4/2P5/8/1P3PPP/2RR1BK1 w - - 0 1",
"2rr2k1/1b1qbp1n/p1np1p2/1p2pPp1/P1B1P2P/1PNB4/1PPQ2P1/2RR2K1 w - g6 0 1",
"r4rk1/pp1nppbp/2p2np1/q7/2BNP2P/2P1B3/PP1Q1PP1/2RR2K1 w - - 0 1",
"r1b1rk11/1pq1bppp/p1np1n2/4p3/3NP3/1BN1B3/PPP1QPPP/R4RK1 w - - 0 1",
"3r2k1/pb1qrpp1/1p2p2p/n2p4/3N2P1/1P1QPN2/PBB3P1/3R2K1 w - - 0 1",
"r2q1rk1/pb1nbppp/1p1ppn2/6B1/2NPP3/1BP1B3/PP1N1PPP/R2Q1RK1 w - - 0 1",
"5rk1/1ppbqpp1/p2p1n2/3P2p1/2P1P1b1/2N1BN2/PPBQ1PPP/3R1RK1 w - - 0 1",
"r1bq1r1k/pp2ppbp/2np2p1/q7/2BNP3/2N1BP2/PPPQ2PP/2KR3R w - - 0 1",
"3q1rk1/1b2bppp/p2p1n2/1p6/3P4/1BP1PN2/PP3PQP/2BR2K1 w - - 0 1",
"2r1b1k1/1p2qpp1/p2p2p1/3Pp2p/1PP1P3/1B4P1/P4QKP/4R3 w - - 0 1",
"r1bqk2r/pp2bppp/2n1pn2/2p5/2P5/2N1PN2/PP2BPPP/R1BQ1RK1 w kq - 0 1",
"r1b2rk1/1pqnbppp/p2ppn2/8/2NBP3/1PN1B3/PBPQ1PPP/2KR3R w - - 0 1",
"r4rk1/1b2bppp/pqnp1n2/1p2p3/3BP3/1BP1BN2/PP1Q1PPP/2KR3R w - - 0 1",
"2r3k1/p4pp1/2p1b2p/1q1p4/3P4/1P2Q2P/PBB2PP1/3R2K1 w - - 0 1",
"8/5pk1/6p1/7p/P4PP1/6PK/8/8 w - - 0 1",
"8/3k4/1p6/p4p2/P4p1p/5K1p/6P1/7b w - - 0 1",
"6k1/6pp/8/2p5/2Pp4/8/KP5P/8 w - - 0 1",
"8/2p2pp1/1p6/1P1p4/3P4/5kp1/5P2/6K1 w - - 0 1",
"5k2/8/2Kp4/3P4/8/8/8/8 w - - 0 1",
"8/8/2k5/8/3PK3/8/8/8 w - - 0 1",
"4k3/4p3/8/8/8/8/4P3/4K3 w - - 0 1",
"8/4k3/8/3p4/8/3K4/4P3/8 w - - 0 1",
"3k4/3p4/8/8/8/8/3P4/3K4 w - - 0 1",
"5k2/5p2/5p2/8/8/5P2/5P2/5K2 w - - 0 1",
"2r3k1/5pp1/1q6/8/3Q4/1P6/P4PPP/3R2K1 w - - 0 1",
"3r1k2/4bppp/3p4/p2Pp3/4P3/1P5P/PB3PP1/3R2K1 w - - 0 1",
"r2q1rk1/1b2bppp/p2p1np1/1p6/3NP3/1BP1B3/PP1Q1PPP/2KR3R w - - 0 1",
"r1bqk2r/2ppbppp/2n2n2/1p2p3/4P3/1N3N2/PPPPBPPP/R1BQK2R w KQkq - 0 1",
"2rr2k1/pb1nbp1p/1pq1p1p1/3pPp2/2PP4/1PB1N1P1/P4P1P/R2QR1K1 w - - 0 1",
"r1bq1rk1/1p2bppp/p1n1pn2/8/2BNP3/2N1B3/PPP1QPPP/2KR3R w - - 0 1",
"1k6/1P6/1K6/8/8/8/8/8 w - - 0 1",
"8/3k4/8/3P4/8/3K4/8/8 w - - 0 1",
"8/8/1k6/3KP3/8/8/8/8 w - - 0 1",
"4r1k1/1p3ppp/p7/1qnP4/2b5/2N5/PP1Q1PPP/2BR1RK1 w - - 0 1",
"r1b2k1r/ppp1bppp/8/1B1Q4/5q2/8/PPPP1PPP/R3R1K1 w - - 0 1",
"2k5/8/8/3pP3/8/8/8/4K3 w - d6 0 1",
"rnbqkbnr/ppp2ppp/8/3pp3/8/2P5/PP1PPPPP/RNBQKBNR w KQkq d6 0 3",
"rnbq1bnr/ppppkppp/8/4p3/4P3/8/PPPPKPPP/RNBQ1BNR w - - 2 3",
"rnbqkbnr/ppp1pppp/8/8/3pP3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 2",
"7r/2p2kpp/8/1p6/1P6/8/2P2KPP/7R w - - 0 1",
};
int _qp(int depth) {
depth = depth > 0 ? depth : 12;
const int n = (int)(sizeof(_q0) / sizeof(_q0[0]));
const int saved_threads = _q1m;
const size_t saved_hash_mb = _q1u * sizeof(TTCluster) / (1024 * 1024);
_q1m = 1;
_q4k(16);
while ((int)_q1t.size() < 1) _q1t.push_back(std::make_unique<ThreadState>());
Board board;
info.stop.store(false, std::memory_order_relaxed);
info.hard_time = 0;
info.soft_time = 0;
info.node_limit.store(0);
info.infinite = false;
info.fixed_movetime = false;
info.set_start_now();
_q1q.clear();
uint64_t total_nodes_sum = 0;
ThreadResult res;
for (int i = 0; i < n; ++i) {
board.setFen(_q0[i]);
bool ok = true;
for (Color c : {Color::WHITE, Color::BLACK})
if (board.pieces(PieceType::KING, c).count() != 1) ok = false;
Movelist bml;
movegen::legalmoves(bml, board);
if (!ok || bml.empty()) {
std::cout << "info string bench FEN " << i << " invalid: " << _q0[i] << "\n";
continue;
}
_q4e();
_q4g = (uint8_t)((_q4g + 1) & 31);
for (int t = 0; t < (int)_q1t.size(); ++t) _q1t[t]->clear_history();
res = ThreadResult{};
thread_worker(board, 0, depth, &res);
total_nodes_sum += _q4c();
info.stop.store(false, std::memory_order_relaxed);
}
const int ms = std::max(1, elapsed_ms());
std::cout << total_nodes_sum << " nodes " << (uint64_t)(total_nodes_sum * 1000ULL / ms) << " nps\n";
_q1m = saved_threads;
_q4k(saved_hash_mb);
return 0;
}
int _q3i(int games, int depth) {
for (int g = 0; g < games; ++g) {
Board board;
board.setFen(constants::STARTPOS);
Movelist ml;
movegen::legalmoves(ml, board);
int rplies = 2 + (g % 3);
for (int p = 0; p < rplies && !ml.empty(); ++p) {
board.makeMove(ml[(g * 7 + p * 13) % (int)ml.size()]);
movegen::legalmoves(ml, board);
}
int plies = 0;
while (!ml.empty() && plies < 400) {
info.stop.store(false, std::memory_order_relaxed);
info.hard_time = 0; info.soft_time = 0; info.node_limit.store(0);
info.fixed_movetime = false; info.infinite = false;
info.set_start_now();
Move m = search(board, depth);
if (m == Move::NO_MOVE) break;
board.makeMove(m);
movegen::legalmoves(ml, board);
++plies;
}
std::cout << "game " << g << " finished after " << plies << " plies, final fen "
<< board.getFen() << "\n";
std::cout.flush();
}
std::cout << "selfplay done: " << games << " games, 0 crashes\n";
return 0;
}
void _q3u() {
for (const auto& t : _q4m())
std::cout << t.name << ",int," << *t.value << "," << t.lo << "," << t.hi
<< "," << t.step << ",0.002\n";
}
#ifdef _WIN32
#include <windows.h>
#endif
static const char* _q5 = "acherontia.nnue";
static bool _q1i = false;
static std::string _q18() {
#ifdef _WIN32
char buf[MAX_PATH * 2];
DWORD n = GetModuleFileNameA(nullptr, buf, (DWORD)sizeof(buf));
if (n == 0 || n >= sizeof(buf)) return "";
std::string p(buf, n);
size_t k = p.find_last_of("\\/");
return k == std::string::npos ? "" : p.substr(0, k + 1);
#else
char buf[4096];
ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
if (n <= 0) return "";
std::string p(buf, (size_t)n);
size_t k = p.find_last_of('/');
return k == std::string::npos ? "" : p.substr(0, k + 1);
#endif
}
static bool file_exists(const std::string& p) {
std::ifstream f(p, std::ios::binary);
return (bool)f;
}
static bool load_net_quiet(const std::string& path) {
return file_exists(path) && _q1j.load(path);
}
static std::string scan_dir_for_net(const std::string& dir) {
#ifdef _WIN32
std::string pattern = dir + "\\*.nnue";
WIN32_FIND_DATAA fd;
HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
if (h == INVALID_HANDLE_VALUE) return "";
std::string plain, synth;
do {
if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
std::string name(fd.cFileName);
#else
DIR* d = opendir(dir.c_str());
if (!d) return "";
std::string plain, synth;
while (struct dirent* e = readdir(d)) {
std::string name(e->d_name);
if (name == "." || name == "..") continue;
#endif
if (name.size() < 6 || name.compare(name.size() - 5, 5, ".nnue") != 0) continue;
std::string p = (dir == ".") ? name : dir + (dir.back() == '/' || dir.back() == '\\' ? "" : "/") + name;
if (name.compare(0, 9, "nn-synth-") == 0) { if (synth.empty()) synth = p; }
else if (plain.empty()) plain = p;
#ifdef _WIN32
} while (FindNextFileA(h, &fd));
FindClose(h);
#else
}
closedir(d);
#endif
if (!plain.empty() && load_net_quiet(plain)) return plain;
if (!synth.empty() && load_net_quiet(synth)) return synth;
return "";
}
static std::string load_net_with_fallback(const std::string& requested) {
std::string path = requested;
if (!file_exists(path)) {
std::string alt = _q18() + requested;
if (file_exists(alt)) path = alt;
else {
alt = _q18() + _q5;
if (file_exists(alt)) path = alt;
}
}
if (load_net_quiet(path)) return path;
std::string found = scan_dir_for_net(".");
if (found.empty()) found = scan_dir_for_net(_q18());
return found;
}
static void _q17() {
if (_q1i) return;
_q1i = true;
const auto t0 = std::chrono::steady_clock::now();
const std::string loaded = load_net_with_fallback(_q1k);
const int ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
std::chrono::steady_clock::now() - t0).count();
if (!loaded.empty()) {
std::cout << "info string loaded NNUE from " << loaded << "\n";
std::cout << "info string net load ms=" << ms << "\n";
} else {
std::cout << "info string no NNUE loaded (tried " << _q1k
<< " + directory scan): " << _q1j.lastError
<< " - falling back to material eval\n";
}
std::cout.flush();
}
static const char* _q3j() {
#if defined(__AVX512VNNI__)
return "avx512-vnni";
#elif defined(__AVX512F__)
return "avx512";
#elif defined(__AVXVNNI__)
return "avx-vnni";
#elif defined(__AVX2__)
return "avx2";
#elif defined(__SSE2__)
return "sse2";
#else
return "scalar";
#endif
}
void _q31() {
std::cout << "id name Agnes\n";
std::cout << "id author Ranak Chongtham\n";
std::cout << "option name EvalFile type string default " << _q5 << "\n";
std::cout << "option name Hash type spin default 256 min 1 max 65536\n";
std::cout << "option name Threads type spin default 1 min 1 max 256\n";
std::cout << "option name Move Overhead type spin default 10 min 1 max 100\n";
std::cout << "option name NNUEScale type spin default 40 min 1 max 400\n";
std::cout << "option name Clear Hash type button\n";
std::cout << "option name SyzygyPath type string default <empty>\n";
std::cout << "option name UCI_Chess960 type check default false\n";
#if defined(TUNING)
for (const auto& t : _q4m())
std::cout << "option name " << t.name << " type spin default " << t.def
<< " min " << t.lo << " max " << t.hi << "\n";
#endif
std::cout << "uciok\n";
}
void _q4o() {
Board board;
board.setFen(constants::STARTPOS);
auto safe_stoi = [](const std::string& s, int def) -> int {
if (s.empty()) return def;
char* end = nullptr;
long v = std::strtol(s.c_str(), &end, 10);
if (end == s.c_str()) return def;
return (int)std::clamp(v, (long)-2147483647L - 1, 2147483647L);
};
std::string line;
while (std::getline(std::cin, line)) {
if (line.empty()) continue;
std::istringstream iss(line);
std::string token;
iss >> token;
if (token == "uci") {
std::cout << "info string simd=" << _q3j() << "\n";
_q31();
} else if (token == "isready") {
if (!_q1r.load(std::memory_order_relaxed)) _q17();
std::cout << "readyok\n";
} else if (token == "ucinewgame") {
_q4q();
_q17();
board.setFen(constants::STARTPOS);
_q4e();
for (auto& t : _q1t) if (t) t->clear_history();
_q1d.clear();
} else if (token == "setoption") {
_q4q();
std::string word, name, value;
iss >> word;
while (iss >> word && word != "value") {
if (!name.empty()) name += " ";
name += word;
}
std::getline(iss, value);
if (!value.empty() && value.front() == ' ') value.erase(0, 1);
if (name == "EvalFile" && !value.empty()) {
while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
_q1k = value;
_q1i = true;
const std::string loaded = load_net_with_fallback(value);
if (!loaded.empty())
std::cout << "info string loaded NNUE from " << loaded << "\n";
else
std::cout << "info string failed to load NNUE from " << value
<< " (" << _q1j.lastError << ")\n";
} else if (name == "Threads") {
_q1m = std::clamp(safe_stoi(value, 1), 1, 256);
} else if (name == "Hash") {
_q4k((size_t)std::clamp(safe_stoi(value, 256), 1, 65536));
} else if (name == "Move Overhead") {
_q1h = std::clamp(safe_stoi(value, _q1h), 1, 100);
} else if (name == "NNUEScale" && !value.empty()) {
_q1l = std::clamp(safe_stoi(value, _q1l), 1, 400);
} else if (name == "Clear Hash") {
_q4e();
} else if (name == "UCI_Chess960") {
board.set960(value == "true");
std::string fen = board.getFen();
board.setFen(fen);
} else if (name == "SyzygyPath") {
_q4q();
if (!value.empty() && value != "<empty>" && value != "(none)") {
if (tb_init(value.c_str()) && syzygy::ready())
std::cout << "info string Syzygy " << syzygy::largest()
<< "-man tablebases loaded from " << value << "\n";
else
std::cout << "info string SyzygyPath: no tablebases found at "
<< value << "\n";
} else {
tb_free();
std::cout << "info string Syzygy tablebases unloaded\n";
}
std::cout.flush();
}
#if defined(TUNING)
else {
for (auto& t : _q4m())
if (name == t.name && !value.empty()) *t.value = std::clamp(safe_stoi(value, *t.value), t.lo, t.hi);
}
#endif
} else if (token == "position") {
_q4q();
std::string p;
iss >> p;
if (p == "startpos") { board.setFen(constants::STARTPOS); iss >> p; }
else if (p == "fen") {
std::string fen;
int ntok = 0;
bool saw_moves = false;
for (int i = 0; i < 6; ++i) {
std::string f;
if (!(iss >> f)) break;
if (f == "moves") { saw_moves = true; break; }
if (i > 0) fen += " ";
fen += f;
++ntok;
}
if (ntok == 3) fen += " -";
if (ntok <= 4) fen += " 0";
if (ntok <= 5) fen += " 1";
board.setFen(fen);
if (!saw_moves) iss >> p;
}
while (iss >> p) {
Move m = uci::uciToMove(board, p);
if (m == Move::NO_MOVE) break;
board.makeMove(m);
}
} else if (token == "go") {
_q4q();
_q17();
GoParams g;
int depth = -1;
std::string sm;
while (iss >> token) {
if (token == "depth") iss >> depth;
else if (token == "movetime") iss >> g.movetime;
else if (token == "wtime") iss >> g.wtime;
else if (token == "btime") iss >> g.btime;
else if (token == "winc") iss >> g.winc;
else if (token == "binc") iss >> g.binc;
else if (token == "movestogo") iss >> g.movestogo;
else if (token == "nodes") iss >> g.nodes;
else if (token == "mate") iss >> g.mate;
else if (token == "infinite") g.infinite = true;
else if (token == "ponder") g.ponder = true;
else if (token == "searchmoves") { }
else if (token.size() >= 4 && token[0] >= 'a' && token[0] <= 'h') {
Move sm = uci::uciToMove(board, token);
if (sm != Move::NO_MOVE) g.searchmoves.push_back(sm);
}
}
_q1g = g;
info.infinite = g.infinite || g.ponder;
info.node_limit.store(g.nodes > 0 ? (uint64_t)g.nodes : 0);
info.set_start_now();
_q1q = g.searchmoves;
int soft = 0, hard = 0;
if (g.infinite || g.ponder) {
depth = depth > 0 ? depth : _qf - 4;
} else {
compute_tm(soft, hard, g, board);
if (soft > 0 && depth < 0) depth = _qf - 4;
if (depth < 0) depth = (g.nodes > 0 || g.mate > 0) ? _qf - 4 : 8;
}
info.soft_time = soft;
info.hard_time = hard;
info.fixed_movetime = (g.movetime > 0);
if (_q1n && !info.fixed_movetime && depth > _q47)
depth = _q47;
int d = depth < 0 ? _qf - 4 : depth;
if (g.searchmoves.empty()) {
syzygy::RootProbe rp = syzygy::probe_root(board, board.isRepetition());
if (rp.move != Move::NO_MOVE) {
std::ostringstream oss;
oss << "info string syzygy root probe (largest=" << syzygy::largest()
<< ")\n";
oss << "info depth 1 seldepth 1 score cp " << rp.cp_score
<< " nodes 0 nps 0 tbhits 1 pv " << uci::moveToUci(rp.move) << "\n";
oss << "bestmove " << uci::moveToUci(rp.move) << "\n";
std::cout << oss.str();
std::cout.flush();
continue;
}
}
_q1r.store(true, std::memory_order_relaxed);
_q1s = std::thread([board, d]() mutable {
Move best = search(board, d);
std::ostringstream oss;
oss << "info string tm soft=" << info.soft_time
<< " hard=" << info.hard_time
<< " used=" << elapsed_ms() << "\n";
if (best == Move::NO_MOVE) oss << "bestmove (none)\n";
else oss << "bestmove " << uci::moveToUci(best) << "\n";
std::cout << oss.str();
std::cout.flush();
_q1r.store(false, std::memory_order_relaxed);
});
} else if (token == "ponderhit") {
info.set_start_now();
int soft = 0, hard = 0;
compute_tm(soft, hard, _q1g, board);
info.soft_time = soft;
info.hard_time = hard;
info.fixed_movetime = (_q1g.movetime > 0);
info.infinite = false;
} else if (token == "stop") {
info.stop.store(true, std::memory_order_relaxed);
} else if (token == "quit") {
_q4q();
break;
} else if (token == "bench") {
int d = 0;
iss >> d;
_qp(d);
} else if (token == "perft") {
_q4q();
int d = 0;
iss >> d;
std::string rest;
iss >> rest;
Board pb = board;
if (!rest.empty()) pb.setFen(line.substr(line.find("perft") + 7));
std::cout << perft(pb, d) << "\n";
} else if (token == "divide") {
_q4q();
int d = 1;
iss >> d;
perft_divide(board, d);
} else if (token == "perftsuite") {
_q2z();
} else if (token == "selfplay") {
int g2 = 3, d2 = 6;
iss >> g2 >> d2;
_q3i(g2, d2);
} else if (token == "spsa_dump" || token == "tune_dump") {
_q3u();
} else if (token == "eval") {
_q17();
std::cout << "eval " << evaluate(board) << "\n";
} else if (token == "d") {
_q17();
std::cout << board << "\n";
std::cout << "FEN: " << board.getFen() << "\n";
std::cout << "Hash: " << board.hash() << "\n";
std::cout << "Eval: " << evaluate(board)
<< (_q1j.loaded ? "  [NNUE]" : std::string("  [MATERIAL-ONLY]"))
<< "\n";
}
std::cout.flush();
}
_q4q();
}
int main(int argc, char** argv) {
std::ios::sync_with_stdio(false);
std::cin.tie(nullptr);
_q26();
_q25();
_q4k(256);
_q1t.push_back(std::make_unique<ThreadState>());
_q1t[0]->clear_history();
if (argc >= 2) {
std::string a = argv[1];
if (a == "bench") { _q17(); return _qp(argc >= 3 ? std::atoi(argv[2]) : 0); }
if (a == "perftsuite") return _q2z();
}
_q4o();
_q4f();
return 0;
}
