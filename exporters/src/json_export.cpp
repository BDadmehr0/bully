// Authoring-JSON exporters: compiled assets -> the same JSON shape the
// importers accept, so an export/import cycle is lossless for authored content.

#include <cstdint>
#include <string>
#include <utility>
#include "bully/core/json.h"
#include "bully/exporters/exporters.h"

namespace bully::exporters {

namespace {

core::json::Value vec3(const formats::Vec3f& value)
{
    core::json::Value array = core::json::Value::make_array();
    array.push(value.x);
    array.push(value.y);
    array.push(value.z);
    return array;
}

}  // namespace

std::string encode_scene_json(const formats::SceneDocument& scene)
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format_version"] = "1.0.0";
    root["name"] = scene.name;
    root["source_path"] = scene.source_path;

    Value environment = Value::make_object();
    environment["fog_enabled"] = scene.environment.fog_enabled;
    environment["fog_start"] = scene.environment.fog_start;
    environment["fog_end"] = scene.environment.fog_end;
    environment["time_of_day"] = scene.environment.time_of_day;
    environment["skybox"] = scene.environment.skybox_texture;
    environment["lightmap_resolution"] =
        static_cast<std::int64_t>(scene.environment.lightmap_resolution);
    root["environment"] = std::move(environment);

    Value entities = Value::make_array();
    for (const auto& entity : scene.entities) {
        Value entry = Value::make_object();
        entry["id"] = entity.entity_id;
        entry["name"] = entity.name;
        entry["model"] = entity.model_asset;
        entry["prefab"] = entity.prefab;
        entry["parent"] = entity.parent;
        entry["position"] = vec3(entity.transform.position);
        entry["rotation"] = vec3(entity.transform.rotation_euler);
        entry["scale"] = vec3(entity.transform.scale);
        entry["visible"] = entity.visible;
        entry["static"] = entity.static_entity;
        if (!entity.components.empty()) {
            Value components = Value::make_array();
            for (const auto& component : entity.components) {
                Value item = Value::make_object();
                item["type"] = component.type;
                auto data = core::json::parse(component.data_json);
                item["data"] = data ? data.value() : Value::make_object();
                components.push(std::move(item));
            }
            entry["components"] = std::move(components);
        }
        entities.push(std::move(entry));
    }
    root["entities"] = std::move(entities);

    Value lights = Value::make_array();
    for (const auto& light : scene.lights) {
        Value entry = Value::make_object();
        entry["id"] = light.light_id;
        entry["type"] = static_cast<std::int64_t>(light.type);
        entry["position"] = vec3(light.position);
        entry["direction"] = vec3(light.direction);
        entry["color"] = vec3(light.color);
        entry["intensity"] = light.intensity;
        entry["radius"] = light.radius;
        entry["cast_shadows"] = light.cast_shadows;
        entry["baked"] = light.baked;
        lights.push(std::move(entry));
    }
    root["lights"] = std::move(lights);

    Value triggers = Value::make_array();
    for (const auto& trigger : scene.triggers) {
        Value entry = Value::make_object();
        entry["id"] = trigger.trigger_id;
        entry["kind"] = trigger.kind;
        entry["center"] = vec3(trigger.center);
        entry["extents"] = vec3(trigger.extents);
        entry["once"] = trigger.once;
        auto payload = core::json::parse(trigger.payload_json);
        entry["payload"] = payload ? payload.value() : Value::make_object();
        triggers.push(std::move(entry));
    }
    root["triggers"] = std::move(triggers);

    Value spawns = Value::make_array();
    for (const auto& spawn : scene.spawns) {
        Value entry = Value::make_object();
        entry["id"] = spawn.spawn_id;
        entry["kind"] = spawn.kind;
        entry["position"] = vec3(spawn.position);
        entry["model"] = spawn.model_asset;
        spawns.push(std::move(entry));
    }
    root["spawns"] = std::move(spawns);
    return root.dump(2);
}

