// Bully Studio - game host.
//
// Two modes, one code path:
//
//   bully_game <project>                 windowed (SDL3 + OpenGL) when available
//   bully_game <project> --headless      no window, fixed number of frames
//   bully_game <package.bpackage>        run straight from a shipped package
//
// The headless mode is not a stub: it boots the same runtime session, runs the
// same fixed-step loop and prints the same statistics, which is how a gameplay
// bug is reproduced in CI without a display. Rendering is the only thing it
// skips.
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "bully/core/log.h"
#include "bully/runtime/session.h"

#ifdef BULLY_HAVE_SDL3
#include <SDL3/SDL.h>
#endif

namespace {

struct Options {
    std::filesystem::path project;
    std::filesystem::path package;
    std::string scene;
    bool headless = false;
    bool diagnose = false;
    std::uint32_t frames = 0;      // 0 = run until the window closes
    double frame_delta = 1.0 / 60.0;
    bool verbose = false;
};

void print_usage()
{
    std::printf(
        "bully_game - Bully Studio runtime host\n"
        "\n"
        "Usage: bully_game <project-dir | package.bpackage> [options]\n"
        "\n"
        "Options:\n"
        "  --headless          run without a window (CI, bug reproduction)\n"
        "  --frames N          stop after N frames (headless default: 600)\n"
        "  --scene <id>        entry scene asset id or logical path\n"
        "  --delta <seconds>   fixed frame delta for headless runs (default 1/60)\n"
        "  --verbose           debug logging\n"
        "  --diagnose          report the video driver and display state, then exit\n"
        "  --help              this text\n");
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            print_usage();
            return false;
        }
        if (argument == "--headless") {
            options.headless = true;
        } else if (argument == "--diagnose") {
            options.diagnose = true;
        } else if (argument == "--verbose") {
            options.verbose = true;
        } else if (argument == "--frames" && i + 1 < argc) {
            options.frames = static_cast<std::uint32_t>(std::stoul(argv[++i]));
        } else if (argument == "--delta" && i + 1 < argc) {
            options.frame_delta = std::stod(argv[++i]);
        } else if (argument == "--scene" && i + 1 < argc) {
            options.scene = argv[++i];
        } else if (argument.rfind("--", 0) == 0) {
            std::fprintf(stderr, "error: unknown option '%s'\n", argument.c_str());
            return false;
        } else if (options.project.empty() && options.package.empty()) {
            const std::filesystem::path path(argument);
            if (path.extension() == ".bpackage") {
                options.package = path;
            } else {
                // `bully_game mygame/project.bullyproj` is a natural thing to
                // type (and what `make run` used to do); treat a manifest path
                // as its containing project directory.
                options.project = path.filename() == "project.bullyproj"
                                      ? path.parent_path()
                                      : path;
            }
        } else {
            std::fprintf(stderr, "error: unexpected argument '%s'\n", argument.c_str());
            return false;
        }
    }
    if (options.diagnose) return true;
    if (options.project.empty() && options.package.empty()) {
        std::fprintf(stderr, "error: no project or package given\n");
        print_usage();
        return false;
    }
    if (!options.package.empty()) {
        std::error_code code;
        if (!std::filesystem::exists(options.package, code)) {
            std::fprintf(stderr, "error: package not found: %s\n",
                         options.package.string().c_str());
            return false;
        }
    } else {
        std::error_code code;
        if (!std::filesystem::is_directory(options.project, code)) {
            std::fprintf(stderr,
                         "error: not a project directory: %s\n"
                         "       pass the project root (the folder holding "
                         "project.bullyproj), e.g.\n"
                         "         bully_game mygame --headless\n",
                         options.project.string().c_str());
            return false;
        }
        // `new` creates an (empty, .gitkeep-only) built/ directory, so the
        // directory existing proves nothing. A project with no compiled assets
        // would boot, mount nothing and report zeros, which reads as success.
        std::size_t compiled = 0;
        const auto built = options.project / "built";
        if (std::filesystem::is_directory(built, code)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(built)) {
                const auto name = entry.path().filename().string();
                if (name == ".gitkeep") continue;
                if (entry.is_regular_file()) ++compiled;
            }
        }
        if (compiled == 0) {
            std::fprintf(stderr,
                         "error: no compiled assets in %s\n"
                         "       build the project first:\n"
                         "         bully_asset_cli build --project %s\n",
                         built.string().c_str(),
                         options.project.string().c_str());
            return false;
        }
    }
    if (!options.headless && options.frames == 0) options.frames = 0;
    if (options.headless && options.frames == 0) options.frames = 600;
    return true;
}

void print_statistics(const bully::runtime::GameSession& session)
{
    const auto stats = session.statistics();
    std::printf("frames:            %llu\n",
                static_cast<unsigned long long>(stats.frames));
    std::printf("fixed steps:       %llu\n", static_cast<unsigned long long>(stats.steps));
    std::printf("simulated seconds: %.3f\n", stats.simulated_seconds);
    std::printf("missions active:   %u\n", stats.missions_active);
    std::printf("missions complete: %u\n", stats.missions_completed);
    std::printf("assets decoded:    %zu\n", stats.assets_decoded);
    const auto assets = session.assets().statistics();
    std::printf("mounted assets:    %zu (%zu mounts)\n", assets.entries, assets.mounts);
}

