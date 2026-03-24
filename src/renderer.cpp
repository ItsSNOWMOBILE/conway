#include "conway/renderer.hpp"

#include <array>
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
#  include <sys/ioctl.h>
#  include <termios.h>
#  include <unistd.h>
#endif

// ── ANSI constants ────────────────────────────────────────────────────────────

namespace {

constexpr const char* k_fg_alive  = "\033[38;2;0;230;100m";
constexpr const char* k_fg_dead   = "\033[38;2;18;18;28m";
constexpr const char* k_bg_alive  = "\033[48;2;0;230;100m";
constexpr const char* k_bg_dead   = "\033[48;2;18;18;28m";
constexpr const char* k_reset     = "\033[0m";
constexpr const char* k_hide_cur  = "\033[?25l";
constexpr const char* k_show_cur  = "\033[?25h";
constexpr const char* k_alt_enter = "\033[?1049h";
constexpr const char* k_alt_exit  = "\033[?1049l";
constexpr const char* k_eol       = "\033[K";

// UTF-8 encoded Unicode half-block characters.
constexpr const char* k_upper_half = "\xe2\x96\x80";  // ▀  U+2580
constexpr const char* k_lower_half = "\xe2\x96\x84";  // ▄  U+2584

// Glyph lookup table indexed by (top_alive<<1)|bottom_alive.
struct CellGlyph { const char* glyph; };
constexpr std::array<CellGlyph, 4> k_glyphs = {{
    /* 00 dead /dead  */ { " "           },
    /* 01 dead /alive */ { k_lower_half  },
    /* 10 alive/dead  */ { k_upper_half  },
    /* 11 alive/alive */ { " "           },   // bg=alive paints the full block
}};

}  // namespace

// ── Platform state ────────────────────────────────────────────────────────────

#ifdef _WIN32
static HANDLE g_hOut        = INVALID_HANDLE_VALUE;
static HANDLE g_hIn         = INVALID_HANDLE_VALUE;
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
    frame_buf_.reserve(static_cast<std::size_t>(term_cols_ * term_rows_) * 24);

    frame_buf_  = k_alt_enter;
    frame_buf_ += k_bg_dead;
    frame_buf_ += "\033[2J";
    frame_buf_ += k_hide_cur;
    frame_buf_ += "\033[H";
    flush_frame();
}

Renderer::~Renderer() {
    frame_buf_  = k_show_cur;
    frame_buf_ += k_reset;
    frame_buf_ += k_alt_exit;
    flush_frame();
    restore_platform();
}

// ── Public interface ──────────────────────────────────────────────────────────

std::pair<std::size_t, std::size_t> Renderer::usable_size() const noexcept {
    const int usable_rows = std::max(term_rows_ - 3, 2);
    return {static_cast<std::size_t>(term_cols_),
            static_cast<std::size_t>(usable_rows * 2)};
}