std::string encode_mission_json(const formats::MissionDocument& mission)
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format_version"] = "1.0.0";
    root["name"] = mission.name;
    root["description"] = mission.description;
    root["giver"] = mission.giver_npc;
    root["scene"] = mission.scene_asset;
    root["start_node"] = mission.start_node;
    root["repeatable"] = mission.repeatable;
    root["timed"] = mission.timed;
    root["time_limit"] = mission.time_limit_seconds;

    Value dependencies = Value::make_array();
    for (const auto& dependency : mission.dependencies) dependencies.push(dependency);
    root["dependencies"] = std::move(dependencies);

    Value objectives = Value::make_array();
    for (const auto& objective : mission.objectives) {
        Value entry = Value::make_object();
        entry["id"] = objective.objective_id;
        entry["description"] = objective.description;
        entry["kind"] = static_cast<std::int64_t>(objective.kind);
        entry["target"] = objective.target;
        entry["location"] = objective.location;
        entry["count"] = static_cast<std::int64_t>(objective.required_count);
        entry["optional"] = objective.optional;
        entry["hidden"] = objective.hidden;
        entry["dialogue"] = objective.dialogue_line;
        entry["reward_item"] = objective.reward_item;
        entry["reward_currency"] = static_cast<std::int64_t>(objective.reward_currency);
        objectives.push(std::move(entry));
    }
    root["objectives"] = std::move(objectives);

    Value nodes = Value::make_array();
    for (const auto& node : mission.nodes) {
        Value entry = Value::make_object();
        entry["id"] = node.node_id;
        entry["objective"] = node.objective_id;
        Value edges = Value::make_array();
        for (const auto& edge : node.edges) {
            Value edge_json = Value::make_object();
            edge_json["to"] = edge.to;
            edge_json["kind"] = static_cast<std::int64_t>(edge.kind);
            edge_json["condition"] = edge.condition_id;
            edges.push(std::move(edge_json));
        }
        entry["edges"] = std::move(edges);
        nodes.push(std::move(entry));
    }
    root["nodes"] = std::move(nodes);

    Value conditions = Value::make_array();
    for (const auto& condition : mission.conditions) {
        Value entry = Value::make_object();
        entry["id"] = condition.condition_id;
        entry["kind"] = static_cast<std::int64_t>(condition.kind);
        entry["key"] = condition.key;
        entry["value"] = condition.value;
        entry["threshold"] = condition.threshold;
        entry["negate"] = condition.negate;
        conditions.push(std::move(entry));
    }
    root["conditions"] = std::move(conditions);
    return root.dump(2);
}

std::string encode_dialogue_json(const formats::DialogueDocument& dialogue)
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format_version"] = "1.0.0";
    root["name"] = dialogue.name;
    root["language"] = dialogue.language;
    root["entry_node"] = dialogue.entry_node;

    Value characters = Value::make_array();
    for (const auto& character : dialogue.characters) {
        Value entry = Value::make_object();
        entry["id"] = character.character_id;
        entry["name"] = character.display_name;
        entry["voice_actor"] = character.voice_actor;
        entry["color"] = character.color_hex;
        characters.push(std::move(entry));
    }
    root["characters"] = std::move(characters);

    Value lines = Value::make_array();
    for (const auto& line : dialogue.lines) {
        Value entry = Value::make_object();
        entry["id"] = line.line_id;
        entry["character"] = line.character;
        entry["text"] = line.text;
        entry["language"] = line.language;
        entry["delivery"] = static_cast<std::int64_t>(line.delivery);
        entry["emotion"] = line.emotion_notes;
        entry["selected_take"] = line.selected_take;
        entry["audio_start"] = line.audio_start;
        entry["audio_end"] = line.audio_end;
        entry["next"] = line.next_node;
        Value subtitle = Value::make_object();
        subtitle["start"] = line.subtitle.start;
        subtitle["end"] = line.subtitle.end;
        entry["subtitle"] = std::move(subtitle);
        Value takes = Value::make_array();
        for (const auto& take : line.takes) {
            Value take_json = Value::make_object();
            take_json["id"] = take.take_id;
            take_json["audio"] = take.audio_asset;
            take_json["voice_actor"] = take.voice_actor;
            take_json["date"] = take.recording_date;
            take_json["notes"] = take.notes;
            take_json["start"] = take.start;
            take_json["end"] = take.end;
            take_json["selected"] = take.is_selected;
            takes.push(std::move(take_json));
        }
        entry["takes"] = std::move(takes);
        lines.push(std::move(entry));
    }
    root["lines"] = std::move(lines);

    Value nodes = Value::make_array();
    for (const auto& node : dialogue.nodes) {
        Value entry = Value::make_object();
        entry["id"] = node.node_id;
        entry["kind"] = static_cast<std::int64_t>(node.kind);
        entry["line"] = node.line_id;
        entry["flag"] = node.condition_flag;
        entry["value"] = node.condition_value;
        entry["true"] = node.true_target;
        entry["false"] = node.false_target;
        entry["next"] = node.next_target;
        if (!node.branches.empty()) {
            Value branches = Value::make_array();
            for (const auto& branch : node.branches) {
                Value branch_json = Value::make_object();
                branch_json["label"] = branch.label;
                branch_json["target"] = branch.target_node;
                branch_json["flag"] = branch.required_flag;
                branch_json["value"] = branch.required_value;
                branches.push(std::move(branch_json));
            }
            entry["branches"] = std::move(branches);
        }
        nodes.push(std::move(entry));
    }
    root["nodes"] = std::move(nodes);
    return root.dump(2);
}

