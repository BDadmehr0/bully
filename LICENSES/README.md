# Third-party licenses

Bully Studio itself is MIT-licensed (see the repository LICENSE). Vendored or
optional third-party components:

| Component | Version | License | Where |
|---|---|---|---|
| SQLite amalgamation | 3.38.2 | Public domain | `third_party/sqlite/` |
| Catch2 | 3.5.4 | BSL-1.0 | `third_party/catch2/` (test-only) |
| Qt 6 | 6.x | LGPL-3.0 / commercial | system package, optional (Studio GUI) |
| SDL3 | 3.x | Zlib | system package (runtime window/input) |
| Assimp | 5.x | BSD-3-Clause | system package (FBX/DAE bridge, optional) |
| GLM | 0.9.9+ | MIT | system package (vector math) |
| OpenAL Soft | 1.23+ | LGPL-2.0 | system package (audio playback) |
| FreeType | 2.13+ | FTL / GPLv2 | system package (font rasterisation) |

Optional system dependencies are discovered by CMake and are never required to
build the headless asset pipeline, the CLI or the test-suite.