void Renderer::draw(const Grid& grid, bool paused, double fps) {
    const std::size_t gw   = grid.width();
    const std::size_t gh   = grid.height();
    const auto* const data = grid.data();
    const int display_rows = (static_cast<int>(gh) + 1) / 2;

    // ── Detect grid-size change → invalidate differential state ───────────────
    const bool size_changed = (prev_gw_ != gw || prev_gh_ != gh);
    if (size_changed) {
        // Fill with 0xFF (impossible state) so every cell is considered dirty.
        prev_display_.assign(static_cast<std::size_t>(display_rows) * gw,
                             std::uint8_t{0xFF});
        prev_gw_ = gw;
        prev_gh_ = gh;
        // Clear the screen once so stale characters don't show through.
        frame_buf_  = k_hide_cur;
        frame_buf_ += "\033[2J";
        flush_frame();
    }

    frame_buf_.clear();
    frame_buf_ += k_hide_cur;

    // ── Grid rows — differential ───────────────────────────────────────────────
    for (int ty = 0; ty < display_rows; ++ty) {
        const std::size_t y0   = static_cast<std::size_t>(ty * 2);
        const std::size_t y1   = y0 + 1;
        const std::size_t base = static_cast<std::size_t>(ty) * gw;

        // Fast dirty check: scan prev_display_ and break on first mismatch.
        // This is a plain byte-comparison loop — the compiler vectorises it.
        bool dirty = false;
        for (std::size_t x = 0; x < gw && !dirty; ++x) {
            const bool top = data[y0 * gw + x] != 0;
            const bool bot = (y1 < gh) && (data[y1 * gw + x] != 0);
            const auto cur = static_cast<std::uint8_t>(
                (static_cast<int>(top) << 1) | static_cast<int>(bot));
            dirty = (cur != prev_display_[base + x]);
        }

        if (!dirty) continue;

        // Move cursor to the start of this terminal row (1-indexed).
        buf_move_to(0, ty);

        // Render with colour-state machine: only emit fg/bg codes on change.
        int cur_fg = -1;
        int cur_bg = -1;

        for (std::size_t x = 0; x < gw; ++x) {
            const bool top = data[y0 * gw + x] != 0;
            const bool bot = (y1 < gh) && (data[y1 * gw + x] != 0);

            const int idx = (static_cast<int>(top) << 1) | static_cast<int>(bot);
            prev_display_[base + x] = static_cast<std::uint8_t>(idx);

            // For the full-alive case (idx==3) we paint with bg=alive.
            // For the full-dead case (idx==0) we paint with bg=dead.
            // For half-blocks fg is always alive, bg is always dead.
            const int need_fg = (idx == 0) ? 0 : 1;  // dead-only → dead fg
            const int need_bg = (idx == 3) ? 1 : 0;  // full-alive → alive bg

            if (need_fg != cur_fg) { buf_set_fg(need_fg == 1); cur_fg = need_fg; }
            if (need_bg != cur_bg) { buf_set_bg(need_bg == 1); cur_bg = need_bg; }

            frame_buf_ += k_glyphs[static_cast<std::size_t>(idx)].glyph;
        }

        buf_reset_colors();
    }

    // ── Status bar (always redrawn — it changes every frame) ──────────────────
    buf_move_to(0, display_rows);
    buf_reset_colors();

    // Separator line
    frame_buf_ += "\033[2m";
    frame_buf_.append(static_cast<std::size_t>(term_cols_), '-');
    frame_buf_ += k_reset;
    frame_buf_ += "\r\n";

    // Stats line
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

    // Key hints
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
        if (ch == 0 || ch == 224) { _getch(); return '\0'; }
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

void Renderer::buf_move_home() { frame_buf_ += "\033[H"; }

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

void Renderer::buf_reset_colors() { frame_buf_ += k_reset; }
void Renderer::buf_hide_cursor()  { frame_buf_ += k_hide_cur; }
void Renderer::buf_show_cursor()  { frame_buf_ += k_show_cur; }

void Renderer::flush_frame() {
    if (frame_buf_.empty()) return;
    std::fwrite(frame_buf_.data(), 1, frame_buf_.size(), stdout);
    std::fflush(stdout);
}

// ── Private: platform init / restore ─────────────────────────────────────────

void Renderer::init_platform() {
#ifdef _WIN32
    g_hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    g_hIn  = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(g_hOut, &g_origOutMode);
    GetConsoleMode(g_hIn,  &g_origInMode);
    SetConsoleMode(g_hOut,
        g_origOutMode
        | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        | DISABLE_NEWLINE_AUTO_RETURN);
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleMode(g_hIn,
        g_origInMode
        & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT));
#else
    tcgetattr(STDIN_FILENO, &g_orig_termios);
    termios raw = g_orig_termios;
    raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO));
    raw.c_cc[VMIN]  = 0;
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
    term_cols_ = 80;
    term_rows_ = 24;
}

}  // namespace conway
