# Game host: a thin SDL3 shell around the headless runtime session.
#
# SDL3 is optional at configure time (a headless container has no graphics
# stack). Without it the executable still builds and `--headless` works, which
# keeps the same loop under test everywhere.
add_executable(bully_game src/main.cpp)
target_link_libraries(bully_game PRIVATE bully_runtime bully_formats bully_core)

if(TARGET SDL3::SDL3)
    target_link_libraries(bully_game PRIVATE SDL3::SDL3 OpenGL::GL)
    # We provide a plain ANSI main(); SDL_MAIN_HANDLED stops SDL from
    # redefining it (required on Windows, harmless on Linux) and means the
    # SDL3main shim library is not needed.
    target_compile_definitions(bully_game PRIVATE BULLY_HAVE_SDL3=1 SDL_MAIN_HANDLED)
    message(STATUS "  bully_game        : windowed + headless (SDL3)")
else()
    message(STATUS "  bully_game        : headless only (SDL3 not found)")
endif()
