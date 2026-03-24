#pragma once

#include "conway/grid.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace conway {

/// Full-screen ANSI terminal renderer using Unicode half-block characters.
///
/// Each terminal character cell represents a 1×2 pixel block:
///   ▀  top-alive / bottom-dead   (U+2580)
///   ▄  top-dead  / bottom-alive  (U+2584)
///   █  both alive                (U+2588)
///      both dead  — space with dark background
///
/// This doubles the effective vertical resolution and produces a near-square
/// pixel aspect ratio on standard fonts.
///
/// Rendering uses:
///  - 24-bit (true-colour) ANSI escape sequences.
///  - An alternate screen buffer so the user's shell history is preserved.
///  - A single buffered write per frame to minimise flicker.
///  - Within each row, colour codes are emitted only when the state changes
///    (run-length optimisation), reducing output size by ~70 % for typical
///    patterns.
class Renderer {
public:
    Renderer();
    ~Renderer();

    // Non-copyable, non-movable — owns terminal state.
    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&)                 = delete;
    Renderer& operator=(Renderer&&)      = delete;

    /// Grid dimensions that exactly fill the usable terminal area.
    /// Leaves 3 rows at the bottom for the status bar.
    /// Returns {grid_width, grid_height} where grid_height = (term_rows-3)*2.
    [[nodiscard]] std::pair<std::size_t, std::size_t> usable_size() const noexcept;

    /// Render one frame.  Performs a complete redraw; colour-state tracking
    /// inside each row keeps the emitted byte count low.
    void draw(const Grid& grid, bool paused, double fps);

    /// Non-blocking key poll.  Returns '\0' if no key is pending.
    [[nodiscard]] char poll_key() const noexcept;

private:
    int         term_cols_{80};
    int         term_rows_{24};
    std::string frame_buf_;   ///< Accumulated ANSI output, flushed once per frame.

    void init_platform();
    void restore_platform();
    void query_terminal_size() noexcept;

    // Helpers that append directly to frame_buf_.
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
