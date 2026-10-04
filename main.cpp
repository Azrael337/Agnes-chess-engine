#include "chess.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <emmintrin.h>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

using namespace chess;
#include "nnue.h"
//use this search's nnue.h implementation as this is the one working

constexpr int INF         = 1'000'000;
constexpr int MATE_SCORE  = 900'000;
constexpr int DRAW_SCORE  = 0;
constexpr int MAX_DEPTH   = 64;
constexpr int MAX_PLY     = 128;
constexpr int MAX_THREADS = 8;
constexpr int MATE_BOUND  = MATE_SCORE - MAX_PLY;
constexpr int NO_EVAL     = -2'000'000;
constexpr int CORR_HIST_SIZE = 16384;

static const int PIECE_VAL_MG[7] = { 100, 320, 330, 500, 900, 20000, 0 };
static const int PIECE_VAL_EG[7] = { 120, 300, 320, 550, 950, 20000, 0 };

static int LMR_TABLE[MAX_DEPTH][64];
void init_lmr() {
    for (int d = 0; d < MAX_DEPTH; ++d)
        for (int m = 0; m < 64; ++m) {
            if (d <= 0 || m <= 0) { LMR_TABLE[d][m] = 0; continue; }
            LMR_TABLE[d][m] = (int)(0.5 + std::log((double)d) * std::log((double)m) / 2.25);
        }
}

// ─────────────────────────────────────────────
//  Transposition Table — B1: 16-byte torn-read-safe entries
// ─────────────────────────────────────────────
enum TTFlag : uint8_t { TT_NONE = 0, TT_EXACT, TT_ALPHA, TT_BETA };

struct alignas(16) TTEntry {
    uint32_t key32;
    int32_t  score;
    uint32_t move_data;
    int16_t  eval_i16;                   // B1: narrowed from int32 (clamped on store)
    uint8_t  depth;
    uint8_t  gen_flag;
    TTFlag get_flag() const { return static_cast<TTFlag>(gen_flag & 0x0F); }
    uint8_t get_gen() const { return gen_flag >> 4; }
    Move get_best_move() const {
        Move m; std::memcpy(&m, &move_data, sizeof(Move)); return m;
    }
};
static_assert(sizeof(TTEntry) == 16, "TTEntry must pack to exactly 16 bytes");

static inline uint32_t move_to_u32(const Move& m) { uint32_t r; std::memcpy(&r, &m, 4); return r; }
static inline Move     u32_to_move(uint32_t r)   { Move m;    std::memcpy(&m, &r, 4); return m; }

// B1: single aligned 16-byte copy in each direction. On x86-64 (all supported
// builds use __SSE2__) movaps/movdqa give practical single-copy atomicity, so a
// concurrent reader never sees a half-updated entry.
static inline void tt_write(TTEntry& dst, const TTEntry& src) {
#ifdef __SSE2__
    _mm_store_si128(reinterpret_cast<__m128i*>(&dst),
                    _mm_load_si128(reinterpret_cast<const __m128i*>(&src)));
#else
    dst = src;
#endif
}
static inline TTEntry tt_read(const TTEntry& src) {
    TTEntry out;
#ifdef __SSE2__
    _mm_store_si128(reinterpret_cast<__m128i*>(&out),
                    _mm_load_si128(reinterpret_cast<const __m128i*>(&src)));
#else
    out = src;
#endif
    return out;
}

static TTEntry* tt = nullptr;
static size_t   g_tt_size = 1 << 21;
static size_t   g_tt_mask = g_tt_size - 1;
static uint8_t  tt_generation = 0;

inline int score_to_tt(int s, int ply) {
    if (s >=  MATE_BOUND) return s + ply;
    if (s <= -MATE_BOUND) return s - ply;
    return s;
}
inline int score_from_tt(int s, int ply) {
    if (s >=  MATE_BOUND) return s - ply;
    if (s <= -MATE_BOUND) return s + ply;
    return s;
}

void tt_clear() {
    if (!tt) return;
    std::memset(tt, 0, g_tt_size * sizeof(TTEntry));
    tt_generation = 0;
}
void tt_resize(size_t mb) {
    delete[] tt;
    size_t target = std::max<size_t>(mb, 1) * 1024 * 1024 / sizeof(TTEntry);
    size_t s = 1;
    while (s * 2 <= target) s <<= 1;
    g_tt_size = s; g_tt_mask = s - 1;
    tt = new TTEntry[g_tt_size];         // alignas(16) type -> C++17 aligned new
    tt_clear();
}
// B1: probe returns a private copy (never a pointer into shared memory).
bool tt_probe(uint64_t key, TTEntry& out) {
    out = tt_read(tt[key & g_tt_mask]);
    return out.key32 == static_cast<uint32_t>(key >> 32) && out.get_flag() != TT_NONE;
}
void tt_store(uint64_t key, int score, int static_eval, Move best, int depth, TTFlag flag, int ply) {
    TTEntry& slot = tt[key & g_tt_mask];
    TTEntry cur = tt_read(slot);
    bool replace = (cur.get_flag() == TT_NONE)
                || (cur.get_gen() != tt_generation)
                || (depth + 3 >= (int)cur.depth);
    if (!replace) return;
    TTEntry n;
    n.key32    = static_cast<uint32_t>(key >> 32);
    n.score    = score_to_tt(score, ply);
    n.move_data = move_to_u32(best);
    n.eval_i16 = static_cast<int16_t>(std::clamp(static_eval, -32767, 32767));
    n.depth    = static_cast<uint8_t>(std::min(depth, 255));
    n.gen_flag = static_cast<uint8_t>((tt_generation << 4) | static_cast<uint8_t>(flag));
    tt_write(slot, n);
}

// ─────────────────────────────────────────────
//  Shared best move across threads.
// ─────────────────────────────────────────────
static_assert(sizeof(Move) == sizeof(uint32_t), "Move must be 4 bytes for lock-free sharing");
static std::atomic<uint32_t> g_shared_best{0};
static std::atomic<int>      g_shared_depth{0};

// ─────────────────────────────────────────────
//  Thread State
// ─────────────────────────────────────────────
struct ThreadState {
    Move killers[MAX_PLY][2];
    Move counterMoves[2][6][64];
    int  history[2][64][64];
    int  contHist[4][6][64][6][64];
    int  capContHist[6][64][6][64];      // B2: [prev_pt][prev_to][moved_pt][to]
    int  capHist[7][64][7];
    int  pawn_corrHist[CORR_HIST_SIZE];
    int  eval_stack[512];
    PieceType mv_pt[MAX_PLY + 2];
    int       mv_to[MAX_PLY + 2];
    int  double_ext;
    uint64_t cutoffs;
    uint64_t cutoffs_first;
    nnue::Accumulator accStack[MAX_PLY + 64];
    int  accDepth;
    uint64_t nodes;
    int  seldepth;
    int  thread_id;

