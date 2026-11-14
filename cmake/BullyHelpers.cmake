# Shared CMake helpers for the Bully Studio monorepo.
#
# Conventions used by every target in this project:
#   * Public headers live in <module>/include/bully/<module>/
#   * Sources live in <module>/src/
#   * Target names are snake_case; consumers link the namespaced alias
#     bully::<module>.
include_guard(GLOBAL)

# ------------------------------------------------------------------ warnings --
function(bully_setup_warnings)
    if(MSVC)
        add_compile_options(/W4 /permissive-)
    else()
        # C warnings every language understands.
        add_compile_options(
            -Wall -Wextra -Wpedantic
            -Wunused
            -Wformat=2
            -Wimplicit-fallthrough)
        # C++-only warnings: applying these to C sources (the vendored SQLite
        # amalgamation) makes the compiler emit "valid for C++ but not for C"
        # notes, which is pure noise. Generator expressions scope them by language.
        add_compile_options(
            $<$<COMPILE_LANGUAGE:CXX>:-Wshadow>
            $<$<COMPILE_LANGUAGE:CXX>:-Wnon-virtual-dtor>
            $<$<COMPILE_LANGUAGE:CXX>:-Wold-style-cast>
            $<$<COMPILE_LANGUAGE:CXX>:-Wcast-align>
            $<$<COMPILE_LANGUAGE:CXX>:-Woverloaded-virtual>
            $<$<COMPILE_LANGUAGE:CXX>:-Wconversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wsign-conversion>
            $<$<COMPILE_LANGUAGE:CXX>:-Wdouble-promotion>)
        if(BULLY_WARNINGS_AS_ERRORS)
            add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-Werror>)
        endif()
        # libstdc++ container assertions turn silent out-of-bounds vector access
        # into an immediate abort. Some distributions (Arch) enable them in their
        # own builds, which is how an out-of-bounds read in the texture mip
        # generator was caught on one machine and not another. Turning them on
        # for Debug and sanitizer builds makes the diagnosis reproducible.
        if(BULLY_STL_ASSERTIONS AND NOT MSVC)
            add_compile_definitions(_GLIBCXX_ASSERTIONS)
        endif()
        if(BULLY_ENABLE_SANITIZERS)
            add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
            add_link_options(-fsanitize=address,undefined)
        endif()
    endif()
endfunction()

# --------------------------------------------------------------- libraries ----
# Declares a static library from <module>/src, exposing <module>/include.
function(bully_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS;PUBLIC_INCLUDE" ${ARGN})

    file(GLOB_RECURSE _sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    if(ARG_SOURCES)
        set(_sources ${ARG_SOURCES})
    endif()

    add_library(${name} STATIC ${_sources})
    add_library(bully::${name} ALIAS ${name})

    set(_include_dirs "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(ARG_PUBLIC_INCLUDE)
        list(APPEND _include_dirs ${ARG_PUBLIC_INCLUDE})
    endif()

    target_include_directories(${name}
        PUBLIC $<BUILD_INTERFACE:${_include_dirs}>
               $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

    if(ARG_PUBLIC_DEPS)
        target_link_libraries(${name} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_PRIVATE_DEPS)
        target_link_libraries(${name} PRIVATE ${ARG_PRIVATE_DEPS})
    endif()

    target_compile_features(${name} PUBLIC cxx_std_20)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        OUTPUT_NAME ${name})
endfunction()

# Declares an executable. Sources default to <app>/src/**/*.cpp.
function(bully_add_executable name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;OUTPUT_NAME" ${ARGN})
    if(NOT ARG_SOURCES)
        file(GLOB_RECURSE ARG_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
    endif()
    add_executable(${name} ${ARG_SOURCES})
    if(ARG_DEPS)
        target_link_libraries(${name} PRIVATE ${ARG_DEPS})
    endif()
    if(ARG_OUTPUT_NAME)
        set_target_properties(${name} PROPERTIES OUTPUT_NAME ${ARG_OUTPUT_NAME})
    endif()
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

# ---------------------------------------------------------------- reporting ---
function(bully_print_configuration)
    message(STATUS "")
    message(STATUS "Bully Studio configuration")
    message(STATUS "  version           : ${PROJECT_VERSION}")
    message(STATUS "  build type        : ${CMAKE_BUILD_TYPE}")
    message(STATUS "  compiler          : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    message(STATUS "  Qt6 studio shell  : ${BULLY_BUILD_STUDIO}")
    message(STATUS "  game runtime      : ${BULLY_BUILD_RUNTIME}")
    message(STATUS "  asset CLI         : ${BULLY_BUILD_CLI}")
    message(STATUS "  tests             : ${BULLY_BUILD_TESTS}")
    message(STATUS "  install prefix    : ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "")
endfunction()
