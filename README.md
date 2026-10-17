# Agnes Chess Engine

<div align="center">
  <img src="logo.png" alt="Agnes chess engine" width="300">

  ![License](https://img.shields.io/badge/License-GPL_3.0-green)
  ![Language](https://img.shields.io/badge/Language-C++-orange)
  ![Release](https://img.shields.io/badge/Release-1.0-yellow)
  ![Random](https://img.shields.io/badge/Defeat-ME-red)

**Agnes** is a lightweight UCI chess engine written in C++ with a focus on strong search and experimentation.

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

### Network

Agnes loads the Stockfish 17 "big net" `nn-1111cefa1111.nnue`, or any net trained to the same layout (for example with nnue-pytorch). The network is checked against the expected header and feature hashes on load, so a net with a different architecture is rejected with a clear error rather than producing garbage scores.

| Part | Configuration |
| ---- | ------------- |
| Input features | `HalfKAv2_hm`: 32 king buckets × 11 piece planes × 64 squares (22,528 features), with horizontal mirroring for the king file |
| Feature transformer | 3072 outputs per perspective, int16 accumulators, pairwise-product output, 8 PSQT buckets |
| Layer stacks | 8 buckets, selected by `(piece count − 1) / 4` |
| fc_0 | 3072 → 16 (15 outputs plus one skip output) |
| Activations | squared clipped ReLU (15) concatenated with clipped ReLU (15), padded to 32 |
| fc_1 | 32 → 32, clipped ReLU |
| fc_2 | 32 → 1, plus a scaled skip connection from fc_0 |

Feature transformer biases and weights are doubled on load, as Stockfish does. The forward pass reproduces Stockfish 17's integer arithmetic. The result is split into a PSQT part and a positional part, each divided by 16, and both are returned from the side to move's point of view.

### Incremental updates

Each perspective keeps an accumulator. `do_move` only records the move on a stack. When the search needs an evaluation, `evaluate_tree` walks back to the nearest ancestor whose accumulator is already computed, then rolls forward with the appropriate primitive for each move: a fused add/subtract update for normal moves, promotions and captures, a full refresh for king moves and castling. Null moves occupy a stack slot without changing features, so the walk stays consistent through null-move pruning. The lazy scheme means nodes that are pruned before evaluation never pay the update cost.

### Final score

The raw network output goes through this pipeline in `nnue_raw_from_acc`:

1. Combine the parts: `raw = (125 × psqt + 131 × positional) / 128`.
2. Apply the `NNUEScale` option: `e = raw × NNUEScale / 100` (default `NNUEScale = 40`).
3. Apply 50-move damping when the halfmove clock exceeds 90: `e = e × (200 − halfmove) / 200`.
4. Clamp to `±(MATE_BOUND − 1)` so evaluations never look like mate scores.

Correction history is then added on top during search (see below). If no network is loaded, Agnes falls back to a tapered material-only evaluation and prints `info string no NNUE loaded ... falling back to material eval`.

### Correction history

The static evaluation is corrected by a learned offset built from five tables: pawn structure, minor-piece placement, non-pawn material for each colour, and the piece moved by the previous move. Each table stores a bounded running average of the difference between the search result and the static evaluation at nodes with that key. The weighted sum is divided by the grain size and added to the raw evaluation before pruning decisions and quiescence stand-pat. Updates are skipped when the search result is already explained by the static evaluation, and when the best move is a capture.

### Network loading

The net is loaded lazily, on the first `isready`, `go`, `eval`, or `d`, so `uci` answers immediately. Agnes first tries the `EvalFile` path as given, then the same name next to the executable. Setting `EvalFile` loads a different net at runtime.

---


## Search

### Core framework

- **Algorithm:** fail-soft alpha-beta negamax with Principal Variation Search. The first move at each node is searched with the full window, later moves with a null window, and re-searched on fail-high.
- **Iterative deepening** from depth 1 up to the requested depth. Each iteration seeds move ordering for the next one. Hard limits are `MAX_DEPTH = 64` for the requested depth and `MAX_PLY = 128` for the tree.
- **Aspiration windows** start at depth 4 when the previous score is not a mate. The window is `asp_delta` (default 10) around the previous score plus a thread-dependent offset and a term that grows with the square of the score. On failure the window is widened by `delta / 3`.
- **Mate distance pruning** tightens alpha and beta to the best and worst mate distances reachable from the current ply.
- **Draw detection** covers threefold-style repetition (a single repeat inside the search tree counts as a draw), insufficient material, and the 50-move rule. The 50-move rule is skipped when it would be checkmate. Draw scores carry a small random offset (`±2`) to avoid shuffling.
- **Triangular PV** is kept per ply, so the reported PV is the line the search actually followed.

### Transposition table

- Four 16-byte entries per 64-byte cluster, so one cluster fills one cache line.
- Indexing uses a 64-bit multiply-high, so the cluster count does not need to be a power of two.
- Each entry stores a 16-bit key check, the best move, a score and static evaluation (16-bit), depth, a 5-bit generation, a PV flag and a 2-bit bound type.
- Mate scores are adjusted by ply on store and load so they stay correct when transposed.
- Replacement prefers entries from the current generation and deeper entries; an entry is replaced when the new bound is exact, the key differs, the new depth is sufficiently greater, or the old entry is from an older generation.
- Size is set with `Hash` (in MB). The table is cleared in parallel with `Clear Hash` or `ucinewgame`.

### Pruning and reductions

Each rule runs only under its own conditions. The depths and margins below are the code's defaults and are all exposed as tunables.

- **Razoring** (depth ≤ 5): if the static evaluation is far below alpha, a quiescence search confirms it before the node is cut.
- **Reverse futility pruning** (depth ≤ 9, non-PV, not in check): returns when the static evaluation exceeds beta by a depth-dependent margin, reduced when improving.
- **Internal iterative reduction** (depth ≥ 6, PV or cut node, no hash move): reduces depth by one to make up for a missing move hint.
- **ProbCut** (depth ≥ 5): tries SEE-winning captures with a raised beta, prechecked by quiescence, and confirms with a reduced search before cutting.
- **Null move pruning** (depth ≥ 3, side with non-pawn material, evaluation above a depth-dependent gate): reduction is `4 + depth/3 + min((eval − beta)/180, 3)`, plus one at cut nodes. At depth 14 and above, a verification search is run with null moves disabled for a stretch of the tree to guard against zugzwang.
- **Late move pruning:** quiet moves beyond a threshold of `(3 + depth²) / (2 − improving)` are skipped.
- **History pruning:** quiet moves whose combined history is below `−4000 × depth` are skipped at shallow reduced depths.
- **Futility pruning:** quiet moves are skipped at reduced depth when the static evaluation plus a margin cannot reach alpha. Captures get a similar rule that includes the captured piece value.
- **SEE pruning:** quiet moves need SEE ≥ `−25 × lmr_depth²`, captures need SEE ≥ `−150 × depth` (adjusted by capture history).

### Late move reductions

Reductions come from a logarithmic table, `0.80 + ln(d)·ln(m)/2.20` for quiets and `0.20 + ln(d)·ln(m)/3.20` for noisy moves, in 1/1024-ply units. The table value is adjusted by:

- increases at cut nodes, when there is no hash move, when not improving, for moves that are all-node cases, for quiet moves after a hash-capture, and when the move's cutoff history is high;
- decreases at PV nodes (more at TT-PV nodes), for checking moves, for killer moves, and at the root;
- a term from the move's history score, clamped to a fixed range.

A reduced search that fails high is re-searched at full depth, and the depth is adjusted up or down based on how far the score moved relative to the best score so far. PV nodes get a final full-window re-search.

### Singular extensions

When the hash move's stored lower bound stands out against a reduced search that excludes it, the move is extended.

- Extension of 1 when the score falls below the singular beta, and 2 (double extension) when it falls well below it, with a per-line budget that caps double extensions along a single line.
- Multi-cut: when the singular beta is itself above beta, the node returns early.
- Negative extensions when the hash move is not singular and the search suggests it is not the only good move: −3 when the stored score already fails high, −2 at cut nodes, −1 when the stored score is at or below alpha.

### Move ordering

Moves come from a staged picker, so the full move list is not scored or sorted up front.

1. **Hash move** from the transposition table.
2. **Good noisy moves** (captures and queen promotions), scored by MVV-LVA plus capture history and capture-continuation history. Each capture is checked with SEE; losing captures are deferred to the end.
3. **Killer moves** (two per ply) and the **counter move** for the previous move's piece and destination.
4. **Quiet moves**, scored by:
   - main history indexed by side, from and to squares, and whether each square is attacked by an enemy pawn, minor or rook (the threat-indexed part);
   - pawn-structure history, indexed by a 10-bit pawn hash;
   - continuation history over the previous plies 1, 2, 3, 4 and 6, with weights 16, 16, 8, 8 and 4.
   Underpromotions are pushed down.
5. **Bad noisy moves** that failed the SEE check.

In quiescence and when in check the picker works in evasion or capture-only modes.

### History updates

- A cut-off quiet move gets a bonus of `min(170·d − 90, 1700)` and updates killers, the counter move and all history tables.
- Earlier quiets that were tried and failed get a malus of `min(450·d − 150, 1500)`.
- A cut-off capture gets a capture-history bonus, and every capture tried before it gets a malus.
- Fail-soft scores at cut-offs are blended with beta.
- A quiet move that fails low with no best move earns a small prior bonus to the previous move's continuation history.

### Quiescence search

- Stand-pat uses the corrected static evaluation. When in check, all evasions are searched, with the quiet evasions cut off after a few have been tried.
- Captures and queen promotions are searched with delta pruning (`qs_fut_base` = 200 cp above stand-pat) and a SEE ≥ 0 check.
- Transposition table entries are used for cut-offs and for the stored evaluation.

---

## Time Management

Time allocation follows the standard UCI clocks.

- **`movetime`** uses the given time minus `Move Overhead`.
- **Sudden death** uses an optimal scale of about `0.045` of the remaining time (capped by a fraction of the clock), with a maximum scale of 6.0 times that.
- **`movestogo`** uses a larger optimal scale that depends on the game ply, and a maximum scale based on the moves remaining.
- The **hard limit** is the smaller of `0.825 × clock` and the maximum scale times the soft limit, minus the overhead. The **soft limit** is the optimal allocation.

During iterative deepening the soft limit is scaled each iteration by:

- **Stability:** a best move that has not changed for several iterations shrinks the budget (factor 0.65 to 1.40).
- **Score trend:** a falling score grows the budget (factor 0.60 to 1.70).
- **Node effort:** the fraction of nodes spent on the best root move. Less effort on the best move means the search is less settled and gets more time (factor 0.5 to 1.5).

The search stops when the scaled soft limit is reached, or when the next iteration is predicted to overrun the hard limit. A proven forced mate stops the search once the mate depth has been searched four plies further. A position with exactly one legal move returns immediately when a clock is running.

Node-limited searches (`go nodes N`) stop when the node count is reached. `go depth N`, `go movetime N`, and `go infinite` are also supported. `go ponder` searches until `ponderhit` or `stop`; `ponderhit` recomputes the time limits from the stored `go` parameters.

---

## Parallel Search

- **Threads:** `Threads` (default 1, up to 256) starts that many independent iterative-deepening searches. Each has its own history tables, accumulator stack and search state. All share the transposition table and the global stop flag.
- **Diversity:** each thread's aspiration window is offset by its thread index, so threads search slightly different windows.
- **Reporting:** only thread 0 prints `info` lines.
- **Final move:** when every thread has finished, `pick_best_move` chooses the result. A proven mate beats everything, and the fastest mate wins. Otherwise each thread's move gets a vote weighted by `(score − minimum score + 14) × depth`, and the highest total wins, with ties broken by the greater depth.

---


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
  
```command
g++ -O3 -std=c++17 -mavx2 -mbmi2 -static -static-libgcc -static-libstdc++ -o agnes.exe main.cpp
```
## Getting Started

Once built, you can test the engine directly from a terminal:

```bash
./agnes
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

Because of the "big" network Agnes will most of the times lose on time thus no sprt tests can be conducted at this stage.

## Performance

* Efficient incremental NNUE updates for minimal eval overhead
* Dynamic time management adapts to search depth and position complexity
* Thread-pool architecture enables strong scalability up to 8 cores
* Compact network size (88mm NNUE) provides fast inference on modern hardware

## License

See [LICENSE](LICENSE).
