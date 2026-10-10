<p align="center">
  <img src="logo.png" alt="Agnes chess engine">
</p>

# Agnes Chess Engine

> A chess engine using a compact nnue

Agnes is a lightweight UCI chess engine written in C++ with a focus on efficient search and a compact NNUE evaluation.

## Evaluation

* **NNUE Architecture:** HalfKAv2_hm^
* **Network Layers:** L1=128, L2=32, L3=8 with PSQT buckets
* **Network File:** Acherontia (88mm.nnue)
* **Incremental NNUE Evaluation:** Efficient position updates with feature transformer
* **Tapered Material Evaluation:** Smooth transition between middlegame and endgame
* **Customizable NNUE Scaling:** Adjust evaluation strength via UCI option

## Search

* **Core Algorithm:** Fail-soft Alpha-Beta Negamax with Principal Variation Search
Depth: Iterative Deepening up to 64 plies (128-ply search cap)
* **Transposition Table:** 1-4096 MB, 16-byte tear-safe entries, generation-based
    aging with depth-preferred replacement, PV flag
* **Pruning Techniques:**
    * Late Move Pruning (LMP)
    * Late Move Reductions (LMR) - logarithmic table with graded history
        adjustment and ~10 contextual terms (PV, cut-node, TT-state, check,
        killers, improving, threat state)
    * Null Move Pruning - cut nodes only, adaptive R, verification search at
        high depth
    * Reverse Futility Pruning
    * Futility Pruning - reduced-depth margins with history term, plus
        capture futility
    * History Pruning
    * SEE Pruning - quiets and captures, on reduced depths
    Razoring
    * Internal Iterative Reductions (PV and cut nodes)
    * ProbCut - qsearch pre-check, TT guard, SEE-gated captures, TT store
    * Mate Distance Pruning
    * Delta Pruning in quiescence search
* **Move Ordering Heuristics:**
    * TT move first
    * MVV-LVA with SEE-based good/bad capture split, loosened by capture history
    Capture History and Capture Continuation History
    * Continuation History - 6 plies, per side to move
    * Threat-Indexed History (enemy pawn-attack map)
    * Pawn-Structure History
    * Killer Move Heuristic
    * Countermove Heuristic (color-indexed)
* **Extensions:**
    * Singular Extensions with double and triple extensions
    * Negative Extensions (reduced TT move on failed singularity test)
    * Double-extension budgeting per line
* **Quiescence Search:** full evasion search in check, delta + SEE pruning,
    correction-history adjusted stand-pat, quiet queen promotions included
* **Parallel Search:** Lazy SMP up to 8 threads, lock-free shared best move
    (deepest iteration wins), per-thread aspiration jitter, helper-thread
    depth skipping

## UCI Options

* **EvalFile** (string): Path to custom NNUE evaluation file
* **Hash** (spin): Transposition table size in MB (default: 256, range: 1-4096)
* **Threads** (spin): Number of search threads (default: 1, range: 1-8)
* **NNUEScale** (spin): NNUE evaluation scaling factor (default: 40, range: 1-400)

## UCI Commands Supported

* Standard UCI protocol commands: `position`, `go`, `stop`, `setoption`, `uci`, `isready`, `ucinewgame`
* **Ponder Mode:** Full support for `ponder` searches with `ponderhit` and `stop` interruption
* **Time Controls:** `depth`, `movetime`, `wtime`, `btime`, `winc`, `binc`, `infinite`
* **Ponder Support:** Correctly handles ponder-off behavior with pending time allocation

## Play Strength

Estimated strength is currently around **2900–3100 Elo**.

More testing will be done using engine-vs-engine games.

## Disservin Chess Library

Agnes uses the
[Disservin chess-library](https://github.com/Disservin/chess-library)
for board representation, legal move generation, and move handling.

## Building

Agnes is written in C++ and requires a C++ compiler supporting modern C++ standards.

### Requirements

* C++ compiler (C++17 or later)
* CMake or Make (if applicable)

## UCI

Agnes fully supports the Universal Chess Interface (UCI) protocol and can be used with any UCI-compatible chess GUI such as Cute Chess, Chess.com, Lichess, and others.

## Performance

* Efficient incremental NNUE updates for minimal eval overhead
* Dynamic time management adapts to search depth and position complexity
* Thread-pool architecture enables strong scalability up to 8 cores
* Compact network size (88mm NNUE) provides fast inference on modern hardware

## License

See [LICENSE](LICENSE).