    void clear_history() {
        std::memset(killers,       0, sizeof(killers));
        std::memset(counterMoves,  0, sizeof(counterMoves));
        std::memset(history,       0, sizeof(history));
        std::memset(contHist,      0, sizeof(contHist));
        std::memset(capContHist,   0, sizeof(capContHist));   // B2
        std::memset(capHist,       0, sizeof(capHist));
        std::memset(pawn_corrHist, 0, sizeof(pawn_corrHist));
    }
};

struct SearchInfo {
    std::atomic<bool> stop{false};
    int  soft_time = 0;
    int  hard_time = 0;
    bool fixed_movetime = false;
    std::chrono::steady_clock::time_point start_time;
    bool time_up() const {
        if (hard_time <= 0) return false;
        auto e = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start_time).count();
        return e >= hard_time;
    }
};

static SearchInfo info;
static ThreadState* thread_states = nullptr;
static int num_threads = 1;

static nnue::Network g_nnue;
static std::string   g_nnue_path = "88mm.nnue";

static std::thread g_search_thread;
static int g_pending_soft = 0, g_pending_hard = 0;

void wait_for_search_end() {
    info.stop.store(true, std::memory_order_relaxed);
    if (g_search_thread.joinable()) g_search_thread.join();
}

// ─────────────────────────────────────────────
//  Correction History
// ─────────────────────────────────────────────
inline uint64_t pawn_corr_key(const Board& b) {
    return (b.pieces(PieceType::PAWN, Color::WHITE).getBits() * 0x9E3779B97F4A7C15ULL)
         ^ (b.pieces(PieceType::PAWN, Color::BLACK).getBits() * 0xC2B2AE3D27D4EB4FULL);
}
inline void update_pawn_corr(ThreadState& ts, const Board& b, int diff, int depth) {
    int idx = (int)(pawn_corr_key(b) & (CORR_HIST_SIZE - 1));
    int w = std::min(depth, 16);
    int target = std::max(-64, std::min(64, diff / 4));
    ts.pawn_corrHist[idx] = (ts.pawn_corrHist[idx] * (32 - w) + target * w) / 32;
}
inline int get_pawn_corr(ThreadState& ts, const Board& b) {
    return ts.pawn_corrHist[pawn_corr_key(b) & (CORR_HIST_SIZE - 1)];
}

// ─────────────────────────────────────────────
//  NNUE / move wrappers
// ─────────────────────────────────────────────
inline void do_move(Board& b, const Move& m, ThreadState& ts) {
    if (g_nnue.loaded) {
        ts.accDepth++;
        ts.accStack[ts.accDepth] = ts.accStack[ts.accDepth - 1];
        if (m.typeOf() == Move::CASTLING) { b.makeMove(m); g_nnue.refresh(b, ts.accStack[ts.accDepth]); }
        else { g_nnue.update_for_move(ts.accStack[ts.accDepth], b, m); b.makeMove(m); }
        return;
    }
    b.makeMove(m);
}
inline void undo_move(Board& b, const Move& m, ThreadState& ts) { b.unmakeMove(m); if (g_nnue.loaded) ts.accDepth--; }
inline void do_null_move(Board& b, ThreadState& ts) {
    if (g_nnue.loaded) {
        ts.accDepth++;
        ts.accStack[ts.accDepth] = ts.accStack[ts.accDepth - 1];
        g_nnue.update_for_null_move(ts.accStack[ts.accDepth], b);
        b.makeNullMove(); return;
    }
    b.makeNullMove();
}
inline void undo_null_move(Board& b, ThreadState& ts) { b.unmakeNullMove(); if (g_nnue.loaded) ts.accDepth--; }

// ─────────────────────────────────────────────
//  Eval glue
// ─────────────────────────────────────────────
inline int game_phase(const Board& b) {
    constexpr int MAX_PHASE = 24;
    int p = MAX_PHASE;
    for (Color c : {Color::WHITE, Color::BLACK}) {
        p -= (int)b.pieces(PieceType::KNIGHT, c).count() * 1;
        p -= (int)b.pieces(PieceType::BISHOP, c).count() * 1;
        p -= (int)b.pieces(PieceType::ROOK,   c).count() * 2;
        p -= (int)b.pieces(PieceType::QUEEN,  c).count() * 4;
    }
    p = std::max(0, p);
    return (p * 256 + MAX_PHASE / 2) / MAX_PHASE;
}
static const int SEE_VAL[7] = { 100, 320, 330, 500, 900, 20000, 0 };
inline Color see_flip(Color c) { return c == Color::WHITE ? Color::BLACK : Color::WHITE; }

int eval_material_simple(const Board& b) {
    int ph = game_phase(b);
    int mg = 0, eg = 0;
    for (Color c : {Color::WHITE, Color::BLACK}) {
        int s = (c == Color::WHITE) ? 1 : -1;
        for (PieceType pt : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                              PieceType::ROOK, PieceType::QUEEN}) {
            int cnt = (int)b.pieces(pt, c).count();
            mg += s * cnt * PIECE_VAL_MG[(int)pt];
            eg += s * cnt * PIECE_VAL_EG[(int)pt];
        }
    }
    int sc = (mg * (256 - ph) + eg * ph) / 256;
    return (b.sideToMove() == Color::WHITE) ? sc : -sc;
}
static int g_nnue_scale_pct = 40;
inline int nnue_to_cp(int raw) { return raw * g_nnue_scale_pct / 100; }

int evaluate(const Board& b) {
    if (g_nnue.loaded) return nnue_to_cp(g_nnue.evaluate(b));
    return eval_material_simple(b);
}
inline int evaluate_tree(const Board& b, ThreadState& ts) {
    if (g_nnue.loaded)
        return nnue_to_cp(g_nnue.evaluate_from_accumulator(
                   ts.accStack[ts.accDepth], b.sideToMove(), (int)b.occ().count()));
    return eval_material_simple(b);
}

