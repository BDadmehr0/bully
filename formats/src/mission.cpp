#include "bully/formats/mission.h"

#include <algorithm>
#include <set>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkObj = fourcc("OBJ ");
constexpr std::uint32_t kChunkNode = fourcc("NODE");
constexpr std::uint32_t kChunkCnd = fourcc("CND ");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_string_list(BinaryWriter& w, const std::vector<std::string>& values)
{
    w.u32(static_cast<std::uint32_t>(values.size()));
    for (const auto& value : values) w.string(value);
}

std::vector<std::string> read_string_list(BinaryReader& r, std::uint32_t limit = 4096)
{
    const std::uint32_t count = std::min<std::uint32_t>(r.u32(), limit);
    std::vector<std::string> values(count);
    for (auto& value : values) value = r.string();
    return values;
}

}  // namespace

std::string_view to_string(ObjectiveKind kind) noexcept
{
    switch (kind) {
        case ObjectiveKind::GoTo: return "goto";
        case ObjectiveKind::TalkTo: return "talk_to";
        case ObjectiveKind::Collect: return "collect";
        case ObjectiveKind::Defeat: return "defeat";
        case ObjectiveKind::Escort: return "escort";
        case ObjectiveKind::UseObject: return "use_object";
        case ObjectiveKind::Survive: return "survive";
        case ObjectiveKind::Custom: return "custom";
    }
    return "custom";
}

std::string_view to_string(ConditionKind kind) noexcept
{
    switch (kind) {
        case ConditionKind::FlagEquals: return "flag_equals";
        case ConditionKind::ObjectiveComplete: return "objective_complete";
        case ConditionKind::ObjectiveFailed: return "objective_failed";
        case ConditionKind::Timer: return "timer";
        case ConditionKind::PlayerStat: return "player_stat";
        case ConditionKind::Custom: return "custom";
    }
    return "custom";
}

const MissionObjective* MissionDocument::find_objective(std::string_view objective_id) const
{
    for (const auto& objective : objectives) {
        if (objective.objective_id == objective_id) return &objective;
    }
    return nullptr;
}

const MissionNode* MissionDocument::find_node(std::string_view node_id) const
{
    for (const auto& node : nodes) {
        if (node.node_id == node_id) return &node;
    }
    return nullptr;
}

std::vector<std::uint8_t> MissionDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(description);
        body.string(giver_npc);
        body.string(start_node);
        body.string(scene_asset);
        body.boolean(repeatable);
        body.boolean(timed);
        body.f32(time_limit_seconds);
        write_string_list(body, dependencies);
        write_string_list(body, completion_flags);
        write_string_list(body, failure_flags);
        body.u32(static_cast<std::uint32_t>(objectives.size()));
        body.u32(static_cast<std::uint32_t>(nodes.size()));
    });
    for (const auto& objective : objectives) {
        w.chunk(kChunkObj, [&](BinaryWriter& body) {
            body.string(objective.objective_id);
            body.string(objective.description);
            body.u8(static_cast<std::uint8_t>(objective.kind));
            body.string(objective.target);
            body.string(objective.location);
            body.u32(objective.required_count);
            body.boolean(objective.optional);
            body.boolean(objective.hidden);
            body.string(objective.dialogue_line);
            body.string(objective.reward_item);
            body.u32(objective.reward_currency);
        });
    }
    for (const auto& node : nodes) {
        w.chunk(kChunkNode, [&](BinaryWriter& body) {
            body.string(node.node_id);
            body.string(node.objective_id);
            write_string_list(body, node.gates);
            body.u32(static_cast<std::uint32_t>(node.edges.size()));
            for (const auto& edge : node.edges) {
                body.string(edge.to);
                body.u8(static_cast<std::uint8_t>(edge.kind));
                body.string(edge.condition_id);
            }
        });
    }
    if (!conditions.empty()) {
        w.chunk(kChunkCnd, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(conditions.size()));
            for (const auto& condition : conditions) {
                body.string(condition.condition_id);
                body.u8(static_cast<std::uint8_t>(condition.kind));
                body.string(condition.key);
                body.string(condition.value);
                body.f32(condition.threshold);
                body.boolean(condition.negate);
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(metadata_json); });
    return encode_document(kMissionFormat, 0, w.bytes());
}

