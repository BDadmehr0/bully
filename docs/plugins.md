# Plugins - extension model and stable C ABI

Bully Studio is extensible through native shared libraries loaded at runtime.
A plugin can add importers, exporters, asset processors, QA rules, editor
panels, project templates, build targets and runtime systems.

The contract lives in [`plugins/sdk/include/bully/plugin_api.h`](../plugins/sdk/include/bully/plugin_api.h)
and is deliberately **plain C99**: a plugin can be written in C, C++, Rust, Zig
or anything else that can produce a `.so`/`.dll`/`.dylib` exporting one symbol.

```
plugin directory/
├── manifest.json        # metadata, capabilities, permissions - read before dlopen
└── libmy_plugin.so      # exports bully_plugin_describe()
```

## Discovery and load order

The host never trusts a plugin before it has been validated:

1. **Discover** - scan each plugin directory for `manifest.json`. Nothing is
   loaded; a broken manifest is reported and skipped (`PluginState::Disabled`).
2. **Validate** - id, version, library path, API version, dependencies,
   permissions (`PluginManifest::validate`).
3. **Resolve** - order by declared dependencies; missing optional dependencies
   are tolerated, missing required ones disable the dependent plugin.
4. **Check ABI** - the manifest's `api.major` must equal
   `BULLY_PLUGIN_ABI_MAJOR`; a *lower* minor is accepted with a warning.
5. **Load** - `dlopen(..., RTLD_NOW | RTLD_LOCAL)` so a plugin cannot drag the
   host into a symbol collision.
6. **Locate** `bully_plugin_describe` (`BULLY_PLUGIN_EXPORT_SYMBOL`). The
   library must export **exactly this** symbol name; the common mistake
   (`BULLY_PLUGIN_API_SYMBOL` = `bully_plugin_api`) is the plugin-side typedef in
   the header, not the exported function. A plugin that exports the typedef name
   fails with `BULLY-PLUG-0022`.
7. **Describe** - read the descriptor, compare id/API version with the manifest.
8. **Initialize** - hand over the `bully_host_api` vtable; the plugin registers
   importers/panels/QA rules through it.

Any failure marks the plugin `Failed`/`Disabled` with a reason, and the reason is
stored so the Studio can show it. Nothing else in the editor is affected.

```
$ bully_asset_cli plugins build/plugins/samples
com.bully.samples.hello_importer v1.0.0 [discovered] api 1.0 Hello Importer (sample)
```

## Manifest (`manifest.json`)

```json
{
  "id": "com.example.my_plugin",
  "name": "My Plugin",
  "version": "1.2.0",
  "description": "Adds a .mesh importer and a QA rule.",
  "vendor": "Example Ltd",
  "library": "libmy_plugin.so",
  "api": { "major": 1, "minor": 0 },
  "dependencies": [{ "id": "com.bully.importer.obj", "minimum_version": "1.0.0", "optional": true }],
  "permissions": ["project.read"],
  "capabilities": ["importer", "qa_rule"],
  "reload_safe": true,
  "enabled_by_default": true
}
```

| Field | Rules |
|---|---|
| `id` | required, reverse-DNS, unique (duplicates are refused) |
| `version` | required, dotted numeric; compared with `version_at_least()` |
| `library` | required, relative to the plugin directory |
| `api.major/minor` | required (defaults 1/0); major must match the host |
| `permissions` | must come from the known list; unknown ones are rejected |
| `capabilities` | unknown values warn (`BULLY-PLUG-0011`) and are ignored |
| `reload_safe` | required for hot reload; otherwise only unload+load is allowed |

Known permissions: `project.read`, `project.write`, `network`, `process.spawn`
(and only those the host can actually enforce - a plugin cannot request "root").

## The two vtables

**Host → plugin** `bully_host_api`: logging, host allocator, importer / editor
panel / QA rule registration, project file read/write, path resolution. Every
field may be `NULL` in a headless build; plugins must null-check.

**Plugin → host** `bully_plugin_api`: `describe` and `initialize` are required;
`shutdown`, `run_import`, `run_export`, `run_qa_rule` and `reload` are optional.

Both structs start with `struct_size`, `abi_major`, `abi_minor`. New services are
**appended**; a host can detect an older plugin and vice versa, which is what
makes the ABI additive rather than breaking.

```c
// The one symbol every plugin exports:
const bully_plugin_api* bully_plugin_describe(void);
```

### Memory ownership

* Host passes input buffers; they are only valid for the duration of the call.
* Plugin returns output through `bully_buf` allocated with `host->alloc`; the
  host takes ownership and frees it. Returning a `malloc`ed pointer from a
  different CRT is a bug the host cannot detect - use the host allocator.
* Strings are `bully_str` (pointer + size). They are **not** NUL-terminated.
* Nothing returned by `describe()` may be freed; it must live for the plugin's
  lifetime.

## Capabilities

| Capability | Registration call | What the host does |
|---|---|---|
| importer | `register_importer` + `run_import` | routes matching extensions through the plugin, then the normal pipeline (QA, packaging) |
| exporter | `run_export` | lists the exporter in the Studio export menu |
| processor | `register_importer` (as a transform) | can post-process compiled assets |
| editor panel | `register_editor_panel` (JSON) | renders the panel in the Studio dock |
| script | Lua glue | registers a script module |
| project template | `manifest.capabilities` | adds the template to "New Project" |
| build target | `run_export` variant | adds a target to the Build Manager |
| runtime system | `reload` | hooks into the runtime update loop |
| qa rule | `register_qa_rule` + `run_qa_rule` | runs the rule in `qa` and the QA dashboard |

A registered importer appears in `bully_asset_cli list` output alongside the
built-in importers, and its assets go through the same `AssetType`, cache, QA and
packaging path - plugin output is never a second-class citizen.