// ─────────────────────────────────────────────
//  SEE
// ─────────────────────────────────────────────
bool see_ge(const Board& board, Move move, int threshold = 0) {
    if (move.typeOf() == Move::CASTLING) return true;
    Square from = move.from(), to = move.to();
    Piece movedPiece = board.at(from);
    if (movedPiece == Piece::NONE) return true;
    PieceType attackerPT = movedPiece.type();
    PieceType capturedPT = (move.typeOf() == Move::ENPASSANT) ? PieceType::PAWN : board.at<PieceType>(to);
    int balance = ((capturedPT == PieceType::NONE) ? 0 : SEE_VAL[(int)capturedPT]) - threshold;
    if (balance < 0) return false;
    PieceType nextVictim = attackerPT;
    if (move.typeOf() == Move::PROMOTION) {
        balance += SEE_VAL[(int)move.promotionType()] - SEE_VAL[(int)PieceType::PAWN];
        nextVictim = move.promotionType();
    }
    balance = SEE_VAL[(int)nextVictim] - balance;
    if (balance <= 0) return true;
    uint64_t occRaw = board.occ().getBits();
    occRaw &= ~(1ULL << from.index());
    occRaw |=  (1ULL << to.index());
    if (move.typeOf() == Move::ENPASSANT) {
        int capSq = to.index() + ((board.sideToMove() == Color::WHITE) ? -8 : 8);
        occRaw &= ~(1ULL << capSq);
    }
    Bitboard occ(occRaw);
    Color side = see_flip(board.sideToMove());
    Bitboard bq = board.pieces(PieceType::BISHOP, Color::WHITE) | board.pieces(PieceType::BISHOP, Color::BLACK)
                | board.pieces(PieceType::QUEEN,  Color::WHITE) | board.pieces(PieceType::QUEEN,  Color::BLACK);
    Bitboard rq = board.pieces(PieceType::ROOK,   Color::WHITE) | board.pieces(PieceType::ROOK,   Color::BLACK)
                | board.pieces(PieceType::QUEEN,  Color::WHITE) | board.pieces(PieceType::QUEEN,  Color::BLACK);
    auto get_atk = [&](Bitboard o) {
        Bitboard a(0ULL);
        a |= attacks::pawn(Color::BLACK, to) & board.pieces(PieceType::PAWN, Color::WHITE);
        a |= attacks::pawn(Color::WHITE, to) & board.pieces(PieceType::PAWN, Color::BLACK);
        a |= attacks::knight(to) & (board.pieces(PieceType::KNIGHT, Color::WHITE) | board.pieces(PieceType::KNIGHT, Color::BLACK));
        a |= attacks::bishop(to, o) & bq;
        a |= attacks::rook(to, o)   & rq;
        a |= attacks::king(to) & (board.pieces(PieceType::KING, Color::WHITE) | board.pieces(PieceType::KING, Color::BLACK));
        return a & o;
    };
    Bitboard atk = get_atk(occ);
    bool relStm = true;
    while (true) {
        Bitboard sa = atk & board.us(side);
        if (!sa) break;
        PieceType pt = PieceType::NONE;
        Square    asq(0);
        for (PieceType c : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP, PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            Bitboard cbb = sa & board.pieces(c, side);
            if (cbb) { pt = c; asq = Square(cbb.lsb()); break; }
        }
        if (pt == PieceType::NONE) break;
        occ &= ~Bitboard(1ULL << asq.index());
        atk = get_atk(occ);
        relStm = !relStm;
        balance = -balance - 1 - SEE_VAL[(int)nextVictim];
        nextVictim = pt;
        if (balance >= 0) {
            if (nextVictim == PieceType::KING && (atk & board.us(see_flip(side))))
                relStm = !relStm;
            break;
        }
        side = see_flip(side);
    }
    return relStm;
}

// ─────────────────────────────────────────────
//  History helpers
// ─────────────────────────────────────────────
constexpr int HIST_LIMIT = 16384;
inline void add_hist(int& h, int bonus) { h += bonus - h * std::abs(bonus) / HIST_LIMIT; }
inline int history_bonus(int d) { return std::min(8 * d * d, 8192); }
inline int mvv_lva(PieceType a, PieceType v) { return PIECE_VAL_MG[(int)v] * 10 - PIECE_VAL_MG[(int)a]; }

inline int cont_hist_at(const ThreadState& ts, int ply, int back, int moved_pt, int to) {
    int p = ply - back;
    if (p < 0) return 0;
    if (ts.mv_pt[p] == PieceType::NONE || ts.mv_to[p] < 0) return 0;
    return ts.contHist[back - 1][(int)ts.mv_pt[p]][ts.mv_to[p]][moved_pt][to];
}
inline void update_cont_hist(ThreadState& ts, int ply, int moved_pt, int to, int bonus) {
    for (int back = 1; back <= 4; ++back) {
        int p = ply - back;
        if (p < 0) break;
        if (ts.mv_pt[p] == PieceType::NONE || ts.mv_to[p] < 0) continue;
        add_hist(ts.contHist[back - 1][(int)ts.mv_pt[p]][ts.mv_to[p]][moved_pt][to], bonus);
    }
}

int score_move(const Board& board, const Move& move, const Move& hash_move, int ply, ThreadState& ts) {
    if (move == hash_move) return 100'000'000;
    bool is_cap = board.at(move.to()) != Piece::NONE || move.typeOf() == Move::ENPASSANT;
    PieceType att = board.at<PieceType>(move.from());

    if (is_cap) {
        PieceType vic = (move.typeOf() == Move::ENPASSANT) ? PieceType::PAWN : board.at<PieceType>(move.to());
        int ch = ts.capHist[(int)att][move.to().index()][(int)vic];
        // B2: 1-ply capture continuation history joins the capture score.
        if (ply >= 1 && ts.mv_pt[ply - 1] != PieceType::NONE && ts.mv_to[ply - 1] >= 0)
            ch += ts.capContHist[(int)ts.mv_pt[ply - 1]][ts.mv_to[ply - 1]][(int)att][move.to().index()];
        int base = see_ge(board, move, -ch / 32) ? 60'000'000 : -30'000'000;
        int promo_bonus = (move.typeOf() == Move::PROMOTION &&
                           move.promotionType() == PieceType::QUEEN) ? 30'000'000 : 0;
        return base + promo_bonus + ch + mvv_lva(att, vic);
    }

    if (move.typeOf() == Move::PROMOTION) {
        if (move.promotionType() == PieceType::QUEEN) return 50'000'000;
        return -40'000'000 + PIECE_VAL_MG[(int)move.promotionType()];
    }

    if (ply < MAX_PLY) {
        if (move == ts.killers[ply][0]) return 45'000'000;
        if (move == ts.killers[ply][1]) return 44'900'000;
    }
    if (ply >= 1 && ts.mv_pt[ply - 1] != PieceType::NONE && ts.mv_to[ply - 1] >= 0 &&
        move == ts.counterMoves[(int)board.sideToMove()][(int)ts.mv_pt[ply - 1]][ts.mv_to[ply - 1]])
        return 43'000'000;

    int c  = (int)board.sideToMove();
    int mp = (int)att;
    int qh = ts.history[c][move.from().index()][move.to().index()];
    for (int back = 1; back <= 4; ++back)
        qh += cont_hist_at(ts, ply, back, mp, move.to().index());
    return qh;
}
void order_moves(const Board& board, Movelist& moves, const Move& hm, int ply, ThreadState& ts) {
    std::array<int, 256> sc;
    int n = (int)moves.size();
    for (int i = 0; i < n; ++i)
        sc[i] = score_move(board, moves[i], hm, ply, ts);
    for (int i = 1; i < n; ++i) {
        Move tm = moves[i]; int tv = sc[i]; int j = i - 1;
        while (j >= 0 && sc[j] < tv) { moves[j+1] = moves[j]; sc[j+1] = sc[j]; --j; }
        moves[j+1] = tm; sc[j+1] = tv;
    }
}

