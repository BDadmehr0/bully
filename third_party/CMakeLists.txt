# Vendored third-party code.
#
# SQLite is public domain (see LICENSES/sqlite.txt). The system library is used
# when the CMake package is available (Arch Linux: `pacman -S sqlite`), otherwise
# the in-repo amalgamation is compiled, so the build never fails for a missing
# optional dependency.
option(BULLY_USE_SYSTEM_SQLITE "Prefer the system SQLite over the bundled amalgamation" ON)

if(BULLY_USE_SYSTEM_SQLITE)
    find_package(SQLite3 QUIET)
endif()

if(SQLite3_FOUND)
    add_library(bully_sqlite3 INTERFACE)
    target_link_libraries(bully_sqlite3 INTERFACE SQLite::SQLite3)
    message(STATUS "  sqlite3           : system (${SQLite3_VERSION})")
else()
    add_library(bully_sqlite3 STATIC sqlite/sqlite3.c)
    target_include_directories(bully_sqlite3 PUBLIC sqlite)
    target_compile_definitions(bully_sqlite3 PUBLIC
        SQLITE_THREADSAFE=1
        SQLITE_ENABLE_JSON1=1
        SQLITE_DQS=0
        SQLITE_DEFAULT_MEMSTATUS=0
        SQLITE_OMIT_DEPRECATED=1)
    # Third-party C code: relax project warnings.
    target_compile_options(bully_sqlite3 PRIVATE -w)
    set_target_properties(bully_sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON EXPORT_NAME sqlite3)
    message(STATUS "  sqlite3           : bundled amalgamation")
endif()

# Catch2 v3 is vendored (BSL-1.0, see LICENSES/catch2.txt) so `ctest` works on a
# machine that has not installed the Arch `catch2` package. When the package is
# present it is preferred and the vendored copy is skipped entirely.
if(BULLY_BUILD_TESTS)
    find_package(Catch2 3 QUIET)
    if(Catch2_FOUND)
        message(STATUS "  catch2            : system (${Catch2_VERSION})")
    else()
        set(CATCH_INSTALL_DOCS OFF CACHE BOOL "" FORCE)
        set(CATCH_INSTALL_EXTRAS OFF CACHE BOOL "" FORCE)
        set(CATCH_BUILD_TESTING OFF CACHE BOOL "" FORCE)
        add_subdirectory(catch2 ${CMAKE_BINARY_DIR}/third_party/catch2 EXCLUDE_FROM_ALL)
        message(STATUS "  catch2            : bundled ${CATCH2_VERSION}")
    endif()
endif()
