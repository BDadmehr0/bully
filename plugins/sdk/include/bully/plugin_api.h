/* ==========================================================================
 * Bully Studio - Stable Plugin C ABI
 *
 * This header is THE contract between the host and dynamically loaded native
 * plugins. It is intentionally plain C99 with fixed-width types so plugins can
 * be written in C, C++, Rust or Zig without pulling in host headers.
 *
 * Rules:
 *   * Plugins never link against host C++ symbols. They receive a vtable of
 *     function pointers (bully_host_api) and return their own vtable
 *     (bully_plugin_api).
 *   * The ABI is versioned. A plugin declares the ABI it was compiled against
 *     (BULLY_PLUGIN_ABI_MAJOR/MINOR); the host refuses a different major and
 *     accepts an older minor with a warning.
 *   * Every entry point is optional except `bully_plugin_describe`.
 *   * A plugin crash must not take down the editor: the host loads plugins in
 *     this order -> validate manifest, load library, describe, symbol check,
 *     initialize. Any failure is reported and the plugin is disabled.
 *   * Reloading is supported: the host unloads only when the plugin declares
 *     itself reload-safe and after calling shutdown.
 *
 * Version history:
 *   1.0  initial ABI: importers, processors, editor panels, build targets.
 * ========================================================================== */
#ifndef BULLY_PLUGIN_API_H
#define BULLY_PLUGIN_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BULLY_PLUGIN_ABI_MAJOR 1
#define BULLY_PLUGIN_ABI_MINOR 0

#define BULLY_PLUGIN_EXPORT_SYMBOL "bully_plugin_describe"
#define BULLY_PLUGIN_API_SYMBOL "bully_plugin_api"

/* ---------------------------------------------------------------- results */
typedef enum bully_result {
    BULLY_OK = 0,
    BULLY_ERR_INVALID_ARGUMENT = 1,
    BULLY_ERR_NOT_SUPPORTED = 2,
    BULLY_ERR_IO = 3,
    BULLY_ERR_PARSE = 4,
    BULLY_ERR_VERSION = 5,
    BULLY_ERR_INTERNAL = 6
} bully_result;

/* --------------------------------------------------------------- strings */
/* String views are not NUL-terminated; `data` may be NULL when `size` is 0. */
typedef struct bully_str {
    const char* data;
    size_t size;
} bully_str;

/* -------------------------------------------------------------- buffers */
/* The host owns input buffers; the plugin allocates output buffers through
 * the host allocator so the host can free them safely. */
typedef struct bully_buf {
    uint8_t* data;
    size_t size;
    size_t capacity;
} bully_buf;

/* ---------------------------------------------------- plugin capabilities */
typedef enum bully_capability {
    BULLY_CAP_IMPORTER = 1,
    BULLY_CAP_EXPORTER = 2,
    BULLY_CAP_PROCESSOR = 3,
    BULLY_CAP_EDITOR_PANEL = 4,
    BULLY_CAP_SCRIPT = 5,
    BULLY_CAP_PROJECT_TEMPLATE = 6,
    BULLY_CAP_BUILD_TARGET = 7,
    BULLY_CAP_RUNTIME_SYSTEM = 8,
    BULLY_CAP_QA_RULE = 9
} bully_capability;

typedef struct bully_capability_set {
    uint32_t capabilities; /* bitmask of (1u << bully_capability) */
} bully_capability_set;

/* --------------------------------------------------------------- logging */
typedef enum bully_log_level {
    BULLY_LOG_TRACE = 0,
    BULLY_LOG_DEBUG = 1,
    BULLY_LOG_INFO = 2,
    BULLY_LOG_WARN = 3,
    BULLY_LOG_ERROR = 4
} bully_log_level;

/* ------------------------------------------------------------- host API */
/*
 * Services the host offers to plugins. Pointer fields may be NULL when the
 * service is unavailable in the current build (e.g. no Qt UI); plugins MUST
 * null-check every function pointer they use.
 *
 * The struct only ever grows by appending fields; `struct_size` lets a plugin
 * detect an older host.
 */
