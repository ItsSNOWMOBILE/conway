#include "conway/patterns.hpp"

#include <algorithm>
#include <cstdlib>
#include <stdexcept>

namespace conway::patterns {

// ── Placement ─────────────────────────────────────────────────────────────────

void place(Grid& grid, const CellList& pattern, std::size_t cx, std::size_t cy) {
    for (auto [dx, dy] : pattern) {
        const auto ax = static_cast<long long>(cx) + dx;
        const auto ay = static_cast<long long>(cy) + dy;
        if (ax >= 0 && ay >= 0
            && static_cast<std::size_t>(ax) < grid.width()
            && static_cast<std::size_t>(ay) < grid.height())
        {
            grid.set(static_cast<std::size_t>(ax),
                     static_cast<std::size_t>(ay), true);
        }
    }
}

// ── Pattern data ──────────────────────────────────────────────────────────────
// Coordinates are (col, row) offsets from the top-left of the pattern's
// bounding box.  place() re-centres them on the grid.

// Period-∞ spaceship.
static const CellList k_glider = {
    {1, 0},
    {2, 1},
    {0, 2}, {1, 2}, {2, 2},
};

// Period-2 oscillator.
static const CellList k_blinker = {
    {0, 0}, {1, 0}, {2, 0},
};

// Period-2 oscillator.
static const CellList k_toad = {
    {1, 0}, {2, 0}, {3, 0},
    {0, 1}, {1, 1}, {2, 1},
};

// Period-2 oscillator.
static const CellList k_beacon = {
    {0, 0}, {1, 0},
    {0, 1},
                    {3, 2},
    {2, 3}, {3, 3},
};

// Period-3 oscillator (48 cells).
// Canonical orientation: 13×13 bounding box.
static const CellList k_pulsar = {
    {2, 0}, {3, 0}, {4, 0},   {8, 0}, {9, 0}, {10, 0},

    {0, 2}, {5, 2},  {7, 2}, {12, 2},
    {0, 3}, {5, 3},  {7, 3}, {12, 3},
    {0, 4}, {5, 4},  {7, 4}, {12, 4},

    {2, 5}, {3, 5}, {4, 5},   {8, 5}, {9, 5}, {10, 5},

    {2, 7}, {3, 7}, {4, 7},   {8, 7}, {9, 7}, {10, 7},

    {0, 8},  {5, 8},  {7, 8},  {12, 8},
    {0, 9},  {5, 9},  {7, 9},  {12, 9},
    {0, 10}, {5, 10}, {7, 10}, {12, 10},

    {2, 12}, {3, 12}, {4, 12}, {8, 12}, {9, 12}, {10, 12},
};

// Period-15 oscillator.
static const CellList k_pentadecathlon = {
    {0, 0}, {1, 0}, {2, 0},
    {0, 1},          {2, 1},   // centre cell missing
    {0, 2}, {1, 2}, {2, 2},
    {0, 3}, {1, 3}, {2, 3},
    {0, 4}, {1, 4}, {2, 4},
    {0, 5}, {1, 5}, {2, 5},
    {0, 6},          {2, 6},   // centre cell missing
    {0, 7}, {1, 7}, {2, 7},
};

// Gosper glider gun — period-30, emits one glider every 30 generations.
static const CellList k_glider_gun = {
    {24, 0},
    {22, 1}, {24, 1},
    {12, 2}, {13, 2}, {20, 2}, {21, 2}, {34, 2}, {35, 2},
    {11, 3}, {15, 3}, {20, 3}, {21, 3}, {34, 3}, {35, 3},
    { 0, 4}, { 1, 4}, {10, 4}, {16, 4}, {20, 4}, {21, 4},
    { 0, 5}, { 1, 5}, {10, 5}, {14, 5}, {16, 5}, {17, 5}, {22, 5}, {24, 5},
    {10, 6}, {16, 6}, {24, 6},
    {11, 7}, {15, 7},
    {12, 8}, {13, 8},
};

// R-pentomino: stabilises after 1,103 generations, producing 116 cells.
static const CellList k_r_pentomino = {
    {1, 0}, {2, 0},
    {0, 1}, {1, 1},
    {1, 2},
};

// Acorn: stabilises after 5,206 generations.
static const CellList k_acorn = {
    {1, 0},
    {3, 1},
    {0, 2}, {1, 2}, {4, 2}, {5, 2}, {6, 2},
};

// Diehard: vanishes completely after 130 generations.
static const CellList k_diehard = {
    {6, 0},
    {0, 1}, {1, 1},
    {1, 2}, {5, 2}, {6, 2}, {7, 2},
};

// ── Accessors ─────────────────────────────────────────────────────────────────

const CellList& glider()          { return k_glider; }
const CellList& blinker()         { return k_blinker; }
const CellList& toad()            { return k_toad; }
const CellList& beacon()          { return k_beacon; }
const CellList& pulsar()          { return k_pulsar; }
const CellList& pentadecathlon()  { return k_pentadecathlon; }
const CellList& glider_gun()      { return k_glider_gun; }
const CellList& r_pentomino()     { return k_r_pentomino; }
const CellList& acorn()           { return k_acorn; }
const CellList& diehard()         { return k_diehard; }

std::vector<PatternInfo> all_patterns() {
    return {
        {"glider",         glider,        "Period-inf spaceship"},
        {"blinker",        blinker,       "Period-2 oscillator (3 cells)"},
        {"toad",           toad,          "Period-2 oscillator (6 cells)"},
        {"beacon",         beacon,        "Period-2 oscillator (8 cells)"},
        {"pulsar",         pulsar,        "Period-3 oscillator (48 cells)"},
        {"pentadecathlon", pentadecathlon,"Period-15 oscillator (18 cells)"},
        {"glider-gun",     glider_gun,    "Gosper glider gun (period-30)"},
        {"r-pentomino",    r_pentomino,   "Methuselah: 1,103 gens"},
        {"acorn",          acorn,         "Methuselah: 5,206 gens"},
        {"diehard",        diehard,       "Methuselah: dies at gen 130"},
    };
}

}  // namespace conway::patterns
