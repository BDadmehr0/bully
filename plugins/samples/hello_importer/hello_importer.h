// Sample plugin header: declares the C-linkage entry point plus internal
// helpers. A real third-party plugin only needs bully/plugin_api.h.
#pragma once

#include "bully/plugin_api.h"

extern "C" {

/// Required symbol: returns the plugin's API table.
const bully_plugin_api* bully_plugin_describe(void);
}
