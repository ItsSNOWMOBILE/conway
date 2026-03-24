#include "conway/grid.hpp"

#include <algorithm>
#include <cassert>
#include <numeric>
#include <random>

#ifdef CONWAY_PARALLEL
#  include <execution>
#endif

namespace conway {

// ── Construction ──────────────────────────────────────────────────────────────

Grid::Grid(std::size_t width, std::size_t height, bool toroidal)
    : w_{width}
    , h_{height}
    , toroidal_{toroidal}
    , front_(width * height, Cell{0})
    , back_(width * height, Cell{0})
    , prev_row_(height)
    , next_row_(height)
    , prev_col_(width)
    , next_col_(width)
    , live_rows_(height, std::uint8_t{0})
    , next_live_rows_(height, std::uint8_t{0})
    , row_indices_(height)
{
    assert(width  >= 3 && "Grid width must be >= 3");
    assert(height >= 3 && "Grid height must be >= 3");
    rebuild_tables();
    std::iota(row_indices_.begin(), row_indices_.end(), std::size_t{0});
}

// ── Private helpers ───────────────────────────────────────────────────────────

void Grid::rebuild_tables() noexcept {
    for (std::size_t i = 0; i < h_; ++i) {
        if (toroidal_) {
            prev_row_[i] = (i == 0)      ? h_ - 1 : i - 1;
            next_row_[i] = (i == h_ - 1) ? 0      : i + 1;
        } else {
            prev_row_[i] = (i == 0)      ? 0      : i - 1;
            next_row_[i] = (i == h_ - 1) ? h_ - 1 : i + 1;
        }
    }
    for (std::size_t i = 0; i < w_; ++i) {
        if (toroidal_) {
            prev_col_[i] = (i == 0)      ? w_ - 1 : i - 1;
            next_col_[i] = (i == w_ - 1) ? 0      : i + 1;
        } else {
            prev_col_[i] = (i == 0)      ? 0      : i - 1;
            next_col_[i] = (i == w_ - 1) ? w_ - 1 : i + 1;
        }
    }
}

void Grid::recompute_live_rows() noexcept {
    for (std::size_t y = 0; y < h_; ++y) {
        const Cell* row = front_.data() + y * w_;
        std::uint8_t has_live = 0;
        for (std::size_t x = 0; x < w_ && !has_live; ++x)
            has_live = row[x];
        live_rows_[y] = has_live;
    }
}

// ── Simulation ────────────────────────────────────────────────────────────────

void Grid::step() noexcept {
    const std::size_t w = w_;

    // Process one row.  Returns the number of live cells written to back_.
    //
    // Key formula (branch-free):
    //   next = (n == 3) | (old & (n == 2))
    //
    // Interior columns use direct ±1 arithmetic — no table lookups — so the
    // compiler can emit a fully vectorised inner loop (AVX2: 32 cells/cycle).
    // Only the two border columns (x=0 and x=w-1) fall back to table lookups.
    //
    // Activity check: if neither this row nor either neighbour row contains
    // a live cell, the output row is guaranteed to be all-dead.  We write
    // zeros and skip the computation entirely.
    auto process_row = [&](std::size_t y) -> std::size_t {
        const bool active =  live_rows_[prev_row_[y]]
                          || live_rows_[y]
                          || live_rows_[next_row_[y]];

        Cell* __restrict dst = back_.data() + y * w;

        if (!active) {
            // Back buffer holds stale data from two generations ago — zero it.
            std::fill_n(dst, w, Cell{0});
            next_live_rows_[y] = 0;
            return std::size_t{0};
        }

        const Cell* __restrict rp = front_.data() + prev_row_[y] * w;
        const Cell* __restrict rc = front_.data() + y            * w;
        const Cell* __restrict rn = front_.data() + next_row_[y] * w;

        // ── Left border (x = 0) ───────────────────────────────────────────────
        {
            const std::size_t xl = prev_col_[0];
            const std::size_t xr = next_col_[0];
            const int n = rp[xl] + rp[0] + rp[xr]
                        + rc[xl]          + rc[xr]
                        + rn[xl] + rn[0] + rn[xr];
            dst[0] = static_cast<Cell>((n==3) | (rc[0] & static_cast<Cell>(n==2)));
        }

        // ── Interior columns (no table lookups, auto-vectorisable) ────────────
        for (std::size_t x = 1; x + 1 < w; ++x) {
            const int n = rp[x-1] + rp[x] + rp[x+1]
                        + rc[x-1]          + rc[x+1]
                        + rn[x-1] + rn[x] + rn[x+1];
            dst[x] = static_cast<Cell>((n==3) | (rc[x] & static_cast<Cell>(n==2)));
        }

        // ── Right border (x = w-1) ────────────────────────────────────────────
        {
            const std::size_t xl = prev_col_[w-1];
            const std::size_t xr = next_col_[w-1];
            const int n = rp[xl] + rp[w-1] + rp[xr]
                        + rc[xl]            + rc[xr]
                        + rn[xl] + rn[w-1] + rn[xr];
            dst[w-1] = static_cast<Cell>((n==3) | (rc[w-1] & static_cast<Cell>(n==2)));
        }

        // Count live output cells and update activity for next generation.
        std::size_t row_live = 0;
        for (std::size_t x = 0; x < w; ++x) row_live += dst[x];
        next_live_rows_[y] = static_cast<std::uint8_t>(row_live > 0);
        return row_live;
    };

#ifdef CONWAY_PARALLEL
    // Each row is independent.  process_row writes to unique slices of back_
    // and unique indices of next_live_rows_, so there are no data races.
    // The row_indices_ buffer is pre-allocated — zero heap activity per call.
    std::for_each(std::execution::par_unseq,
                  row_indices_.begin(), row_indices_.end(),
                  [&](std::size_t y) { process_row(y); });

    // Count lives in a separate parallel pass (avoids atomic accumulation).
    const auto live = static_cast<std::size_t>(
        std::count(back_.begin(), back_.end(), Cell{1}));
#else
    std::size_t live = 0;
    for (std::size_t y = 0; y < h_; ++y)
        live += process_row(y);
#endif

    front_.swap(back_);
    live_count_ = live;
    ++generation_;
    live_rows_.swap(next_live_rows_);
}

// ── Mutation ──────────────────────────────────────────────────────────────────

void Grid::clear() noexcept {
    std::fill(front_.begin(),       front_.end(),       Cell{0});
    std::fill(live_rows_.begin(),   live_rows_.end(),   std::uint8_t{0});
    std::fill(next_live_rows_.begin(), next_live_rows_.end(), std::uint8_t{0});
    live_count_ = 0;
    generation_ = 0;
}

void Grid::randomize(double density) {
    assert(density >= 0.0 && density <= 1.0);
    std::mt19937_64             rng{std::random_device{}()};
    std::bernoulli_distribution dist{density};

    std::size_t live = 0;
    for (auto& cell : front_) {
        cell  = dist(rng) ? Cell{1} : Cell{0};
        live += cell;
    }
    live_count_ = live;
    generation_ = 0;
    recompute_live_rows();
}

void Grid::set(std::size_t x, std::size_t y, bool alive) noexcept {
    assert(x < w_ && y < h_);
    Cell&      cell    = front_[y * w_ + x];
    const Cell new_val = alive ? Cell{1} : Cell{0};
    live_count_ = live_count_ - cell + new_val;
    cell = new_val;

    // Update activity for this row.
    if (alive) {
        live_rows_[y] = 1;
    } else {
        // Rescan to see if any live cells remain in the row.
        const Cell* row = front_.data() + y * w_;
        std::uint8_t has_live = 0;
        for (std::size_t i = 0; i < w_ && !has_live; ++i) has_live = row[i];
        live_rows_[y] = has_live;
    }
}

// ── Observation ───────────────────────────────────────────────────────────────

bool Grid::get(std::size_t x, std::size_t y) const noexcept {
    assert(x < w_ && y < h_);
    return front_[y * w_ + x] != Cell{0};
}

}  // namespace conway