## Writing a plugin

The sample in [`plugins/samples/hello_importer`](../plugins/samples/hello_importer)
is the reference: a `.hello` importer plus a QA rule, built on the public C ABI
only.

```cpp
#include <string>
#include "bully/plugin_api.h"

namespace {

const std::string kId = "com.example.hello";
const std::string kName = "Hello";
const std::string kVersion = "1.0.0";
const std::string kVendor = "Example";
const std::string kDescription = "Minimal importer";

const bully_plugin_descriptor kDescriptor{
    bully_str{kId.data(), kId.size()},
    bully_str{kName.data(), kName.size()},
    bully_str{kVersion.data(), kVersion.size()},
    bully_str{kVendor.data(), kVendor.size()},
    bully_str{kDescription.data(), kDescription.size()},
    BULLY_PLUGIN_ABI_MAJOR,
    BULLY_PLUGIN_ABI_MINOR,
    (1u << BULLY_CAP_IMPORTER) | (1u << BULLY_CAP_QA_RULE),
    1,  // reload_safe
};

bully_result initialize(const bully_host_api* host, void* /*plugin_state*/)
{
    if (host == nullptr || host->register_importer == nullptr) return BULLY_ERR_NOT_SUPPORTED;
    const std::string importer = "com.example.hello.importer";
    const std::string display = "Hello";
    const std::string ext = ".hello";
    return host->register_importer(host->host_handle, bully_str{importer.data(), importer.size()},
                                   bully_str{display.data(), display.size()},
                                   bully_str{kVersion.data(), kVersion.size()},
                                   bully_str{ext.data(), ext.size()}, nullptr);
}

bully_result run_import(bully_str /*importer_id*/, const bully_import_request* request,
                        bully_import_response* response)
{
    // Read request->source_path, parse it, allocate response->output through the
    // host allocator, and return BULLY_OK.
    (void)request;
    (void)response;
    return BULLY_ERR_NOT_SUPPORTED;
}

}  // namespace

extern "C" const bully_plugin_api* bully_plugin_describe()
{
    static const bully_plugin_api api{
        sizeof(bully_plugin_api),
        BULLY_PLUGIN_ABI_MAJOR,
        BULLY_PLUGIN_ABI_MINOR,
        [] { return &kDescriptor; },  // describe
        initialize,                   // initialize
        nullptr,                      // shutdown
        run_import,                   // run_import
        nullptr,                      // run_export
        nullptr,                      // run_qa_rule
        nullptr,                      // reload
    };
    return &api;
}
```

`bully_str` is just `{const char*, size_t}` - build one from a `std::string` as
above, or use a literals helper of your own. Strings must outlive the call that
receives them, and the descriptor must live for the plugin's lifetime.

Build it as a module; the only hard requirements are `-shared`, PIC, and a
`bully_plugin_describe` symbol with default visibility:

```cmake
add_library(my_plugin MODULE plugin.cpp)
target_include_directories(my_plugin PRIVATE <prefix>/include/bully)
target_compile_features(my_plugin PRIVATE cxx_std_17)
set_target_properties(my_plugin PROPERTIES PREFIX "" OUTPUT_NAME "my_plugin")
```

A plugin **does not link** against the host libraries. In-tree samples may link
the public format library (`bully_formats`) to reuse a writer, exactly like
`hello_importer` does; third-party plugins are free to emit bytes themselves.

Place `libmy_plugin.so` next to its `manifest.json` in any directory listed in
`PluginHost::Options::plugin_directories` (the Studio uses
`<project>/plugins`, the CLI takes a directory argument).

## Isolation, failures and reload

* Every plugin callback is wrapped in `try`/`catch`; an exception becomes
  `Failed` (`BULLY-PLUG-0023`) and the plugin is unloaded.
* A **segfault cannot be contained in-process on Linux.** The host says so
  instead of pretending otherwise: on a crash the Studio writes a crash marker
  in the plugin state directory and refuses to auto-load that plugin on the next
  start until the user re-enables it.
* Hot **reload** calls `shutdown()`, `dlclose`, then loads again. It is refused
  unless the plugin declares `reload_safe` (`BULLY-PLUG-0031`) *and*
  `allow_reloading` is on in `PluginHost::Options`.
* **Unload** retracts everything the plugin registered: importers, QA rule ids
  and editor panel descriptors are all keyed by plugin id, so a disabled plugin
  leaves no ghost entries in the Studio or in `bully_asset_cli list`.

## QA integration

The built-in `plugin.compatibility` rule (see
[docs/testing.md](testing.md)) scans the manifest of every plugin found under
`$BULLY_PLUGIN_PATH` and fails a project that depends on a plugin whose declared
API major differs from the host. Additional plugin QA rules run through
`run_qa_rule` and may tighten or extend the built-in rule set; their findings use
the plugin's own diagnostic codes and are attributed to the plugin in the report.

## Diagnostic codes

`BULLY-PLUG-0001..0041`, grouped as:

| Range | Meaning |
|---|---|
| `0001`-`0017` | manifest problems (missing id/version/library, bad API, self dependency, unknown permission) |
| `0018`-`0029` | load problems (not discovered, disabled, dlopen failure, wrong/missing export symbol, incomplete API table, null descriptor, exceptions) |
| `0030`-`0039` | runtime problems (reload refused, not loaded, unknown importer, unknown QA rule) |
| `0040`-`0041` | bridge/pipeline integration |

## Testing plugins

`tests/test_plugins.cpp` builds the sample plugin, loads it through the real
host, imports a `.hello` asset, runs its QA rule, unloads it and asserts that
every registration disappeared. Use it as a template for your own plugin's
integration test.
