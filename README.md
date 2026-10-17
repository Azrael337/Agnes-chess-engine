# Agnes Chess Engine

<div align="center">
  <img src="logo.png" alt="Agnes chess engine" width="300">

  ![License](https://img.shields.io/badge/License-GPL_3.0-green)
  ![Language](https://img.shields.io/badge/Language-C++-orange)
  ![Release](https://img.shields.io/badge/Release-1.0-yellow)
  ![Random](https://img.shields.io/badge/Defeat-ME-red)

**Agnes** is a lightweight UCI chess engine written in C++ with a focus on efficient search and a compact NNUE evaluation.

</div>

---

## Table of Contents

- [Evaluation](#evaluation)
- [Search](#search)
- [UCI Options](#uci-options)
- [UCI Commands](#uci-commands)
- [Building](#building)
- [Getting Started](#getting-started)
- [Play Strength](#play-strength)
- [Performance](#performance)
- [Acknowledgements](#acknowledgements)
- [License](#license)

---

## Evaluation
Agnes uses stockfish's older nnue (specifically nn-1111cefa1111.nnue) for it's evaluations

## Search

Agnes uses a fail-soft alpha-beta negamax framework enhanced with Principal Variation Search, iterative deepening, and a rich set of pruning, ordering, and extension heuristics.

### Core

* **Algorithm:** Fail-soft Alpha-Beta Negamax with Principal Variation Search (PVS)
* **Depth:** Iterative deepening up to 64 plies (128-ply search cap)
* **Transposition Table:** 1–4096 MB, 16-byte tear-safe entries, generation-based aging with depth-preferred replacement and PV flag

### Pruning Techniques

* Late Move Pruning (LMP)
* Late Move Reductions (LMR) — logarithmic table with graded history adjustment and ~10 contextual terms (PV, cut-node, TT-state, check, killers, improving, threat state)
* Null Move Pruning — cut nodes only, adaptive R, verification search at high depth
* Reverse Futility Pruning
* Futility Pruning — reduced-depth margins with a history term, plus capture futility
* History Pruning
* SEE Pruning — quiets and captures, on reduced depths
* Razoring
* Internal Iterative Reductions (PV and cut nodes)
* ProbCut — qsearch pre-check, TT guard, SEE-gated captures, TT store
* Mate Distance Pruning
* Delta Pruning in quiescence search

### Move Ordering

* TT move first
* MVV-LVA with SEE-based good/bad capture split, loosened by capture history
* Capture History and Capture Continuation History
* Continuation History — 6 plies, per side to move
* Threat-Indexed History (enemy pawn-attack map)
* Pawn-Structure History
* Killer Move Heuristic
* Countermove Heuristic (color-indexed)

### Extensions

* Singular Extensions with double and triple extensions
* Negative Extensions (reduced TT move on failed singularity test)
* Double-extension budgeting per line

### Quiescence Search

* Full evasion search when in check
* Delta + SEE pruning
* Correction-history adjusted stand-pat
* Quiet queen promotions included

### Parallel Search

* Lazy SMP up to 8 threads
* Lock-free shared best move (deepest iteration wins)
* Per-thread aspiration jitter
* Helper-thread depth skipping

## UCI Options

| Option      | Type   | Default | Range     | Description                                        |
| ----------- | ------ | ------- | --------- | -------------------------------------------------- |
| `EvalFile`  | string | —       | —         | Path to a custom NNUE evaluation file              |
| `Hash`      | spin   | 256     | 1–4096    | Transposition table size in MB                     |
| `Threads`   | spin   | 1       | 1–8       | Number of search threads (Lazy SMP)                |
| `NNUEScale` | spin   | 40      | 1–400     | NNUE evaluation scaling factor                     |

## UCI Commands

* Standard UCI protocol commands: `uci`, `isready`, `ucinewgame`, `position`, `go`, `stop`, `setoption`
* **Ponder Mode:** Full support for `ponder` searches with `ponderhit` and `stop` interruption
* **Time Controls:** `depth`, `movetime`, `wtime`, `btime`, `winc`, `binc`, `infinite`
* **Ponder Support:** Correctly handles ponder-off behavior with pending time allocation

Agnes is compatible with any UCI-capable chess GUI such as Cute Chess, Banksia, ChessBase, and online platforms like Lichess and Chess.com (via bots).

## Building

### Requirements

* A C++ compiler supporting C++17 or later (GCC 9+, Clang 10+, or MSVC 2019+)

## Getting Started

Once built, you can test the engine directly from a terminal:

```bash
./build/agnes
```

```text
uci
isready
position startpos moves e2e4 e7e5
go depth 18
```

For a more comfortable experience, connect Agnes to a UCI GUI (e.g. Cute Chess or Banksia) or use it for engine-vs-engine testing with tools such as `cutechess-cli`.

## Play Strength

Estimated strength is currently around **2900–3100 Elo**.

More testing will be done using engine-vs-engine games.

## Performance

* Efficient incremental NNUE updates for minimal eval overhead
* Dynamic time management adapts to search depth and position complexity
* Thread-pool architecture enables strong scalability up to 8 cores
* Compact network size (88mm NNUE) provides fast inference on modern hardware

## Acknowledgements

* [Disservin chess-library](https://github.com/Disservin/chess-library) — used for board representation, legal move generation, and move handling.

## License

See [LICENSE](LICENSE).
