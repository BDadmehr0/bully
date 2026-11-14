# bully::plugins - versioned C-ABI plugin host.
#
# The SDK header (plugins/sdk/include/bully/plugin_api.h) is the contract; the
# host is a separate library so tools that only need the manifest model do not
# link dl.
bully_add_library(bully_plugins
    PUBLIC_DEPS bully_core bully_pipeline
    PRIVATE_DEPS ${CMAKE_DL_LIBS})

target_include_directories(bully_plugins PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/sdk/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/bully>)

set_target_properties(bully_plugins PROPERTIES
    EXPORT_NAME plugins
    DESCRIPTION "Plugin host and stable C ABI")

# Sample plugins prove the ABI end to end and are used by the test-suite.
if(BULLY_BUILD_TESTS)
    add_subdirectory(samples)
endif()
