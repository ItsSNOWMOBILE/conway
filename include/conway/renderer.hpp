#pragma once

#include "conway/grid.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace conway {

/// Full-screen ANSI terminal renderer using Unicode half-block characters.
///
/// Each terminal character cell represents a 1×2 pixel block:
///   ▀  top-alive / bottom-dead   (U+2580)
///   ▄  top-dead  / bottom-alive  (U+2584)
///      top-alive / bottom-alive  — space with alive background
///      both dead                 — space with dead background
///
/// Optimisations:
///   - Differential rendering: prev_display_ stores the (top<<1)|bottom
///     state for every display position.  draw() scans for changed rows and
///     skips them entirely — no cursor move, no ANSI codes emitted.  For
///     sparse patterns this cuts terminal output by ~80-90%.
///   - Within dirty rows, a colour-state machine suppresses redundant fg/bg
///     escape sequences across consecutive cells with the same state.
///   - The entire frame is accumulated in frame_buf_ and flushed in a single
///     fwrite() call, minimising flicker.
class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(Renderer&&)      = delete;

    /// Grid dimensions that exactly fill the usable terminal area.
    /// Reserves 3 rows at the bottom for the status bar.
    /// Returns {grid_width, grid_height} where grid_height = (term_rows-3)*2.
    [[nodiscard]] std::pair<std::size_t, std::size_t> usable_size() const noexcept;

    /// Render one frame with differential updates.
    void draw(const Grid& grid, bool paused, double fps);

    /// Non-blocking key poll.  Returns '\0' if no key is pending.
    [[nodiscard]] char poll_key() const noexcept;

private:
    int         term_cols_{80};
    int         term_rows_{24};
    std::string frame_buf_;

    // Differential state: (top_alive<<1)|bottom_alive per display cell.
    // 0xFF is used as a sentinel meaning "unknown / force redraw".
    std::vector<std::uint8_t> prev_display_;
    std::size_t               prev_gw_{0};
    std::size_t               prev_gh_{0};

    void init_platform();
    void restore_platform();
    void query_terminal_size() noexcept;

    void buf_move_home();
    void buf_move_to(int col, int row);
    void buf_set_fg(bool alive);
    void buf_set_bg(bool alive);
    void buf_reset_colors();
    void buf_hide_cursor();
    void buf_show_cursor();
    void flush_frame();
};

}  // namespace conway
