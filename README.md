# Agnes Chess Engine

<div align="center">
  <img src="logo.png" alt="Agnes chess engine" width="300">

  ![License](https://img.shields.io/badge/License-GPL_3.0-green?style=for-the-badge)
  ![C++](https://img.shields.io/badge/C++-17-purple?style=for-the-badge)
  ![Release](https://img.shields.io/badge/Release-Agnes_1.0-yellow?style=for-the-badge)
 <br>
  ![Random](https://img.shields.io/badge/Defeat_ME-🤍-red?style=for-the-badge)


**A simple strong chess engine in C++ using a compact nnue and an efficient search**

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
- [License](#license)

---

## Evaluation
Agnes uses stockfish style net based on the below architecture:
- features HalfKAv2_hm^
- --l1 512  --l2 16  --l3 32
- trained on nnue-pytorch pipeline on a cpu(ryzen 5825u) **,it was painful**

## Search
Nothing too fancy just negamax with fail-soft aplha-beta pruning and lots of pruning and move ordering identical to other top engines.

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
# AVX2 (most modern CPUs)
g++ -std=c++17 -O3 -mavx2 -mbmi -mbmi2 -mpopcnt -DNDEBUG -DBACKENED_NO_VIRTUAL -pthread agnes.cpp -o agnes-avx2

# AVX-512 VNNI (the dpbusd path in nnue.h)
g++ -std=c++17 -O3 -mavx512f -mavx512bw -mavx512vl -mavx512vnni -mbmi2 -DNDEBUG -DBACKENED_NO_VIRTUAL -pthread agnes.cpp -o agnes-vnni

# Baseline, for very old CPUs (slow scalar NNUE)
g++ -std=c++17 -O3 -msse4.2 -mpopcnt -DNDEBUG -DBACKENED_NO_VIRTUAL -pthread agnes.cpp -o agnes-sse42
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

Estimated strength is currently around **2900–3100 Elo** as per release 1.0.

Release 2.0* can be 3400 elo


## Performance

* Efficient incremental NNUE updates for minimal eval overhead
* Dynamic time management adapts to search depth and position complexity
* Thread-pool architecture enables strong scalability up to 256 cores

## License

See [LICENSE](LICENSE).
