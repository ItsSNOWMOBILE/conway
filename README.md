# Conway

A high-performance Conway's Game of Life simulator written in C++17. Runs in any terminal using Unicode half-block rendering, with differential frame updates, SIMD-friendly auto-vectorization, and optional parallel execution via Intel TBB.

Includes ten classic patterns -- gliders, oscillators, glider guns, and methuselahs -- with interactive controls for pausing, stepping, speed adjustment, and topology switching.

## Features

- **Half-block rendering** -- each terminal character encodes a 1x2 cell pair using Unicode block elements, doubling vertical resolution
- **Differential updates** -- only changed rows are redrawn, reducing terminal output by 80-90% on sparse patterns
- **Activity tracking** -- rows with no live cells are skipped entirely, giving 10-100x speedup on sparse patterns like glider guns
- **Double-buffered grid** -- front/back buffers swap each generation with zero allocation overhead
- **Branch-free rules** -- the core formula `next = (n == 3) | (old & (n == 2))` compiles to branchless SIMD on modern compilers
- **Parallel execution** -- row processing parallelized via `std::execution::par_unseq` when TBB is available (built-in on MSVC)
- **Toroidal and flat topologies** -- toggle at runtime between wrap-around and dead-border grids
- **Cross-platform** -- Windows (virtual terminal processing), Linux, and macOS (termios)

## How It Works

The simulation splits the grid into interior and border columns. Interior columns use direct `+-1` arithmetic for neighbor lookups, enabling the compiler to auto-vectorize across 32 cells per cycle (AVX2). Border columns fall back to pre-computed lookup tables for toroidal or flat wrapping.

```
                ┌─────────────────────────────────┐
                │          Terminal resize         │
                │       (detect W x H at init)     │
                └──────────────┬──────────────────┘
                               │
                ┌──────────────▼──────────────────┐
                │       Grid (W x H uint8_t)      │
                │  front[] ◄──swap──► back[]       │
                │  live_rows[] (activity tracker)  │
                └──────────────┬──────────────────┘
                               │ step()
                ┌──────────────▼──────────────────┐
                │   Interior cols: direct +-1      │
                │   Border cols: lookup tables     │
                │   Rule: (n==3)|(old & (n==2))    │
                └──────────────┬──────────────────┘
                               │
                ┌──────────────▼──────────────────┐
                │     Renderer (differential)      │
                │  prev_display[] vs current state │
                │  ▀ ▄ █ ' ' with ANSI color FSM  │
                │  single fwrite() per frame       │
                └─────────────────────────────────┘
```

## Built-in Patterns

| Pattern          | Type              | Notes                                     |
|------------------|-------------------|-------------------------------------------|
| `glider`         | Spaceship         | Moves diagonally, period-infinity         |
| `blinker`        | Oscillator        | Period-2, 3 cells                         |
| `toad`           | Oscillator        | Period-2, 6 cells                         |
| `beacon`         | Oscillator        | Period-2, 8 cells                         |
| `pulsar`         | Oscillator        | Period-3, 48 cells                        |
| `pentadecathlon` | Oscillator        | Period-15, 18 cells                       |
| `glider-gun`     | Gun               | Gosper glider gun, emits glider every 30 generations |
| `r-pentomino`    | Methuselah        | Stabilizes after 1,103 generations        |
| `acorn`          | Methuselah        | Stabilizes after 5,206 generations        |
| `diehard`        | Methuselah        | Dies after 130 generations                |

## Requirements

| Component | Version         |
|-----------|-----------------|
| CMake     | 3.20+           |
| C++       | C++17 compiler  |
| TBB       | Optional (parallel execution on GCC/Clang) |

MSVC ships parallel STL without TBB. On GCC/Clang, install `libtbb-dev` to enable parallelism.

## Building

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

The binary is placed in `build/` (or `build/Release/` on MSVC).

To install system-wide:

```bash
cmake --install build
```

## Usage

```
conway [options]
```

| Flag            | Description                          | Default  |
|-----------------|--------------------------------------|----------|
| `-f <fps>`      | Target frames per second (1-120)     | 20       |
| `-d <density>`  | Random fill density (0.0-1.0)        | 0.30     |
| `-p <pattern>`  | Start with a named pattern           | random   |
| `--flat`        | Use flat topology instead of toroidal| off      |
| `-h, --help`    | Show help                            |          |

### Examples

```bash
# random soup at 30 fps
./conway -f 30

# gosper glider gun on a flat grid
./conway -p glider-gun --flat

# sparse random fill
./conway -d 0.10 -f 60
```

### Interactive Controls

| Key       | Action                          |
|-----------|---------------------------------|
| `Space`   | Pause / resume                  |
| `s`       | Single-step (while paused)      |
| `r`       | Randomize grid                  |
| `c`       | Clear grid                      |
| `t`       | Toggle topology (toroidal/flat) |
| `+` / `=` | Increase speed (+5 fps)        |
| `-`       | Decrease speed (-5 fps)         |
| `q` / `Esc` | Quit                         |

## Project Structure

```
conway/
├── CMakeLists.txt
├── include/conway/
│   ├── grid.hpp            # simulation engine
│   ├── patterns.hpp        # pattern library interface
│   └── renderer.hpp        # terminal renderer
└── src/
    ├── main.cpp            # entry point, CLI parsing, event loop
    ├── grid.cpp            # double-buffered grid with activity tracking
    ├── patterns.cpp        # coordinate data for all built-in patterns
    └── renderer.cpp        # differential half-block ANSI renderer
```

## Known Limitations

- **No file I/O** -- patterns are compiled-in; there is no support for loading RLE or plaintext pattern files from disk
- **Terminal-bound resolution** -- the grid size is determined by terminal dimensions at startup and does not resize dynamically
- **No GUI** -- rendering targets ANSI-capable terminals only; no graphical windowing support

## License

MIT
