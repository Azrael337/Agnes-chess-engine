<p align="center">
  <img src="logo.png" alt="Agnes chess engine">
</p>

# Agnes Chess Engine

> A chess engine using stockfish network

Agnes is a lightweight UCI chess engine written in C++ with a focus on efficient search and a compact NNUE evaluation.

## Evaluation

* **NNUE Architecture:** HalfKAv2_hm^
* **Network Layers:** L1=128, L2=32, L3=8 with PSQT buckets
* **Network File:** Acherontia (88mm.nnue)
* **Incremental NNUE Evaluation:** Efficient position updates with feature transformer
* **Tapered Material Evaluation:** Smooth transition between middlegame and endgame
* **Customizable NNUE Scaling:** Adjust evaluation strength via UCI option

## Search

* **Core Algorithm:** Alpha-Beta Negamax with fail-soft
* **Move Ordering:** Principal Variation Search (PVS)
* **Depth:** Iterative Deepening up to 64 plies
* **Transposition Table:** Configurable 1-4096 MB with generation-based aging
* **Pruning Techniques:**
  * Late Move Reductions (LMR) with logarithmic formula
  * Null Move Pruning
  * Reverse Futility Pruning
  * Futility Pruning
  * ProbCut
  * Delta Pruning in quiescence search
* **Move Ordering Heuristics:**
  * Killer Move Heuristic
  * History Heuristic
  * Continuation History (4 ply)
  * Countermove Heuristic
* **Extensions:**
  * Singular Extensions
  * Static exchange evaluation (SEE) for capture ordering and pruning
* **Time Management:** Aspiration Windows with soft and hard time limits
* **Quiescence Search:** Delta pruning and tactical awareness
* **Parallel Search:** Multi-threaded support (up to 8 threads)

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

## Stockfish Network
Agnes uses the [stockfish-nets(specifically-nn-1111cefa1111.nnue)](https://github.com/official-stockfish/networks) for evaluation.
I had earlier tried to train my own NNUE from scratch but I lack the hardware and sanity to train one, so I rage quit and for some time and settled here where I am right now.

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