// ─────────────────────────────────────────────
//  Quiescence
// ─────────────────────────────────────────────
int qsearch(Board& b, int alpha, int beta, int ply, ThreadState& ts) {
    ts.nodes++;
    if (info.stop.load(std::memory_order_relaxed) ||
        ((ts.nodes & 2047) == 0 && info.time_up())) {
        info.stop.store(true, std::memory_order_relaxed);
        return 0;
    }
    if (ply > ts.seldepth) ts.seldepth = ply;
    if (ply >= MAX_PLY - 1) return evaluate_tree(b, ts);

    uint64_t key = b.hash();
    __builtin_prefetch(&tt[key & g_tt_mask]);
    bool is_pv = (beta - alpha > 1);

    Move qtt = Move::NO_MOVE;
    TTEntry qe;
    if (tt_probe(key, qe)) {                                   // B1: value semantics
        qtt = qe.get_best_move();
        if (!is_pv) {
            int s = score_from_tt(qe.score, ply);
            TTFlag f = qe.get_flag();
            if (f == TT_EXACT) return s;
            if (f == TT_ALPHA && s <= alpha) return alpha;
            if (f == TT_BETA  && s >= beta)  return beta;
        }
    }

    bool in_check = b.inCheck();
    int raw_eval  = in_check ? 0 : evaluate_tree(b, ts);
    int stand_pat = in_check ? -INF : raw_eval + get_pawn_corr(ts, b) / 4;

    if (!in_check) {
        if (stand_pat >= beta) return stand_pat;
        if (stand_pat > alpha) alpha = stand_pat;
        if (!is_pv && stand_pat < alpha - 1000) return alpha;
    }

    Movelist moves;
    if (in_check) {
        movegen::legalmoves(moves, b);
    } else {
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(moves, b);
        // PROMO-GUARD (unchanged)
        static constexpr uint64_t RANK7_W = 0x00FF000000000000ULL;
        static constexpr uint64_t RANK2_B = 0x000000000000FF00ULL;
        uint64_t pawns = b.pieces(PieceType::PAWN, b.sideToMove()).getBits();
        if (pawns & (b.sideToMove() == Color::WHITE ? RANK7_W : RANK2_B)) {
            Movelist quiets;
            movegen::legalmoves<movegen::MoveGenType::QUIET>(quiets, b);
            for (const Move& m : quiets) {
                if (m.typeOf() != Move::PROMOTION || m.promotionType() != PieceType::QUEEN) continue;
                bool dup = false;
                for (const Move& ex : moves) if (ex == m) { dup = true; break; }
                if (!dup) moves.add(m);
            }
        }
    }

    if (in_check && moves.empty()) return -(MATE_SCORE - ply);

    int n = (int)moves.size();
    std::array<int, 256> sc;
    for (int i = 0; i < n; ++i) {
        if (qtt != Move::NO_MOVE && moves[i] == qtt) { sc[i] = 100'000'000; continue; }
        PieceType a = b.at<PieceType>(moves[i].from());
        PieceType v = (moves[i].typeOf() == Move::ENPASSANT) ? PieceType::PAWN : b.at<PieceType>(moves[i].to());
        if (v == PieceType::NONE)
            sc[i] = ts.history[(int)b.sideToMove()][moves[i].from().index()][moves[i].to().index()];
        else
            sc[i] = 1'000'000 + ts.capHist[(int)a][moves[i].to().index()][(int)v] + mvv_lva(a, v);
    }

    int best_score = stand_pat;
    Move best_move = Move::NO_MOVE;
    int orig_alpha = alpha;

    for (int i = 0; i < n; ++i) {
        int bi = i;
        for (int j = i + 1; j < n; ++j) if (sc[j] > sc[bi]) bi = j;
        if (bi != i) { std::swap(moves[i], moves[bi]); std::swap(sc[i], sc[bi]); }

        if (!in_check && moves[i].typeOf() != Move::PROMOTION) {
            PieceType a = b.at<PieceType>(moves[i].from());
            PieceType v = (moves[i].typeOf() == Move::ENPASSANT) ? PieceType::PAWN : b.at<PieceType>(moves[i].to());
            if (PIECE_VAL_MG[(int)v] < PIECE_VAL_MG[(int)a] && !see_ge(b, moves[i], 0)) continue;
        }

        do_move(b, moves[i], ts);
        int s = -qsearch(b, -beta, -alpha, ply + 1, ts);
        undo_move(b, moves[i], ts);
        if (info.stop.load(std::memory_order_relaxed)) return 0;

        if (s > best_score) {
            best_score = s;
            best_move  = moves[i];
            if (s > alpha) alpha = s;
            if (alpha >= beta) break;
        }
    }

    if (!info.stop.load(std::memory_order_relaxed)) {
        TTFlag f = (best_score >= beta) ? TT_BETA
                 : (alpha > orig_alpha) ? TT_EXACT
                                        : TT_ALPHA;
        tt_store(key, best_score, in_check ? 0 : raw_eval, best_move, 0, f, ply);
    }
    return best_score;
}

