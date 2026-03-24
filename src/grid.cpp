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
{
    assert(width  >= 3 && "Grid width must be >= 3");
    assert(height >= 3 && "Grid height must be >= 3");
    rebuild_tables();
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

// ── Simulation ────────────────────────────────────────────────────────────────

void Grid::step() noexcept {
    const std::size_t w = w_;
    std::size_t       live = 0;

    // The inner loop is branch-free and uses only integer addition and
    // bitwise operations — the compiler can auto-vectorise with SIMD.
    //
    // Key formula (no branching):
    //   next = (n == 3) | (old & (n == 2))
    //
    // where n = neighbour count, old = current cell state (0 or 1).
    //
    // Verification:
    //   n=2, old=1 → 0 | 1 = 1  (survival)
    //   n=3, old=0 → 1 | 0 = 1  (birth)
    //   n=3, old=1 → 1 | 0 = 1  (survival)
    //   n=4, old=1 → 0 | 0 = 0  (overcrowding)

#ifdef CONWAY_PARALLEL
    // Parallel outer loop over rows — each row is independent.
    std::vector<std::size_t> row_indices(h_);
    std::iota(row_indices.begin(), row_indices.end(), std::size_t{0});

    std::for_each(std::execution::par_unseq,
                  row_indices.begin(), row_indices.end(),
                  [&](std::size_t y) {
        const Cell* __restrict rp = front_.data() + prev_row_[y] * w;
        const Cell* __restrict rc = front_.data() + y            * w;
        const Cell* __restrict rn = front_.data() + next_row_[y] * w;
        Cell*       __restrict dst = back_.data() + y            * w;

        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t xl = prev_col_[x];
            const std::size_t xr = next_col_[x];

            const int n = rp[xl] + rp[x] + rp[xr]
                        + rc[xl]          + rc[xr]
                        + rn[xl] + rn[x] + rn[xr];

            dst[x] = static_cast<Cell>((n == 3) | (rc[x] & static_cast<Cell>(n == 2)));
        }
    });

    live = static_cast<std::size_t>(
        std::count(back_.begin(), back_.end(), Cell{1}));
#else
    for (std::size_t y = 0; y < h_; ++y) {
        const Cell* __restrict rp  = front_.data() + prev_row_[y] * w;
        const Cell* __restrict rc  = front_.data() + y            * w;
        const Cell* __restrict rn  = front_.data() + next_row_[y] * w;
        Cell*       __restrict dst = back_.data()  + y            * w;

        for (std::size_t x = 0; x < w; ++x) {
            const std::size_t xl = prev_col_[x];
            const std::size_t xr = next_col_[x];

            const int n = rp[xl] + rp[x] + rp[xr]
                        + rc[xl]          + rc[xr]
                        + rn[xl] + rn[x] + rn[xr];

            const Cell next = static_cast<Cell>(
                (n == 3) | (rc[x] & static_cast<Cell>(n == 2)));

            dst[x] = next;
            live  += next;
        }
    }
#endif

    front_.swap(back_);
    live_count_ = live;
    ++generation_;
}

// ── Mutation ──────────────────────────────────────────────────────────────────

void Grid::clear() noexcept {
    std::fill(front_.begin(), front_.end(), Cell{0});
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
}

void Grid::set(std::size_t x, std::size_t y, bool alive) noexcept {
    assert(x < w_ && y < h_);
    Cell&      cell    = front_[y * w_ + x];
    const Cell new_val = alive ? Cell{1} : Cell{0};
    // Adjust live count without branching.
    live_count_ = live_count_ - cell + new_val;
    cell = new_val;
}

// ── Observation ───────────────────────────────────────────────────────────────

bool Grid::get(std::size_t x, std::size_t y) const noexcept {
    assert(x < w_ && y < h_);
    return front_[y * w_ + x] != Cell{0};
}

}  // namespace conway