typedef struct bully_host_api {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;

    /* Logging. `message` need not be NUL-terminated. */
    void (*log)(void* host, bully_log_level level, bully_str category, bully_str message);

    /* Memory owned by the host allocator; free with `free`. */
    void* (*alloc)(void* host, size_t size);
    void (*free)(void* host, void* pointer);

    /* Registers a runtime asset importer implemented by the plugin. The plugin
     * provides callbacks; `user_data` is passed back verbatim. */
    bully_result (*register_importer)(void* host, bully_str importer_id,
                                      bully_str display_name, bully_str version,
                                      bully_str extensions /* comma separated */,
                                      void* user_data);

    /* Asks the host to register an editor panel described by a JSON string
     * matching the panel schema (see docs/plugins.md). */
    bully_result (*register_editor_panel)(void* host, bully_str panel_json, void* user_data);

    /* Registers an extra QA rule: `rule_json` declares the rule's name and the
     * plugin's `run_rule` callback is invoked by the QA runner. */
    bully_result (*register_qa_rule)(void* host, bully_str rule_json, void* user_data);

    /* Reads a project file. Returns BULLY_OK and fills `out` (host-allocated)
     * when successful; caller frees `out->data` via `free`. */
    bully_result (*read_project_file)(void* host, bully_str relative_path, bully_buf* out);

    /* Writes a project file (atomically). */
    bully_result (*write_project_file)(void* host, bully_str relative_path,
                                       const uint8_t* data, size_t size);

    /* Resolves a project-relative path to an absolute path in `out`
     * (host-allocated, NUL-terminated). */
    bully_result (*resolve_path)(void* host, bully_str relative_path, bully_str* out);

    void* host_handle;
} bully_host_api;

/* --------------------------------------------------- argument containers */
/* Key/value pairs used by importer and exporter callbacks. */
typedef struct bully_kv {
    bully_str key;
    bully_str value;
} bully_kv;

typedef struct bully_kv_list {
    const bully_kv* items;
    size_t count;
} bully_kv_list;

typedef struct bully_import_request {
    bully_str asset_id;
    bully_str source_path;       /* absolute */
    bully_str relative_path;     /* project-relative */
    bully_str output_path;       /* absolute destination */
    bully_kv_list settings;
    const bully_host_api* host;
    void* host_handle;
} bully_import_request;

typedef struct bully_import_response {
    /* Plugin allocates `output.data` with host->alloc. The host takes ownership
     * and frees it after writing the file. */
    bully_buf output;
    bully_str summary;           /* optional, host-copies */
    bully_str report_json;       /* optional, host-copies */
    uint32_t warning_count;
} bully_import_response;

/* ---------------------------------------------------------- plugin API */
typedef struct bully_plugin_descriptor {
    bully_str plugin_id;        /* reverse-DNS, unique */
    bully_str display_name;
    bully_str version;          /* semver */
    bully_str vendor;
    bully_str description;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t capabilities;      /* bitmask of (1u << bully_capability) */
    int reload_safe;            /* nonzero => host may unload and reload */
} bully_plugin_descriptor;

typedef struct bully_plugin_api {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;

    /* Required: fills the descriptor (static storage, valid for plugin life). */
    const bully_plugin_descriptor* (*describe)(void);

    /* Required: called once after load. Return BULLY_OK to enable. */
    bully_result (*initialize)(const bully_host_api* host, void* plugin_state);
    /* Optional: called before unload. */
    void (*shutdown)(void);

    /* Optional importers. `importer_id` must match a registration call. */
    bully_result (*run_import)(bully_str importer_id, const bully_import_request* request,
                              bully_import_response* response);

    /* Optional exporters: write a compiled asset to an external format. */
    bully_result (*run_export)(bully_str exporter_id, bully_str asset_path,
                              bully_str destination_path);

    /* Optional QA rule execution: returns a JSON finding list. */
    bully_result (*run_qa_rule)(bully_str rule_name, bully_str project_root, bully_buf* out);

    /* Optional: hot-reload hook. Called when the host detects that sources the
     * plugin cares about changed; return BULLY_OK when the reload succeeded. */
    bully_result (*reload)(void);
} bully_plugin_api;

/* --------------------------------------------------------- entry points */
/* Every shared plugin exports exactly this function. */
typedef const bully_plugin_api* (*bully_plugin_api_fn)(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* BULLY_PLUGIN_API_H */
