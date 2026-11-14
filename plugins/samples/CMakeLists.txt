# Sample plugins, built as shared libraries with the classic MODULE form.
#
# Each sample demonstrates one capability of the ABI, and the test-suite loads
# them through PluginHost to verify discovery, ABI checking, registration and
# importer execution without touching host internals.
add_library(bully_sample_plugin MODULE
    hello_importer/hello_importer.cpp)
target_include_directories(bully_sample_plugin PRIVATE
    ${CMAKE_SOURCE_DIR}/plugins/sdk/include
    ${CMAKE_SOURCE_DIR}/engine/core/include
    ${CMAKE_SOURCE_DIR}/formats/include
    ${CMAKE_SOURCE_DIR}/asset_pipeline/include)
target_link_libraries(bully_sample_plugin PRIVATE bully_core bully_formats bully_pipeline)
set_target_properties(bully_sample_plugin PROPERTIES
    PREFIX "lib"
    OUTPUT_NAME "bully_sample_importer"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/plugins/samples"
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN OFF)

# Drop the sample's manifest next to the built library so PluginHost can
# discover it during the test run and `bully_asset_cli plugins <dir>` can list it.
set(BULLY_SAMPLE_PLUGIN_LIBRARY "${CMAKE_SHARED_MODULE_PREFIX}bully_sample_importer${CMAKE_SHARED_MODULE_SUFFIX}")
configure_file(
    hello_importer/manifest.json.in
    "${CMAKE_BINARY_DIR}/plugins/samples/manifest.json"
    @ONLY)
set(BULLY_SAMPLE_PLUGIN_MANIFEST "${CMAKE_BINARY_DIR}/plugins/samples/manifest.json")