std::string encode_animation_json(const formats::AnimationDocument& animation)
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format_version"] = "1.0.0";
    root["name"] = animation.name;
    root["clip"] = animation.clip_name;
    root["duration"] = animation.duration;
    root["sample_rate"] = animation.sample_rate;
    root["loop"] = static_cast<std::int64_t>(animation.loop);
    root["source_path"] = animation.source_path;

    Value root_motion = Value::make_object();
    root_motion["enabled"] = animation.root_motion.enabled;
    root_motion["bone"] = animation.root_motion.bone;
    root_motion["strips_translation"] = animation.root_motion.strips_translation;
    root_motion["strips_rotation"] = animation.root_motion.strips_rotation;
    root["root_motion"] = std::move(root_motion);

    Value bones = Value::make_array();
    for (const auto& bone : animation.bones) {
        Value entry = Value::make_object();
        entry["name"] = bone.name;
        entry["parent"] = static_cast<std::int64_t>(bone.parent);
        entry["rest_translation"] = vec3(bone.rest_translation);
        Value rotation = Value::make_array();
        rotation.push(bone.rest_rotation.x);
        rotation.push(bone.rest_rotation.y);
        rotation.push(bone.rest_rotation.z);
        rotation.push(bone.rest_rotation.w);
        entry["rest_rotation"] = std::move(rotation);
        entry["rest_scale"] = vec3(bone.rest_scale);
        bones.push(std::move(entry));
    }
    root["bones"] = std::move(bones);

    Value tracks = Value::make_array();
    for (const auto& track : animation.tracks) {
        Value entry = Value::make_object();
        entry["bone"] = track.bone_name;
        entry["interpolation"] = static_cast<std::int64_t>(track.interpolation);
        Value translations = Value::make_array();
        for (const auto& key : track.translations) {
            Value key_json = Value::make_object();
            key_json["time"] = key.time;
            key_json["value"] = vec3(key.value);
            translations.push(std::move(key_json));
        }
        entry["translations"] = std::move(translations);
        Value rotations = Value::make_array();
        for (const auto& key : track.rotations) {
            Value key_json = Value::make_object();
            key_json["time"] = key.time;
            Value value = Value::make_array();
            value.push(key.value.x);
            value.push(key.value.y);
            value.push(key.value.z);
            value.push(key.value.w);
            key_json["value"] = std::move(value);
            rotations.push(std::move(key_json));
        }
        entry["rotations"] = std::move(rotations);
        Value scales = Value::make_array();
        for (const auto& key : track.scales) {
            Value key_json = Value::make_object();
            key_json["time"] = key.time;
            key_json["value"] = vec3(key.value);
            scales.push(std::move(key_json));
        }
        entry["scales"] = std::move(scales);
        tracks.push(std::move(entry));
    }
    root["tracks"] = std::move(tracks);

    Value facial = Value::make_array();
    for (const auto& track : animation.facial_tracks) {
        Value entry = Value::make_object();
        entry["target"] = track.target;
        entry["interpolation"] = static_cast<std::int64_t>(track.interpolation);
        Value weights = Value::make_array();
        for (const auto& key : track.weights) {
            Value key_json = Value::make_object();
            key_json["time"] = key.time;
            key_json["weight"] = key.value.x;
            weights.push(std::move(key_json));
        }
        entry["weights"] = std::move(weights);
        facial.push(std::move(entry));
    }
    root["facial"] = std::move(facial);

    Value events = Value::make_array();
    for (const auto& event : animation.events) {
        Value entry = Value::make_object();
        entry["name"] = event.name;
        entry["time"] = event.time;
        entry["payload"] = event.payload;
        events.push(std::move(entry));
    }
    root["events"] = std::move(events);
    return root.dump(2);
}

std::string encode_material_json(const formats::MaterialDocument& material)
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format_version"] = "1.0.0";
    root["name"] = material.name;
    root["shader_preset"] = material.shader_preset;
    root["parent"] = material.parent_material;

    Value textures = Value::make_array();
    for (const auto& binding : material.textures) {
        Value entry = Value::make_object();
        entry["slot"] = std::string(formats::to_string(binding.slot));
        entry["texture"] = binding.texture_asset;
        entry["sampler"] = binding.sampler_override;
        entry["uv_scale"] = Value::make_array();
        entry["uv_scale"].push(binding.uv_scale_u);
        entry["uv_scale"].push(binding.uv_scale_v);
        textures.push(std::move(entry));
    }
    root["textures"] = std::move(textures);

    Value parameters = Value::make_array();
    for (const auto& parameter : material.parameters) {
        Value entry = Value::make_object();
        entry["name"] = parameter.name;
        entry["type"] = static_cast<std::int64_t>(parameter.type);
        // Parameters are up to four floats; keep all four so a round-trip never
        // silently truncates a vec3/colour.
        Value values = Value::make_array();
        for (float value : parameter.values) values.push(value);
        entry["values"] = std::move(values);
        parameters.push(std::move(entry));
    }
    root["parameters"] = std::move(parameters);

    Value state = Value::make_object();
    state["blend"] = static_cast<std::int64_t>(material.state.blend);
    state["cull"] = static_cast<std::int64_t>(material.state.cull);
    state["depth_test"] = material.state.depth_test;
    state["depth_write"] = material.state.depth_write;
    state["alpha_cutoff"] = material.state.alpha_cutoff;
    root["render_state"] = std::move(state);
    return root.dump(2);
}

}  // namespace bully::exporters