int run_headless(const bully::runtime::GameSession& session, const Options& options)
{
    auto& mutable_session = const_cast<bully::runtime::GameSession&>(session);
    std::printf("Running %u headless frame(s) at %.4f s\n", options.frames, options.frame_delta);
    const auto stats = mutable_session.simulate(options.frames, options.frame_delta);
    std::printf("\nHeadless run finished\n");
    print_statistics(session);
    std::printf("story flags:       %zu\n", session.story().flags().values.size());
    return stats.steps > 0 || options.frames == 0 ? 0 : 1;
}

#ifdef BULLY_HAVE_SDL3
/// True for drivers that accept every call and show nothing.
///
/// This is the "the window never opens" bug: with SDL_VIDEO_DRIVER=dummy (or the
/// offscreen driver) SDL_Init succeeds, SDL_CreateWindow succeeds and the loop
/// runs - there is simply no window to look at. Printing "Window open" in that
/// state is a lie, so the host refuses and explains instead.
bool is_headless_video_driver()
{
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == nullptr) return false;
    return SDL_strcmp(driver, "dummy") == 0 || SDL_strcmp(driver, "offscreen") == 0 ||
           SDL_strcmp(driver, "evdev") == 0;
}

int run_windowed(bully::runtime::GameSession& session, const Options& options)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
        std::fprintf(stderr,
                     "       No video device: this session has no display "
                     "(SSH without X forwarding, a bare TTY, or a container).\n"
                     "       Run with --headless, or start a desktop session / use "
                     "ssh -X.\n");
        return 1;
    }
    // Check the driver *before* creating a window: SDL happily creates one on
    // the dummy driver and never shows it.
    if (is_headless_video_driver()) {
        std::fprintf(stderr,
                     "error: SDL selected the '%s' video driver, which cannot open a "
                     "real window\n"
                     "       (usually because no display server is reachable).\n"
                     "       Run with --headless, or start a desktop session / use "
                     "ssh -X.\n",
                     SDL_GetCurrentVideoDriver());
        SDL_Quit();
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Bully Studio", 1280, 720, 0);
    if (window == nullptr) {
        std::fprintf(stderr, "error: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::fprintf(stderr, "error: SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::printf("Window open (%s driver, %dx%d) - close it (or press Escape) to exit\n",
                SDL_GetCurrentVideoDriver() != nullptr ? SDL_GetCurrentVideoDriver() : "?",
                1280, 720);
    std::printf("  W/A/S/D move, E interact, Space advance dialogue\n");
    std::fflush(stdout);

    std::uint64_t previous = SDL_GetTicks();
    bool running = true;
    std::uint32_t frames = 0;
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) running = false;
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) running = false;
        }
        // Physical input -> logical snapshot, which is all the session sees.
        std::map<std::string, float> raw;
        const bool* keys = SDL_GetKeyboardState(nullptr);
        if (keys != nullptr) {
            raw["key:w"] = keys[SDL_SCANCODE_W] ? 1.0f : 0.0f;
            raw["key:a"] = keys[SDL_SCANCODE_A] ? 1.0f : 0.0f;
            raw["key:s"] = keys[SDL_SCANCODE_S] ? 1.0f : 0.0f;
            raw["key:d"] = keys[SDL_SCANCODE_D] ? 1.0f : 0.0f;
            raw["key:e"] = keys[SDL_SCANCODE_E] ? 1.0f : 0.0f;
            raw["key:space"] = keys[SDL_SCANCODE_SPACE] ? 1.0f : 0.0f;
            raw["key:escape"] = keys[SDL_SCANCODE_ESCAPE] ? 1.0f : 0.0f;
        }
        session.set_input_snapshot(raw);

        const std::uint64_t now = SDL_GetTicks();
        const double delta = static_cast<double>(now - previous) / 1000.0;
        previous = now;
        const auto plan = session.begin_frame(delta);
        for (std::uint32_t step = 0; step < plan.steps; ++step) session.step();
        session.end_frame();
        ++frames;
        if (options.frames != 0 && frames >= options.frames) running = false;

        // Clear to the scene's ambient colour and show a couple of live values.
        // A plain blank window gives no feedback that the loop is running; this
        // is still not a 3D renderer, and docs/runtime.md says so.
        float r = 0.09f;
        float g = 0.10f;
        float b = 0.13f;
        if (const auto* scene = session.scene()) {
            r = scene->environment.ambient_color.x;
            g = scene->environment.ambient_color.y;
            b = scene->environment.ambient_color.z;
        }
        SDL_SetRenderDrawColor(renderer, static_cast<Uint8>(r * 255.0f),
                               static_cast<Uint8>(g * 255.0f),
                               static_cast<Uint8>(b * 255.0f), 255);
        SDL_RenderClear(renderer);
        // A moving marker proves the fixed-step loop is alive.
        const float phase = static_cast<float>(frames % 240) / 240.0f;
        SDL_FRect marker{};
        marker.w = 64.0f;
        marker.h = 64.0f;
        marker.x = 40.0f + phase * 1000.0f;
        marker.y = 300.0f;
        SDL_SetRenderDrawColor(renderer, 240, 200, 90, 255);
        SDL_RenderFillRect(renderer, &marker);
        SDL_RenderPresent(renderer);
    }

    print_statistics(session);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif

}  // namespace

