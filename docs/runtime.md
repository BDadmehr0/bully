# Runtime layer

The runtime is what a shipped game runs on. It is split in two on purpose:

```
engine/runtime/        headless subsystems - no window, GPU, or audio device
├── asset_database     mounts built/ or a .bpackage, decodes and caches assets
├── story              flags, mission progress, dialogue traversal, save/load
└── clock              fixed-step clock + input action map

apps/game/             SDL3 shell on top (window, input polling, audio output)
```

Everything that decides *what happens* lives in `engine/runtime` and is unit
tested (`tests/runtime`, suite `runtime`, 24 cases). `apps/game` only turns SDL
events into the input map, calls the story layer and draws. That split is why a
progression bug can be reproduced in a test instead of by playing the game.

## Asset database

```cpp
runtime::AssetDatabase assets;
assets.mount_directory(project / "built");        // editor / developer loop
assets.mount_package(project / "packages/game.bpackage");  // shipped build

auto scene = assets.load_scene("scene.schoolyard");
auto model = assets.load_model("models/crate.bmodel");
```

* **Mounts are ordered**: a later mount wins for the same logical path, which is
  how hot-reload (`mount_directory(built, "live")`) and DLC override assets
  without copying files.
* Assets resolve by **asset id** or by **logical path**, case-insensitively and
  with either path separator. Directory mounts read the id from each file's
  `INFO` chunk at mount time, so id lookups work without decoding meshes.
* Every package entry's CRC is verified **at mount time** - a corrupt package is
  refused up front (`BULLY-PKG-*`) rather than failing mid-playthrough.
* Typed loaders (`load_model`, `load_texture`, `load_material`, `load_animation`,
  `load_audio`, `load_dialogue`, `load_scene`, `load_mission`) decode, validate
  and cache. Loading the same asset twice returns the same `shared_ptr`; the
  cache is cleared with `clear_caches()` after a rebuild.
* Validation **errors** refuse the load; validation **warnings** are logged and
  the asset is still usable, because a shipped asset that trips a soft rule must
  not be unloadable.
* A wrong typed loader (asking for a texture and getting a model) fails with a
  format diagnostic instead of returning an empty document.

Diagnostic codes: `BULLY-RT-0001..0019` (mount and lookup).

## Story state

`StoryState` is the entire save game:

| Area | Operations |
|---|---|
| flags | typed get/set/clear, `matches()` with the same truthiness rules as conditions |
| missions | `start_mission` (dependency-gated), `advance_objective`, `tick_mission` (timed), `fail_mission`, completion/failure flags |
| dialogue | `begin_dialogue`, `current_dialogue_node`, `advance_dialogue` (branch index for choices, flag gating, heard-line tracking) |
| scene | `current_scene()`, `enter_scene()` |
| save | `to_json()` / `from_json()` - a restored save continues exactly where it stopped |

Mission completion is derived, not stored: a mission completes when every
**required** objective is complete, which is why an optional objective can never
block a mission. Dependencies are checked against the same store, so ordering is
enforced by data rather than by script.

Diagnostic codes: `BULLY-RT-0100..0119` (missions and dialogue), `0120` (save
data).

## Frame clock

`FrameClock` is a fixed-timestep accumulator:

* `begin_frame(delta)` returns how many fixed steps to run,
* `end_step()` per simulated step, `end_frame()` once,
* `alpha()` gives the interpolation factor for rendering,
* `max_steps_per_frame` caps catch-up (a two second stall does not queue 120
  steps), and dropped steps are counted for the debug overlay,
* `set_time_scale()` / `set_paused()` for slow-motion and menus,
* deltas are clamped to one second, so a debugger halt cannot teleport the world.

## Input map

`InputMap` maps physical inputs (`key:w`, `pad:south`, `mouse:left`) to named
actions (`move.forward`, `interact`, `pause`). Multiple bindings per action take
the **strongest** input rather than summing (holding W and pushing the stick does
not double the speed), `axis()` cancels opposite directions, and
`just_pressed()` latches edges so a held button fires once.

Rebinding is a data edit, which is what the Studio's Controls panel writes.

## Running it

```sh
bully_game <project-dir | package.bpackage> [--headless] [--frames N] [--verbose]
```

A project directory is the folder holding `project.bullyproj`; a manifest path
is also accepted and resolved to its parent. The project must have been built -
`bully_game` loads compiled assets and never compiles them, so a project with an
empty `built/` is rejected with exit 2 and the command to run.

Without `--headless` the SDL3 host opens a window; with it, the same fixed-step
loop runs for a fixed number of frames and prints statistics. Both modes boot the
same `GameSession`.

### If the window never appears

`--diagnose` explains it:

```console
$ bully_game --diagnose
bully_game video diagnostics
  build          : SDL3 present
  SDL driver     : dummy
  displays       : 1
  drivers built  : offscreen dummy evdev
  window possible: NO
```

A `dummy` or `offscreen` driver means SDL has no display server to talk to
(plain SSH, a TTY, a container). In that state `SDL_CreateWindow` still
succeeds - which is why the host used to print "Window open" over a window that
could never appear. It now detects the driver before creating a window and
exits 1 with the reason. `scripts/run_game.py` probes this with `--diagnose`
before building, so you get the explanation instead of a silent no-op.

## What is *not* here yet

**There is no 3D renderer.** The windowed host clears the window to the loaded
scene's ambient colour and draws a moving marker so the loop is visibly alive;
it does not rasterise meshes. Physics, audio mixing backends and animation
blending are likewise separate subsystems, listed in
[architecture.md](architecture.md) as the remaining runtime layers. The asset,
story, clock and input layers are complete and tested, and `apps/game` consumes
exactly those.
