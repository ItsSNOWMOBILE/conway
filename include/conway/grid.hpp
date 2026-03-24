#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

/// Row-major, double-buffered grid for Conway's Game of Life.
///
/// The simulation step is branch-free in the hot path and uses precomputed
/// neighbor-index tables to avoid per-cell modular arithmetic, making it
/// suitable for auto-vectorisation.
///
/// Topology is configurable at construction time:
///   - Toroidal  (default): edges wrap around — infinite plane on a torus.
///   - Flat               : edges are permanently dead.
class Grid {
public:
    using Cell = std::uint8_t;  ///< 0 = dead, 1 = alive

    /// @param width    Number of columns (must be >= 3).
    /// @param height   Number of rows    (must be >= 3).
    /// @param toroidal If true, the grid wraps at all four edges.
    Grid(std::size_t width, std::size_t height, bool toroidal = true);

    // ── Simulation ────────────────────────────────────────────────────────────

    /// Advance the simulation by one generation (O(width × height)).
    void step() noexcept;

    // ── Mutation ──────────────────────────────────────────────────────────────

    /// Set every cell to dead and reset the generation counter.
    void clear() noexcept;

    /// Populate the grid randomly.
    /// @param density  Probability that each cell starts alive [0, 1].
    void randomize(double density = 0.30);

    /// Toggle a single cell.  Keeps live_count() consistent.
    void set(std::size_t x, std::size_t y, bool alive) noexcept;

    // ── Observation ───────────────────────────────────────────────────────────

    [[nodiscard]] bool get(std::size_t x, std::size_t y) const noexcept;

    [[nodiscard]] std::size_t width()      const noexcept { return w_; }
    [[nodiscard]] std::size_t height()     const noexcept { return h_; }
    [[nodiscard]] std::size_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
    [[nodiscard]] bool        toroidal()   const noexcept { return toroidal_; }

    /// Raw pointer to the row-major cell buffer (read-only).
    [[nodiscard]] const Cell* data() const noexcept { return front_.data(); }

private:
    std::size_t w_;
    std::size_t h_;
    bool        toroidal_;
    std::size_t generation_{0};
    std::size_t live_count_{0};

    std::vector<Cell>        front_;     ///< Current generation.
    std::vector<Cell>        back_;      ///< Next generation (scratch buffer).
    std::vector<std::size_t> prev_row_;  ///< prev_row_[y] = row index above y.
    std::vector<std::size_t> next_row_;  ///< next_row_[y] = row index below y.
    std::vector<std::size_t> prev_col_;  ///< prev_col_[x] = col index left of x.
    std::vector<std::size_t> next_col_;  ///< next_col_[x] = col index right of x.

    void rebuild_tables() noexcept;
};

}  // namespace conway
