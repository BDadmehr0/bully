# Asset pipeline layer, split into two libraries to keep the dependency graph
# acyclic:
#
#   bully::asset_model  asset identity/settings/build-state types (core only)
#   bully::pipeline     importers, processors, cache, packaging, QA
#
# bully::project (project metadata + registry) depends on asset_model and is
# itself a dependency of the pipeline kernel, so asset_model must not include
# anything from the project layer.
bully_add_library(bully_asset_model
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/src/asset.cpp
    PUBLIC_DEPS bully_core)
set_target_properties(bully_asset_model PROPERTIES
    EXPORT_NAME asset_model
    DESCRIPTION "Asset identity, import settings and build state")

file(GLOB_RECURSE BULLY_PIPELINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
list(REMOVE_ITEM BULLY_PIPELINE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/asset.cpp")

add_library(bully_pipeline STATIC ${BULLY_PIPELINE_SOURCES})
add_library(bully::pipeline ALIAS bully_pipeline)
target_include_directories(bully_pipeline
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bully_pipeline
    PUBLIC bully_core bully_formats bully_asset_model bully_project
    PRIVATE bully_sqlite3)
target_compile_features(bully_pipeline PUBLIC cxx_std_20)
# Retro presets are data (assets/presets/*.json); the compiler knows where the
# source tree's copy lives so a fresh build finds them without configuration.
target_compile_definitions(bully_pipeline PRIVATE
    BULLY_DEFAULT_PRESET_DIR="${CMAKE_SOURCE_DIR}/assets/presets")
set_target_properties(bully_pipeline PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    EXPORT_NAME pipeline
    DESCRIPTION "Asset pipeline: importers, processors, cache, packaging, QA")