// ─────────────────────────────────────────────
//  Main search
// ─────────────────────────────────────────────
int negamax(Board& b, int depth, int alpha, int beta, int ply, bool null_ok, bool cut_node,
            ThreadState& ts, Move excluded = Move::NO_MOVE) {
    ts.nodes++;
    if (info.stop.load(std::memory_order_relaxed) ||
        ((ts.nodes & 2047) == 0 && info.time_up())) {
        info.stop.store(true, std::memory_order_relaxed);
        return 0;
    }
    if (ply >= MAX_PLY - 1)
        return b.inCheck() ? evaluate_tree(b, ts) : qsearch(b, alpha, beta, ply, ts);

    if (ply > 0 && b.isRepetition()) return DRAW_SCORE;
    if (b.isHalfMoveDraw()) {
        auto [reason, result] = b.getHalfMoveDrawType();
        if (reason == GameResultReason::CHECKMATE) return -(MATE_SCORE - ply);
        return DRAW_SCORE;
    }

    bool in_check = b.inCheck();
    if (depth <= 0) return qsearch(b, alpha, beta, ply, ts);

    bool is_pv   = (beta - alpha > 1);
    bool is_root = (ply == 0);

    // Mate distance pruning
    if (!is_root) {
        int mate_alpha = -(MATE_SCORE - ply);
        int mate_beta  =  (MATE_SCORE - ply - 1);
        if (alpha < mate_alpha) alpha = mate_alpha;
        if (beta  > mate_beta)  beta  = mate_beta;
        if (alpha >= beta) return alpha;
    }

    uint64_t key = b.hash();
    __builtin_prefetch(&tt[key & g_tt_mask]);

    Move   hm = Move::NO_MOVE;
    int    tt_depth = 0, tt_score = 0, tt_eval = 0;
    TTFlag tt_flag = TT_NONE;
    bool   tt_hit = false;

    if (excluded == Move::NO_MOVE) {
        TTEntry qe;
        if (tt_probe(key, qe)) {                               // B1: value semantics
            tt_hit = true;
            hm = qe.get_best_move();
            tt_depth = qe.depth;
            tt_score = score_from_tt(qe.score, ply);
            tt_eval  = qe.eval_i16;
            tt_flag  = qe.get_flag();
            if (!is_pv && tt_depth >= depth) {
                if (tt_flag == TT_EXACT) return tt_score;
                if (tt_flag == TT_ALPHA && tt_score <= alpha) return alpha;
                if (tt_flag == TT_BETA  && tt_score >= beta)  return beta;
            }
        }
    }

    int raw_eval = in_check ? 0 : (tt_hit ? tt_eval : evaluate_tree(b, ts));
    int corr = in_check ? 0 : get_pawn_corr(ts, b) / 4;
    int static_eval = in_check ? 0 : raw_eval + corr;

    ts.eval_stack[ply] = in_check ? NO_EVAL : static_eval;
    bool improving = !in_check && ply >= 2 &&
                     ts.eval_stack[ply - 2] != NO_EVAL && static_eval > ts.eval_stack[ply - 2];

    if (!is_pv && !in_check && depth <= 5) {
        int m = 300 + 200 * depth;
        if (static_eval + m < alpha) {
            int r = qsearch(b, alpha, alpha + 1, ply, ts);
            if (r <= alpha) return r;
        }
    }
    if (!is_pv && !in_check && depth <= 7) {
        int m = 75 * depth;
        if (static_eval - m >= beta) return static_eval - m;
    }
    if (excluded == Move::NO_MOVE && hm == Move::NO_MOVE && depth >= 4) --depth;  // IIR

    // ProbCut
    if (!is_pv && !in_check && excluded == Move::NO_MOVE && depth >= 5 &&
        std::abs(beta) < MATE_BOUND) {
        int pb = beta + 180;
        if (static_eval + 40 * depth >= pb) {
            Movelist caps;
            movegen::legalmoves<movegen::MoveGenType::CAPTURE>(caps, b);
            for (const Move& m : caps) {
                if (!see_ge(b, m, 150)) continue;
                do_move(b, m, ts);
                ts.mv_pt[ply] = PieceType::NONE;
                ts.mv_to[ply] = -1;
                int s = -negamax(b, depth - 4, -pb, -pb + 1, ply + 1, false, true, ts);
                undo_move(b, m, ts);
                if (info.stop.load(std::memory_order_relaxed)) return 0;
                if (s >= pb) return pb;
            }
        }
    }

    // Null move pruning
    bool has_pieces = (b.us(b.sideToMove()) & ~b.pieces(PieceType::PAWN, b.sideToMove())).count() > 1;
    if (!in_check && null_ok && !is_pv && depth >= 3 && has_pieces) {
        int R = 3 + depth / 4
              + std::clamp((static_eval - beta) / 200, 0, 3)
              + (cut_node ? 1 : 0);
        R = std::min(R, depth - 1);
        PieceType saved_pt = ts.mv_pt[ply]; int saved_to = ts.mv_to[ply];
        ts.mv_pt[ply] = PieceType::NONE; ts.mv_to[ply] = -1;
        do_null_move(b, ts);
        int ns = -negamax(b, depth - 1 - R, -beta, -beta + 1, ply + 1, false, false, ts);
        undo_null_move(b, ts);
        ts.mv_pt[ply] = saved_pt; ts.mv_to[ply] = saved_to;
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (ns >= beta) {
            if (ns >= MATE_BOUND) return beta;
            return ns;
        }
    }

    // Singular + double extensions
    int extension = 0;
    if (!is_root && !in_check && excluded == Move::NO_MOVE &&
        hm != Move::NO_MOVE && depth >= 8 &&
        tt_depth >= depth - 3 && tt_flag != TT_ALPHA &&
        tt_score >= beta && std::abs(tt_score) < MATE_BOUND) {
        int sb = tt_score - 2 * depth;
        int sd = (depth - 1) / 2;
        int s = negamax(b, sd, sb - 1, sb, ply, false, false, ts, hm);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (s < sb) {
            extension = 1;
            if (depth >= 12 && s < sb - 20 && ts.double_ext < 4) extension = 2;
        } else if (sb >= beta) return sb;
    }

    Movelist moves;
    movegen::legalmoves(moves, b);
    if (moves.empty())
        return in_check ? -(MATE_SCORE - ply) : DRAW_SCORE;

    int base_ext = 0;

    order_moves(b, moves, hm, ply, ts);

    auto qhist_of = [&](const Move& m, int mpt, int col) {
        int h = ts.history[col][m.from().index()][m.to().index()];
        for (int back = 1; back <= 4; ++back)
            h += cont_hist_at(ts, ply, back, mpt, m.to().index());
        return h;
    };
    // B2: capture continuation history updates share the node's previous-move context.
    auto upd_cap_cont = [&](int moved_pt, int to, int bonus) {
        if (ply >= 1 && ts.mv_pt[ply - 1] != PieceType::NONE && ts.mv_to[ply - 1] >= 0)
            add_hist(ts.capContHist[(int)ts.mv_pt[ply - 1]][ts.mv_to[ply - 1]][moved_pt][to], bonus);
    };

    int best_score = -INF;
    Move best_move = moves[0];
    int orig_alpha = alpha;
    int quiets_tried = 0;
    Move quiet_list[64];
    int n_quiet = 0;
    Move cap_list[64];
    int n_cap = 0;

    for (int i = 0; i < (int)moves.size(); ++i) {
        if (moves[i] == excluded) continue;

        bool is_cap   = b.at(moves[i].to()) != Piece::NONE || moves[i].typeOf() == Move::ENPASSANT;
        bool is_promo = moves[i].typeOf() == Move::PROMOTION;
        bool is_quiet = !is_cap && !is_promo;

        if (!is_root && best_score > -MATE_BOUND) {
            if (!is_pv && !in_check && is_quiet && depth <= 8) {
                int lmp = improving ? (2 + depth * depth / 2) : (1 + depth * depth / 3);
                if (quiets_tried >= lmp) continue;
                if (depth <= 5 &&
                    qhist_of(moves[i], (int)b.at<PieceType>(moves[i].from()), (int)b.sideToMove()) < -3000 * depth) continue;
            }
            if (!in_check && is_quiet && depth <= 6) {
                int fp_margin = 110 + 85 * depth - (improving ? 40 : 0);
                if (static_eval + fp_margin <= alpha) continue;
            }
            if (!is_pv && !in_check && depth <= 8) {
                if (is_quiet && !see_ge(b, moves[i], -20 * depth * depth)) continue;
                if (is_cap   && !see_ge(b, moves[i], -90 * depth)) continue;
            }
        }

        int mc = (int)b.sideToMove();
        int mf = moves[i].from().index();
        int mt = moves[i].to().index();
        PieceType mp = b.at<PieceType>(moves[i].from());

        if (is_quiet) {
            if (n_quiet < 64) quiet_list[n_quiet++] = moves[i];
            ++quiets_tried;
        } else if (is_cap) {
            if (n_cap < 64) cap_list[n_cap++] = moves[i];
        }

        bool dext_now = (extension == 2 && moves[i] == hm);
        if (dext_now) ts.double_ext++;

        do_move(b, moves[i], ts);
        ts.mv_pt[ply] = mp;
        ts.mv_to[ply] = mt;
        bool gives_check = b.inCheck();

        int nd = depth - 1 + base_ext + ((moves[i] == hm) ? extension : 0);
        int score;

        if (i == 0) {
            score = -negamax(b, nd, -beta, -alpha, ply + 1, true, false, ts);
        } else {
            int r = 0;
            if (is_quiet && depth >= 3 && !in_check) {
                r = LMR_TABLE[std::min(depth, MAX_DEPTH - 1)][std::min(i, 63)];
                r += cut_node;
                if (hm == Move::NO_MOVE) r += 1;                     // B3: no TT move -> worse ordering
                if (is_pv) r = std::max(0, r - 1);
                if (gives_check) r -= 1;
                if (ply < MAX_PLY && (moves[i] == ts.killers[ply][0] || moves[i] == ts.killers[ply][1])) r -= 1;
                int qh = qhist_of(moves[i], (int)mp, mc);
                // B4: graded history adjustment (was binary +-1 at 8000/-3000)
                r -= std::clamp(qh / 8192, -2, 2);
                r = std::max(0, std::min(r, depth - 2));
            }
            // B5: late captures get reduced too. i >= 6 approximates "bad capture"
            // under the O1 ordering (good captures sort first).
            else if (is_cap && depth >= 5 && i >= 6 && !gives_check && !in_check) {
                r = 1 + cut_node;
                if (is_pv) r = std::max(0, r - 1);
                r = std::max(0, std::min(r, depth - 2));
            }
            if (r > 0)
                score = -negamax(b, nd - r, -alpha - 1, -alpha, ply + 1, true, true, ts);
            else
                score = -negamax(b, nd, -alpha - 1, -alpha, ply + 1, true, true, ts);
            if (score > alpha && r > 0)
                score = -negamax(b, nd, -alpha - 1, -alpha, ply + 1, true, true, ts);
            if (score > alpha && score < beta)
                score = -negamax(b, nd, -beta, -alpha, ply + 1, true, false, ts);
        }
        undo_move(b, moves[i], ts);
        if (dext_now) ts.double_ext--;
        if (info.stop.load(std::memory_order_relaxed)) return 0;

        if (score > best_score) { best_score = score; best_move = moves[i]; }
        if (score > alpha) {
            alpha = score;
            int bonus = history_bonus(depth);
            if (is_quiet) {
                add_hist(ts.history[mc][mf][mt], bonus);
                update_cont_hist(ts, ply, (int)mp, mt, bonus);
            } else if (is_cap) {
                PieceType v = (moves[i].typeOf() == Move::ENPASSANT) ? PieceType::PAWN : b.at<PieceType>(mt);
                add_hist(ts.capHist[(int)mp][mt][(int)v], bonus);
                upd_cap_cont((int)mp, mt, bonus);                    // B2
            }
        }
        if (alpha >= beta) {
            ++ts.cutoffs;
            if (i == 0) ++ts.cutoffs_first;

            if (is_quiet && ply >= 1 && ts.mv_pt[ply - 1] != PieceType::NONE && ts.mv_to[ply - 1] >= 0)
                ts.counterMoves[mc][(int)ts.mv_pt[ply - 1]][ts.mv_to[ply - 1]] = moves[i];

            if (is_quiet && ply < MAX_PLY) {
                if (moves[i] != ts.killers[ply][0]) {
                    ts.killers[ply][1] = ts.killers[ply][0];
                    ts.killers[ply][0] = moves[i];
                }
                int malus = -history_bonus(depth);
                for (int k = 0; k < n_quiet; ++k) {
                    if (quiet_list[k] == moves[i]) continue;
                    int f2 = quiet_list[k].from().index();
                    int t2 = quiet_list[k].to().index();
                    PieceType pt2 = b.at<PieceType>(quiet_list[k].from());
                    add_hist(ts.history[mc][f2][t2], malus);
                    update_cont_hist(ts, ply, (int)pt2, t2, malus);
                }
            }

            // O4 + B2: malus other captures on EVERY cutoff, in capHist AND
            // capContHist. The cutoff capture already got its rewards above.
            int cap_malus = -history_bonus(depth);
            for (int k = 0; k < n_cap; ++k) {
                if (cap_list[k] == moves[i]) continue;
                PieceType ca = b.at<PieceType>(cap_list[k].from());
                PieceType cv = (cap_list[k].typeOf() == Move::ENPASSANT)
                             ? PieceType::PAWN : b.at<PieceType>(cap_list[k].to());
                add_hist(ts.capHist[(int)ca][cap_list[k].to().index()][(int)cv], cap_malus);
                upd_cap_cont((int)ca, cap_list[k].to().index(), cap_malus);   // B2
            }

            if (excluded == Move::NO_MOVE) {
                tt_store(key, best_score, raw_eval, best_move, depth, TT_BETA, ply);
                if (!in_check && depth >= 4 && std::abs(best_score) < MATE_BOUND)
                    update_pawn_corr(ts, b, best_score - static_eval, depth);
            }
            return beta;
        }
    }

    if (excluded == Move::NO_MOVE) {
        TTFlag f = (alpha > orig_alpha) ? TT_EXACT : TT_ALPHA;
        tt_store(key, best_score, raw_eval, best_move, depth, f, ply);
        if (!in_check && depth >= 4 && std::abs(best_score) < MATE_BOUND)
            update_pawn_corr(ts, b, best_score - static_eval, depth);
    }
    return best_score;
}

