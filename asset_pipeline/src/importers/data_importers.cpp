// Importers for authored project sources:
//   .bmaterial.json -> .bmaterial   (Material Studio)
//   .banim.json     -> .banim       (Animation Studio)
//   .scene.json     -> .bscene      (Level Editor)
//   .mission.json   -> .bmission    (Mission Editor)
//   .dialogue.json  -> .bdialogue   (Dialogue & Dubbing Studio)
//
// The JSON schemas are documented in docs/formats/ and are the contract between
// the Studio editors and the pipeline. Structural validation is delegated to the
// compiled document types, so an editor and the CLI agree on what is valid.
#include <filesystem>

#include "bully/core/fs.h"
#include "bully/core/json.h"
#include "bully/core/strings.h"
#include "bully/formats/animation.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/material.h"
#include "bully/formats/mission.h"
#include "bully/formats/scene.h"
#include "bully/pipeline/builtin_importers.h"

namespace bully::pipeline {

namespace {

/// Resolves an authored reference ("crate", "models/crate.obj",
/// "assets/models/crate.obj") to a registered asset id. Scenes and missions are
/// authored against sources, but the registry stores generated UUIDs, so the
/// reference has to be translated at import time or the dependency graph (and
/// therefore incremental builds, QA and packaging) would never see it.
std::string resolve_asset_reference(const ImportContext& context, const std::string& reference)
{
    if (reference.empty()) return {};
    if (!context.resolve_asset) return reference;
    const auto resolved = context.resolve_asset(reference);
    return resolved.empty() ? reference : resolved;
}

/// Splits an authored list that may contain either ids or paths.
std::vector<std::string> resolve_asset_references(const ImportContext& context,
                                                  const std::vector<std::string>& references)
{
    std::vector<std::string> resolved;
    resolved.reserve(references.size());
    for (const auto& reference : references) {
        if (reference.empty()) continue;
        resolved.push_back(resolve_asset_reference(context, reference));
    }
    return resolved;
}

core::Result<core::json::Value> load_json(const ImportContext& context)
{
    auto text = core::FileSystem::read_text(context.source_path);
    if (!text) return core::Result<core::json::Value>(text.diagnostics());
    return core::json::parse(text.value());
}

std::string stem_of(const std::string& path)
{
    return std::filesystem::path(path).stem().string();
}

// ------------------------------------------------------------- material -----

class MaterialImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.material_json"; }
    [[nodiscard]] std::string display_name() const override { return "Bully material source (.json)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".bmaterial.json"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Material; }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto parsed = load_json(context);
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        const auto& root = parsed.value();

        formats::MaterialDocument document;
        document.asset_id = context.asset_id;
        document.name = root.get_string("name", stem_of(context.relative_source_path));
        document.shader_preset = root.get_string("shader_preset", "retro.lambert");
        document.parent_material = root.get_string("parent", "");
        document.notes = root.get_string("notes", "");
        document.state.blend = static_cast<formats::BlendMode>(root.get_int("blend", 0));
        document.state.cull = static_cast<formats::CullMode>(root.get_int("cull", 0));
        document.state.depth_test = root.get_bool("depth_test", true);
        document.state.depth_write = root.get_bool("depth_write", true);
        document.state.alpha_cutoff = static_cast<float>(root.get_number("alpha_cutoff", 0.5));

        std::vector<AssetDependency> dependencies;
        if (const core::json::Value* textures = root.find("textures");
            textures != nullptr && textures->is_array()) {
            for (const auto& entry : textures->items()) {
                formats::TextureBinding binding;
                binding.slot = static_cast<formats::TextureSlot>(entry.get_int("slot", 0));
                binding.texture_asset = entry.get_string("texture");
                binding.sampler_override = entry.get_string("sampler");
                binding.uv_scale_u = static_cast<float>(entry.get_number("uv_scale_u", 1.0));
                binding.uv_scale_v = static_cast<float>(entry.get_number("uv_scale_v", 1.0));
                if (!binding.texture_asset.empty() &&
                    !core::starts_with_ci(binding.texture_asset, "texture:")) {
                    dependencies.push_back(
                        AssetDependency{binding.texture_asset, true, "material texture slot"});
                }
                document.textures.push_back(std::move(binding));
            }
        }
        if (const core::json::Value* parameters = root.find("parameters");
            parameters != nullptr && parameters->is_array()) {
            for (const auto& entry : parameters->items()) {
                auto& parameter = document.set_parameter(
                    entry.get_string("name"), static_cast<formats::ParamType>(entry.get_int("type", 0)));
                if (const core::json::Value* values = entry.find("values");
                    values != nullptr && values->is_array()) {
                    for (std::size_t i = 0; i < 4 && i < values->size(); ++i) {
                        parameter.values[i] = static_cast<float>((*values)[i].as_number());
                    }
                }
            }
        }
        document.import_settings_json = context.settings.to_json().dump(0);

        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Material;
        result.output_bytes = document.serialize();
        result.summary = "material '" + document.name + "' -> preset " + document.shader_preset;
        result.report_json = document.to_json();
        result.dependencies = std::move(dependencies);
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

// ------------------------------------------------------------ animation -----

class AnimationImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.anim_json"; }
    [[nodiscard]] std::string display_name() const override { return "Bully animation source (.json)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".banim.json"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Animation; }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto parsed = load_json(context);
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        const auto& root = parsed.value();

        formats::AnimationDocument document;
        document.asset_id = context.asset_id;
        document.name = root.get_string("name", stem_of(context.relative_source_path));
        document.clip_name = root.get_string("clip", document.name);
        document.duration = static_cast<float>(root.get_number("duration", 0.0));
        document.sample_rate = static_cast<float>(root.get_number("sample_rate", 30.0));
        document.loop = static_cast<formats::LoopMode>(root.get_int("loop", 0));
        document.root_motion.enabled = root.get_bool("root_motion", false);
        document.root_motion.bone = root.get_string("root_bone", "root");

        auto read_keys_vec3 = [](const core::json::Value& entry, const char* channel,
                                 std::vector<formats::VectorKey>& out) {
            if (const core::json::Value* keys = entry.find(channel);
                keys != nullptr && keys->is_array()) {
                for (const auto& key_json : keys->items()) {
                    auto& key = out.emplace_back();
                    key.time = static_cast<float>(key_json.at("time").as_number());
                    if (const core::json::Value* value = key_json.find("value");
                        value != nullptr && value->is_array() && value->size() >= 3) {
                        key.value = formats::Vec3f{static_cast<float>((*value)[0].as_number()),
                                                   static_cast<float>((*value)[1].as_number()),
                                                   static_cast<float>((*value)[2].as_number())};
                    }
                }
            }
        };

        if (const core::json::Value* tracks = root.find("tracks");
            tracks != nullptr && tracks->is_array()) {
            for (const auto& entry : tracks->items()) {
                formats::BoneTrack track;
                track.bone_name = entry.get_string("bone");
                track.interpolation = static_cast<formats::InterpolationMode>(entry.get_int("interpolation", 0));
                read_keys_vec3(entry, "translation", track.translations);
                read_keys_vec3(entry, "scale", track.scales);
                if (const core::json::Value* rotations = entry.find("rotation");
                    rotations != nullptr && rotations->is_array()) {
                    for (const auto& key_json : rotations->items()) {
                        auto& key = track.rotations.emplace_back();
                        key.time = static_cast<float>(key_json.at("time").as_number());
                        if (const core::json::Value* value = key_json.find("value");
                            value != nullptr && value->is_array() && value->size() >= 4) {
                            key.value = formats::Quat{static_cast<float>((*value)[0].as_number()),
                                                      static_cast<float>((*value)[1].as_number()),
                                                      static_cast<float>((*value)[2].as_number()),
                                                      static_cast<float>((*value)[3].as_number())};
                        }
                    }
                }
                document.tracks.push_back(std::move(track));
            }
        }
        if (const core::json::Value* events = root.find("events");
            events != nullptr && events->is_array()) {
            for (const auto& entry : events->items()) {
                formats::AnimationEvent event;
                event.name = entry.get_string("name");
                event.payload = entry.get_string("payload");
                event.time = static_cast<float>(entry.get_number("time", 0.0));
                document.events.push_back(std::move(event));
            }
        }
        document.metadata_json = context.settings.to_json().dump(0);

        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Animation;
        result.output_bytes = document.serialize();
        result.summary = "animation '" + document.name + "', " +
                         std::to_string(document.keyframe_count()) + " keyframes";
        result.report_json = document.to_json();
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

// ---------------------------------------------------------------- scene -----

class SceneImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.scene_json"; }
    [[nodiscard]] std::string display_name() const override { return "Bully scene source (.json)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".scene.json"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Scene; }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto parsed = load_json(context);
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        const auto& root = parsed.value();

        formats::SceneDocument document;
        document.asset_id = context.asset_id;
        document.name = root.get_string("name", stem_of(context.relative_source_path));
        document.source_path = context.relative_source_path;
        if (const core::json::Value* env = root.find("environment")) {
            document.environment.fog_enabled = env->get_bool("fog_enabled", false);
            document.environment.fog_start = static_cast<float>(env->get_number("fog_start", 50.0));
            document.environment.fog_end = static_cast<float>(env->get_number("fog_end", 200.0));
            document.environment.time_of_day = static_cast<float>(env->get_number("time_of_day", 12.0));
            document.environment.lightmap_resolution =
                static_cast<std::uint32_t>(env->get_int("lightmap_resolution", 1024));
        }
        std::vector<AssetDependency> dependencies;
        if (const core::json::Value* entities = root.find("entities");
            entities != nullptr && entities->is_array()) {
            for (const auto& entry : entities->items()) {
                formats::SceneEntity entity;
                entity.entity_id = entry.get_string("id");
                entity.name = entry.get_string("name", entity.entity_id);
                entity.parent = entry.get_string("parent");
                entity.model_asset = resolve_asset_reference(context, entry.get_string("model"));
                entity.prefab = resolve_asset_reference(context, entry.get_string("prefab"));
                entity.visible = entry.get_bool("visible", true);
                entity.static_entity = entry.get_bool("static", false);
                if (!entity.model_asset.empty()) {
                    dependencies.push_back(
                        AssetDependency{entity.model_asset, true, "scene entity model"});
                }
                if (const core::json::Value* position = entry.find("position");
                    position != nullptr && position->is_array() && position->size() >= 3) {
                    entity.transform.position = formats::Vec3f{
                        static_cast<float>((*position)[0].as_number()),
                        static_cast<float>((*position)[1].as_number()),
                        static_cast<float>((*position)[2].as_number())};
                }
                if (const core::json::Value* rotation = entry.find("rotation");
                    rotation != nullptr && rotation->is_array() && rotation->size() >= 3) {
                    entity.transform.rotation_euler = formats::Vec3f{
                        static_cast<float>((*rotation)[0].as_number()),
                        static_cast<float>((*rotation)[1].as_number()),
                        static_cast<float>((*rotation)[2].as_number())};
                }
                if (const core::json::Value* scale = entry.find("scale");
                    scale != nullptr && scale->is_array() && scale->size() >= 3) {
                    entity.transform.scale = formats::Vec3f{
                        static_cast<float>((*scale)[0].as_number()),
                        static_cast<float>((*scale)[1].as_number()),
                        static_cast<float>((*scale)[2].as_number())};
                }
                if (const core::json::Value* components = entry.find("components");
                    components != nullptr && components->is_array()) {
                    for (const auto& component_json : components->items()) {
                        formats::EntityComponent component;
                        component.type = component_json.get_string("type");
                        if (const core::json::Value* data = component_json.find("data")) {
                            component.data_json = data->dump(0);
                        }
                        entity.components.push_back(std::move(component));
                    }
                }
                document.entities.push_back(std::move(entity));
            }
        }
        if (const core::json::Value* lights = root.find("lights");
            lights != nullptr && lights->is_array()) {
            for (const auto& entry : lights->items()) {
                formats::SceneLight light;
                light.light_id = entry.get_string("id");
                light.type = static_cast<formats::LightType>(entry.get_int("type", 1));
                light.intensity = static_cast<float>(entry.get_number("intensity", 1.0));
                light.radius = static_cast<float>(entry.get_number("radius", 5.0));
                light.cast_shadows = entry.get_bool("cast_shadows", false);
                light.baked = entry.get_bool("baked", false);
                if (const core::json::Value* position = entry.find("position");
                    position != nullptr && position->is_array() && position->size() >= 3) {
                    light.position = formats::Vec3f{static_cast<float>((*position)[0].as_number()),
                                                    static_cast<float>((*position)[1].as_number()),
                                                    static_cast<float>((*position)[2].as_number())};
                }
                document.lights.push_back(std::move(light));
            }
        }
        if (const core::json::Value* triggers = root.find("triggers");
            triggers != nullptr && triggers->is_array()) {
            for (const auto& entry : triggers->items()) {
                formats::TriggerVolume trigger;
                trigger.trigger_id = entry.get_string("id");
                trigger.kind = entry.get_string("kind", "mission");
                trigger.once = entry.get_bool("once", false);
                if (const core::json::Value* data = entry.find("payload")) {
                    trigger.payload_json = data->dump(0);
                }
                document.triggers.push_back(std::move(trigger));
            }
        }
        if (const core::json::Value* spawns = root.find("spawns");
            spawns != nullptr && spawns->is_array()) {
            for (const auto& entry : spawns->items()) {
                formats::SpawnPoint spawn;
                spawn.spawn_id = entry.get_string("id");
                spawn.kind = entry.get_string("kind", "player");
                document.spawns.push_back(std::move(spawn));
            }
        }
        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Scene;
        result.output_bytes = document.serialize();
        result.summary = std::to_string(document.entities.size()) + " entities, " +
                         std::to_string(document.lights.size()) + " lights, " +
                         std::to_string(document.triggers.size()) + " triggers";
        result.report_json = document.to_json();
        result.dependencies = std::move(dependencies);
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

// -------------------------------------------------------------- mission -----

class MissionImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.mission_json"; }
    [[nodiscard]] std::string display_name() const override { return "Bully mission source (.json)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".mission.json"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Mission; }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto parsed = load_json(context);
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        const auto& root = parsed.value();

        formats::MissionDocument document;
        document.asset_id = context.asset_id;
        document.name = root.get_string("name", stem_of(context.relative_source_path));
        document.description = root.get_string("description");
        document.giver_npc = root.get_string("giver");
        document.start_node = root.get_string("start_node");
        document.scene_asset = resolve_asset_reference(context, root.get_string("scene"));
        document.repeatable = root.get_bool("repeatable", false);
        document.timed = root.get_bool("timed", false);
        document.time_limit_seconds = static_cast<float>(root.get_number("time_limit", 0.0));

        std::vector<AssetDependency> dependencies;
        if (!document.scene_asset.empty()) {
            dependencies.push_back(AssetDependency{document.scene_asset, false, "mission scene"});
        }
        if (const core::json::Value* list = root.find("dependencies");
            list != nullptr && list->is_array()) {
            for (const auto& entry : list->items()) {
                const std::string dependency =
                    resolve_asset_reference(context, entry.as_string());
                if (dependency.empty()) continue;
                document.dependencies.push_back(dependency);
                dependencies.push_back(AssetDependency{dependency, true, "mission dependency"});
            }
        }
        if (const core::json::Value* objectives = root.find("objectives");
            objectives != nullptr && objectives->is_array()) {
            for (const auto& entry : objectives->items()) {
                formats::MissionObjective objective;
                objective.objective_id = entry.get_string("id");
                objective.description = entry.get_string("description");
                objective.kind = static_cast<formats::ObjectiveKind>(entry.get_int("kind", 0));
                objective.target = entry.get_string("target");
                objective.location = entry.get_string("location");
                objective.required_count =
                    static_cast<std::uint32_t>(entry.get_int("count", 1));
                objective.optional = entry.get_bool("optional", false);
                objective.hidden = entry.get_bool("hidden", false);
                objective.dialogue_line =
                    resolve_asset_reference(context, entry.get_string("dialogue"));
                objective.reward_item = entry.get_string("reward_item");
                objective.reward_currency =
                    static_cast<std::uint32_t>(entry.get_int("reward_currency", 0));
                document.objectives.push_back(std::move(objective));
            }
        }
        if (const core::json::Value* nodes = root.find("nodes");
            nodes != nullptr && nodes->is_array()) {
            for (const auto& entry : nodes->items()) {
                formats::MissionNode node;
                node.node_id = entry.get_string("id");
                node.objective_id = entry.get_string("objective");
                if (const core::json::Value* edges = entry.find("edges");
                    edges != nullptr && edges->is_array()) {
                    for (const auto& edge_json : edges->items()) {
                        formats::MissionEdge edge;
                        edge.to = edge_json.get_string("to");
                        edge.kind = static_cast<formats::EdgeKind>(edge_json.get_int("kind", 0));
                        edge.condition_id = edge_json.get_string("condition");
                        node.edges.push_back(std::move(edge));
                    }
                }
                document.nodes.push_back(std::move(node));
            }
        }
        if (const core::json::Value* conditions = root.find("conditions");
            conditions != nullptr && conditions->is_array()) {
            for (const auto& entry : conditions->items()) {
                formats::MissionCondition condition;
                condition.condition_id = entry.get_string("id");
                condition.kind = static_cast<formats::ConditionKind>(entry.get_int("kind", 0));
                condition.key = entry.get_string("key");
                condition.value = entry.get_string("value");
                condition.threshold = static_cast<float>(entry.get_number("threshold", 0.0));
                condition.negate = entry.get_bool("negate", false);
                document.conditions.push_back(std::move(condition));
            }
        }
        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        ImportResult result;
        result.asset_type = AssetType::Mission;
        result.output_bytes = document.serialize();
        result.summary = std::to_string(document.objectives.size()) + " objectives, " +
                         std::to_string(document.nodes.size()) + " nodes";
        result.report_json = document.to_json();
        result.dependencies = std::move(dependencies);
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

// ------------------------------------------------------------- dialogue -----

class DialogueImporter final : public Importer {
public:
    [[nodiscard]] std::string id() const override { return "com.bully.importer.dialogue_json"; }
    [[nodiscard]] std::string display_name() const override { return "Bully dialogue source (.json)"; }
    [[nodiscard]] std::string version() const override { return "1.0.0"; }
    [[nodiscard]] std::vector<std::string> extensions() const override { return {".dialogue.json"}; }
    [[nodiscard]] AssetType produces() const override { return AssetType::Dialogue; }

    [[nodiscard]] core::Result<ImportResult> import(const ImportContext& context) override
    {
        core::DiagnosticList diagnostics;
        auto parsed = load_json(context);
        if (!parsed) return core::Result<ImportResult>(parsed.diagnostics());
        const auto& root = parsed.value();

        formats::DialogueDocument document;
        document.asset_id = context.asset_id;
        document.name = root.get_string("name", stem_of(context.relative_source_path));
        document.language = root.get_string("language", "en");
        document.source_path = context.relative_source_path;
        document.entry_node = root.get_string("entry_node");
        document.metadata_json = context.settings.to_json().dump(0);

        std::vector<AssetDependency> dependencies;
        std::uint32_t missing_audio = 0;
        if (const core::json::Value* characters = root.find("characters");
            characters != nullptr && characters->is_array()) {
            for (const auto& entry : characters->items()) {
                formats::DialogueCharacter character;
                character.character_id = entry.get_string("id");
                character.display_name = entry.get_string("name", character.character_id);
                character.voice_actor = entry.get_string("voice_actor");
                character.color_hex = entry.get_string("color", "#FFFFFF");
                character.notes = entry.get_string("notes");
                document.characters.push_back(std::move(character));
            }
        }
        if (const core::json::Value* lines = root.find("lines");
            lines != nullptr && lines->is_array()) {
            for (const auto& entry : lines->items()) {
                formats::DialogueLine line;
                line.line_id = entry.get_string("id");
                line.character = entry.get_string("character");
                line.text = entry.get_string("text");
                line.language = entry.get_string("language", document.language);
                line.delivery = static_cast<formats::LineDelivery>(entry.get_int("delivery", 0));
                line.emotion_notes = entry.get_string("emotion");
                line.selected_take = entry.get_string("selected_take");
                line.audio_start = static_cast<float>(entry.get_number("audio_start", 0.0));
                line.audio_end = static_cast<float>(entry.get_number("audio_end", 0.0));
                line.localization =
                    static_cast<formats::LocalizationStatus>(entry.get_int("localization", 0));
                line.next_node = entry.get_string("next");
                if (const core::json::Value* subtitle = entry.find("subtitle")) {
                    line.subtitle.start = static_cast<float>(subtitle->get_number("start", line.audio_start));
                    line.subtitle.end = static_cast<float>(subtitle->get_number("end", line.audio_end));
                } else {
                    line.subtitle.start = line.audio_start;
                    line.subtitle.end = line.audio_end;
                }
                if (const core::json::Value* events = entry.find("events");
                    events != nullptr && events->is_array()) {
                    for (const auto& event : events->items()) {
                        line.playback_events.push_back(event.as_string());
                    }
                }
                if (const core::json::Value* takes = entry.find("takes");
                    takes != nullptr && takes->is_array()) {
                    for (const auto& take_json : takes->items()) {
                        formats::VoiceTake take;
                        take.take_id = take_json.get_string("id");
                        take.audio_asset =
                            resolve_asset_reference(context, take_json.get_string("audio"));
                        take.voice_actor = take_json.get_string("voice_actor");
                        take.recording_date = take_json.get_string("date");
                        take.notes = take_json.get_string("notes");
                        take.start = static_cast<float>(take_json.get_number("start", 0.0));
                        take.end = static_cast<float>(take_json.get_number("end", 0.0));
                        take.is_selected = take_json.get_bool("selected", false);
                        if (!take.audio_asset.empty()) {
                            dependencies.push_back(
                                AssetDependency{take.audio_asset, false, "dialogue voice take"});
                        } else {
                            ++missing_audio;
                        }
                        line.takes.push_back(std::move(take));
                    }
                }
                document.lines.push_back(std::move(line));
            }
        }
        if (const core::json::Value* nodes = root.find("nodes");
            nodes != nullptr && nodes->is_array()) {
            for (const auto& entry : nodes->items()) {
                formats::DialogueNode node;
                node.node_id = entry.get_string("id");
                node.kind = static_cast<formats::NodeKind>(entry.get_int("kind", 0));
                node.line_id = entry.get_string("line");
                node.condition_flag = entry.get_string("flag");
                node.condition_value = entry.get_string("value");
                node.true_target = entry.get_string("true");
                node.false_target = entry.get_string("false");
                node.next_target = entry.get_string("next");
                if (const core::json::Value* branches = entry.find("branches");
                    branches != nullptr && branches->is_array()) {
                    for (const auto& branch_json : branches->items()) {
                        formats::DialogueBranch branch;
                        branch.label = branch_json.get_string("label");
                        branch.target_node = branch_json.get_string("target");
                        branch.required_flag = branch_json.get_string("flag");
                        branch.required_value = branch_json.get_string("value");
                        node.branches.push_back(std::move(branch));
                    }
                }
                document.nodes.push_back(std::move(node));
            }
        }
        auto validation = document.validate();
        diagnostics.append(validation.diagnostics());
        if (missing_audio > 0) {
            diagnostics.warn("BULLY-DLG-0030",
                             std::to_string(missing_audio) +
                                 " voice take(s) have no audio assigned; the recording checklist "
                                 "will list them");
        }
        ImportResult result;
        result.asset_type = AssetType::Dialogue;
        result.output_bytes = document.serialize();
        result.summary = std::to_string(document.lines.size()) + " lines, " +
                         std::to_string(document.nodes.size()) + " nodes, " +
                         std::to_string(document.missing_recordings().size()) + " unrecorded";
        result.report_json = document.to_json();
        result.dependencies = std::move(dependencies);
        result.diagnostics = std::move(diagnostics);
        return result;
    }
};

}  // namespace

std::shared_ptr<Importer> make_material_importer() { return std::make_shared<MaterialImporter>(); }
std::shared_ptr<Importer> make_animation_importer() { return std::make_shared<AnimationImporter>(); }
std::shared_ptr<Importer> make_scene_importer() { return std::make_shared<SceneImporter>(); }
std::shared_ptr<Importer> make_mission_importer() { return std::make_shared<MissionImporter>(); }
std::shared_ptr<Importer> make_dialogue_importer() { return std::make_shared<DialogueImporter>(); }

}  // namespace bully::pipeline
