// ============================================================================
//  AGNES — search, time management, UCI.
//
//  Conventions:
//    * backened.hpp is included as "chess.hpp" (it owns the CHESS_HPP guard).
//    * Scores are engine-cp (NNUE raw * NNUEScale). TT scores are int16.
//    * MATE_SCORE 32000, MATE_BOUND 31872, NO_EVAL -32768
//    * Fail-soft everywhere
// ============================================================================

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

// ─────────────────────────────────────────────────────────────────────────────
//  B3: tunables. Every numeric constant that matters is routed through TUNE()
//  so SPSA can pick it up. Without -DTUNING the options stay hidden.
// ─────────────────────────────────────────────────────────────────────────────
struct Tunable {
    const char* name;
    int*        value;
    int         def, lo, hi, step;
};
std::vector<Tunable>& tunables() {
    static std::vector<Tunable> t;
    return t;
}
#define TUNE(name, def, lo, hi, step)                                        \
    static int name = (def);                                                 \
    namespace {                                                              \
    struct Reg_##name {                                                      \
        Reg_##name() { tunables().push_back({#name, &name, def, lo, hi, step}); } \
    };                                                                       \
    static Reg_##name reg_##name##_inst;                                     \
    }

// ─────────────────────────────────────────────────────────────────────────────
//  C6: score constants
// ─────────────────────────────────────────────────────────────────────────────
constexpr int INF         = 32001;
constexpr int MATE_SCORE  = 32000;
constexpr int MATE_BOUND  = MATE_SCORE - 128;     // 31872
constexpr int TB_WIN      = MATE_BOUND - 1;       // tablebase win region ceiling
constexpr int MAX_PLY     = 128;
constexpr int MAX_DEPTH   = 64;
constexpr int NO_EVAL     = -32768;
constexpr int DRAW_SCORE  = 0;

inline bool is_decisive(int s) { return std::abs(s) >= MATE_BOUND; }
inline bool is_win(int s)      { return s >= MATE_BOUND; }
inline bool is_loss(int s)     { return s <= -MATE_BOUND; }
inline int  mated_in(int ply)  { return -(MATE_SCORE - ply); }

// ─────────────────────────────────────────────────────────────────────────────
//  B3: TUNE constants (seeds from SF17 / Reckless-style engines; retune via SPSA)
// ─────────────────────────────────────────────────────────────────────────────
TUNE(asp_delta,          10,    5,    40,    1)   // initial aspiration window
TUNE(asp_widen_div,       3,    2,     8,    1)   // delta += delta / asp_widen_div
TUNE(razor_r0,          450,  200,   900,   20)   // razoring constant (quadratic)
TUNE(razor_r1,          270,  100,   600,   20)
TUNE(rfp_margin,         80,   40,   180,    4)   // reverse futility margin / depth
TUNE(rfp_improving,      60,   20,   120,    4)
TUNE(rfp_max_depth,       9,    4,    14,    1)
TUNE(nmp_base,            4,    2,     8,    1)   // null-move R base
TUNE(nmp_depth_div,       3,    2,     6,    1)
TUNE(nmp_eval_div,      180,   80,   400,   10)
TUNE(nmp_eval_cap,        3,    1,     6,    1)
TUNE(nmp_gate,          390,  100,   800,   20)   // eval >= beta - nmp_gate + 18*depth
TUNE(nmp_gate_slope,     18,    8,    40,    2)
TUNE(nmp_verif_min_d,    14,    8,    20,    1)
TUNE(iir_min_depth,       6,    3,    10,    1)
TUNE(probcut_margin,    200,  100,   400,   10)
TUNE(probcut_improving,  60,   20,   120,    5)
TUNE(probcut_min_depth,   5,    3,     8,    1)
TUNE(probcut_dred,        4,    2,     6,    1)
TUNE(lmp_base,            3,    1,     8,    1)   // (lmp_base + d^2) / (2 - improving)
TUNE(hist_prune,       4000, 1500,  8000,  250)   // quiet hist < -hist_prune * depth
TUNE(fut_base,          120,   50,   300,   10)   // quiet futility: fut_base + 100*lmrd
TUNE(fut_slope,         100,   50,   250,   10)
TUNE(capfut_base,       230,  100,   500,   15)   // capture futility
TUNE(capfut_slope,      200,   80,   400,   10)
TUNE(see_quiet,         -25,  -60,   -10,    5)   // see threshold = see_quiet * lmr_d^2
TUNE(see_capture,      -150, -300,   -60,   10)   // see threshold = see_capture * depth
TUNE(sing_min_depth,      6,    4,    10,    1)
TUNE(sing_beta_base,     52,   20,   100,    5)   // margin = (base + pv*extra) * depth / 64
TUNE(sing_beta_extra,    74,   30,   150,    5)
TUNE(sing_dbl_margin,    20,   10,    60,    5)
TUNE(sing_neg_ext,       -3,   -6,    -1,    1)
TUNE(dext_cap,            8,    4,    16,    1)
TUNE(hist_bonus_a,      170,   60,   350,   10)   // bonus = min(hist_bonus_a*d - b, cap)
TUNE(hist_bonus_b,       90,   20,   250,   10)
TUNE(hist_bonus_cap,   1700,  800,  3200,  100)
TUNE(hist_malus_a,      450,  200,   800,   25)   // malus = min(hist_malus_a*d - b, cap)
TUNE(hist_malus_b,      150,   20,   400,   10)
TUNE(hist_malus_cap,   1500,  700,  3000,  100)
TUNE(prior_fail_low_d,    3,    2,     6,    1)   // min depth for the fail-low prior bonus
TUNE(corr_weight_pawn,    8,    1,    20,    1)
TUNE(corr_weight_minor,   6,    1,    20,    1)
TUNE(corr_weight_np,      6,    1,    20,    1)
TUNE(corr_weight_cont,    5,    1,    20,    1)
TUNE(corr_grain,         16,    4,    32,    2)
TUNE(corr_limit,       1024,  256,  4096,   64)
TUNE(dext_lmr_cap,        8,    4,    16,    1)   // double-extension budget along a line
TUNE(lmr_cutnode,      1024,    0,  2048,   64)
TUNE(lmr_cutnode_ttm,  1024,    0,  2048,   64)
TUNE(lmr_improving,     900,    0,  2048,   64)
TUNE(lmr_pv,           1024,    0,  2048,   64)
TUNE(lmr_ttpv,         1024,    0,  2048,   64)
TUNE(lmr_check,         800,    0,  2048,   64)
TUNE(lmr_ttcap,        1024,    0,  2048,   64)
TUNE(lmr_cuthist,       700,    0,  2048,   64)
TUNE(lmr_killer,       1024,    0,  2048,   64)
TUNE(lmr_allnode,      1024,    0,  2048,   64)
TUNE(lmr_hist_div,     9000, 4000, 24000,  500)
TUNE(lmr_hist_cap,     2048,  512,  4096,  128)
TUNE(do_deeper_margin,   40,   10,   120,    5)
TUNE(do_shallower_mg,     1,    1,     6,    1)
TUNE(qs_fut_base,       200,  100,   400,   10)
TUNE(tm_stability_a,    140,  100,  200,   10)    // stability factor base, /100
TUNE(tm_stability_b,      5,    1,   15,    1)    // subtracted per stable iteration, /100
TUNE(tm_falling_lo,      60,   30,  120,    5)    // /100
TUNE(tm_falling_hi,     170,  100,  300,   10)    // /100
TUNE(tm_node_a,         150,   50,  300,   10)    // node-effort factor, /100
TUNE(tm_node_b,          50,   10,  150,    5)    // subtracted, /100
TUNE(tm_hard_cap,      825,  600,  950,   10)     // hard = min(hard_cap% * clock, max_scale*soft)
TUNE(tm_max_scale,      600,  300, 1000,   25)     // /100
TUNE(mtg_scale_cap,     630,  300, 1000,   25)     // movestogo max_scale, /100
TUNE(mtg_scale_slope,    11,    3,   30,    1)    // 1.5 + mtg_scale_slope/100 * mtg
TUNE(tm_opt_base,        45,   20,  100,    5)     // sudden-death optimal scale, /1000
TUNE(vote_base,          14,    1,   60,    1)     // votes += (score-min+vote_base)*depth

// LMR tables: 1/1024-ply units. quiet: 0.80 + ln(d)*ln(m)/2.20
//             noisy: 0.20 + ln(d)*ln(m)/3.20          (D9 seeds)
static const int LMRD = 64;
static int LMR_Q[LMRD][LMRD];
static int LMR_N[LMRD][LMRD];
void init_lmr() {
    for (int d = 1; d < LMRD; ++d)
        for (int m = 1; m < LMRD; ++m) {
            LMR_Q[d][m] = (int)(1024.0 * (0.80 + std::log((double)d) * std::log((double)m) / 2.20));
            LMR_N[d][m] = (int)(1024.0 * (0.20 + std::log((double)d) * std::log((double)m) / 3.20));
        }
    for (int m = 0; m < LMRD; ++m) { LMR_Q[0][m] = LMR_N[0][m] = 0; }
    for (int d = 0; d < LMRD; ++d) { LMR_Q[d][0] = LMR_N[d][0] = 0; }
}
inline int lmr_q(int d, int m) { return LMR_Q[std::min(d, LMRD - 1)][std::min(m, LMRD - 1)]; }
inline int lmr_n(int d, int m) { return LMR_N[std::min(d, LMRD - 1)][std::min(m, LMRD - 1)]; }

// ─────────────────────────────────────────────────────────────────────────────
//  C2: transposition table — 4 entries x 16 B per 64 B cluster (one cache
//  line), mulhi64 indexing (any cluster count), 5-bit generation, pv flag,
//  int16 scores. SF-style replacement.
// ─────────────────────────────────────────────────────────────────────────────
enum Bound : uint8_t { BOUND_NONE = 0, BOUND_UPPER, BOUND_LOWER, BOUND_EXACT };

struct TTEntry {
    uint16_t key16;
    uint16_t move16;
    int16_t  score;
    int16_t  eval;
    uint8_t  depth;
    uint8_t  gen_bound;      // gen:5 | pv:1 | bound:2
    uint8_t  pad[6];
};
static_assert(sizeof(TTEntry) == 16, "TTEntry must be exactly 16 bytes");

struct alignas(64) TTCluster { TTEntry e[4]; };

static inline uint16_t move_to_u16(const Move& m) { return m.move(); }
static inline Move     u16_to_move(uint16_t r)    { return Move(r); }

// Single 16-byte copy in each direction (SSE2 movdqa) — practical atomicity.
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

static inline uint64_t mulhi64(uint64_t a, uint64_t b) {
    return (uint64_t)(((__uint128_t)a * (__uint128_t)b) >> 64);
}

static TTCluster* tt         = nullptr;
static size_t     g_tt_clusters = 1 << 16;   // 4 MB default
static uint8_t    tt_generation = 0;

inline size_t tt_cluster_of(uint64_t key) { return mulhi64(key, g_tt_clusters); }
inline void   tt_prefetch(uint64_t key)   { __builtin_prefetch(&tt[tt_cluster_of(key)]); }

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

void tt_clear_parallel();

void tt_free() {
    if (tt) { aligned_free_raw(tt); tt = nullptr; }
}
void tt_resize(size_t mb) {
    size_t want = std::max<size_t>(mb, 1) * 1024 * 1024 / sizeof(TTCluster);
    TTCluster* p = static_cast<TTCluster*>(aligned_alloc_raw(64, want * sizeof(TTCluster)));
    if (!p) return;                          // keep the old table on OOM
    tt_free();
    tt = p;
    g_tt_clusters = want;
    tt_clear_parallel();
}
void tt_clear_parallel() {
    if (!tt) return;
    unsigned hw = std::thread::hardware_concurrency();
    int nth = (int)std::clamp(hw ? hw : 1u, 1u, 16u);
    auto worker = [&](int id) {
        size_t chunk = (g_tt_clusters + nth - 1) / nth;
        size_t lo = std::min<size_t>(id * chunk, g_tt_clusters);
        size_t hi = std::min(lo + chunk, g_tt_clusters);
        std::memset(tt + lo, 0, (hi - lo) * sizeof(TTCluster));
    };
    std::vector<std::thread> th;
    for (int i = 1; i < nth; ++i) th.emplace_back(worker, i);
    worker(0);
    for (auto& t : th) t.join();
    tt_generation = 0;
}

struct TTData {
    bool     hit;
    Move     move;
    int      score;
    int      eval;
    int      depth;
    Bound    bound;
    bool     pv;
};
struct TTWriter {
    TTEntry* e;
    bool     found;
};