core::Result<MissionDocument> MissionDocument::deserialize(
    const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kMissionFormat, bytes);
    if (!payload) return core::Result<MissionDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    MissionDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.description = reader.string();
            document.giver_npc = reader.string();
            document.start_node = reader.string();
            document.scene_asset = reader.string();
            document.repeatable = reader.boolean();
            document.timed = reader.boolean();
            document.time_limit_seconds = reader.f32();
            document.dependencies = read_string_list(reader);
            document.completion_flags = read_string_list(reader);
            document.failure_flags = read_string_list(reader);
            (void)reader.u32();
            (void)reader.u32();
        } else if (chunk_id == kChunkObj) {
            MissionObjective objective;
            objective.objective_id = reader.string();
            objective.description = reader.string();
            objective.kind = static_cast<ObjectiveKind>(reader.u8());
            objective.target = reader.string();
            objective.location = reader.string();
            objective.required_count = reader.u32();
            objective.optional = reader.boolean();
            objective.hidden = reader.boolean();
            objective.dialogue_line = reader.string();
            objective.reward_item = reader.string();
            objective.reward_currency = reader.u32();
            document.objectives.push_back(std::move(objective));
        } else if (chunk_id == kChunkNode) {
            MissionNode node;
            node.node_id = reader.string();
            node.objective_id = reader.string();
            node.gates = read_string_list(reader);
            const std::uint32_t edge_count = std::min<std::uint32_t>(reader.u32(), 256);
            node.edges.resize(edge_count);
            for (auto& edge : node.edges) {
                edge.to = reader.string();
                edge.kind = static_cast<EdgeKind>(reader.u8());
                edge.condition_id = reader.string();
            }
            document.nodes.push_back(std::move(node));
        } else if (chunk_id == kChunkCnd) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 4096);
            document.conditions.resize(count);
            for (auto& condition : document.conditions) {
                condition.condition_id = reader.string();
                condition.kind = static_cast<ConditionKind>(reader.u8());
                condition.key = reader.string();
                condition.value = reader.string();
                condition.threshold = reader.f32();
                condition.negate = reader.boolean();
            }
        } else if (chunk_id == kChunkMeta) {
            document.metadata_json = reader.string();
        } else {
            diagnostics.info("BULLY-MSN-0001",
                             "skipping unknown mission chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-MSN-0002", "failed to decode mission");
        return core::Result<MissionDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<MissionDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status MissionDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-MSN-0003", "mission has no asset id");
    if (name.empty()) diagnostics.warn("BULLY-MSN-0004", "mission has no display name", asset_id);

    std::set<std::string> objective_ids;
    for (const auto& objective : objectives) {
        if (objective.objective_id.empty()) {
            diagnostics.error("BULLY-MSN-0005", "mission contains an objective without an id", name);
            continue;
        }
        if (!objective_ids.insert(objective.objective_id).second) {
            diagnostics.error("BULLY-MSN-0006",
                              "duplicate objective id '" + objective.objective_id + "'", name);
        }
        if (objective.required_count == 0) {
            diagnostics.warn("BULLY-MSN-0007",
                             "objective '" + objective.objective_id + "' requires a count of zero", name);
        }
        if (objective.description.empty()) {
            diagnostics.warn("BULLY-MSN-0008",
                             "objective '" + objective.objective_id + "' has no description", name);
        }
    }
    if (objectives.empty()) diagnostics.warn("BULLY-MSN-0009", "mission has no objectives", name);

    std::set<std::string> node_ids;
    for (const auto& node : nodes) {
        if (node.node_id.empty()) {
            diagnostics.error("BULLY-MSN-0010", "mission contains a node without an id", name);
            continue;
        }
        if (!node_ids.insert(node.node_id).second) {
            diagnostics.error("BULLY-MSN-0011", "duplicate node id '" + node.node_id + "'", name);
        }
        if (!node.objective_id.empty() && objective_ids.find(node.objective_id) == objective_ids.end()) {
            diagnostics.error("BULLY-MSN-0012",
                              "node '" + node.node_id + "' references missing objective '" +
                                  node.objective_id + "'",
                              name);
        }
    }
    std::set<std::string> condition_ids;
    for (const auto& condition : conditions) {
        condition_ids.insert(condition.condition_id);
    }
    for (const auto& node : nodes) {
        for (const auto& edge : node.edges) {
            if (node_ids.find(edge.to) == node_ids.end()) {
                diagnostics.error("BULLY-MSN-0013",
                                  "node '" + node.node_id + "' links to missing node '" + edge.to + "'",
                                  name);
            }
            if (!edge.condition_id.empty() && condition_ids.find(edge.condition_id) == condition_ids.end()) {
                diagnostics.error("BULLY-MSN-0014",
                                  "node '" + node.node_id + "' edge is gated by unknown condition '" +
                                      edge.condition_id + "'",
                                  name);
            }
        }
        for (const auto& gate : node.gates) {
            if (condition_ids.find(gate) == condition_ids.end()) {
                diagnostics.error("BULLY-MSN-0015",
                                  "node '" + node.node_id + "' has unknown gate '" + gate + "'", name);
            }
        }
    }
    if (!start_node.empty() && node_ids.find(start_node) == node_ids.end()) {
        diagnostics.error("BULLY-MSN-0016", "start node '" + start_node + "' does not exist", name);
    }
    // Reachability: every node must be reachable from start_node.
    if (!start_node.empty() && node_ids.count(start_node) != 0) {
        std::set<std::string> visited{start_node};
        std::vector<std::string> frontier{start_node};
        while (!frontier.empty()) {
            const std::string current = frontier.back();
            frontier.pop_back();
            const MissionNode* node = find_node(current);
            if (node == nullptr) continue;
            for (const auto& edge : node->edges) {
                if (visited.insert(edge.to).second) frontier.push_back(edge.to);
            }
        }
        for (const auto& node : nodes) {
            if (visited.find(node.node_id) == visited.end()) {
                diagnostics.warn("BULLY-MSN-0017",
                                 "node '" + node.node_id + "' is unreachable from the start node", name);
            }
        }
    }
    if (timed && time_limit_seconds <= 0.0f) {
        diagnostics.error("BULLY-MSN-0018", "timed mission has no positive time limit", name);
    }
    for (const auto& dependency : dependencies) {
        if (dependency == asset_id) {
            diagnostics.error("BULLY-MSN-0019", "mission depends on itself", name);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string MissionDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "bmission";
    root["version"] = kMissionFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["description"] = description;
    root["giver"] = giver_npc;
    root["start_node"] = start_node;
    root["scene"] = scene_asset;
    root["repeatable"] = repeatable;
    root["timed"] = timed;
    root["time_limit_seconds"] = static_cast<double>(time_limit_seconds);

    Value dependencies_json = Value::make_array();
    for (const auto& dependency : dependencies) dependencies_json.push(dependency);
    root["dependencies"] = std::move(dependencies_json);

    Value objectives_json = Value::make_array();
    for (const auto& objective : objectives) {
        Value entry = Value::make_object();
        entry["id"] = objective.objective_id;
        entry["description"] = objective.description;
        entry["kind"] = std::string(to_string(objective.kind));
        entry["target"] = objective.target;
        entry["count"] = static_cast<std::uint64_t>(objective.required_count);
        entry["optional"] = objective.optional;
        objectives_json.push(std::move(entry));
    }
    root["objectives"] = std::move(objectives_json);

    Value nodes_json = Value::make_array();
    for (const auto& node : nodes) {
        Value entry = Value::make_object();
        entry["id"] = node.node_id;
        entry["objective"] = node.objective_id;
        Value edges_json = Value::make_array();
        for (const auto& edge : node.edges) {
            Value edge_json = Value::make_object();
            edge_json["to"] = edge.to;
            edge_json["kind"] = static_cast<std::int64_t>(edge.kind);
            edges_json.push(std::move(edge_json));
        }
        entry["edges"] = std::move(edges_json);
        nodes_json.push(std::move(entry));
    }
    root["nodes"] = std::move(nodes_json);
    root["condition_count"] = static_cast<std::uint64_t>(conditions.size());
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
