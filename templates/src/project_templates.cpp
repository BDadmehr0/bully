
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include "bully/templates/project_templates.h"

#include "bully/core/fs.h"
#include "bully/core/log.h"
#include "bully/core/strings.h"
#include "bully/pipeline/builtin_importers.h"

namespace bully::templates {

namespace {

TemplateInfo make_retro_action()
{
    TemplateInfo info;
    info.name = "retro-action";
    info.description =
        "Bully-inspired retro 3D action game: third-person character, school levels, missions";
    info.retro_preset = "ps2";
    info.runtime_modules = {"renderer", "scene", "physics", "animation", "audio", "gameplay",
                            "dialogue"};
    info.directories = {"assets/models", "assets/textures", "assets/audio", "assets/audio/voice",
                        "content/scenes",   "content/missions", "content/dialogue",
                        "content/story"};
    info.samples.push_back({"assets/models/crate.obj", R"(# Bully Studio sample asset - a 10x10x10 crate
o crate
v -5.0 -5.0 -5.0
v  5.0 -5.0 -5.0
v  5.0  5.0 -5.0
v -5.0  5.0 -5.0
v -5.0 -5.0  5.0
v  5.0 -5.0  5.0
v  5.0  5.0  5.0
v -5.0  5.0  5.0
vt 0.0 0.0
vt 1.0 0.0
vt 1.0 1.0
vt 0.0 1.0
usemtl crate_mat
f 1/1 2/2 3/3 4/4
f 5/1 6/2 7/3 8/4
f 1/1 5/2 6/3 2/4
f 2/1 6/2 7/3 3/4
f 3/1 7/2 8/3 4/4
f 4/1 8/2 5/3 1/4
)"});
    info.samples.push_back({"assets/models/crate.mtl", R"(newmtl crate_mat
Kd 0.65 0.45 0.25
Ks 0.05 0.05 0.05
Ns 4.0
)"});
    info.samples.push_back({"content/dialogue/intro.dialogue.json", R"({
  "name": "intro",
  "language": "en",
  "entry_node": "n_greet",
  "characters": [
    {"id": "chr.player", "name": "Jimmy", "voice_actor": "actor.player", "color": "#4FA3D1"},
    {"id": "chr.gary", "name": "Gary", "voice_actor": "actor.gary", "color": "#D14F4F"}
  ],
  "lines": [
    {
      "id": "dlg.intro.gary.001",
      "character": "chr.gary",
      "text": "Nice outfit. Did your mom pick it?",
      "delivery": 5,
      "audio_start": 0.0,
      "audio_end": 2.4,
      "selected_take": "t01",
      "takes": [
        {"id": "t01", "voice_actor": "actor.gary", "start": 0.0, "end": 2.4,
         "selected": true, "notes": "placeholder timing - record before shipping"}
      ],
      "next": "n_choice"
    },
    {
      "id": "dlg.intro.jimmy.001",
      "character": "chr.player",
      "text": "Whatever, Gary.",
      "audio_start": 2.4,
      "audio_end": 3.6
    },
    {
      "id": "dlg.intro.jimmy.002",
      "character": "chr.player",
      "text": "You'll get yours.",
      "audio_start": 3.6,
      "audio_end": 5.0
    }
  ],
  "nodes": [
    {"id": "n_greet", "kind": 0, "line": "dlg.intro.gary.001", "next": "n_choice"},
    {
      "id": "n_choice",
      "kind": 1,
      "branches": [
        {"label": "Ignore him", "target": "n_ignore"},
        {"label": "Threaten him", "target": "n_threaten"}
      ]
    },
    {"id": "n_ignore", "kind": 0, "line": "dlg.intro.jimmy.001", "next": "n_end"},
    {"id": "n_threaten", "kind": 0, "line": "dlg.intro.jimmy.002", "next": "n_end"},
    {"id": "n_end", "kind": 5}
  ]
}
)"});
    info.samples.push_back({"content/scenes/schoolyard.scene.json", R"({
  "name": "schoolyard",
  "environment": {"fog_enabled": true, "fog_start": 40.0, "fog_end": 160.0, "time_of_day": 14.0},
  "entities": [
    {"id": "ent.crate_01", "name": "Crate 01", "model": "crate",
     "position": [0.0, 5.0, 0.0], "static": true}
  ],
  "lights": [
    {"id": "light.sun", "type": 0, "intensity": 1.2, "position": [40.0, 60.0, 20.0], "baked": false}
  ],
  "triggers": [
    {"id": "trg.intro_mission", "kind": "mission", "once": true,
     "payload": {"mission": "m_first_day"}}
  ],
  "spawns": [
    {"id": "spawn.player_start", "kind": "player"},
    {"id": "spawn.gary", "kind": "npc"}
  ]
}
)"});
    info.samples.push_back({"content/missions/m_first_day.mission.json", R"({
  "name": "First Day",
  "description": "Get through your first day of school.",
  "giver": "chr.gary",
  "start_node": "n_start",
  "repeatable": false,
  "objectives": [
    {"id": "obj.attend_class", "description": "Attend English class", "kind": 0,
     "location": "trg.classroom"},
    {"id": "obj.talk_gary", "description": "Talk to Gary in the schoolyard", "kind": 1,
     "target": "chr.gary", "dialogue": "dlg.intro.gary.001"}
  ],
  "nodes": [
    {"id": "n_start", "objective": "obj.attend_class", "edges": [{"to": "n_talk"}]},
    {"id": "n_talk", "objective": "obj.talk_gary", "edges": [{"to": "n_end"}]},
    {"id": "n_end"}
  ],
  "conditions": [
    {"id": "cnd.class_done", "kind": 1, "key": "obj.attend_class"}
  ]
}
)"});
    info.samples.push_back({"content/story/act01.md", R"(# Act One - New Kid

## Chapter 1 - First Bell

**Scene: Schoolyard (morning)**

Jimmy Hopkins arrives at Bullworth Academy. Gary Smith sizes him up in the yard.

- Mission: **First Day** (`m_first_day`)
- Dialogue: `content/dialogue/intro.dialogue.json`

> "Nice outfit. Did your mom pick it?"
> -- Gary

### Flags
- `met_gary` - set when the intro conversation finishes
- `class_attended` - set after the English class scene
)"});
    return info;
}

TemplateInfo make_third_person()
{
    TemplateInfo info;
    info.name = "third-person";
    info.description = "Third-person adventure: exploration, camera rig, interactables";
    info.retro_preset = "xbox";
    info.runtime_modules = {"renderer", "scene", "physics", "animation", "audio", "gameplay"};
    info.directories = {"assets/models", "assets/textures", "content/scenes", "content/missions"};
    return info;
}

TemplateInfo make_narrative()
{
    TemplateInfo info;
    info.name = "narrative";
    info.description = "Narrative-driven game built around dialogue graphs and story flags";
    info.retro_preset = "none";
    info.runtime_modules = {"renderer", "scene", "audio", "dialogue", "gameplay"};
    info.directories = {"assets/audio/voice", "content/dialogue", "content/story",
                        "content/scenes"};
    return info;
}

TemplateInfo make_open_world()
{
    TemplateInfo info;
    info.name = "open-world";
    info.description = "Open-world prototype: streaming scenes, NPC placement, mission triggers";
    info.retro_preset = "ps2";
    info.runtime_modules = {"renderer", "scene", "physics", "animation", "audio", "gameplay",
                            "dialogue", "streaming"};
    info.directories = {"assets/models", "assets/textures", "content/scenes", "content/missions",
                        "content/dialogue", "content/story"};
    return info;
}

TemplateInfo make_dialogue_focused()
{
    TemplateInfo info;
    info.name = "dialogue-focused";
    info.description = "Dialogue/dubbing heavy project with voice production pipeline";
    info.retro_preset = "none";
    info.runtime_modules = {"audio", "dialogue", "scene", "renderer"};
    info.directories = {"assets/audio/voice", "assets/audio/music", "content/dialogue",
                        "content/story"};
    return info;
}

}  // namespace

const std::vector<TemplateInfo>& template_catalog()
{
    static const std::vector<TemplateInfo> catalog{make_retro_action(), make_third_person(),
                                                   make_narrative(), make_open_world(),
                                                   make_dialogue_focused()};
    return catalog;
}

const TemplateInfo* find_template(std::string_view name)
{
    const std::string lowered = core::to_lower(name);
    for (const auto& info : template_catalog()) {
        if (info.name == lowered) return &info;
    }
    return nullptr;
}

core::Result<pipeline::ProjectSettings> create_project(const std::filesystem::path& target,
                                                       std::string_view template_name,
                                                       std::string project_name)
{
    core::DiagnosticList diagnostics;
    const TemplateInfo* info = find_template(template_name);
    if (info == nullptr) {
        diagnostics.error("BULLY-TPL-0001",
                          "unknown template '" + std::string(template_name) +
                              "'. Run 'bully_asset_cli templates' to see the catalogue.",
                          target.string());
        return core::Result<pipeline::ProjectSettings>(std::move(diagnostics));
    }
    if (core::FileSystem::exists(target)) {
        auto files = core::FileSystem::list_files(target, false);
        if (files && !files.value().empty()) {
            diagnostics.error("BULLY-TPL-0002",
                              "target directory already exists and is not empty", target.string());
            return core::Result<pipeline::ProjectSettings>(std::move(diagnostics));
        }
    }
    pipeline::ProjectLayout layout(target);
    pipeline::ProjectSettings settings;
    settings.name = project_name.empty() ? target.filename().string() : std::move(project_name);
    settings.template_name = info->name;
    settings.retro_preset = info->retro_preset;
    settings.runtime_modules = info->runtime_modules;
    auto status = layout.initialize(settings);
    if (!status) return core::Result<pipeline::ProjectSettings>(status.diagnostics());
    for (const auto& directory : info->directories) {
        auto directory_status = core::FileSystem::create_directories(target / directory);
        if (!directory_status) return core::Result<pipeline::ProjectSettings>(
            directory_status.diagnostics());
    }
    for (const auto& [relative, content] : info->samples) {
        auto write_status = core::FileSystem::write_text(target / relative, content);
        if (!write_status) {
            return core::Result<pipeline::ProjectSettings>(write_status.diagnostics());
        }
        core::log_info("templates", "created " + relative);
    }
    // A README makes the generated project self-documenting.
    std::string readme = "# " + settings.name + "\n\n";
    readme += "Created by Bully Studio from template **" + info->name + "**.\n\n";
    readme += "## Layout\n\n";
    readme += "- `assets/` - source assets (never modified by the pipeline)\n";
    readme += "- `content/` - authored scenes, missions, dialogue and story documents\n";
    readme += "- `built/` - compiled runtime assets\n";
    readme += "- `packages/` - distributable `.bpackage` output\n";
    readme += "- `reports/` - build and QA reports\n\n";
    readme += "## Build\n\n```sh\nbully_asset_cli build\nbully_asset_cli qa\n"
              "bully_asset_cli package\n```\n";
    auto readme_status = core::FileSystem::write_text(target / "README.md", readme);
    if (!readme_status) return core::Result<pipeline::ProjectSettings>(readme_status.diagnostics());
    return settings;
}

}  // namespace bully::templates