// C2 probe: return data for a key16 match (any generation — stale entries are
// still position-correct); on a miss, hand back the replacement candidate.
TTData tt_probe(uint64_t key) {
    TTCluster& c = tt[tt_cluster_of(key)];
    const uint16_t k16 = (uint16_t)key;
    for (int i = 0; i < 4; ++i) {
        TTEntry e = tt_read(c.e[i]);
        if (e.key16 == k16 && e.gen_bound != 0) {
            TTData d;
            d.hit   = true;
            d.move  = u16_to_move(e.move16);
            d.score = e.score;
            d.eval  = e.eval;
            d.depth = e.depth;
            d.bound = (Bound)(e.gen_bound & 0x03);
            d.pv    = (e.gen_bound >> 2) & 1;
            return d;
        }
    }
    // replacement candidate: lowest (depth - 8*relative_age)
    int best = 0, bestv = 1 << 30;
    for (int i = 0; i < 4; ++i) {
        TTEntry e = tt_read(c.e[i]);
        int age  = (int)((tt_generation - (e.gen_bound >> 3)) & 31);
        int v    = (int)e.depth - 8 * age;
        if (v < bestv) { bestv = v; best = i; }
    }
    TTData d;
    d.hit   = false;
    d.move  = Move::NO_MOVE;
    d.score = 0;
    d.eval  = NO_EVAL;
    d.depth = 0;
    d.bound = BOUND_NONE;
    d.pv    = false;
    (void)best;
    return d;
}
// C2 store rule (SF): always overwrite on a key match with an exact bound or a
// big depth jump; keep the old move if the new one is empty. Never touch other
// keys' entries unless the replacement candidate is clearly worse.
void tt_store(uint64_t key, Move move, int score, int eval, int depth, Bound bound, bool pv, int ply) {
    TTCluster& c = tt[tt_cluster_of(key)];
    const uint16_t k16 = (uint16_t)key;
    int slot = -1;
    for (int i = 0; i < 4; ++i) {
        TTEntry e = tt_read(c.e[i]);
        if (e.key16 == k16 || (e.gen_bound & 0x03) == BOUND_NONE) { slot = i; break; }
    }
    if (slot < 0) {
        int best = 0, bestv = 1 << 30;
        for (int i = 0; i < 4; ++i) {
            TTEntry e = tt_read(c.e[i]);
            int age = (int)((tt_generation - (e.gen_bound >> 3)) & 31);
            int v   = (int)e.depth - 8 * age;
            if (v < bestv) { bestv = v; best = i; }
        }
        slot = best;
    }
    TTEntry old = tt_read(c.e[slot]);
    bool key_ok = (old.key16 == k16);
    bool replace = !key_ok
                || bound == BOUND_EXACT
                || depth + 2 + 2 * pv > (int)old.depth
                || (old.gen_bound >> 3) != tt_generation
                || (old.gen_bound & 0x03) == BOUND_NONE;
    if (!replace) return;
    TTEntry n;
    n.key16  = k16;
    uint16_t mv16 = move_to_u16(move);
    if (key_ok && mv16 == 0 && old.move16 != 0) mv16 = old.move16;   // keep the old move
    n.move16     = mv16;
    n.score      = (int16_t)std::clamp(score_to_tt(score, ply), -32767, 32767);
    n.eval       = (int16_t)std::clamp(eval, -32768, 32767);   // NO_EVAL sentinel preserved
    n.depth      = (uint8_t)std::clamp(depth, 0, 255);
    n.gen_bound  = (uint8_t)((tt_generation << 3) | (pv ? 4 : 0) | (uint8_t)bound);
    tt_write(c.e[slot], n);
}