// ─────────────────────────────────────────────
//  Output helpers
// ─────────────────────────────────────────────
std::string score_str(int score) {
    if (score >=  MATE_BOUND) return "score mate "  + std::to_string((MATE_SCORE - score + 1) / 2);
    if (score <= -MATE_BOUND) return "score mate -" + std::to_string((MATE_SCORE + score + 1) / 2);
    return "score cp " + std::to_string(score);
}
std::string pv_line(const Board& b, int max_len) {
    std::string s;
    Board b2 = b;
    Movelist ml;
    int len = std::min(max_len + 6, 20);
    for (int i = 0; i < len; ++i) {
        TTEntry e;                                             // B1: value semantics
        if (!tt_probe(b2.hash(), e)) break;
        Move m = e.get_best_move();
        if (m == Move::NO_MOVE) break;
        movegen::legalmoves(ml, b2);
        bool ok = false;
        for (const auto& mm : ml) if (mm == m) { ok = true; break; }
        if (!ok) break;
        s += " " + uci::moveToUci(m);
        b2.makeMove(m);
        if (b2.isRepetition()) break;
    }
    return s;
}
int tt_hashfull() {
    int used = 0;
    size_t stride = g_tt_size / 1000; if (!stride) stride = 1;
    for (int i = 0; i < 1000; ++i) {
        TTEntry e = tt_read(tt[i * stride]);                   // B1: 16B read even for stats
        if (e.get_flag() != TT_NONE && e.get_gen() == tt_generation) ++used;
    }
    return used;
}

