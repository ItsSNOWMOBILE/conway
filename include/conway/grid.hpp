#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace conway {

/// Row-major, double-buffered grid for Conway's Game of Life.
///
/// Optimisations:
///   1. Interior / border split — interior columns use direct ±1 arithmetic
///      (no index-table lookups) so the hot inner loop is auto-vectorisable.
///   2. Activity tracking — live_rows_[y] records whether row y contains any
///      live cell.  step() skips rows with no live influence entirely, which
///      is O(1) per dead row instead of O(W).  Sparse patterns (glider gun,
///      methuselahs) can be 10-100× faster as a result.
///   3. Pre-allocated row-index buffer — the parallel execution path no longer
///      heap-allocates a std::vector<size_t> on every call to step().
class Grid {
public:
    using Cell = std::uint8_t;  ///< 0 = dead, 1 = alive

    Grid(std::size_t width, std::size_t height, bool toroidal = true);

    // ── Simulation ────────────────────────────────────────────────────────────

    /// Advance the simulation by one generation.
    void step() noexcept;

    // ── Mutation ──────────────────────────────────────────────────────────────

    void clear() noexcept;
    void randomize(double density = 0.30);
    void set(std::size_t x, std::size_t y, bool alive) noexcept;

    // ── Observation ───────────────────────────────────────────────────────────

    [[nodiscard]] bool        get(std::size_t x, std::size_t y) const noexcept;
    [[nodiscard]] std::size_t width()      const noexcept { return w_; }
    [[nodiscard]] std::size_t height()     const noexcept { return h_; }
    [[nodiscard]] std::size_t generation() const noexcept { return generation_; }
    [[nodiscard]] std::size_t live_count() const noexcept { return live_count_; }
    [[nodiscard]] bool        toroidal()   const noexcept { return toroidal_; }
    [[nodiscard]] const Cell* data()       const noexcept { return front_.data(); }

private:
    std::size_t w_, h_;
    bool        toroidal_;
    std::size_t generation_{0};
    std::size_t live_count_{0};

    std::vector<Cell>        front_;
    std::vector<Cell>        back_;
    std::vector<std::size_t> prev_row_;
    std::vector<std::size_t> next_row_;
    std::vector<std::size_t> prev_col_;
    std::vector<std::size_t> next_col_;

    // Activity tracking.
    // uint8_t (not bool) so adjacent bytes are independently writable
    // from parallel SIMD lanes without introducing data races.
    std::vector<std::uint8_t> live_rows_;       ///< current generation
    std::vector<std::uint8_t> next_live_rows_;  ///< scratch for next generation

    // Pre-allocated row-index range for std::execution::par_unseq.
    std::vector<std::size_t> row_indices_;

    void rebuild_tables() noexcept;
    void recompute_live_rows() noexcept;
};

}  // namespace conway
