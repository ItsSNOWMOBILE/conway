#include "conway/renderer.hpp"

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>

// ── Platform includes ─────────────────────────────────────────────────────────

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  define NOMINMAX
#  include <windows.h>
#  include <conio.h>
#else
#  include <fcntl.h>
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

// ── ANSI escape sequences ─────────────────────────────────────────────────────

namespace {

// True-colour palette.
// Alive: vivid green.  Dead: near-black with a hint of blue.
constexpr const char* k_fg_alive  = "\033[38;2;0;230;100m";
constexpr const char* k_fg_dead   = "\033[38;2;18;18;28m";
constexpr const char* k_bg_alive  = "\033[48;2;0;230;100m";
constexpr const char* k_bg_dead   = "\033[48;2;18;18;28m";
constexpr const char* k_reset     = "\033[0m";
constexpr const char* k_hide_cur  = "\033[?25l";
constexpr const char* k_show_cur  = "\033[?25h";
constexpr const char* k_alt_enter = "\033[?1049h";  // enter alternate screen
constexpr const char* k_alt_exit  = "\033[?1049l";  // leave alternate screen
constexpr const char* k_home      = "\033[H";        // cursor to (1,1)
constexpr const char* k_eol       = "\033[K";        // erase to end of line

// Unicode half-block characters (UTF-8 encoded).
constexpr const char* k_upper_half = "\xe2\x96\x80";  // ▀  U+2580
constexpr const char* k_lower_half = "\xe2\x96\x84";  // ▄  U+2584
constexpr const char* k_full_block = "\xe2\x96\x88";  // █  U+2588

// Encode the 4 possible (top, bottom) combinations in a lookup table.
// Entry index: (top_alive << 1) | bottom_alive
struct CellGlyph {
    const char* fg;
    const char* bg;
    const char* glyph;   // UTF-8 string
};

constexpr std::array<CellGlyph, 4> k_glyphs = {{
    /* 00 dead/dead   */ { k_fg_dead,  k_bg_dead,  " "            },
    /* 01 dead/alive  */ { k_fg_alive, k_bg_dead,  k_lower_half   },
    /* 10 alive/dead  */ { k_fg_alive, k_bg_dead,  k_upper_half   },
    /* 11 alive/alive */ { k_fg_alive, k_bg_alive, " "            },
}};

}  // namespace

// ── Platform state (file-scope) ───────────────────────────────────────────────

#ifdef _WIN32
static HANDLE g_hOut   = INVALID_HANDLE_VALUE;
static HANDLE g_hIn    = INVALID_HANDLE_VALUE;
static DWORD  g_origOutMode = 0;
static DWORD  g_origInMode  = 0;
#else
static termios g_orig_termios{};
#endif

// ── conway::Renderer ──────────────────────────────────────────────────────────

