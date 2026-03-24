#pragma once

#include "conway/grid.hpp"

#include <string_view>
#include <utility>
#include <vector>

namespace conway::patterns {

/// A list of (col, row) offsets describing a pattern relative to an origin.
using CellList = std::vector<std::pair<int, int>>;

/// Place @p pattern on @p grid, centred at (@p cx, @p cy).
/// Cells that fall outside the grid boundary are silently discarded.
void place(Grid& grid, const CellList& pattern, std::size_t cx, std::size_t cy);

// ── Built-in patterns ─────────────────────────────────────────────────────────

/// Period-∞ spaceship (moves diagonally).
const CellList& glider();

/// Period-2 oscillator (3 cells).
const CellList& blinker();

/// Period-2 oscillator (6 cells).
const CellList& toad();

/// Period-2 oscillator (8 cells / 2 gliders).
const CellList& beacon();

/// Period-3 oscillator (48 cells).
const CellList& pulsar();

/// Period-15 oscillator (18 cells).
const CellList& pentadecathlon();

/// Period-30 gun — emits a glider every 30 generations.
const CellList& glider_gun();

/// Methuselah — stabilises after 1,103 generations.
const CellList& r_pentomino();

/// Methuselah — stabilises after 5,206 generations.
const CellList& acorn();

/// Methuselah — dies completely after 130 generations.
const CellList& diehard();

// ── Registry ──────────────────────────────────────────────────────────────────

struct PatternInfo {
    std::string_view  name;
    const CellList& (*cells)();
    std::string_view  description;
};

/// Return all built-in patterns, in display order.
[[nodiscard]] std::vector<PatternInfo> all_patterns();

}  // namespace conway::patterns