// ─────────────────────────────────────────────
//  Thread Worker
// ─────────────────────────────────────────────
void thread_worker(Board board, int thread_id, int max_depth, Move* best_move_out) {
    ThreadState& ts = thread_states[thread_id];
    ts.nodes = 0;
    ts.seldepth = 0;
    ts.thread_id = thread_id;
    ts.double_ext = 0;
    ts.cutoffs = 0;
    ts.cutoffs_first = 0;
    ts.clear_history();
    std::fill(std::begin(ts.eval_stack), std::end(ts.eval_stack), NO_EVAL);
    std::fill(std::begin(ts.mv_pt), std::end(ts.mv_pt), PieceType::NONE);
    std::fill(std::begin(ts.mv_to), std::end(ts.mv_to), -1);
    if (g_nnue.loaded) {
        ts.accDepth = 0;
        g_nnue.refresh(board, ts.accStack[ts.accDepth]);
    }

    Move best_move = Move::NO_MOVE;
    {
        Movelist ml; movegen::legalmoves(ml, board);
        if (ml.empty()) { if (thread_id == 0) *best_move_out = Move::NO_MOVE; return; }
        best_move = ml[0];
        if (ml.size() == 1 && info.hard_time > 0) {
            uint32_t raw = 0;
            std::memcpy(&raw, &best_move, sizeof(raw));
            g_shared_best.store(raw, std::memory_order_relaxed);
            if (thread_id == 0) *best_move_out = best_move;
            return;
        }
    }

    int prev_score = 0;
    int prev_prev_score = 0;
    int ms_before_depth = 0;
    Move prev_best_move = Move::NO_MOVE;
    int best_stable = 0;
    int score_stable = 0;

    auto elapsed_ms = [&]() {
        return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - info.start_time).count();
    };

    for (int depth = 1; depth <= max_depth; ++depth) {
        int ms = elapsed_ms();
        if (info.hard_time > 0 && ms >= info.hard_time) {
            info.stop.store(true, std::memory_order_relaxed);
            break;
        }

        int score;
        bool completed = false;

        bool use_asp = depth >= 5 && std::abs(prev_score) < MATE_BOUND;
        if (use_asp) {
            int delta = 15 + 3 * (ts.thread_id % 3);           // B7: per-thread window jitter
            int a  = prev_score - delta;
            int bt = prev_score + delta;
            int iter_depth = depth;
            while (true) {
                score = negamax(board, iter_depth, a, bt, 0, true, false, ts);
                if (info.stop.load(std::memory_order_relaxed)) break;
                if      (score <= a) {
                    a -= delta;
                    delta += delta / 2 + 10;                   // B6: gradual widening (was x2)
                    if (depth >= 8 && iter_depth == depth) iter_depth = depth - 1;  // B6: fail-low depth-1
                }
                else if (score >= bt) {
                    bt += delta;
                    delta += delta / 2 + 10;                   // B6
                }
                else { completed = true; break; }
                if (a < -INF / 2) a = -INF;
                if (bt >  INF / 2) bt =  INF;
                if (a == -INF && bt == INF) {
                    score = negamax(board, iter_depth, -INF, INF, 0, true, false, ts);
                    if (info.stop.load(std::memory_order_relaxed)) break;
                    completed = true;
                    break;
                }
            }
        } else {
            score = negamax(board, depth, -INF, INF, 0, true, false, ts);
            if (!info.stop.load(std::memory_order_relaxed)) completed = true;
        }

        if (!completed) {
            if (thread_id == 0) *best_move_out = best_move;
            break;
        }

        prev_prev_score = prev_score;
        prev_score = score;
        bool score_drop = (prev_prev_score - score) >= 30;     // B8: fail-low detector

        {
            TTEntry re;
            if (tt_probe(board.hash(), re)) {                  // B1: value semantics
                Move m = re.get_best_move();
                if (m != Move::NO_MOVE) {
                    Movelist ml; movegen::legalmoves(ml, board);
                    for (const auto& mm : ml) if (mm == m) { best_move = m; break; }
                }
            }
        }

        {
            int prev_d = g_shared_depth.load(std::memory_order_relaxed);
            if (depth > prev_d &&
                g_shared_depth.compare_exchange_strong(prev_d, depth)) {
                uint32_t raw = 0;
                std::memcpy(&raw, &best_move, sizeof(raw));
                g_shared_best.store(raw, std::memory_order_relaxed);
            }
        }

        if (best_move == prev_best_move) ++best_stable; else { best_stable = 0; prev_best_move = best_move; }
        if (std::abs(score - prev_prev_score) < 15) ++score_stable; else score_stable = 0;

        if (thread_id == 0) {
            int ms_now = elapsed_ms();
            int ms_depth = ms_now - ms_before_depth;
            uint64_t total = 0;
            for (int i = 0; i < num_threads; ++i) total += thread_states[i].nodes;
            int nps = (ms_now > 0) ? (int)(total * 1000ULL / ms_now) : 0;

            std::ostringstream oss;
            oss << "info depth " << depth
                << " seldepth "  << ts.seldepth
                << " "           << score_str(score)
                << " nodes "     << total
                << " nps "       << nps
                << " hashfull "  << tt_hashfull()
                << " time "      << ms_now
                << " cutoffs "   << ts.cutoffs
                << " fmc "       << (ts.cutoffs
                                    ? (int)(100.0 * ts.cutoffs_first / (double)ts.cutoffs) : 0)
                << " pv"         << pv_line(board, depth)
                << "\n";
            std::cout << oss.str();
            std::cout.flush();

            if (info.hard_time > 0) {
                if (info.fixed_movetime) {
                    if (ms_now >= info.hard_time) {
                        info.stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                } else {
                    double factor = 1.0;
                    // B8: suppress the early-exit factors after a >=30cp score drop
                    if (!score_drop) {
                        if (best_stable >= 6)      factor = 0.55;
                        else if (best_stable >= 4) factor = 0.70;
                        else if (best_stable >= 2) factor = 0.85;
                        if (score_stable >= 3) factor *= 0.85;
                        if (score_stable >= 5) factor *= 0.85;
                        factor = std::max(factor, 0.5);
                    }

                    int target_ms = (int)(info.soft_time * factor);
                    if (ms_now >= std::max(target_ms, info.hard_time / 20)) {
                        info.stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                    if (ms_now + 3LL * std::max(ms_depth, 1) >= info.hard_time) {
                        info.stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                }
            }
            ms_before_depth = ms_now;
        }
    }

    if (thread_id == 0) {
        if (best_move == Move::NO_MOVE) {
            Movelist ml; movegen::legalmoves(ml, board);
            if (!ml.empty()) best_move = ml[0];
        }
        *best_move_out = best_move;
    }
}

Move search(Board& board, int max_depth, int soft_ms, int hard_ms) {
    info.stop.store(false, std::memory_order_relaxed);
    info.soft_time = soft_ms;
    info.hard_time = hard_ms;
    info.fixed_movetime = (soft_ms > 0 && soft_ms == hard_ms);
    info.start_time = std::chrono::steady_clock::now();
    tt_generation = (uint8_t)((tt_generation + 1) & 0x0F);
    g_shared_best.store(0, std::memory_order_relaxed);
    g_shared_depth.store(0, std::memory_order_relaxed);

    Move best_move = Move::NO_MOVE;
    std::vector<std::thread> threads;
    for (int i = 0; i < num_threads; ++i)
        threads.emplace_back(thread_worker, board, i, max_depth, &best_move);
    for (auto& t : threads) t.join();

    uint32_t raw = g_shared_best.load(std::memory_order_relaxed);
    if (raw) {
        Move m;
        std::memcpy(&m, &raw, sizeof(m));
        Movelist ml; movegen::legalmoves(ml, board);
        for (const auto& mm : ml) if (mm == m) return m;
    }
    return best_move;
}

// ─────────────────────────────────────────────
//  UCI
// ─────────────────────────────────────────────
void uci_loop() {
    Board board;
    board.setFen(constants::STARTPOS);
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        std::istringstream iss(line);
        std::string token;
        iss >> token;
        if (token == "uci") {
            std::cout << "id name Tiger-1\n";
            std::cout << "id author Ranak Chongtham\n";
            std::cout << "option name EvalFile type string default " << g_nnue_path << "\n";
            std::cout << "option name Hash type spin default 256 min 1 max 4096\n";
            std::cout << "option name Threads type spin default 1 min 1 max " << MAX_THREADS << "\n";
            std::cout << "option name NNUEScale type spin default 40 min 1 max 400\n";
            std::cout << "uciok\n";
        } else if (token == "isready") {
            std::cout << "readyok\n";
        } else if (token == "ucinewgame") {
            wait_for_search_end();
            board.setFen(constants::STARTPOS);
            tt_clear();
            for (int i = 0; i < MAX_THREADS; ++i) thread_states[i].clear_history();
        } else if (token == "setoption") {
            wait_for_search_end();
            std::string word, name, value;
            iss >> word;
            while (iss >> word && word != "value") {
                if (!name.empty()) name += " ";
                name += word;
            }
            std::getline(iss, value);
            if (!value.empty() && value.front() == ' ') value.erase(0, 1);
            if (name == "EvalFile" && !value.empty()) {
                g_nnue_path = value;
                if (g_nnue.load(g_nnue_path))
                    std::cout << "info string loaded NNUE from " << g_nnue_path << "\n";
                else
                    std::cout << "info string failed to load NNUE from " << g_nnue_path << "\n";
            } else if (name == "Threads") {
                num_threads = std::max(1, std::min(MAX_THREADS, std::stoi(value)));
            } else if (name == "Hash") {
                tt_resize((size_t)std::max(1, std::stoi(value)));
            } else if (name == "NNUEScale" && !value.empty()) {
                g_nnue_scale_pct = std::max(1, std::min(400, std::stoi(value)));
            }
        } else if (token == "position") {
            wait_for_search_end();
            std::string p;
            iss >> p;
            if (p == "startpos") { board.setFen(constants::STARTPOS); iss >> p; }
            else if (p == "fen") {
                // Robust FEN parsing: GUIs often send only 4 tokens (no clocks),
                // sometimes followed directly by "moves". Pad what's missing and
                // never swallow the moves list into the FEN.
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
            wait_for_search_end();
            int depth = -1, movetime = 0, wtime = 0, btime = 0, winc = 0, binc = 0;
            bool infinite = false, ponder = false;
            while (iss >> token) {
                if      (token == "depth")    iss >> depth;
                else if (token == "movetime") iss >> movetime;
                else if (token == "wtime")    iss >> wtime;
                else if (token == "btime")    iss >> btime;
                else if (token == "winc")     iss >> winc;
                else if (token == "binc")     iss >> binc;
                else if (token == "infinite") infinite = true;
                else if (token == "ponder")   ponder = true;
            }

            int soft = 0, hard = 0;
            auto clock_limits = [&]() {
                int myT = (board.sideToMove() == Color::WHITE) ? wtime : btime;
                int myI = (board.sideToMove() == Color::WHITE) ? winc  : binc;
                if (myT > 0) {
                    soft = (int)(myT * 0.045) + myI * 85 / 100;
                    hard = std::min(myT - 50, std::max(soft * 4, myT / 4));
                    hard = std::max(hard, 1);
                    soft = std::max(1, std::min(soft, hard));
                }
            };

            if (infinite) {
                depth = MAX_DEPTH;
            } else if (ponder) {
                clock_limits();
                g_pending_soft = soft; g_pending_hard = hard;
                soft = 0; hard = 0;
                depth = MAX_DEPTH;
            } else if (movetime > 0) {
                soft = hard = movetime;
                if (depth < 0) depth = MAX_DEPTH;
            } else {
                clock_limits();
                if (soft > 0) { if (depth < 0) depth = MAX_DEPTH; }
                else if (depth < 0) depth = 8;
            }

            Board sb = board;
            int d = (depth < 0) ? MAX_DEPTH : depth;
            g_search_thread = std::thread([sb, d, soft, hard]() mutable {
                Move best = search(sb, d, soft, hard);
                std::ostringstream oss;
                if (best == Move::NO_MOVE) oss << "bestmove (none)\n";
                else                       oss << "bestmove " << uci::moveToUci(best) << "\n";
                std::cout << oss.str();
                std::cout.flush();
            });
        } else if (token == "ponderhit") {
            info.soft_time = g_pending_soft;
            info.hard_time = g_pending_hard;
            info.fixed_movetime = (g_pending_soft > 0 && g_pending_soft == g_pending_hard);
        } else if (token == "stop") {
            info.stop.store(true, std::memory_order_relaxed);
        } else if (token == "quit") {
            wait_for_search_end();
            break;
        } else if (token == "d") {
            std::cout << board << "\n";
            std::cout << "FEN: "  << board.getFen() << "\n";
            std::cout << "Hash: " << board.hash() << "\n";
            std::cout << "Eval: " << evaluate(board)
                      << (g_nnue.loaded ? "  [NNUE, raw " + std::to_string(g_nnue.evaluate(board)) + "]"
                                        : std::string("  [MATERIAL-ONLY]"))
                      << "\n";
        }
        std::cout.flush();
    }
    wait_for_search_end();
}

int main() {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    init_lmr();
    tt_resize(256);
    thread_states = new ThreadState[MAX_THREADS];
    for (int i = 0; i < MAX_THREADS; ++i) thread_states[i].clear_history();

    if (g_nnue.load(g_nnue_path))
        std::cerr << "info string loaded NNUE from " << g_nnue_path << "\n";
    else
        std::cerr << "info string no NNUE loaded. Falling back to material eval.\n";

    uci_loop();
    delete[] tt;
    delete[] thread_states;
    return 0;
}
