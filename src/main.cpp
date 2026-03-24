#include "conway/grid.hpp"
#include "conway/patterns.hpp"
#include "conway/renderer.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;

// ── Configuration ─────────────────────────────────────────────────────────────

struct Config {
    double      density      = 0.30;   ///< Initial live-cell probability.
    int         target_fps   = 20;     ///< Simulation frames per second.
    bool        toroidal     = true;   ///< Torus topology (wrap-around edges).
    std::string init_pattern;          ///< Empty = randomise.
};

// ── CLI ───────────────────────────────────────────────────────────────────────

static void print_usage(const char* prog) {
    std::cout
        << "\nConway's Game of Life — high-performance C++ implementation\n"
        << "\nUsage:\n"
        << "  " << prog << " [options]\n"
        << "\nOptions:\n"
        << "  -f <fps>       Target frames per second   (default: 20, max: 120)\n"
        << "  -d <density>   Random fill density 0-1    (default: 0.30)\n"
        << "  -p <pattern>   Start with a named pattern (default: random)\n"
        << "  --flat         Flat topology (dead border instead of wrap-around)\n"
        << "  -h, --help     Show this help text\n"
        << "\nBuilt-in patterns:\n";

    for (const auto& info : conway::patterns::all_patterns()) {
        std::cout << "  " << std::left;
        std::cout.width(18);
        std::cout << info.name << "  " << info.description << "\n";
    }

    std::cout
        << "\nControls (interactive):\n"
        << "  Space          Pause / resume\n"
        << "  s              Single-step (while paused)\n"
        << "  r              Randomise with current density\n"
        << "  c              Clear grid\n"
        << "  t              Toggle toroidal / flat topology\n"
        << "  +  or  =       Increase speed (+5 fps)\n"
        << "  -              Decrease speed (-5 fps)\n"
        << "  q  or  Esc     Quit\n"
        << "\n";
}

static Config parse_args(int argc, char* argv[]) {
    Config cfg;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            std::exit(0);
        } else if (arg == "--flat") {
            cfg.toroidal = false;
        } else if ((arg == "-f" || arg == "--fps") && i + 1 < argc) {
            const char* s = argv[++i];
            std::from_chars(s, s + std::strlen(s), cfg.target_fps);
            cfg.target_fps = std::clamp(cfg.target_fps, 1, 120);
        } else if ((arg == "-d" || arg == "--density") && i + 1 < argc) {
            cfg.density = std::atof(argv[++i]);
            cfg.density = std::clamp(cfg.density, 0.0, 1.0);
        } else if ((arg == "-p" || arg == "--pattern") && i + 1 < argc) {
            cfg.init_pattern = argv[++i];
        } else {
            std::cerr << "Unknown argument: " << arg
                      << "  (run with -h for help)\n";
            std::exit(1);
        }
    }
    return cfg;
}

// ── Initial state ─────────────────────────────────────────────────────────────

static void apply_initial_state(conway::Grid& grid, const Config& cfg) {
    if (cfg.init_pattern.empty()) {
        grid.randomize(cfg.density);
        return;
    }

    const auto pats = conway::patterns::all_patterns();
    const auto it   = std::find_if(pats.begin(), pats.end(), [&](const auto& p) {
        return p.name == cfg.init_pattern;
    });

    if (it == pats.end()) {
        std::cerr << "Unknown pattern: '" << cfg.init_pattern
                  << "'.  Run with -h to list available patterns.\n";
        std::exit(1);
    }

    grid.clear();
    conway::patterns::place(grid, it->cells(),
                            grid.width()  / 2,
                            grid.height() / 2);
}

// ── Main loop ─────────────────────────────────────────────────────────────────

int main(int argc, char* argv[]) {
    const Config cfg = parse_args(argc, argv);

    // Renderer must be constructed first so it can report the terminal size.
    conway::Renderer renderer;
    const auto [grid_w, grid_h] = renderer.usable_size();

    conway::Grid grid{grid_w, grid_h, cfg.toroidal};
    apply_initial_state(grid, cfg);

    using Clock    = std::chrono::steady_clock;
    using Duration = std::chrono::duration<double>;

    bool   running    = true;
    bool   paused     = false;
    int    target_fps = cfg.target_fps;
    double fps_smooth = static_cast<double>(target_fps);
    auto   last_frame = Clock::now();

    while (running) {
        const auto frame_start = Clock::now();

        // ── Input ─────────────────────────────────────────────────────────────
        const char key = renderer.poll_key();
        switch (key) {
            case 'q':
            case '\x1b':  // Escape
                running = false;
                continue;

            case ' ':
                paused = !paused;
                break;

            case 's':
                // Single-step is most useful when paused, but also works live.
                grid.step();
                break;

            case 'r':
                grid.randomize(cfg.density);
                paused = false;
                break;

            case 'c':
                grid.clear();
                paused = true;
                break;

            case 't': {
                // Rebuild with toggled topology; preserve live-cell positions.
                const bool new_torus = !grid.toroidal();
                conway::Grid next{grid.width(), grid.height(), new_torus};
                const auto* data = grid.data();
                for (std::size_t y = 0; y < grid.height(); ++y)
                    for (std::size_t x = 0; x < grid.width(); ++x)
                        if (data[y * grid.width() + x])
                            next.set(x, y, true);
                grid = std::move(next);
                break;
            }

            case '+': case '=':
                target_fps = std::min(target_fps + 5, 120);
                break;

            case '-': case '_':
                target_fps = std::max(target_fps - 5, 1);
                break;

            default:
                break;
        }

        // ── Simulate ──────────────────────────────────────────────────────────
        if (!paused) grid.step();

        // ── Render ────────────────────────────────────────────────────────────
        renderer.draw(grid, paused, fps_smooth);

        // ── Frame timing ──────────────────────────────────────────────────────
        const Duration target_period{1.0 / target_fps};
        const Duration elapsed{Clock::now() - frame_start};
        if (elapsed < target_period)
            std::this_thread::sleep_for(target_period - elapsed);

        // Exponential moving average for displayed FPS.
        const double frame_sec = Duration{Clock::now() - last_frame}.count();
        fps_smooth = fps_smooth * 0.85 + (1.0 / frame_sec) * 0.15;
        last_frame = Clock::now();
    }

    return 0;
}