namespace conway {

Renderer::Renderer() {
    init_platform();
    query_terminal_size();

    // Reserve a generous initial buffer to avoid reallocations during rendering.
    frame_buf_.reserve(static_cast<std::size_t>(term_cols_ * term_rows_) * 24);

    // Enter the alternate screen buffer, clear it, and hide the cursor.
    frame_buf_  = k_alt_enter;
    frame_buf_ += k_bg_dead;    // fill background
    frame_buf_ += "\033[2J";    // clear entire screen
    frame_buf_ += k_hide_cur;
    frame_buf_ += k_home;
    flush_frame();
}

Renderer::~Renderer() {
    // Restore cursor and leave the alternate screen (user's shell history returns).
    frame_buf_  = k_show_cur;
    frame_buf_ += k_reset;
    frame_buf_ += k_alt_exit;
    flush_frame();

    restore_platform();
}

// ── Public interface ──────────────────────────────────────────────────────────

std::pair<std::size_t, std::size_t> Renderer::usable_size() const noexcept {
    // Reserve 3 terminal rows at the bottom for the status bar.
    const int usable_rows = std::max(term_rows_ - 3, 2);
    // Each terminal row represents 2 grid rows (half-block trick).
    return {static_cast<std::size_t>(term_cols_),
            static_cast<std::size_t>(usable_rows * 2)};
}

void Renderer::draw(const Grid& grid, bool paused, double fps) {
    const std::size_t gw   = grid.width();
    const std::size_t gh   = grid.height();
    const auto* const data = grid.data();

    frame_buf_.clear();
    buf_hide_cursor();
    buf_move_home();

    // Half-block rendering: each terminal row covers two grid rows.
    const int display_rows = (static_cast<int>(gh) + 1) / 2;

    for (int ty = 0; ty < display_rows; ++ty) {
        const std::size_t y0 = static_cast<std::size_t>(ty * 2);
        const std::size_t y1 = y0 + 1;

        // Track current fg/bg state to suppress redundant ANSI codes.
        int cur_fg = -1;  // -1 = unknown / just reset
        int cur_bg = -1;

        for (std::size_t x = 0; x < gw; ++x) {
            const bool top = data[y0 * gw + x] != 0;
            const bool bot = (y1 < gh) && (data[y1 * gw + x] != 0);

            const int idx = (static_cast<int>(top) << 1) | static_cast<int>(bot);
            const auto& g = k_glyphs[static_cast<std::size_t>(idx)];

            const int need_fg = (top || bot) ? 1 : 0;
            const int need_bg = (top && bot) ? 1 : 0;

            if (need_fg != cur_fg) { buf_set_fg(need_fg == 1); cur_fg = need_fg; }
            if (need_bg != cur_bg) { buf_set_bg(need_bg == 1); cur_bg = need_bg; }

            frame_buf_ += g.glyph;
        }

        // End of terminal row: reset colours and move to the next line.
        buf_reset_colors();
        frame_buf_ += "\r\n";
    }

    // ── Status bar ────────────────────────────────────────────────────────────
    buf_reset_colors();

    // Line 1: blank separator
    frame_buf_ += "\033[2m";   // dim
    frame_buf_ += std::string(static_cast<std::size_t>(term_cols_), '-');
    frame_buf_ += k_reset;
    frame_buf_ += "\r\n";

    // Line 2: simulation stats
    char stats[256];
    std::snprintf(stats, sizeof(stats),
        "\033[1m\033[32mGen:\033[0m %-10zu  "
        "\033[1m\033[32mLive:\033[0m %-10zu  "
        "\033[1m\033[32mFPS:\033[0m %5.1f  "
        "%s",
        grid.generation(),
        grid.live_count(),
        fps,
        paused ? "\033[1;33m[PAUSED]\033[0m" : "        ");

    frame_buf_ += stats;
    frame_buf_ += k_eol;
    frame_buf_ += "\r\n";

    // Line 3: key hints
    frame_buf_ += "\033[2m";
    frame_buf_ += " q/Esc:quit  Space:pause  s:step  r:rand  "
                  "c:clear  +:faster  -:slower  t:topology";
    frame_buf_ += k_reset;
    frame_buf_ += k_eol;

    flush_frame();
}

char Renderer::poll_key() const noexcept {
#ifdef _WIN32
    if (_kbhit()) {
        const int ch = _getch();
        // Map extended key codes: Esc is 27, arrow keys come as 0/224 + code.
        if (ch == 0 || ch == 224) {
            _getch();  // discard second byte of extended sequence
            return '\0';
        }
        return static_cast<char>(ch);
    }
    return '\0';
#else
    char c = '\0';
    if (::read(STDIN_FILENO, &c, 1) == 1) return c;
    return '\0';
#endif
}

// ── Private: frame buffer helpers ────────────────────────────────────────────

void Renderer::buf_move_home() {
    frame_buf_ += k_home;
}

void Renderer::buf_move_to(int col, int row) {
    char esc[32];
    std::snprintf(esc, sizeof(esc), "\033[%d;%dH", row + 1, col + 1);
    frame_buf_ += esc;
}

void Renderer::buf_set_fg(bool alive) {
    frame_buf_ += alive ? k_fg_alive : k_fg_dead;
}

void Renderer::buf_set_bg(bool alive) {
    frame_buf_ += alive ? k_bg_alive : k_bg_dead;
}

void Renderer::buf_reset_colors() {
    frame_buf_ += k_reset;
}

void Renderer::buf_hide_cursor() {
    frame_buf_ += k_hide_cur;
}

void Renderer::buf_show_cursor() {
    frame_buf_ += k_show_cur;
}

void Renderer::flush_frame() {
    if (frame_buf_.empty()) return;
    std::fwrite(frame_buf_.data(), 1, frame_buf_.size(), stdout);
    std::fflush(stdout);
}

// ── Private: platform init/restore ───────────────────────────────────────────

void Renderer::init_platform() {
#ifdef _WIN32
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);

    // Save original modes.
    GetConsoleMode(g_hOut, &g_origOutMode);
    GetConsoleMode(g_hIn,  &g_origInMode);

    // Enable ANSI escape processing and UTF-8 output.
    SetConsoleMode(g_hOut,
        g_origOutMode
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN);
    SetConsoleOutputCP(CP_UTF8);

    // Disable line input and echo so poll_key() works character-by-character.
    SetConsoleMode(g_hIn,
        g_origInMode
        & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
#else
    // Switch stdin to raw mode so individual key presses are readable.
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    termios raw = g_orig_termios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN]  = 0;  // non-blocking
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
#endif
}

void Renderer::restore_platform() {
#ifdef _WIN32
    if (g_hOut != INVALID_HANDLE_VALUE) SetConsoleMode(g_hOut, g_origOutMode);
    if (g_hIn  != INVALID_HANDLE_VALUE) SetConsoleMode(g_hIn,  g_origInMode);
#else
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
#endif
}

void Renderer::query_terminal_size() noexcept {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi{};
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
        term_cols_ = csbi.srWindow.Right  - csbi.srWindow.Left + 1;
        term_rows_ = csbi.srWindow.Bottom - csbi.srWindow.Top  + 1;
        return;
    }
#else
    winsize ws{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0
        && ws.ws_col > 0 && ws.ws_row > 0)
    {
        term_cols_ = static_cast<int>(ws.ws_col);
        term_rows_ = static_cast<int>(ws.ws_row);
        return;
    }
#endif
    // Safe fallback.
    term_cols_ = 80;
    term_rows_ = 24;
}

}  // namespace conway
