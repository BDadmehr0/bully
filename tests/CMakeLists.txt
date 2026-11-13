# Test suite. Catch2 v3 is vendored under third_party/catch2 (BSL-1.0) so the
# suite builds on a machine without the Arch package installed.
#
# Layout:
#   tests/support  - shared helpers (temporary project fixtures)
#   tests/core     - core + formats unit tests
#   tests/pipeline - pipeline / QA / plugin integration tests
#   tests/qa       - end-to-end headless run over a scaffolded project
# Catch2::Catch2WithMain is provided by third_party/CMakeLists.txt (system
# package when available, vendored copy otherwise).

add_library(bully_test_support STATIC support/test_support.cpp)
target_include_directories(bully_test_support PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_CURRENT_SOURCE_DIR}/pipeline)
target_link_libraries(bully_test_support PUBLIC bully_core bully_formats Catch2::Catch2WithMain)

# --- unit tests: core + formats ---------------------------------------------
add_executable(test_core
    core/test_error.cpp
    core/test_json.cpp
    core/test_fs.cpp
    core/test_strings.cpp
    core/test_uuid.cpp)
target_link_libraries(test_core PRIVATE bully_test_support bully_formats)
add_test(NAME core COMMAND test_core)

add_executable(test_formats
    formats/test_binary.cpp
    formats/test_model.cpp
    formats/test_texture.cpp
    formats/test_animation.cpp
    formats/test_dialogue.cpp
    formats/test_scene.cpp
    formats/test_mission.cpp
    formats/test_audio.cpp
    formats/test_package.cpp)
target_link_libraries(test_formats PRIVATE bully_test_support bully_formats)
add_test(NAME formats COMMAND test_formats)

# --- integration tests: pipeline, QA, plugins --------------------------------
add_executable(test_pipeline
    pipeline/test_obj_import.cpp
    pipeline/test_image_import.cpp
    pipeline/test_audio_import.cpp
    pipeline/test_data_import.cpp
    pipeline/test_pipeline.cpp
    pipeline/test_packaging.cpp
    pipeline/test_qa.cpp
    pipeline/test_templates.cpp
    pipeline/test_presets.cpp)
target_link_libraries(test_pipeline PRIVATE bully_test_support bully_pipeline
    bully_project bully_templates bully_plugins)
target_include_directories(test_pipeline PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/support
    ${CMAKE_SOURCE_DIR}/asset_pipeline/src)
add_dependencies(test_pipeline bully_sample_plugin)
add_test(NAME pipeline COMMAND test_pipeline)

add_executable(test_plugins pipeline/test_plugins.cpp)
target_link_libraries(test_plugins PRIVATE bully_test_support bully_plugins bully_pipeline)
add_dependencies(test_plugins bully_sample_plugin)
target_compile_definitions(test_plugins PRIVATE
    BULLY_SAMPLE_PLUGIN_BUILD_DIR="${CMAKE_BINARY_DIR}/plugins/samples"
    BULLY_SAMPLE_PLUGIN_SOURCE_DIR="${CMAKE_SOURCE_DIR}/plugins/samples/hello_importer")
add_test(NAME plugins COMMAND test_plugins)

# --- exporters: compiled asset -> external format round trips ----------------
add_executable(test_exporters exporters/test_exporters.cpp)
target_link_libraries(test_exporters PRIVATE bully_test_support bully_exporters
    bully_pipeline bully_formats)
add_test(NAME exporters COMMAND test_exporters)

# --- runtime subsystems (assets, story, clock, input) ------------------------
add_executable(test_runtime
    runtime/test_asset_database.cpp
    runtime/test_story.cpp
    runtime/test_clock.cpp
    runtime/test_session.cpp)
target_link_libraries(test_runtime PRIVATE bully_test_support bully_runtime bully_formats)
add_test(NAME runtime COMMAND test_runtime)

# --- headless QA dashboard over a scaffolded project -------------------------
add_executable(test_headless qa/test_headless.cpp)
target_link_libraries(test_headless PRIVATE bully_test_support bully_pipeline
    bully_project bully_templates)
target_compile_definitions(test_headless PRIVATE
    BULLY_CLI_PATH="$<TARGET_FILE:bully_asset_cli>")
add_dependencies(test_headless bully_asset_cli)
add_test(NAME headless COMMAND test_headless)

# Third-party headers and test scaffolding legitimately produce unused helpers.
foreach(target test_core test_formats test_pipeline test_plugins test_headless test_runtime test_exporters)
    target_compile_options(${target} PRIVATE -Wno-unused-function)
endforeach()
