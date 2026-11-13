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
#include <cstdio>
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
                options.project = path;
            }
        } else {
            std::fprintf(stderr, "error: unexpected argument '%s'\n", argument.c_str());
            return false;
        }
    }
    if (options.project.empty() && options.package.empty()) {
        std::fprintf(stderr, "error: no project or package given\n");
        print_usage();
        return false;
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
int run_windowed(bully::runtime::GameSession& session, const Options& options)
{
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "error: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("Bully Studio", 1280, 720, 0);
    if (window == nullptr) {
        std::fprintf(stderr, "error: SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    std::printf("Window open - close it (or press Escape) to exit\n");

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
    }

    print_statistics(session);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
#endif

}  // namespace

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) return 2;
    // The game host owns its sinks: warnings/errors to stderr, everything else
    // silent unless --verbose, so a playing session is not drowned in log noise.
    auto& logger = bully::core::Logger::instance();
    logger.set_min_level(options.verbose ? bully::core::LogLevel::Debug
                                         : bully::core::LogLevel::Warn);
    (void)logger.add_sink(bully::core::Logger::stderr_sink());

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