#ifdef BULLY_HAVE_SDL3
/// Explains whether a window can actually be opened here. Separate from
/// run_windowed so it works without a project, and so a bug report can include
/// it verbatim.
int run_diagnose()
{
    std::printf("bully_game video diagnostics\n");
#ifdef BULLY_HAVE_SDL3
    std::printf("  build          : SDL3 present\n");
#else
    std::printf("  build          : no SDL3 (headless only)\n");
#endif
    const char* requested = std::getenv("SDL_VIDEO_DRIVER");
    std::printf("  SDL_VIDEO_DRIVER: %s\n", requested != nullptr ? requested : "(unset)");
    const char* display = std::getenv("DISPLAY");
    const char* wayland = std::getenv("WAYLAND_DISPLAY");
    const char* session = std::getenv("XDG_SESSION_TYPE");
    std::printf("  DISPLAY        : %s\n", display != nullptr ? display : "(unset)");
    std::printf("  WAYLAND_DISPLAY: %s\n", wayland != nullptr ? wayland : "(unset)");
    std::printf("  XDG_SESSION_TYPE: %s\n", session != nullptr ? session : "(unset)");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("  SDL_Init       : FAILED (%s)\n", SDL_GetError());
        std::printf("\nNo video device is reachable. Use --headless, or connect a display\n"
                    "(desktop session, ssh -X, or set DISPLAY).\n");
        return 1;
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    std::printf("  SDL driver     : %s\n", driver != nullptr ? driver : "(null)");
    int displays = 0;
    SDL_free(SDL_GetDisplays(&displays));
    std::printf("  displays       : %d\n", displays);
    const int num_drivers = SDL_GetNumVideoDrivers();
    std::printf("  drivers built  :");
    for (int i = 0; i < num_drivers; ++i) std::printf(" %s", SDL_GetVideoDriver(i));
    std::printf("\n");
    const bool headless_driver = is_headless_video_driver();
    std::printf("  window possible: %s\n", headless_driver ? "NO" : "yes");
    SDL_Quit();
    if (headless_driver) {
        std::printf("\nSDL selected a driver that cannot show a window. Start a desktop\n"
                    "session, use ssh -X, or run with --headless.\n");
        return 1;
    }
    return 0;
}
#endif

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        // --help is a request, not a usage error.
        for (int i = 1; i < argc; ++i) {
            const std::string argument = argv[i];
            if (argument == "--help" || argument == "-h") return 0;
        }
        return 2;
    }
    // The game host owns its sinks: warnings/errors to stderr, everything else
    // silent unless --verbose, so a playing session is not drowned in log noise.
    auto& logger = bully::core::Logger::instance();
    logger.set_min_level(options.verbose ? bully::core::LogLevel::Debug
                                         : bully::core::LogLevel::Warn);
    (void)logger.add_sink(bully::core::Logger::stderr_sink());

#ifdef BULLY_HAVE_SDL3
    if (options.diagnose) return run_diagnose();
#else
    if (options.diagnose) {
        // Same report shape as the SDL3 path, so scripts and users see one format.
        std::printf("bully_game video diagnostics\n");
        std::printf("  build          : no SDL3 (headless only)\n");
        std::printf("  displays       : 0\n");
        std::printf("  window possible: NO\n");
        std::printf("\nThis binary cannot open a window. Run with --headless.\n"
                    "To build the windowed host, install SDL3 and rebuild:\n"
                    "  sudo pacman -S sdl3 mesa   # then re-run cmake --preset debug\n");
        return 1;
    }
#endif

    bully::runtime::SessionConfig config;
    if (!options.package.empty()) {
        config.source = bully::runtime::SessionSource::Package;
        config.package_path = options.package;
        config.project_root = options.package.parent_path();
    } else {
        config.source = bully::runtime::SessionSource::BuildDirectory;
        config.project_root = options.project;
    }
    config.entry_scene = options.scene;

    bully::runtime::GameSession session;
    auto boot = session.boot(config);
    if (!boot) {
        std::fprintf(stderr, "error: cannot boot\n%s", boot.diagnostics().format().c_str());
        return 1;
    }
    // Boot warnings are worth showing: they usually mean the project references
    // something that was not built.
    if (!boot.diagnostics().empty()) std::printf("%s", boot.diagnostics().format().c_str());

    if (options.headless) return run_headless(session, options);

#ifdef BULLY_HAVE_SDL3
    return run_windowed(session, options);
#else
    std::fprintf(stderr,
                 "This build has no SDL3 support; only --headless is available.\n"
                 "Install SDL3 (pacman -S sdl3) and rebuild to run windowed.\n");
    return 1;
#endif
}
