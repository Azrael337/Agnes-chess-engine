# Agnes Chess Engine

<div align="center">
  <img src="logo.png" alt="Agnes chess engine" width="300">

  ![License](https://img.shields.io/badge/License-GPL_3.0-green)
  ![Language](https://img.shields.io/badge/Language-C++-orange)
  ![Release](https://img.shields.io/badge/Release-1.0-yellow)
  ![Random](https://img.shields.io/badge/Defeat-ME-red)

**A simple strong competitive chess engine in C++.**
This is mainly just an personal experimental chess engine project.

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
Agnes uses stockfish's older nnue for it's evaluation, specifically nn-1111cefa1111.nnue (stockfish 17 used it as it's "big" nnue).

## Search
Nothing too fancy just negamax with fail-soft aplha-beta pruning and lots of pruning and move ordering identical to other top engines.
Multi Parallel search going upto 256 threads.

## UCI Options

| Option      | Type   | Default | Range     | Description                                        |
| ----------- | ------ | ------- | --------- | -------------------------------------------------- |
| `EvalFile`  | string | —       | —         | Path to a custom NNUE evaluation file              |
| `Hash`      | spin   | 256     | 1–65536   | Transposition table size in MB                     |
| `Threads`   | spin   | 1       | 1–256     | Number of search threads (Lazy SMP)                |
| `NNUEScale` | spin   | 40      | 1–400     | NNUE evaluation scaling factor                     |

## UCI Commands
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

If anyone would like to help build the engine like training a fresh nnue for Agnes or if you found any bugs please dm me on Discord(azrael_6989)
And thanks for showing interest into my engine!