int tt_hashfull() {
    if (!tt) return 0;
    int used = 0;
    size_t stride = g_tt_clusters / 1000;
    if (!stride) stride = 1;
    for (int i = 0; i < 1000; ++i) {
        TTCluster& c = tt[(size_t)i * stride];
        for (int j = 0; j < 4; ++j) {
            TTEntry e = tt_read(c.e[j]);
            if ((e.gen_bound & 0x03) != BOUND_NONE && (e.gen_bound >> 3) == tt_generation) ++used;
        }
    }
    return used / 4;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Globals shared by search/UCI
// ─────────────────────────────────────────────────────────────────────────────
static nnue::Network g_nnue;
static std::string   g_nnue_path = "nn-1111cefa1111.nnue";
static int           g_nnue_scale_pct = 40;    // NNUEScale option (F5: keep, one scale)

// ─────────────────────────────────────────────────────────────────────────────
//  C1: search stack. Guarded entries: the worker hands out ss = stacks + 8, so
//  ss[-1..-8] are valid and ply-1..ply-6 lookups never need range checks.
// ─────────────────────────────────────────────────────────────────────────────
struct Stack {
    Move      move        = Move::NO_MOVE;
    PieceType moved_pt    = PieceType::NONE;
    int       to          = -1;
    int       static_eval = NO_EVAL;
    int       raw_eval    = NO_EVAL;
    bool      in_check    = false;
    bool      was_capture = false;
    bool      tt_pv       = false;
    bool      tt_capture  = false;
    int       cutoff_cnt  = 0;
    int       reduction   = 0;
    int       dext        = 0;        // double-extension budget along this line
    int       move_count  = 0;        // A17: moves actually considered
    Move      excluded    = Move::NO_MOVE;
    Move      killers[2]  = {Move::NO_MOVE, Move::NO_MOVE};
    Move      pv[MAX_PLY];
    int       pv_len      = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  C3/C5/C7: per-thread state. ~5 MB, lazily allocated per requested thread
//  (A19). All histories are int16 and persist across searches (A2).
// ─────────────────────────────────────────────────────────────────────────────
constexpr int CORR_SIZE = 16384;   // 2^14, indexed by mix64(key) >> 50

struct AccEntry {
    nnue::Accumulator acc;
    bool              computed = false;
};

struct ThreadState {
    Stack stacks[MAX_PLY + 8];

    // histories (C3)
    int16_t mainHist[2][2][2][64][64];              // [stm][from_threat][to_threat][from][to]
    int16_t pawnHist[2][1024][6][64];               // [stm][pawn_key][pt][to]
    int16_t contHist[2][2][6][64][6][64];           // [prev_in_check][prev_capture][prev_pt][prev_to][pt][to]
    int16_t capHist[7][64][7];                      // [attacker_pt][to][victim_pt]
    int16_t capContHist[6][64][6][64];              // [prev_pt][prev_to][pt][to]
    Move    counterMoves[2][6][64];

    // correction history (C5)
    int16_t pawnCorr[2][CORR_SIZE];
    int16_t minorCorr[2][CORR_SIZE];
    int16_t nonPawnCorr[2][2][CORR_SIZE];           // [stm][color_of_material]
    int16_t contCorr[6][64];                        // keyed by the previous move

    // lazy NNUE accumulator stack (F1)
    AccEntry accStack[MAX_PLY + 8];
    Move     accMoves[MAX_PLY + 8];
    int      accDepth = 0;

    // per-search state
    uint64_t nodes = 0;
    int      seldepth = 0;
    int      thread_id = 0;
    int      nmp_min_ply = 0;
    uint64_t root_nodes[64][64];                    // D15: node effort per root move
    Move     root_best = Move::NO_MOVE;             // A11
    int      root_score = -INF;
    bool     root_best_updated = false;

    void clear_history() {
        std::memset(mainHist,     0, sizeof(mainHist));
        std::memset(pawnHist,     0, sizeof(pawnHist));
        std::memset(contHist,     0, sizeof(contHist));
        std::memset(capHist,      0, sizeof(capHist));
        std::memset(capContHist,  0, sizeof(capContHist));
        std::memset(counterMoves, 0, sizeof(counterMoves));
        std::memset(pawnCorr,     0, sizeof(pawnCorr));
        std::memset(minorCorr,    0, sizeof(minorCorr));
        std::memset(nonPawnCorr,  0, sizeof(nonPawnCorr));
        std::memset(contCorr,     0, sizeof(contCorr));
    }
};

static std::vector<std::unique_ptr<ThreadState>> g_threads;
static int g_num_threads = 1;

// ─────────────────────────────────────────────────────────────────────────────
//  History helpers (C3)
// ─────────────────────────────────────────────────────────────────────────────
static const int SEE_VAL[7] = { 100, 320, 330, 500, 900, 20000, 0 };

constexpr int HIST_LIMIT = 16384;
inline void add_hist(int16_t& h, int bonus) {
    h = (int16_t)std::clamp((int)h + bonus - (int)h * std::abs(bonus) / HIST_LIMIT,
                            -HIST_LIMIT, HIST_LIMIT);
}
inline int history_bonus(int d) { return std::min(hist_bonus_a * d - hist_bonus_b, hist_bonus_cap); }
inline int history_malus(int d) { return std::min(hist_malus_a * d - hist_malus_b, hist_malus_cap); }
inline int mvv_lva(PieceType a, PieceType v) { return SEE_VAL[(int)v] * 10 - SEE_VAL[(int)a]; }

// cont plies and weights (C3): plies 1,2,3,4,6 with weights 16,16,8,8,4
constexpr int CONT_BACK[5] = {1, 2, 3, 4, 6};
constexpr int CONT_W[5]    = {16, 16, 8, 8, 4};

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
// weighted sum over the cont plies used for ordering
inline int cont_hist_sum(ThreadState& ts, const Stack* ss, int ply, int pt, int to) {
    int h = 0;
    for (int i = 0; i < 5; ++i) h += CONT_W[i] * cont_hist_at(ts, CONT_BACK[i], ss, ply, pt, to);
    return h / 16;
}
inline void cont_hist_update(ThreadState& ts, const Stack* ss, int ply, int pt, int to,
                             int bonus, bool with_weights) {
    for (int i = 0; i < 5; ++i) {
        int16_t* s = cont_slot(ts, CONT_BACK[i], ss, ply, pt, to);
        if (s) add_hist(*s, with_weights ? bonus * CONT_W[i] / 16 : bonus);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  A3/C5: correction history — stm dimension, high-bit mixed indices, int16
//  tables. Applied sum lives in 1/corr_grain-cp units.
// ─────────────────────────────────────────────────────────────────────────────
constexpr int CORR_CAP = 8 * 1024;   // per-entry cap in 1/grain units

inline uint64_t mix64(uint64_t x) {
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}
inline int corr_idx(uint64_t key) { return (int)(mix64(key) >> 50); }   // 14 bits

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
    if (!g_nnue.loaded) return 0;
    const int stm = (int)b.sideToMove();
    int pawn  = ts.pawnCorr[stm][corr_idx(pawn_corr_key(b))];
    int minor = ts.minorCorr[stm][corr_idx(minor_corr_key(b))];
    int npW   = ts.nonPawnCorr[stm][0][corr_idx(nonpawn_corr_key(b, Color::WHITE))];
    int npB   = ts.nonPawnCorr[stm][1][corr_idx(nonpawn_corr_key(b, Color::BLACK))];
    int cont  = 0;
    if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
        cont = ts.contCorr[(int)ss[ply - 1].moved_pt][ss[ply - 1].to];
    int sum = corr_weight_pawn * pawn + corr_weight_minor * minor
            + corr_weight_np * npW + corr_weight_np * npB + corr_weight_cont * cont;
    return sum / (corr_grain * 32);
}

inline void update_corr_entry(int16_t& e, int diff, int depth) {
    int target = std::clamp(diff * depth * corr_grain / 8, -corr_limit / 4, corr_limit / 4);
    e = (int16_t)std::clamp((int)e + target - (int)e * std::abs(target) / corr_limit,
                            -CORR_CAP, CORR_CAP);
}

// best_is_capture: gate (C5) — quiet best moves only
inline void update_correction(ThreadState& ts, const Board& b, const Stack* ss, int ply,
                              int best, int static_eval, int depth, int alpha, int beta,
                              bool best_is_capture) {
    if (!g_nnue.loaded || std::abs(best) >= MATE_BOUND || best_is_capture) return;
    // bound-agreement skip (C5): the score is fully explained by the static eval
    if ((best >= beta && best <= static_eval) || (best <= alpha && best >= static_eval)) return;
    const int stm = (int)b.sideToMove();
    int diff = best - static_eval;
    update_corr_entry(ts.pawnCorr[stm][corr_idx(pawn_corr_key(b))],   diff, depth);
    update_corr_entry(ts.minorCorr[stm][corr_idx(minor_corr_key(b))], diff, depth);
    update_corr_entry(ts.nonPawnCorr[stm][0][corr_idx(nonpawn_corr_key(b, Color::WHITE))], diff, depth);
    update_corr_entry(ts.nonPawnCorr[stm][1][corr_idx(nonpawn_corr_key(b, Color::BLACK))], diff, depth);
    if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
        update_corr_entry(ts.contCorr[(int)ss[ply - 1].moved_pt][ss[ply - 1].to], diff, depth);
}

// forward decl (implementation below)
bool see_ge(const Board& b, Move move, int threshold = 0);

// ─────────────────────────────────────────────────────────────────────────────
//  Threat mask (C3): squares attacked by an enemy pawn / minor / rook,
//  computed once per node for the threat-indexed main history.
// ─────────────────────────────────────────────────────────────────────────────
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

// main history including the threat dimensions (C3)
inline int main_hist(ThreadState& ts, const Board& b, const Move& m, const Bitboard& threat) {
    int c = (int)b.sideToMove();
    int f = m.from().index(), t = m.to().index();
    bool ft = (threat.getBits() >> f) & 1, tt2 = (threat.getBits() >> t) & 1;
    return ts.mainHist[c][ft][tt2][f][t];
}
inline void main_hist_update(ThreadState& ts, const Board& b, const Move& m,
                             const Bitboard& threat, int bonus) {
    int c = (int)b.sideToMove();
    int f = m.from().index(), t = m.to().index();
    bool ft = (threat.getBits() >> f) & 1, tt2 = (threat.getBits() >> t) & 1;
    add_hist(ts.mainHist[c][ft][tt2][f][t], bonus);
}

// ─────────────────────────────────────────────────────────────────────────────
//  C4: staged MovePicker. Stages: TT → noisy (SEE-split, lazily) →
//  killers/counter → quiets → bad noisy. Evasion mode (in check) and QS mode
//  are subsets. All legal moves are generated once (movegen is already fast
//  and legal); staging removes the per-node full scoring + insertion sort.
// ─────────────────────────────────────────────────────────────────────────────
struct MovePicker {
    enum Stage { ST_TT = 0, ST_NOISY, ST_KILLERS, ST_QUIET, ST_BAD_NOISY, ST_DONE };

    MovePicker(const Board& bd, ThreadState& t, const Stack* s, int pl, Move ttm,
               bool qs, bool evades, Bitboard thr)
        : b(bd), ts(t), ss(s), ply(pl), ttmove(ttm), qs_mode(qs), evasion(evades), threat(thr) {

        movegen::legalmoves(moves, b);

        for (const Move& m : moves) {
            bool cap = b.isCapture(m);
            bool qp  = (m.typeOf() == Move::PROMOTION && m.promotionType() == PieceType::QUEEN);
            if (cap || qp) { noisy[n_noisy] = m; nsc[n_noisy++] = 0; }
            else           { quiet[n_quiet] = m; qsc[n_quiet++] = 0; }
        }
        noisy_done = quiet_done = killers_done = 0;
        stage = ST_TT;

        // score noisy (MVV-LVA + capHist + capCont), quiets (history) up front;
        // SEE splitting happens lazily at pick time (SF-style).
        for (int i = 0; i < n_noisy; ++i) {
            const Move& m = noisy[i];
            if (m == ttmove) { nsc[i] = 1 << 30; continue; }
            PieceType att = b.at<PieceType>(m.from());
            PieceType vic = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
                          : ((m.typeOf() == Move::PROMOTION && b.at(m.to()) == Piece::NONE)
                             ? PieceType::PAWN : b.at<PieceType>(m.to()));
            int ch = ts.capHist[(int)att][m.to().index()][(int)vic];
            if (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
                ch += ts.capContHist[(int)ss[ply - 1].moved_pt][ss[ply - 1].to][(int)att][m.to().index()];
            nsc[i] = ch + mvv_lva(att, vic)
                   + ((m.typeOf() == Move::PROMOTION) ? 800 + (int)SEE_VAL[(int)m.promotionType()] : 0);
        }
        if (!evasion && !qs_mode) {
            for (int i = 0; i < n_quiet; ++i) {
                const Move& m = quiet[i];
                if (m == ttmove) { qsc[i] = 1 << 30; continue; }
                PieceType pt2 = b.at<PieceType>(m.from());
                int h = main_hist(ts, b, m, threat)
                      + ts.pawnHist[(int)b.sideToMove()][pawn_hist_idx(b)][(int)pt2][m.to().index()]
                      + cont_hist_sum(ts, ss, ply, (int)pt2, m.to().index());
                if (m.typeOf() == Move::PROMOTION) h -= 8000;   // underpromotions sink
                qsc[i] = h;
            }
        }
    }

    int pawn_hist_idx(const Board& bd) const {
        return (int)(mix64(pawn_corr_key(bd)) >> 54);   // 10 bits
    }

    void skip_quiets() { skip_q = true; }

    bool legal_contains(const Move& m) const {
        for (const Move& x : moves) if (x == m) return true;
        return false;
    }

    Move next_move() {
        while (true) {
            switch (stage) {
            case ST_TT: {
                stage = ST_NOISY;
                if (ttmove != Move::NO_MOVE && legal_contains(ttmove)) return ttmove;
                break;
            }
            case ST_NOISY: {
                if (evasion) {
                    // evasion mode: single stage over all moves, by score
                    // (noisy already scored; quiets scored by history here)
                    if (!qs_scored) {
                        qs_scored = true;
                        for (int i = 0; i < n_quiet; ++i) {
                            const Move& m = quiet[i];
                            PieceType pt2 = b.at<PieceType>(m.from());
                            qsc[i] = main_hist(ts, b, m, threat)
                                   + cont_hist_sum(ts, ss, ply, (int)pt2, m.to().index());
                        }
                    }
                    // merge-pick best of noisy/quiet
                    int bi = -1, bv = -1 << 30; bool from_noisy = false;
                    for (int i = noisy_done; i < n_noisy; ++i)
                        if (nsc[i] > bv) { bv = nsc[i]; bi = i; from_noisy = true; }
                    for (int i = quiet_done; i < n_quiet; ++i)
                        if (qsc[i] > bv) { bv = qsc[i]; bi = i; from_noisy = false; }
                    if (bi < 0) { stage = qs_mode ? ST_DONE : ST_KILLERS; break; }
                    if (from_noisy) { std::swap(noisy[bi], noisy[noisy_done]); std::swap(nsc[bi], nsc[noisy_done]); return noisy[noisy_done++]; }
                    std::swap(quiet[bi], quiet[quiet_done]); std::swap(qsc[bi], qsc[quiet_done]);
                    return quiet[quiet_done++];
                }
                if (qs_mode && noisy_done >= n_noisy) { stage = ST_DONE; break; }
                // lazily pick the best remaining noisy; SEE failures go to bad[]
                while (noisy_done < n_noisy) {
                    int bi = noisy_done;
                    for (int i = noisy_done + 1; i < n_noisy; ++i) if (nsc[i] > nsc[bi]) bi = i;
                    std::swap(noisy[bi], noisy[noisy_done]); std::swap(nsc[bi], nsc[noisy_done]);
                    Move m = noisy[noisy_done];
                    if (m == ttmove) { ++noisy_done; continue; }
                    int thr = -nsc[noisy_done] / 18;
                    if (see_ge(b, m, thr)) return noisy[noisy_done++];
                    bad[n_bad++] = m;
                    ++noisy_done;
                }
                stage = qs_mode ? ST_DONE : ST_KILLERS;
                break;
            }
            case ST_KILLERS: {
                if (killers_done < 3) {
                    Move cands[3];
                    cands[0] = ss[ply].killers[0];
                    cands[1] = ss[ply].killers[1];
                    cands[2] = (ply >= 1 && ss[ply - 1].moved_pt != PieceType::NONE && ss[ply - 1].to >= 0)
                             ? ts.counterMoves[(int)b.sideToMove()][(int)ss[ply - 1].moved_pt][ss[ply - 1].to]
                             : Move::NO_MOVE;
                    while (killers_done < 3) {
                        Move m = cands[killers_done++];
                        if (m == Move::NO_MOVE || m == ttmove) continue;
                        bool iscap = b.isCapture(m);
                        if (iscap || !legal_contains(m)) continue;
                        return m;
                    }
                }
                stage = ST_QUIET;
                break;
            }
            case ST_QUIET: {
                if (skip_q || quiet_done >= n_quiet) { stage = ST_BAD_NOISY; break; }
                int bi = quiet_done;
                for (int i = quiet_done + 1; i < n_quiet; ++i) if (qsc[i] > qsc[bi]) bi = i;
                std::swap(quiet[bi], quiet[quiet_done]); std::swap(qsc[bi], qsc[quiet_done]);
                Move m = quiet[quiet_done++];
                if (m == ttmove) break;   // pick again next call (loop)
                return m;
            }
            case ST_BAD_NOISY: {
                if (bad_i < n_bad) return bad[bad_i++];
                stage = ST_DONE;
                break;
            }
            default:
                return Move::NO_MOVE;
            }
        }
    }

    const Board&   b;
    ThreadState&   ts;
    const Stack*   ss;
    int            ply;
    Move           ttmove;
    bool           qs_mode, evasion;
    bool           skip_q = false;
    bool           qs_scored = false;
    Stage          stage;
    Movelist       moves;
    Bitboard       threat;
    Move           noisy[220]; int n_noisy = 0, noisy_done = 0;
    Move           quiet[220]; int n_quiet = 0, quiet_done = 0;
    Move           bad[220];   int n_bad = 0,   bad_i = 0;
    int            nsc[220], qsc[220];
    int            killers_done = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
//  A18: pin-aware SEE (SF-structured swap list over backened's tables).
//  Pinned pieces may not recapture while their pinner is still on its original
//  square; x-rays are covered by recomputing slider attacks against the
//  shrinking occupancy.
// ─────────────────────────────────────────────────────────────────────────────
inline void compute_pins(const Board& b, Bitboard blockers[2], Bitboard pinners[2]) {
    blockers[0] = blockers[1] = pinners[0] = pinners[1] = Bitboard(0ULL);
    const std::uint64_t occ = b.occ().getBits();
    for (Color c : {Color::WHITE, Color::BLACK}) {
        Square ksq = b.kingSq(c);
        std::uint64_t snipers = (bk::ST.pseudoBishop[ksq.index()] & (b.pieces(PieceType::BISHOP, ~c) | b.pieces(PieceType::QUEEN, ~c)).getBits())
                    | (bk::ST.pseudoRook[ksq.index()]   & (b.pieces(PieceType::ROOK,   ~c) | b.pieces(PieceType::QUEEN, ~c)).getBits());
        std::uint64_t sn = snipers;
        while (sn) {
            int s = bk::ctz64(sn); sn &= sn - 1;
            std::uint64_t between = bk::ST.between[ksq.index()][s] & occ & ~(1ULL << s);
            if (bk::pop64(between) == 1) {
                int bsq = bk::ctz64(between);
                if ((b.at(Square(bsq)) != Piece::NONE) && (int)b.at(Square(bsq)).color() == (int)c) {
                    blockers[(int)c] |= Bitboard(1ULL << bsq);
                    pinners[(int)c]  |= Bitboard(1ULL << s);
                }
            }
        }
    }
}

bool see_ge(const Board& b, Move move, int threshold) {
    if (move.typeOf() == Move::CASTLING) return true;
    Square from = move.from(), to = move.to();
    Piece movedPiece = b.at(from);
    if (movedPiece == Piece::NONE) return true;
    PieceType attackerPT = movedPiece.type();

    PieceType capturedPT = (move.typeOf() == Move::ENPASSANT) ? PieceType::PAWN : b.at<PieceType>(to);
    int balance = ((capturedPT == PieceType::NONE) ? 0 : SEE_VAL[(int)capturedPT]) - threshold;
    if (balance < 0) return false;
    PieceType nextVictim = attackerPT;
    if (move.typeOf() == Move::PROMOTION) {
        balance += SEE_VAL[(int)move.promotionType()] - SEE_VAL[(int)PieceType::PAWN];
        nextVictim = move.promotionType();
    }
    balance = SEE_VAL[(int)nextVictim] - balance;
    if (balance <= 0) return true;

    std::uint64_t occRaw = b.occ().getBits();
    occRaw &= ~(1ULL << from.index());
    occRaw |=  (1ULL << to.index());
    if (move.typeOf() == Move::ENPASSANT) {
        int capSq = to.index() + ((b.sideToMove() == Color::WHITE) ? -8 : 8);
        occRaw &= ~(1ULL << capSq);
    }
    const std::uint64_t origOcc = b.occ().getBits();
    Bitboard occ(occRaw);

    Bitboard blockers[2], pinners[2];
    compute_pins(b, blockers, pinners);

    Color side = ~b.sideToMove();
    const Bitboard bq = b.pieces(PieceType::BISHOP) | b.pieces(PieceType::QUEEN);
    const Bitboard rq = b.pieces(PieceType::ROOK)   | b.pieces(PieceType::QUEEN);
    auto get_atk = [&](Bitboard o) {
        Bitboard a(0ULL);
        a |= attacks::pawn(Color::BLACK, to) & b.pieces(PieceType::PAWN, Color::WHITE);
        a |= attacks::pawn(Color::WHITE, to) & b.pieces(PieceType::PAWN, Color::BLACK);
        a |= attacks::knight(to) & b.pieces(PieceType::KNIGHT);
        a |= attacks::bishop(to, o) & bq;
        a |= attacks::rook(to, o)   & rq;
        a |= attacks::king(to) & b.pieces(PieceType::KING);
        return a & o;
    };
    Bitboard atk = get_atk(occ);
    bool relStm = true;
    while (true) {
        Bitboard sa = atk & b.us(side);
        // A18: exclude this side's pinned pieces while every pinner that pins
        // them still stands on its original square (SF rule).
        if (!(pinners[(int)side].getBits() & ~occ.getBits()))
            sa &= ~blockers[(int)side];
        if (!sa) break;
        PieceType pt = PieceType::NONE;
        Square    asq(0);
        for (PieceType c : {PieceType::PAWN, PieceType::KNIGHT, PieceType::BISHOP,
                            PieceType::ROOK, PieceType::QUEEN, PieceType::KING}) {
            Bitboard cbb = sa & b.pieces(c, side);
            if (cbb) { pt = c; asq = Square(cbb.lsb()); break; }
        }
        if (pt == PieceType::NONE) break;
        occ &= ~Bitboard(1ULL << asq.index());
        atk = get_atk(occ);        // x-ray attackers appear automatically
        relStm = !relStm;
        balance = -balance - 1 - SEE_VAL[(int)nextVictim];
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

// ─────────────────────────────────────────────────────────────────────────────
//  Eval glue. eval_material_simple stays as a debug/no-net fallback (H).
// ─────────────────────────────────────────────────────────────────────────────
uint64_t total_nodes();   // defined with the SMP code

static const int PIECE_VAL_MG[7] = { 100, 320, 330, 500, 900, 20000, 0 };
static const int PIECE_VAL_EG[7] = { 120, 300, 320, 550, 950, 20000, 0 };

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

// F5: SF17 combine, NNUEScale conversion, 50-move damping, mate-bound clamp.
// Returns the RAW eval (correction history is applied by the callers).
inline int nnue_raw_from_acc(ThreadState& ts, const Board& b, int d) {
    int psqt, positional;
    g_nnue.eval_parts(ts.accStack[d].acc, b.sideToMove(), (int)b.occ().count(), psqt, positional);
    int raw = (125 * psqt + 131 * positional) / 128;
    int e = raw * g_nnue_scale_pct / 100;
    int hfm = (int)b.halfMoveClock();
    if (hfm > 90) e = e * std::max(0, 200 - hfm) / 200;
    return std::clamp(e, -(MATE_BOUND - 1), MATE_BOUND - 1);
}

int evaluate_tree(Board& b, ThreadState& ts);
int evaluate(const Board& b);

// ─────────────────────────────────────────────────────────────────────────────
//  A1/F1: move wrappers with LAZY accumulator updates. do_move only pushes a
//  record (no 12 KB copy, no row math); evaluate_tree() walks back to the
//  nearest computed ancestor on the real board and rolls forward with the
//  proven-correct eager primitives (apply_move / refresh). Null moves touch
//  nothing — HalfKAv2 features are stm-independent.
// ─────────────────────────────────────────────────────────────────────────────
inline void do_move(Board& b, const Move& m, ThreadState& ts) {
    if (g_nnue.loaded) {
        ++ts.accDepth;
        ts.accMoves[ts.accDepth] = m;
        ts.accStack[ts.accDepth].computed = false;
    }
    b.makeMove(m);
}
inline void undo_move(Board& b, const Move& m, ThreadState& ts) {
    b.unmakeMove(m);
    if (g_nnue.loaded) --ts.accDepth;
}
// Null moves DO occupy an accumulator-stack slot (with a NO_MOVE marker and
// unchanged features) so the lazy walk-back can unwind them correctly.
inline void do_null_move(Board& b, ThreadState& ts) {
    if (g_nnue.loaded) {
        ++ts.accDepth;
        ts.accMoves[ts.accDepth] = Move::NO_MOVE;
        ts.accStack[ts.accDepth].computed = false;
    }
    b.makeNullMove();
}
inline void undo_null_move(Board& b, ThreadState& ts) {
    b.unmakeNullMove();
    if (g_nnue.loaded) --ts.accDepth;
}

#if defined(NNUE_VERIFY)
#include <cassert>
static void nnue_verify_node(Board& b, ThreadState& ts, int d) {
    nnue::Accumulator fresh;
    g_nnue.refresh(b, fresh);
    const auto& a = ts.accStack[d].acc;
    if (std::memcmp(fresh.side[0].acc.data(), a.side[0].acc.data(), sizeof(int16_t) * 3072) != 0 ||
        std::memcmp(fresh.side[1].acc.data(), a.side[1].acc.data(), sizeof(int16_t) * 3072) != 0) {
        std::cout << "info string NNUE_VERIFY accumulator mismatch at accDepth " << d << "\n";
        std::cout << "fen " << b.getFen() << "\n";
        std::abort();
    }
    int p1, q1, p2, q2;
    g_nnue.eval_parts(a, b.sideToMove(), (int)b.occ().count(), p1, q1);
    g_nnue.eval_parts_ref(a, b.sideToMove(), (int)b.occ().count(), p2, q2);
    if (p1 != p2 || q1 != q2) {
        std::cout << "info string NNUE_VERIFY SIMD != scalar (psqt " << p1 << "/" << p2
                  << ", pos " << q1 << "/" << q2 << ")\n";
        std::cout << "fen " << b.getFen() << "\n";
        std::abort();
    }
}
#else
inline void nnue_verify_node(Board&, ThreadState&, int) {}
#endif

int evaluate_tree(Board& b, ThreadState& ts) {
    if (!g_nnue.loaded) return eval_material_simple(b);
    const int d = ts.accDepth;
    if (!ts.accStack[d].computed) {
        int k = d;
        while (k > 0 && !ts.accStack[k].computed) --k;
        // unmake the real moves from d down to k+1 (board ends up at depth k)
        for (int p = d; p > k; --p) {
            Move m = ts.accMoves[p];
            if (m == Move::NO_MOVE) b.unmakeNullMove();
            else b.unmakeMove(m);
        }
        if (!ts.accStack[k].computed) {           // only possible at k == 0
            g_nnue.refresh(b, ts.accStack[0].acc);
            ts.accStack[0].computed = true;
        }
        // roll forward eagerly with the proven-correct primitives
        for (int p = k + 1; p <= d; ++p) {
            Move m = ts.accMoves[p];
            nnue::Accumulator& dst = ts.accStack[p].acc;
            if (m == Move::NO_MOVE) {
                dst = ts.accStack[p - 1].acc;      // a null move changes no features
                b.makeNullMove();
            } else if (m.typeOf() == Move::CASTLING) {
                b.makeMove(m);
                g_nnue.refresh(b, dst);
            } else {
                g_nnue.apply_move(dst, ts.accStack[p - 1].acc, b, m);
                b.makeMove(m);
            }
            ts.accStack[p].computed = true;
        }
    }
    nnue_verify_node(b, ts, d);
    return nnue_raw_from_acc(ts, b, d);
}

int evaluate(const Board& b) {
    if (g_nnue.loaded) {
        nnue::Accumulator acc;
        g_nnue.refresh(b, acc);
        int psqt, positional;
        g_nnue.eval_parts(acc, b.sideToMove(), (int)b.occ().count(), psqt, positional);
        int raw = (125 * psqt + 131 * positional) / 128;
        int e = raw * g_nnue_scale_pct / 100;
        int hfm = (int)b.halfMoveClock();
        if (hfm > 90) e = e * std::max(0, 200 - hfm) / 200;
        return std::clamp(e, -(MATE_BOUND - 1), MATE_BOUND - 1);
    }
    return eval_material_simple(b);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Shared search state
// ─────────────────────────────────────────────────────────────────────────────
struct SearchInfo {
    std::atomic<bool> stop{false};
    std::atomic<uint64_t> node_limit{0};
    int  soft_time = 0;
    int  hard_time = 0;
    bool fixed_movetime = false;
    bool infinite = false;
    std::chrono::steady_clock::time_point start_time;
    bool time_up() const {
        if (hard_time <= 0) return false;
        auto e = std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - start_time).count();
        return e >= hard_time;
    }
};
static SearchInfo info;

static std::thread g_search_thread;
static int g_pending_soft = 0, g_pending_hard = 0;

void wait_for_search_end() {
    info.stop.store(true, std::memory_order_relaxed);
    if (g_search_thread.joinable()) g_search_thread.join();
}

inline int elapsed_ms() {
    return (int)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - info.start_time).count();
}

// ─────────────────────────────────────────────────────────────────────────────
//  D12: quiescence search
// ─────────────────────────────────────────────────────────────────────────────
int qsearch(Board& b, int alpha, int beta, int ply, ThreadState& ts, Stack* ss) {
    ts.nodes++;
    if (ts.thread_id == 0 && (ts.nodes & 1023) == 0) {
        if (info.time_up() ||
            (info.node_limit > 0 &&
             total_nodes() >= info.node_limit.load(std::memory_order_relaxed)))
            info.stop.store(true, std::memory_order_relaxed);
    }
    if (info.stop.load(std::memory_order_relaxed)) return 0;

    if (ply > ts.seldepth) ts.seldepth = ply;
    if (ply >= MAX_PLY - 1) return evaluate_tree(b, ts);

    const uint64_t key = b.hash();
    tt_prefetch(key);
    const bool is_pv = (beta - alpha > 1);

    ss[ply].pv_len = 0;
    ss[ply].static_eval = NO_EVAL;

    // draw checks (A10)
    if (ply > 0 && (b.isRepetitionSearch(ply) || b.isInsufficientMaterial() ||
                    (b.halfMoveClock() >= 100 && !b.inCheck()))) {
        return DRAW_SCORE + (int)(ts.nodes & 3) - 1;
    }

    // TT (A13: value semantics, fail-soft returns, stored eval reuse)
    Move qtt = Move::NO_MOVE;
    int  raw_eval = NO_EVAL;
    bool have_raw = false;
    {
        TTData td = tt_probe(key);
        ss[ply].tt_pv = is_pv || td.pv;
        if (td.hit) {
            qtt = td.move;
            if (!is_pv) {
                int s = score_from_tt(td.score, ply);
                if (td.bound == BOUND_EXACT) return s;
                if (td.bound == BOUND_UPPER && s <= alpha) return s;
                if (td.bound == BOUND_LOWER && s >= beta)  return s;
            }
            if (td.eval != NO_EVAL) { raw_eval = td.eval; have_raw = true; }
        }
    }

    const bool in_check = b.inCheck();
    if (!in_check && !have_raw) raw_eval = evaluate_tree(b, ts);
    const int corr = (!in_check && g_nnue.loaded) ? get_correction(ts, b, ss, ply) : 0;
    const int stand_pat = in_check ? -INF : raw_eval + corr;
    ss[ply].static_eval = in_check ? NO_EVAL : raw_eval;

    if (!in_check) {
        if (stand_pat >= beta)
            return is_decisive(stand_pat) ? stand_pat : (stand_pat + beta) / 2;
        if (stand_pat > alpha) alpha = stand_pat;
    }

    MovePicker mp(b, ts, ss, ply, qtt, /*qs=*/true, in_check, Bitboard(0ULL));
    if (in_check && mp.moves.empty()) return mated_in(ply);

    int best_score = stand_pat;
    Move best_move = Move::NO_MOVE;
    const int orig_alpha = alpha;
    const int futility_base = stand_pat + qs_fut_base;
    int move_count = 0;

    Move m;
    while ((m = mp.next_move()) != Move::NO_MOVE) {
        const bool is_cap = b.isCapture(m);
        const bool is_promo = m.typeOf() == Move::PROMOTION;

        if (!in_check) {
            if (!is_promo && best_score > -MATE_BOUND) {
                PieceType vic = is_cap ? ((m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
                                                                          : b.at<PieceType>(m.to()))
                                       : PieceType::NONE;
                if (futility_base + SEE_VAL[(int)vic] <= alpha) {
                    best_score = std::max(best_score, futility_base + SEE_VAL[(int)vic]);
                    continue;
                }
                if (futility_base <= alpha && !see_ge(b, m, 1)) continue;
                if (!see_ge(b, m, 0)) continue;
            } else if (!see_ge(b, m, 0)) {
                continue;
            }
        } else if (!is_cap && !is_promo && move_count > 2 && best_score > -MATE_BOUND) {
            break;   // enough quiet evasions tried (D12)
        }

        ++move_count;
        ss[ply].move_count = move_count;
        ss[ply].moved_pt    = b.at<PieceType>(m.from());
        ss[ply].to          = m.to().index();
        ss[ply].in_check    = in_check;
        ss[ply].was_capture = is_cap;
        tt_prefetch(b.zobristAfter(m));

        do_move(b, m, ts);
        ss[ply].move = m;
        int s = -qsearch(b, -beta, -alpha, ply + 1, ts, ss);
        undo_move(b, m, ts);
        if (info.stop.load(std::memory_order_relaxed)) return 0;

        if (s > best_score) {
            best_score = s;
            best_move  = m;
            if (s > alpha) {
                alpha = s;
                if (is_pv) {   // triangular PV (C1)
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
        tt_store(key, best_move, best_score, in_check ? NO_EVAL : raw_eval, 0, f,
                 ss[ply].tt_pv, ply);
    }
    return best_score;
}

// ─────────────────────────────────────────────────────────────────────────────
//  D1..D11: main search. Fail-soft, lazy-eval, correction history, staged
//  picker, unified LMR, singular extensions, standard ProbCut, verified NMP.
// ─────────────────────────────────────────────────────────────────────────────
int negamax(Board& b, int depth, int alpha, int beta, int ply, bool null_ok, bool cut_node,
            ThreadState& ts, Stack* ss, Move excluded = Move::NO_MOVE) {

    // ── D1: entry checks ────────────────────────────────────────────────────
    ts.nodes++;
    if (ts.thread_id == 0 && (ts.nodes & 1023) == 0) {
        if (info.time_up() ||
            (info.node_limit > 0 &&
             total_nodes() >= info.node_limit.load(std::memory_order_relaxed)))
            info.stop.store(true, std::memory_order_relaxed);
    }
    if (info.stop.load(std::memory_order_relaxed)) return 0;

    const bool is_pv   = (beta - alpha > 1);
    const bool is_root = (ply == 0);

    // draw checks (A10): one tree occurrence already draws; 50-move with the
    // checkmate exception; insufficient material. Draw noise avoids blind
    // shuffling (exact 0 at the root).
    if (!is_root) {
        if (b.isRepetitionSearch(ply) || b.isInsufficientMaterial()) {
            return DRAW_SCORE + 2 - (int)(ts.nodes & 3);
        }
        if (b.halfMoveClock() >= 100) {
            auto [reason, result] = b.getHalfMoveDrawType();
            if (reason == GameResultReason::CHECKMATE) return mated_in(ply);
            return DRAW_SCORE + 2 - (int)(ts.nodes & 3);
        }
    }
    if (ply >= MAX_PLY - 1)
        return b.inCheck() ? evaluate_tree(b, ts) : qsearch(b, alpha, beta, ply, ts, ss);
    if (depth <= 0)
        return qsearch(b, alpha, beta, ply, ts, ss);

    // mate distance pruning (kept)
    if (!is_root) {
        int mate_alpha = mated_in(ply);
        int mate_beta  = MATE_SCORE - ply - 1;
        if (alpha < mate_alpha) alpha = mate_alpha;
        if (beta  > mate_beta)  beta  = mate_beta;
        if (alpha >= beta) return alpha;
    }

    const bool in_check = b.inCheck();
    ss[ply].in_check    = in_check;
    ss[ply].move_count  = 0;
    ss[ply].pv_len      = 0;
    ss[ply].dext        = ss[ply - 1].dext;
    ss[ply].reduction   = 0;
    ss[ply].excluded    = excluded;

    const uint64_t key = b.hash();
    tt_prefetch(key);

    // A10: no TT cutoffs near the 50-move draw
    const bool tt_ok = (excluded == Move::NO_MOVE) && b.halfMoveClock() < 90;

    // ── D2: TT probe + cutoff ───────────────────────────────────────────────
    Move   hm = Move::NO_MOVE;
    int    tt_depth = 0, tt_score = 0, tt_eval = NO_EVAL;
    Bound  tt_bound = BOUND_NONE;
    bool   tt_hit = false, tt_pv = false;

    if (tt_ok) {
        TTData td = tt_probe(key);
        if (td.hit) {
            tt_hit   = true;
            hm       = td.move;
            tt_depth = td.depth;
            tt_score = score_from_tt(td.score, ply);
            tt_eval  = td.eval;
            tt_bound = td.bound;
            tt_pv    = td.pv;
            if (!is_pv && tt_depth > depth - (tt_score <= beta ? 1 : 0)) {
                if (tt_bound == BOUND_EXACT) return tt_score;
                if (tt_bound == BOUND_LOWER && tt_score >= beta) return tt_score;
                if (tt_bound == BOUND_UPPER && tt_score <= alpha) return tt_score;
            }
        }
    }
    ss[ply].tt_pv      = is_pv || tt_pv;
    ss[ply].tt_capture = tt_hit && hm != Move::NO_MOVE && b.isCapture(hm);

    // TT-cutoff quiet bonus (D2)
    if (tt_hit && tt_score >= beta && hm != Move::NO_MOVE && !b.isCapture(hm) && !in_check) {
        int bonus = history_bonus(depth) / 2;
        main_hist_update(ts, b, hm, threat_mask(b), bonus);
    }

    // ── D3: static eval + refinement ────────────────────────────────────────
    int raw_eval;
    if (in_check) {
        raw_eval = NO_EVAL;
    } else if (tt_hit && tt_eval != NO_EVAL) {
        raw_eval = tt_eval;
    } else {
        raw_eval = evaluate_tree(b, ts);
        if (tt_ok) tt_store(key, Move::NO_MOVE, 0, raw_eval, 0, BOUND_NONE, ss[ply].tt_pv, ply);
    }
    ss[ply].raw_eval = raw_eval;

    const int corr = (!in_check && g_nnue.loaded) ? get_correction(ts, b, ss, ply) : 0;
    const int static_eval = in_check ? NO_EVAL : raw_eval + corr;
    ss[ply].static_eval = static_eval;

    // eval refinement: use a bound-compatible TT score for pruning decisions
    int eval_p = static_eval;
    if (tt_hit) {
        if (tt_bound == BOUND_EXACT)                                 eval_p = tt_score;
        else if (tt_bound == BOUND_LOWER && tt_score > static_eval)  eval_p = tt_score;
        else if (tt_bound == BOUND_UPPER && tt_score < static_eval)  eval_p = tt_score;
    }

    // improving: ply-2 comparison, or ply-4 when ply-2 was in check
    bool improving = false;
    if (!in_check) {
        if (ply >= 2 && !ss[ply - 2].in_check && ss[ply - 2].static_eval != NO_EVAL)
            improving = static_eval > ss[ply - 2].static_eval;
        else if (ply >= 4 && ss[ply - 4].static_eval != NO_EVAL)
            improving = static_eval > ss[ply - 4].static_eval;
    }
    // opponent worsening flag (D3, computed for completeness)
    const bool opp_worsening = ply >= 1 && ss[ply - 1].static_eval != NO_EVAL &&
                               static_eval + ss[ply - 1].static_eval > 0;
    (void)opp_worsening;

    // hindsight depth adjustment (D3)
    if (ply >= 2 && ss[ply - 1].reduction >= 3 && !in_check &&
        ss[ply - 1].static_eval != NO_EVAL && static_eval + ss[ply - 1].static_eval < 0)
        depth += 1;

    // eval-based history for the previous quiet move (D3)
    if (ply >= 1 && !in_check && !ss[ply - 1].in_check && ss[ply - 1].static_eval != NO_EVAL &&
        ss[ply - 1].move != Move::NO_MOVE && !ss[ply - 1].was_capture &&
        ss[ply - 1].moved_pt != PieceType::NONE) {
        int bonus = std::clamp(-13 * (ss[ply - 1].static_eval + static_eval), -1000, 1000);
        Move pm = ss[ply - 1].move;
        int  c2 = 1 - (int)b.sideToMove();
        int  f2 = pm.from().index(), t2 = pm.to().index();
        Bitboard thr = threat_mask(b);   // enemy of the previous mover ≈ our attackers now
        bool ft = (thr.getBits() >> f2) & 1, tt2 = (thr.getBits() >> t2) & 1;
        add_hist(ts.mainHist[c2][ft][tt2][f2][t2], bonus);
    }

    const bool prune_ok = !is_pv && !in_check && excluded == Move::NO_MOVE && !is_root;
    const bool has_np_material = b.hasNonPawnMaterial(b.sideToMove());

    // ── D4: razoring ────────────────────────────────────────────────────────
    if (prune_ok && depth <= 5 && eval_p < alpha - (razor_r0 + razor_r1 * depth * depth)) {
        int r = qsearch(b, alpha, alpha + 1, ply, ts, ss);
        if (r <= alpha) return r;
    }

    // ── D4: reverse futility ────────────────────────────────────────────────
    if (prune_ok && depth <= rfp_max_depth && std::abs(beta) < MATE_BOUND &&
        !ss[ply].tt_capture && eval_p >= beta &&
        eval_p - (rfp_margin * depth - rfp_improving * improving) >= beta) {
        return (eval_p + beta) / 2;
    }

    // ── D4: IIR ─────────────────────────────────────────────────────────────
    if (depth >= iir_min_depth && hm == Move::NO_MOVE && (is_pv || cut_node) &&
        excluded == Move::NO_MOVE)
        depth -= 1;

    // ── D4: ProbCut ─────────────────────────────────────────────────────────
    if (prune_ok && depth >= probcut_min_depth && std::abs(beta) < MATE_BOUND &&
        (!tt_hit || tt_score >= beta + probcut_margin - probcut_improving * improving ||
         tt_depth < depth - 3)) {
        const int beta_cut = beta + probcut_margin - probcut_improving * improving;
        const int see_thr  = beta_cut - static_eval;
        Movelist caps;
        movegen::legalmoves<movegen::MoveGenType::CAPTURE>(caps, b);
        for (const Move& m : caps) {
            if (!see_ge(b, m, see_thr)) continue;
            ss[ply].move        = m;
            ss[ply].moved_pt    = b.at<PieceType>(m.from());
            ss[ply].to          = m.to().index();
            ss[ply].was_capture = true;
            tt_prefetch(b.zobristAfter(m));
            do_move(b, m, ts);
            int v = -qsearch(b, -beta_cut, -beta_cut + 1, ply + 1, ts, ss);
            if (v >= beta_cut && depth > probcut_dred)
                v = -negamax(b, depth - probcut_dred, -beta_cut, -beta_cut + 1, ply + 1,
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

    // ── D4/D5: null move pruning with static-eval gate and verification ────
    if (prune_ok && null_ok && depth >= 3 && has_np_material && ply >= ts.nmp_min_ply &&
        eval_p >= beta - (nmp_gate_slope * depth - nmp_gate) &&
        !(tt_hit && tt_bound == BOUND_UPPER && tt_score < beta)) {
        int R = nmp_base + depth / nmp_depth_div
              + std::min((eval_p - beta) / nmp_eval_div, nmp_eval_cap)
              + (cut_node ? 1 : 0);
        R = std::clamp(R, 1, depth - 1);
        ss[ply].move = Move::NO_MOVE; ss[ply].moved_pt = PieceType::NONE; ss[ply].to = -1;
        do_null_move(b, ts);
        int ns = -negamax(b, depth - 1 - R, -beta, -beta + 1, ply + 1, false, !cut_node, ts, ss);
        undo_null_move(b, ts);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (ns >= beta) {
            if (is_win(ns)) ns = beta;
            if (ts.nmp_min_ply != 0 || depth < nmp_verif_min_d) return ns;
            // zugzwang verification (D5)
            ts.nmp_min_ply = ply + 3 * (depth - R) / 4;
            int v = negamax(b, depth - R, beta - 1, beta, ply, false, cut_node, ts, ss);
            ts.nmp_min_ply = 0;
            if (info.stop.load(std::memory_order_relaxed)) return 0;
            if (v >= beta) return ns;
            return v;
        }
    }

    // ── D6: singular extensions ─────────────────────────────────────────────
    int ext = 0;
    if (!is_root && excluded == Move::NO_MOVE && hm != Move::NO_MOVE &&
        depth >= sing_min_depth + (tt_pv ? 0 : 2) &&
        std::abs(tt_score) < MATE_BOUND &&
        (tt_bound == BOUND_LOWER || tt_bound == BOUND_EXACT) && tt_depth >= depth - 3) {
        const int sing_beta  = tt_score - (sing_beta_base + sing_beta_extra * (tt_pv && !is_pv)) * depth / 64;
        const int sing_depth = (depth - 1) / 2;
        int s = negamax(b, sing_depth, sing_beta - 1, sing_beta, ply, null_ok, cut_node, ts, ss, hm);
        if (info.stop.load(std::memory_order_relaxed)) return 0;
        if (s < sing_beta) {
            ext = 1;
            if (!is_pv && s < sing_beta - sing_dbl_margin) ext = 2;
            ss[ply].dext = ss[ply - 1].dext + (ext >= 2 ? 1 : 0);
            if (ext >= 2 && ss[ply].dext > dext_lmr_cap) ext = 1;
        } else if (sing_beta >= beta) {
            return sing_beta;                       // multi-cut
        } else if (tt_score >= beta) {
            ext = sing_neg_ext;
        } else if (cut_node) {
            ext = -2;
        } else if (tt_score <= alpha) {
            ext = -1;
        }
    }

    // ── move loop ───────────────────────────────────────────────────────────
    const Bitboard threat = threat_mask(b);
    MovePicker mp(b, ts, ss, ply, hm, /*qs=*/false, in_check, threat);

    int  best_score = -INF;
    Move best_move  = Move::NO_MOVE;
    const int orig_alpha = alpha;
    bool raised_alpha = false;
    int  moves_searched = 0;
    Move quiets_tried[64]; int n_quiets = 0;
    Move caps_tried[64];   int n_caps = 0;

    if (excluded == Move::NO_MOVE && mp.moves.empty())
        return in_check ? mated_in(ply) : DRAW_SCORE;

    Move m;
    while ((m = mp.next_move()) != Move::NO_MOVE) {
        if (m == excluded) continue;

        const bool is_cap   = b.isCapture(m);                       // A4: castling excluded
        const bool is_promo = m.typeOf() == Move::PROMOTION;
        const bool is_quiet = !is_cap && !is_promo;                 // A4: castling is quiet
        // A4: castling histories key on the KING's destination square
        const int  mt_idx   = (m.typeOf() == Move::CASTLING)
                            ? Square::castling_king_square(m.to().index() > m.from().index(),
                                                           b.sideToMove()).index()
                            : m.to().index();

        ++moves_searched;
        ss[ply].move_count = moves_searched;

        // ── D7: per-move pruning ────────────────────────────────────────────
        if (!is_root && best_score > -MATE_BOUND && has_np_material) {
            const int lmr_d = std::max(0, depth - 1 - lmr_q(depth, moves_searched) / 1024);
            if (is_quiet && !is_pv && !in_check) {
                int lmp = (lmp_base + depth * depth) / (2 - improving);
                if (moves_searched >= lmp) { mp.skip_quiets(); continue; }
                if (lmr_d < 7) {
                    int h = main_hist(ts, b, m, threat)
                          + ts.pawnHist[(int)b.sideToMove()]
                                       [(int)(mix64(pawn_corr_key(b)) >> 54)][(int)b.at<PieceType>(m.from())][mt_idx]
                          + cont_hist_sum(ts, ss, ply, (int)b.at<PieceType>(m.from()), mt_idx);
                    if (h < -hist_prune * depth) continue;
                }
                if (lmr_d <= 10 && eval_p + fut_base + fut_slope * lmr_d <= alpha) {
                    mp.skip_quiets();
                    continue;
                }
                if (!see_ge(b, m, see_quiet * lmr_d * lmr_d)) continue;
            }
            if (is_cap) {
                PieceType att2 = b.at<PieceType>(m.from());
                PieceType vic2 = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
                                                                 : b.at<PieceType>(m.to());
                int ch = ts.capHist[(int)att2][mt_idx][(int)vic2];
                if (lmr_d <= 8 &&
                    eval_p + capfut_base + capfut_slope * lmr_d + SEE_VAL[(int)vic2] + ch / 32 <= alpha)
                    continue;
                if (!see_ge(b, m, see_capture * depth + ch / 32)) continue;
            }
        }

        // stack record for the child's continuation lookups
        ss[ply].move        = m;
        ss[ply].moved_pt    = b.at<PieceType>(m.from());
        ss[ply].to          = mt_idx;
        ss[ply].was_capture = is_cap;

        tt_prefetch(b.zobristAfter(m));
        do_move(b, m, ts);
        const bool gives_check = b.inCheck();

        const uint64_t nodes_before = ts.nodes;
        int nd = depth - 1 + ((m == hm) ? ext : 0);
        int score;

        if (moves_searched == 1) {
            // first move: full window; child cut_node per A5
            score = -negamax(b, nd, -beta, -alpha, ply + 1, true,
                             is_pv ? false : !cut_node, ts, ss);
        } else {
            // ── D9: unified LMR (quiets AND noisy, 1/1024 units) ────────────
            int r = 0;
            if (depth >= 2) {
                r = is_quiet ? lmr_q(depth, moves_searched) : lmr_n(depth, moves_searched);
                if (cut_node)                 r += lmr_cutnode + (hm == Move::NO_MOVE ? lmr_cutnode_ttm : 0);
                if (!improving)               r += lmr_improving;
                if (is_pv)                    r -= lmr_pv + (ss[ply].tt_pv ? lmr_ttpv : 0);
                if (gives_check)              r -= lmr_check;
                if (ss[ply].tt_capture && is_quiet) r += lmr_ttcap;
                if (ss[ply + 1].cutoff_cnt > 2)     r += lmr_cuthist;
                if (is_quiet) {
                    int h = main_hist(ts, b, m, threat)
                          + ts.pawnHist[(int)b.sideToMove()]
                                       [(int)(mix64(pawn_corr_key(b)) >> 54)][(int)ss[ply].moved_pt][mt_idx]
                          + cont_hist_sum(ts, ss, ply, (int)ss[ply].moved_pt, mt_idx);
                    r -= std::clamp(h * 1024 / lmr_hist_div, -lmr_hist_cap, lmr_hist_cap);
                } else {
                    PieceType att2 = ss[ply].moved_pt;
                    PieceType vic2 = (m.typeOf() == Move::ENPASSANT) ? PieceType::PAWN
                                                                     : b.at<PieceType>(mt_idx);
                    int ch = ts.capHist[(int)att2][mt_idx][(int)vic2];
                    r -= std::clamp(ch * 1024 / lmr_hist_div, -lmr_hist_cap, lmr_hist_cap);
                }
                if ((m == ss[ply].killers[0] || m == ss[ply].killers[1]) && is_quiet) r -= lmr_killer;
                if (hm == Move::NO_MOVE && !cut_node) r += lmr_allnode;
                if (is_root) r -= 1024;                 // D15: r-1 at the root
                r = std::max(0, r);
            }
            const int d_red = std::clamp(nd - r / 1024, 1, nd + 1);
            ss[ply].reduction = nd - d_red;

            score = -negamax(b, d_red, -alpha - 1, -alpha, ply + 1, true, true, ts, ss);
            if (score > alpha && d_red < nd) {
                const bool deeper    = score > best_score + do_deeper_margin + 2 * nd;
                const bool shallower = score < best_score + do_shallower_mg * 10;
                nd += (deeper ? 1 : 0) - (shallower ? 1 : 0);
                nd  = std::clamp(nd, 1, depth - 1 + std::max(ext, 0));
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
            if (is_pv) {   // triangular PV (C1)
                ss[ply].pv[0] = m;
                std::memcpy(ss[ply].pv + 1, ss[ply + 1].pv, ss[ply + 1].pv_len * sizeof(Move));
                ss[ply].pv_len = ss[ply + 1].pv_len + 1;
            }
            if (is_root) {   // A11
                ts.root_best = m;
                ts.root_score = score;
                ts.root_best_updated = true;
            }
        }
        if (is_quiet)  { if (n_quiets < 64) quiets_tried[n_quiets++] = m; }
        else           { if (n_caps   < 64) caps_tried[n_caps++]     = m; }

        // ── D11: beta cutoff — histories, TT, correction, fail-soft return ──
        if (alpha >= beta) {
            ss[ply + 1].cutoff_cnt++;
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
            // D10: malus EVERY capture tried before the cutoff move
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
                best_score = (best_score * depth + beta) / (depth + 1);   // SF blend
            if (tt_ok)
                tt_store(key, best_move, best_score, raw_eval, depth, BOUND_LOWER,
                         ss[ply].tt_pv, ply);
            update_correction(ts, b, ss, ply, best_score, static_eval, depth, orig_alpha, beta,
                              !is_quiet);
            return best_score;
        }
    }

    // no legal (non-excluded) move
    if (moves_searched == 0) {
        if (excluded != Move::NO_MOVE) return alpha;
        return in_check ? mated_in(ply) : DRAW_SCORE;
    }

    // ── D10: prior fail-low bonus for the previous quiet move ───────────────
    if (best_move == Move::NO_MOVE && depth >= prior_fail_low_d && ply >= 1 &&
        ss[ply - 1].moved_pt != PieceType::NONE && !ss[ply - 1].was_capture &&
        ss[ply - 1].move != Move::NO_MOVE) {
        int16_t* slot = cont_slot(ts, 1, ss, ply - 1, (int)ss[ply - 1].moved_pt, ss[ply - 1].to);
        if (slot) add_hist(*slot, history_bonus(depth) / 2);
    }

    // ── D11: store + return (fail-soft) ─────────────────────────────────────
    Bound f = raised_alpha ? BOUND_EXACT : BOUND_UPPER;
    if (tt_ok)
        tt_store(key, best_move, best_score, raw_eval, depth, f, ss[ply].tt_pv, ply);
    update_correction(ts, b, ss, ply, best_score, static_eval, depth, orig_alpha, beta,
                      best_move != Move::NO_MOVE && b.isCapture(best_move));
    return best_score;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Output helpers
// ─────────────────────────────────────────────────────────────────────────────
uint64_t total_nodes() {
    uint64_t n = 0;
    for (size_t i = 0; i < g_threads.size(); ++i)
        if (g_threads[i]) n += g_threads[i]->nodes;
    return n;
}

std::string score_str(int score) {
    std::ostringstream oss;
    if (is_win(score))  oss << "score mate "  << (MATE_SCORE - score + 1) / 2;
    else if (is_loss(score)) oss << "score mate -" << (MATE_SCORE + score + 1) / 2;
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

// ─────────────────────────────────────────────────────────────────────────────
//  E5: time management — movestogo aware, SF-style scales, per-iteration
//  stability / falling / node-effort factors, hard-stop prediction.
// ─────────────────────────────────────────────────────────────────────────────
struct GoParams {
    int wtime = 0, btime = 0, winc = 0, binc = 0;
    int movetime = 0, movestogo = 0, depth = 0, nodes = 0, mate = 0;
    bool infinite = false, ponder = false;
    std::vector<Move> searchmoves;
};
static GoParams g_go;
static int g_move_overhead = 30;

static void compute_tm(int& soft, int& hard, const GoParams& g, const Board& b) {
    soft = hard = 0;
    if (g.movetime > 0) {
        soft = hard = std::max(1, g.movetime - g_move_overhead);
        return;
    }
    int myT = (b.sideToMove() == Color::WHITE) ? g.wtime : g.btime;
    int myI = (b.sideToMove() == Color::WHITE) ? g.winc  : g.binc;
    if (myT <= 0) return;
    const int mtg = g.movestogo > 0 ? std::min(g.movestogo, 50) : 50;
    const int time_left = std::max(1, myT + myI * (mtg - 1) - g_move_overhead * (2 + mtg));
    const int game_ply = 2 * ((int)b.fullMoveNumber() - 1) + (b.sideToMove() == Color::BLACK ? 1 : 0);
    double opt_scale, max_scale;
    if (g.movestogo > 0) {
        opt_scale = std::min((0.88 + game_ply / 116.4) / mtg, 0.88 * myT / time_left);
        max_scale = std::min(mtg_scale_cap / 100.0, 1.5 + mtg_scale_slope / 100.0 * mtg);
    } else {
        opt_scale = std::min(tm_opt_base / 1000.0, 0.20 * myT / time_left);
        max_scale = tm_max_scale / 100.0;
    }
    const double soft_base = opt_scale * time_left;
    hard = std::min((int)(tm_hard_cap / 100.0 * myT) - g_move_overhead,
                    (int)(max_scale * soft_base)) - 10;
    hard = std::max(hard, 1);
    soft = std::clamp((int)soft_base, 1, hard);
}

// E7c: per-thread final results for SMP voting
struct ThreadResult {
    bool valid = false;
    Move move = Move::NO_MOVE;
    int  score = -INF;
    int  depth = 0;
    Move pv[MAX_PLY];
    int  pv_len = 0;
};

static std::vector<std::string> g_tune_dump_names;   // unused placeholder

// G3: root searchmoves filter (resolved at `go` time)
static std::vector<Move> g_root_filter;
inline bool root_filter_ok(const Move& m) {
    if (g_root_filter.empty()) return true;
    for (const Move& f : g_root_filter) if (f == m) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Thread worker (A2/A11/A12/E1..E7)
// ─────────────────────────────────────────────────────────────────────────────
void thread_worker(Board board, int thread_id, int max_depth, ThreadResult* out) {
    ThreadState& ts = *g_threads[thread_id];
    ts.nodes = 0;
    ts.seldepth = 0;
    ts.thread_id = thread_id;
    ts.nmp_min_ply = 0;
    ts.root_best = Move::NO_MOVE;
    ts.root_score = -INF;
    ts.root_best_updated = false;
    std::memset(ts.root_nodes, 0, sizeof(ts.root_nodes));
    // A2: histories persist; only per-search state resets
    for (int i = 0; i < MAX_PLY + 8; ++i) {
        Stack& st = ts.stacks[i];
        st.move = Move::NO_MOVE; st.moved_pt = PieceType::NONE; st.to = -1;
        st.static_eval = NO_EVAL; st.in_check = false; st.was_capture = false;
        st.tt_pv = false; st.tt_capture = false;
        st.cutoff_cnt = 0; st.reduction = 0; st.dext = 0; st.move_count = 0;
        st.excluded = Move::NO_MOVE;
        st.killers[0] = st.killers[1] = Move::NO_MOVE;
        st.pv_len = 0;
    }
    Stack* ss = ts.stacks + 8;
    if (g_nnue.loaded) {
        ts.accDepth = 0;
        g_nnue.refresh(board, ts.accStack[0].acc);
        ts.accStack[0].computed = true;
    }

    Movelist ml;
    movegen::legalmoves(ml, board);
    if (ml.empty()) { out->valid = false; return; }
    Move best_move = ml[0];
    if (ml.size() == 1 && info.hard_time > 0) {   // E1 early-out with a clock
        out->valid = true; out->move = best_move; out->depth = 0;
        out->score = 0; out->pv_len = 1; out->pv[0] = best_move;
        return;
    }

    int  prev_score = 0, score2 = 0;
    double prev_avg = 0.0;
    int  iters = 0;
    Move prev_best = Move::NO_MOVE;
    int  best_stable = 0;
    int  completed_depth = 0;
    int  last_iter_ms = 1;

    for (int depth = 1; depth <= max_depth; ++depth) {
        const int ms_before = elapsed_ms();
        if (info.hard_time > 0 && ms_before >= info.hard_time) {
            info.stop.store(true, std::memory_order_relaxed);
            break;
        }

        // ── A12: SF-style aspiration windows ────────────────────────────────
        int score = 0;
        bool completed = false;
        int fhc = 0;
        ts.root_best_updated = false;

        const bool use_asp = depth >= 4 && std::abs(prev_score) < MATE_BOUND;
        if (use_asp) {
            int delta = asp_delta + (ts.thread_id % 4) +
                        std::abs(prev_score) * std::abs(prev_score) / 12000;
            int a  = std::max(prev_score - delta, -INF);
            int bt = std::min(prev_score + delta, INF);
            while (true) {
                const int adj_depth = std::max(1, depth - fhc);
                score = negamax(board, adj_depth, a, bt, 0, true, false, ts, ss);
                if (info.stop.load(std::memory_order_relaxed)) break;
                if (score <= a) {
                    bt = (a + bt) / 2;
                    a  = std::max(score - delta, -INF);
                    fhc = 0;
                } else if (score >= bt) {
                    bt = std::min(score + delta, INF);
                    ++fhc;
                } else {
                    completed = true;
                    break;
                }
                delta += delta / asp_widen_div;
            }
        } else {
            score = negamax(board, depth, -INF, INF, 0, true, false, ts, ss);
            if (!info.stop.load(std::memory_order_relaxed)) completed = true;
        }

        // ── E6: interrupted iteration → best move per A11 ───────────────────
        if (!completed) {
            if (ts.root_best_updated) best_move = ts.root_best;
            break;
        }

        completed_depth = depth;
        best_move = ts.root_best != Move::NO_MOVE ? ts.root_best : best_move;

        // E3: best-move stability
        if (best_move == prev_best) ++best_stable;
        else { best_stable = 0; prev_best = best_move; }

        // thread result snapshot (E7c)
        out->valid = true;
        out->move  = best_move;
        out->score = score;
        out->depth = depth;
        out->pv_len = ss[0].pv_len;
        std::memcpy(out->pv, ss[0].pv, ss[0].pv_len * sizeof(Move));

        // E4: score trend
        score2 = prev_score;
        prev_score = score;
        prev_avg = (prev_avg * iters + score) / (iters + 1);
        ++iters;

        if (thread_id == 0) {
            const int ms_now = elapsed_ms();
            const int ms_depth = ms_now - ms_before;
            last_iter_ms = std::max(1, ms_depth);
            const uint64_t total = total_nodes();
            const int nps = (ms_now > 0) ? (int)(total * 1000ULL / ms_now) : 0;

            std::ostringstream oss;
            oss << "info depth " << depth
                << " seldepth "  << ts.seldepth
                << " "           << score_str(score)
                << " nodes "     << total
                << " nps "       << nps
                << " hashfull "  << tt_hashfull()
                << " time "      << ms_now
                << " pv"         << pv_to_uci(ss[0].pv, ss[0].pv_len, board)
                << "\n";
            std::cout << oss.str();
            std::cout.flush();

            // ── E5: per-iteration stop decision ─────────────────────────────
            if (info.hard_time > 0) {
                if (info.fixed_movetime) {
                    if (ms_now >= info.hard_time) {
                        info.stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                } else if (info.soft_time > 0) {
                    // E1: stop on a proven forced mate
                    if (is_win(score) && completed_depth >= (MATE_SCORE - score) + 4) {
                        info.stop.store(true, std::memory_order_relaxed);
                        break;
                    }
                    // D15/E2: node effort of the best root move this iteration
                    uint64_t best_nodes = 0, iter_nodes = 0;
                    for (int f2 = 0; f2 < 64; ++f2)
                        for (int t2 = 0; t2 < 64; ++t2) {
                            iter_nodes += ts.root_nodes[f2][t2];
                            if (f2 == best_move.from().index()) {
                                int kt = best_move.to().index();
                                // normalize castling key
                                if (best_move.typeOf() == Move::CASTLING)
                                    kt = Square::castling_king_square(kt > f2,
                                                                      board.sideToMove()).index();
                                if (t2 == kt) best_nodes += ts.root_nodes[f2][t2];
                            }
                        }
                    const double frac = iter_nodes ? (double)best_nodes / (double)iter_nodes : 0.0;
                    const double stability = std::clamp(
                        (tm_stability_a - tm_stability_b * std::min(best_stable, 8)) / 100.0,
                        0.65, 1.4);
                    const double falling = std::clamp(
                        (12 + 2.2 * (prev_avg - score) + 0.9 * (score2 - score)) / 100.0,
                        tm_falling_lo / 100.0, tm_falling_hi / 100.0);
                    const double node_tm = std::clamp(
                        (tm_node_a - tm_node_b * frac) / 100.0, 0.5, 1.5);
                    const int target = (int)(info.soft_time * stability * falling * node_tm);
                    const int limit = std::min(target, info.hard_time);
                    if (ms_now >= limit ||
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

// ─────────────────────────────────────────────────────────────────────────────
//  E7c: SF voting — votes += (score - min + base) * depth; proven mates win;
//  ties resolved by depth.
// ─────────────────────────────────────────────────────────────────────────────
Move pick_best_move(ThreadResult* res, int n) {
    int valid = 0;
    for (int i = 0; i < n; ++i) if (res[i].valid) ++valid;
    if (valid == 0) return Move::NO_MOVE;
    if (valid == 1) {
        for (int i = 0; i < n; ++i) if (res[i].valid) return res[i].move;
    }
    // proven mate: take the fastest mate any thread proved
    int mate_thread = -1, best_mate = -INF;
    for (int i = 0; i < n; ++i)
        if (res[i].valid && is_win(res[i].score) && res[i].score > best_mate) {
            best_mate = res[i].score; mate_thread = i;
        }
    if (mate_thread >= 0) return res[mate_thread].move;

    int min_score = INF;
    for (int i = 0; i < n; ++i) if (res[i].valid) min_score = std::min(min_score, res[i].score);
    std::vector<int64_t> votes(1 << 16, 0);
    std::vector<int>     maxd(1 << 16, 0);
    std::vector<Move>    mv(1 << 16, Move::NO_MOVE);
    for (int i = 0; i < n; ++i) {
        if (!res[i].valid) continue;
        const uint16_t k = res[i].move.move();
        votes[k] += (int64_t)(res[i].score - min_score + vote_base) * res[i].depth;
        if (res[i].depth > maxd[k]) { maxd[k] = res[i].depth; mv[k] = res[i].move; }
    }
    int64_t bestv = -1;
    Move best = Move::NO_MOVE;
    int  bestd = -1;
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

// ─────────────────────────────────────────────────────────────────────────────
//  search(): orchestrate the threads, then vote.
// ─────────────────────────────────────────────────────────────────────────────
Move search(Board& board, int max_depth) {
    info.stop.store(false, std::memory_order_relaxed);
    info.start_time = std::chrono::steady_clock::now();
    tt_generation = (uint8_t)((tt_generation + 1) & 31);

    while ((int)g_threads.size() < g_num_threads)          // A19: lazy alloc
        g_threads.push_back(std::make_unique<ThreadState>());

    std::vector<ThreadResult> res(g_num_threads);
    std::vector<std::thread> threads;
    for (int i = 0; i < g_num_threads; ++i)
        threads.emplace_back(thread_worker, board, i, max_depth, &res[i]);
    for (auto& t : threads) t.join();

    Move best = pick_best_move(res.data(), g_num_threads);
    if (best != Move::NO_MOVE) {
        Movelist ml;
        movegen::legalmoves(ml, board);
        for (const auto& mm : ml) if (mm == best) return best;
    }
    Movelist ml;
    movegen::legalmoves(ml, board);
    return ml.empty() ? Move::NO_MOVE : ml[0];
}

// ─────────────────────────────────────────────────────────────────────────────
//  B2: perft
// ─────────────────────────────────────────────────────────────────────────────
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
static const PerftPos PERFT_SUITE[] = {
    {"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, 4865609ULL},
    {"r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, 4085603ULL},
    {"8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 6, 11030083ULL},
    {"r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 5, 15833292ULL},
    {"rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, 2103487ULL},
    {"r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, 3894594ULL},
    {"3k4/3p4/8/K1P4r/8/8/8/8 b - - 0 1", 6, 1134888ULL},      // illegal ep move #1
    {"8/8/4k3/8/2p5/8/B2P2K1/8 w - - 0 1", 6, 1015133ULL},     // illegal ep move #2
    {"8/8/1k6/2b5/2pP4/8/5K2/8 b - d3 0 1", 6, 1440467ULL},    // EP capture checks opponent
    {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", 6, 661072ULL},          // short castling gives check
    {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", 6, 803711ULL},          // long castling gives check
    {"r3k2r/1b4bq/8/8/8/8/7B/R3K2R w KQkq - 0 1", 4, 1274206ULL}, // castle rights
    {"r3k2r/8/3Q4/8/8/5q2/8/R3K2R b KQkq - 0 1", 4, 1720476ULL},  // castling prevented
    {"2K2r2/4P3/8/8/8/8/8/3k4 w - - 0 1", 6, 3821001ULL},      // promote out of check
    {"8/8/1P2K3/8/2n5/1q6/8/5k2 b - - 0 1", 5, 1004658ULL},    // discovered check
    {"4k3/1P6/8/8/8/8/K7/8 w - - 0 1", 6, 217342ULL},          // promote to give check
    {"8/P1k5/K7/8/8/8/8/8 w - - 0 1", 6, 92683ULL},            // underpromote to check
    {"K1k5/8/P7/8/8/8/8/8 w - - 0 1", 6, 2217ULL},             // self stalemate
    {"8/k1P5/8/1K6/8/8/8/8 w - - 0 1", 7, 567584ULL},          // stalemate & checkmate
    {"8/8/2k5/5q2/5n2/8/5K2/8 b - - 0 1", 4, 23527ULL},        // stalemate & checkmate #2
};

int perftsuite() {
    int fails = 0;
    for (const auto& p : PERFT_SUITE) {
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

// ─────────────────────────────────────────────────────────────────────────────
//  B1: OpenBench-compatible bench (48 FENs, depth 12, 1 thread, Hash 16)
// ─────────────────────────────────────────────────────────────────────────────
static const char* BENCH_FENS[] = {
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

int bench(int depth) {
    depth = depth > 0 ? depth : 12;
    const int n = (int)(sizeof(BENCH_FENS) / sizeof(BENCH_FENS[0]));
    // bench runs its own fixed environment: 1 thread, Hash 16
    const int saved_threads = g_num_threads;
    const size_t saved_hash_mb = g_tt_clusters * sizeof(TTCluster) / (1024 * 1024);
    g_num_threads = 1;
    tt_resize(16);
    while ((int)g_threads.size() < 1) g_threads.push_back(std::make_unique<ThreadState>());

    Board board;
    info.stop.store(false, std::memory_order_relaxed);
    info.hard_time = 0;
    info.soft_time = 0;
    info.node_limit.store(0);
    info.infinite = false;
    info.fixed_movetime = false;
    info.start_time = std::chrono::steady_clock::now();
    g_root_filter.clear();

    uint64_t total_nodes_sum = 0;
    ThreadResult res;
    for (int i = 0; i < n; ++i) {
        board.setFen(BENCH_FENS[i]);
        // sanity: both kings present and at least one legal move
        bool ok = true;
        for (Color c : {Color::WHITE, Color::BLACK})
            if (board.pieces(PieceType::KING, c).count() != 1) ok = false;
        Movelist bml;
        movegen::legalmoves(bml, board);
        if (!ok || bml.empty()) {
            std::cout << "info string bench FEN " << i << " invalid: " << BENCH_FENS[i] << "\n";
            continue;
        }
        tt_clear_parallel();
        tt_generation = (uint8_t)((tt_generation + 1) & 31);
        for (int t = 0; t < (int)g_threads.size(); ++t) g_threads[t]->clear_history();
        res = ThreadResult{};
        thread_worker(board, 0, depth, &res);
        total_nodes_sum += total_nodes();
        info.stop.store(false, std::memory_order_relaxed);
    }
    const int ms = std::max(1, elapsed_ms());
    std::cout << total_nodes_sum << " nodes " << (uint64_t)(total_nodes_sum * 1000ULL / ms) << " nps\n";

    g_num_threads = saved_threads;
    tt_resize(saved_hash_mb);
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
//  B6: quick self-play sanity (catches crashes / illegal moves)
// ─────────────────────────────────────────────────────────────────────────────
int selfplay(int games, int depth) {
    for (int g = 0; g < games; ++g) {
        Board board;
        board.setFen(constants::STARTPOS);
        Movelist ml;
        movegen::legalmoves(ml, board);
        // random-ish opening: play 2 + (g % 3) plies of the first legal move
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
            info.start_time = std::chrono::steady_clock::now();
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

// ─────────────────────────────────────────────────────────────────────────────
//  B3: SPSA dump (OpenBench format)
// ─────────────────────────────────────────────────────────────────────────────
void spsa_dump() {
    for (const auto& t : tunables())
        std::cout << t.name << ",int," << *t.value << "," << t.lo << "," << t.hi
                  << "," << t.step << ",0.002\n";
}

// ─────────────────────────────────────────────────────────────────────────────
//  UCI (B4/G3/G4/G5)
// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
//  Startup robustness: locate files next to the executable (not the GUI's cwd)
//  and load the NNUE lazily so `uci` is answered instantly.
// ─────────────────────────────────────────────────────────────────────────────
#ifdef _WIN32
#include <windows.h>
#endif
static const char* DEFAULT_NET_NAME = "nn-1111cefa1111.nnue";
static bool g_net_load_attempted = false;

static std::string exe_dir() {
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

// Called from isready/go/ucinewgame; does the (slow) net load exactly once.
static void ensure_net_loaded() {
    if (g_net_load_attempted) return;
    g_net_load_attempted = true;
    std::string path = g_nnue_path;
    if (!file_exists(path)) {
        std::string alt = exe_dir() + DEFAULT_NET_NAME;
        if (file_exists(alt)) path = alt;
    }
    if (g_nnue.load(path))
        std::cout << "info string loaded NNUE from " << path << "\n";
    else
        std::cout << "info string no NNUE loaded (" << path << "): "
                  << g_nnue.lastError << " - falling back to material eval\n";
    std::cout.flush();
}

void print_options() {
    std::cout << "id name Agnes\n";
    std::cout << "id author Ranak Chongtham\n";
    std::cout << "option name EvalFile type string default " << DEFAULT_NET_NAME << "\n";
    std::cout << "option name Hash type spin default 256 min 1 max 65536\n";
    std::cout << "option name Threads type spin default 1 min 1 max 256\n";
    std::cout << "option name Move Overhead type spin default 30 min 10 max 100\n";
    std::cout << "option name NNUEScale type spin default 40 min 1 max 400\n";
    std::cout << "option name Clear Hash type button\n";
    std::cout << "option name SyzygyPath type string default <empty>\n";
    std::cout << "option name UCI_Chess960 type check default false\n";
#if defined(TUNING)
    for (const auto& t : tunables())
        std::cout << "option name " << t.name << " type spin default " << t.def
                  << " min " << t.lo << " max " << t.hi << "\n";
#endif
    std::cout << "uciok\n";
}

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
            print_options();
        } else if (token == "isready") {
            wait_for_search_end();
            ensure_net_loaded();
            std::cout << "readyok\n";
        } else if (token == "ucinewgame") {
            wait_for_search_end();
            board.setFen(constants::STARTPOS);
            tt_clear_parallel();
            for (auto& t : g_threads) if (t) t->clear_history();
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
                while (!value.empty() && (value.back() == '\r' || value.back() == ' ')) value.pop_back();
                g_nnue_path = value;
                g_net_load_attempted = true;
                std::string path = value;
                if (!file_exists(path)) {                       // GUI cwd != exe dir
                    std::string alt = exe_dir() + value;
                    if (file_exists(alt)) path = alt;
                    else if (file_exists(exe_dir() + DEFAULT_NET_NAME)) path = exe_dir() + DEFAULT_NET_NAME;
                }
                if (g_nnue.load(path))
                    std::cout << "info string loaded NNUE from " << path << "\n";
                else
                    std::cout << "info string failed to load NNUE from " << path
                              << " (" << g_nnue.lastError << ")\n";
            } else if (name == "Threads") {
                g_num_threads = std::clamp(std::stoi(value), 1, 256);   // A19
            } else if (name == "Hash") {
                tt_resize((size_t)std::clamp(std::stoi(value), 1, 65536));
            } else if (name == "Move Overhead") {
                g_move_overhead = std::clamp(std::stoi(value), 10, 100);
            } else if (name == "NNUEScale" && !value.empty()) {
                g_nnue_scale_pct = std::clamp(std::stoi(value), 1, 400);
            } else if (name == "Clear Hash") {
                tt_clear_parallel();
            } else if (name == "UCI_Chess960") {
                board.set960(value == "true");
                // re-parse the original FEN so castling paths rebuild correctly
                std::string fen = board.getFen();
                board.setFen(fen);
            } else if (name == "SyzygyPath") {
                // G2 deferred (needs Fathom); accepted for GUI compatibility
            }
#if defined(TUNING)
            else {
                for (auto& t : tunables())
                    if (name == t.name && !value.empty()) *t.value = std::clamp(std::stoi(value), t.lo, t.hi);
            }
#endif
        } else if (token == "position") {
            wait_for_search_end();
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
            wait_for_search_end();
            ensure_net_loaded();
            GoParams g;
            int depth = -1;
            std::string sm;
            while (iss >> token) {
                if      (token == "depth")     iss >> depth;
                else if (token == "movetime")  iss >> g.movetime;
                else if (token == "wtime")     iss >> g.wtime;
                else if (token == "btime")     iss >> g.btime;
                else if (token == "winc")      iss >> g.winc;
                else if (token == "binc")      iss >> g.binc;
                else if (token == "movestogo") iss >> g.movestogo;
                else if (token == "nodes")     iss >> g.nodes;
                else if (token == "mate")      iss >> g.mate;
                else if (token == "infinite")  g.infinite = true;
                else if (token == "ponder")    g.ponder = true;
                else if (token == "searchmoves") { /* handled below */ }
                else if (g.searchmoves.empty() || true) {
                    // tokens that are not keywords may belong to searchmoves
                    if (token.size() >= 4 && token[0] >= 'a' && token[0] <= 'h')
                        g.searchmoves.push_back(uci::uciToMove(board, token));
                }
            }
            g_go = g;
            info.infinite = g.infinite || g.ponder;
            info.node_limit.store(g.nodes > 0 ? (uint64_t)g.nodes : 0);
            info.start_time = std::chrono::steady_clock::now();
            g_root_filter = g.searchmoves;

            int soft = 0, hard = 0;
            if (g.infinite || g.ponder) {
                depth = depth > 0 ? depth : MAX_PLY - 4;
            } else {
                compute_tm(soft, hard, g, board);
                if (soft > 0 && depth < 0) depth = MAX_PLY - 4;
                if (depth < 0) depth = std::max(depth, 8);
            }
            info.soft_time = soft;
            info.hard_time = hard;
            info.fixed_movetime = (g.movetime > 0);

            int d = depth < 0 ? MAX_PLY - 4 : depth;
            g_search_thread = std::thread([board, d]() mutable {
                Move best = search(board, d);
                std::ostringstream oss;
                if (best == Move::NO_MOVE) oss << "bestmove (none)\n";
                else                       oss << "bestmove " << uci::moveToUci(best) << "\n";
                std::cout << oss.str();
                std::cout.flush();
            });
        } else if (token == "ponderhit") {
            // recompute soft/hard from the stored go parameters
            int soft = 0, hard = 0;
            compute_tm(soft, hard, g_go, board);
            info.soft_time = soft;
            info.hard_time = hard;
            info.fixed_movetime = (g_go.movetime > 0);
            info.infinite = false;
        } else if (token == "stop") {
            info.stop.store(true, std::memory_order_relaxed);
        } else if (token == "quit") {
            wait_for_search_end();
            break;
        } else if (token == "bench") {
            int d = 0;
            iss >> d;
            bench(d);
        } else if (token == "perft") {
            wait_for_search_end();
            int d = 0;
            iss >> d;
            std::string rest;
            iss >> rest;
            Board pb = board;
            if (!rest.empty()) pb.setFen(line.substr(line.find("perft") + 7));
            std::cout << perft(pb, d) << "\n";
        } else if (token == "divide") {
            wait_for_search_end();
            int d = 1;
            iss >> d;
            perft_divide(board, d);
        } else if (token == "perftsuite") {
            perftsuite();
        } else if (token == "selfplay") {
            int g2 = 3, d2 = 6;
            iss >> g2 >> d2;
            selfplay(g2, d2);
        } else if (token == "spsa_dump" || token == "tune_dump") {
            spsa_dump();
        } else if (token == "eval") {
            ensure_net_loaded();
            std::cout << "eval " << evaluate(board) << "\n";
        } else if (token == "d") {
            ensure_net_loaded();
            std::cout << board << "\n";
            std::cout << "FEN: "  << board.getFen() << "\n";
            std::cout << "Hash: " << board.hash() << "\n";
            std::cout << "Eval: " << evaluate(board)
                      << (g_nnue.loaded ? "  [NNUE]" : std::string("  [MATERIAL-ONLY]"))
                      << "\n";
        }
        std::cout.flush();
    }
    wait_for_search_end();
}

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);
    init_lmr();
    tt_resize(256);
    g_threads.push_back(std::make_unique<ThreadState>());
    g_threads[0]->clear_history();

    // CLI shortcuts (OpenBench): `agnes bench [depth]`, `agnes perftsuite`
    if (argc >= 2) {
        std::string a = argv[1];
        if (a == "bench") return bench(argc >= 3 ? std::atoi(argv[2]) : 0);
        if (a == "perftsuite") return perftsuite();
    }

    // NNUE is loaded lazily (ensure_net_loaded) so nothing is printed before `uci`.
    uci_loop();
    tt_free();
    return 0;
}
