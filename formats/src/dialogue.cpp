#include "bully/formats/dialogue.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/json.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkChrs = fourcc("CHRS");
constexpr std::uint32_t kChunkLine = fourcc("LINE");
constexpr std::uint32_t kChunkNode = fourcc("NODE");
constexpr std::uint32_t kChunkFlag = fourcc("FLAG");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_take(BinaryWriter& w, const VoiceTake& take)
{
    w.string(take.take_id);
    w.string(take.audio_asset);
    w.string(take.voice_actor);
    w.string(take.recording_date);
    w.string(take.notes);
    w.f32(take.start);
    w.f32(take.end);
    w.f32(take.loudness_lufs);
    w.boolean(take.is_selected);
}

VoiceTake read_take(BinaryReader& r)
{
    VoiceTake take;
    take.take_id = r.string();
    take.audio_asset = r.string();
    take.voice_actor = r.string();
    take.recording_date = r.string();
    take.notes = r.string();
    take.start = r.f32();
    take.end = r.f32();
    take.loudness_lufs = r.f32();
    take.is_selected = r.boolean();
    return take;
}

void write_line(BinaryWriter& w, const DialogueLine& line)
{
    w.string(line.line_id);
    w.string(line.character);
    w.string(line.text);
    w.string(line.language);
    w.u8(static_cast<std::uint8_t>(line.delivery));
    w.string(line.emotion_notes);
    w.u32(static_cast<std::uint32_t>(line.takes.size()));
    for (const auto& take : line.takes) write_take(w, take);
    w.string(line.selected_take);
    w.f32(line.subtitle.start);
    w.f32(line.subtitle.end);
    w.boolean(line.subtitle.auto_generated);
    w.f32(line.audio_start);
    w.f32(line.audio_end);
    w.u8(static_cast<std::uint8_t>(line.localization));
    w.u32(static_cast<std::uint32_t>(line.playback_events.size()));
    for (const auto& event : line.playback_events) w.string(event);
    w.string(line.next_node);
}

DialogueLine read_line(BinaryReader& r)
{
    DialogueLine line;
    line.line_id = r.string();
    line.character = r.string();
    line.text = r.string();
    line.language = r.string();
    line.delivery = static_cast<LineDelivery>(r.u8());
    line.emotion_notes = r.string();
    const std::uint32_t take_count = std::min<std::uint32_t>(r.u32(), 256);
    line.takes.resize(take_count);
    for (auto& take : line.takes) take = read_take(r);
    line.selected_take = r.string();
    line.subtitle.start = r.f32();
    line.subtitle.end = r.f32();
    line.subtitle.auto_generated = r.boolean();
    line.audio_start = r.f32();
    line.audio_end = r.f32();
    line.localization = static_cast<LocalizationStatus>(r.u8());
    const std::uint32_t event_count = std::min<std::uint32_t>(r.u32(), 1024);
    line.playback_events.resize(event_count);
    for (auto& event : line.playback_events) event = r.string();
    line.next_node = r.string();
    return line;
}

void write_node(BinaryWriter& w, const DialogueNode& node)
{
    w.string(node.node_id);
    w.u8(static_cast<std::uint8_t>(node.kind));
    w.string(node.line_id);
    w.string(node.condition_flag);
    w.string(node.condition_value);
    w.string(node.true_target);
    w.string(node.false_target);
    w.string(node.next_target);
    w.u32(static_cast<std::uint32_t>(node.branches.size()));
    for (const auto& branch : node.branches) {
        w.string(branch.label);
        w.string(branch.target_node);
        w.string(branch.required_flag);
        w.string(branch.required_value);
    }
}

DialogueNode read_node(BinaryReader& r)
{
    DialogueNode node;
    node.node_id = r.string();
    node.kind = static_cast<NodeKind>(r.u8());
    node.line_id = r.string();
    node.condition_flag = r.string();
    node.condition_value = r.string();
    node.true_target = r.string();
    node.false_target = r.string();
    node.next_target = r.string();
    const std::uint32_t branch_count = std::min<std::uint32_t>(r.u32(), 512);
    node.branches.resize(branch_count);
    for (auto& branch : node.branches) {
        branch.label = r.string();
        branch.target_node = r.string();
        branch.required_flag = r.string();
        branch.required_value = r.string();
    }
    return node;
}

}  // namespace

std::string_view to_string(LocalizationStatus status) noexcept
{
    switch (status) {
        case LocalizationStatus::Missing: return "missing";
        case LocalizationStatus::Draft: return "draft";
        case LocalizationStatus::Translated: return "translated";
        case LocalizationStatus::Reviewed: return "reviewed";
        case LocalizationStatus::Final: return "final";
    }
    return "missing";
}

std::string_view to_string(LineDelivery delivery) noexcept
{
    switch (delivery) {
        case LineDelivery::Neutral: return "neutral";
        case LineDelivery::Angry: return "angry";
        case LineDelivery::Happy: return "happy";
        case LineDelivery::Sad: return "sad";
        case LineDelivery::Fearful: return "fearful";
        case LineDelivery::Sarcastic: return "sarcastic";
        case LineDelivery::Whisper: return "whisper";
        case LineDelivery::Shout: return "shout";
        case LineDelivery::Custom: return "custom";
    }
    return "neutral";
}

const VoiceTake* DialogueLine::selected() const
{
    for (const auto& take : takes) {
        if (!selected_take.empty() && take.take_id == selected_take) return &take;
    }
    return nullptr;
}

const DialogueLine* DialogueDocument::find_line(std::string_view line_id) const
{
    for (const auto& line : lines) {
        if (line.line_id == line_id) return &line;
    }
    return nullptr;
}

const DialogueNode* DialogueDocument::find_node(std::string_view node_id) const
{
    for (const auto& node : nodes) {
        if (node.node_id == node_id) return &node;
    }
    return nullptr;
}

float DialogueDocument::total_audio_seconds() const
{
    float total = 0.0f;
    for (const auto& line : lines) {
        total += std::max(0.0f, line.audio_end - line.audio_start);
    }
    return total;
}

std::vector<std::string> DialogueDocument::missing_recordings() const
{
    std::vector<std::string> missing;
    for (const auto& line : lines) {
        if (line.text.empty()) continue;
        const VoiceTake* take = line.selected();
        if (take == nullptr || take->audio_asset.empty()) missing.push_back(line.line_id);
    }
    return missing;
}

std::vector<std::string> DialogueDocument::subtitle_drift(float tolerance) const
{
    std::vector<std::string> drifting;
    for (const auto& line : lines) {
        const VoiceTake* take = line.selected();
        if (take == nullptr) continue;
        const float audio_length = std::max(0.0f, line.audio_end - line.audio_start);
        const float subtitle_length = line.subtitle.end - line.subtitle.start;
        if (std::fabs(audio_length - subtitle_length) > tolerance ||
            std::fabs(line.subtitle.start - line.audio_start) > tolerance) {
            drifting.push_back(line.line_id);
        }
    }
    return drifting;
}

std::vector<std::uint8_t> DialogueDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(language);
        body.string(source_path);
        body.string(entry_node);
        body.u32(static_cast<std::uint32_t>(lines.size()));
        body.u32(static_cast<std::uint32_t>(nodes.size()));
        body.u32(static_cast<std::uint32_t>(characters.size()));
    });
    w.chunk(kChunkChrs, [&](BinaryWriter& body) {
        body.u32(static_cast<std::uint32_t>(characters.size()));
        for (const auto& character : characters) {
            body.string(character.character_id);
            body.string(character.display_name);
            body.string(character.voice_actor);
            body.string(character.color_hex);
            body.string(character.notes);
        }
    });
    for (const auto& line : lines) {
        w.chunk(kChunkLine, [&](BinaryWriter& body) { write_line(body, line); });
    }
    for (const auto& node : nodes) {
        w.chunk(kChunkNode, [&](BinaryWriter& body) { write_node(body, node); });
    }
    if (!flags.empty()) {
        w.chunk(kChunkFlag, [&](BinaryWriter& body) {
            body.u32(static_cast<std::uint32_t>(flags.size()));
            for (const auto& flag : flags) {
                body.string(flag.flag);
                body.string(flag.written_value);
                body.string(flag.node_id);
            }
        });
    }
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(metadata_json); });
    return encode_document(kDialogueFormat, 0, w.bytes());
}

core::Result<DialogueDocument> DialogueDocument::deserialize(
    const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kDialogueFormat, bytes);
    if (!payload) return core::Result<DialogueDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    DialogueDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.language = reader.string();
            document.source_path = reader.string();
            document.entry_node = reader.string();
            (void)reader.u32();
            (void)reader.u32();
            (void)reader.u32();
        } else if (chunk_id == kChunkChrs) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 4096);
            document.characters.resize(count);
            for (auto& character : document.characters) {
                character.character_id = reader.string();
                character.display_name = reader.string();
                character.voice_actor = reader.string();
                character.color_hex = reader.string();
                character.notes = reader.string();
            }
        } else if (chunk_id == kChunkLine) {
            document.lines.push_back(read_line(reader));
        } else if (chunk_id == kChunkNode) {
            document.nodes.push_back(read_node(reader));
        } else if (chunk_id == kChunkFlag) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 8192);
            document.flags.resize(count);
            for (auto& flag : document.flags) {
                flag.flag = reader.string();
                flag.written_value = reader.string();
                flag.node_id = reader.string();
            }
        } else if (chunk_id == kChunkMeta) {
            document.metadata_json = reader.string();
        } else {
            diagnostics.info("BULLY-DLG-0001",
                             "skipping unknown dialogue chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-DLG-0002", "failed to decode dialogue");
        return core::Result<DialogueDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<DialogueDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status DialogueDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-DLG-0003", "dialogue has no asset id");
    if (lines.empty()) diagnostics.warn("BULLY-DLG-0004", "dialogue contains no lines", name);

    std::set<std::string> line_ids;
    for (const auto& line : lines) {
        if (line.line_id.empty()) {
            diagnostics.error("BULLY-DLG-0005", "dialogue contains a line without an id", name);
            continue;
        }
        if (!line_ids.insert(line.line_id).second) {
            diagnostics.error("BULLY-DLG-0006", "duplicate line id '" + line.line_id + "'", name);
        }
        if (line.character.empty() && line.text.empty()) {
            diagnostics.warn("BULLY-DLG-0007",
                             "line '" + line.line_id + "' has neither speaker nor text", name);
        }
        if (!line.selected_take.empty()) {
            const bool found = std::any_of(line.takes.begin(), line.takes.end(),
                                           [&](const VoiceTake& take) {
                                               return take.take_id == line.selected_take;
                                           });
            if (!found) {
                diagnostics.error("BULLY-DLG-0008",
                                  "line '" + line.line_id + "' selects unknown take '" +
                                      line.selected_take + "'",
                                  name);
            }
        }
        if (line.subtitle.end < line.subtitle.start) {
            diagnostics.error("BULLY-DLG-0009",
                              "line '" + line.line_id + "' has inverted subtitle timing", name);
        }
        if (line.audio_end < line.audio_start) {
            diagnostics.error("BULLY-DLG-0010",
                              "line '" + line.line_id + "' has inverted audio timing", name);
        }
    }

    std::set<std::string> character_ids;
    for (const auto& character : characters) {
        if (character.character_id.empty()) {
            diagnostics.warn("BULLY-DLG-0011", "dialogue contains a character without an id", name);
        } else if (!character_ids.insert(character.character_id).second) {
            diagnostics.error("BULLY-DLG-0012",
                              "duplicate character id '" + character.character_id + "'", name);
        }
    }
    for (const auto& line : lines) {
        if (!line.character.empty() && !character_ids.empty() &&
            character_ids.find(line.character) == character_ids.end()) {
            diagnostics.warn("BULLY-DLG-0013",
                             "line '" + line.line_id + "' references unknown character '" +
                                 line.character + "'",
                             name);
        }
    }

    std::set<std::string> node_ids;
    for (const auto& node : nodes) {
        if (node.node_id.empty()) {
            diagnostics.error("BULLY-DLG-0014", "dialogue contains a node without an id", name);
            continue;
        }
        if (!node_ids.insert(node.node_id).second) {
            diagnostics.error("BULLY-DLG-0015", "duplicate node id '" + node.node_id + "'", name);
        }
    }
    auto check_target = [&](const std::string& target, const std::string& origin) {
        if (target.empty()) return;
        if (node_ids.find(target) == node_ids.end()) {
            diagnostics.error("BULLY-DLG-0016",
                              "node '" + origin + "' links to missing node '" + target + "'", name);
        }
    };
    for (const auto& node : nodes) {
        check_target(node.next_target, node.node_id);
        check_target(node.true_target, node.node_id);
        check_target(node.false_target, node.node_id);
        for (const auto& branch : node.branches) {
            check_target(branch.target_node, node.node_id);
        }
        if (node.kind == NodeKind::Choice && node.branches.empty()) {
            diagnostics.warn("BULLY-DLG-0017",
                             "choice node '" + node.node_id + "' has no branches", name);
        }
        if (node.kind == NodeKind::Choice && !node.branches.empty()) {
            const bool any_unconditional = std::any_of(
                node.branches.begin(), node.branches.end(),
                [](const DialogueBranch& branch) { return branch.required_flag.empty(); });
            if (!any_unconditional) {
                diagnostics.warn("BULLY-DLG-0018",
                                 "choice node '" + node.node_id +
                                     "' only offers conditional branches; players may get stuck",
                                 name);
            }
        }
        if (!node.line_id.empty() && line_ids.find(node.line_id) == line_ids.end()) {
            diagnostics.error("BULLY-DLG-0019",
                              "node '" + node.node_id + "' references missing line '" + node.line_id +
                                  "'",
                              name);
        }
    }
    if (!entry_node.empty() && node_ids.find(entry_node) == node_ids.end()) {
        diagnostics.error("BULLY-DLG-0020", "entry node '" + entry_node + "' does not exist", name);
    }
    for (const auto& line : lines) {
        if (!line.next_node.empty() && node_ids.find(line.next_node) == node_ids.end()) {
            diagnostics.warn("BULLY-DLG-0021",
                             "line '" + line.line_id + "' continues into missing node '" +
                                 line.next_node + "'",
                             name);
        }
    }

    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    auto status = core::Status{};
    status.attach_diagnostics(std::move(diagnostics));
    return status;
}

std::string DialogueDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "bdialogue";
    root["version"] = kDialogueFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["language"] = language;
    root["entry_node"] = entry_node;
    root["line_count"] = static_cast<std::uint64_t>(lines.size());
    root["node_count"] = static_cast<std::uint64_t>(nodes.size());
    root["total_audio_seconds"] = static_cast<double>(total_audio_seconds());

    Value missing = Value::make_array();
    for (const auto& line_id : missing_recordings()) missing.push(line_id);
    root["missing_recordings"] = std::move(missing);

    Value drift = Value::make_array();
    for (const auto& line_id : subtitle_drift()) drift.push(line_id);
    root["subtitle_drift"] = std::move(drift);

    Value characters_json = Value::make_array();
    for (const auto& character : characters) {
        Value entry = Value::make_object();
        entry["id"] = character.character_id;
        entry["name"] = character.display_name;
        entry["voice_actor"] = character.voice_actor;
        characters_json.push(std::move(entry));
    }
    root["characters"] = std::move(characters_json);

    Value lines_json = Value::make_array();
    for (const auto& line : lines) {
        Value entry = Value::make_object();
        entry["id"] = line.line_id;
        entry["character"] = line.character;
        entry["text"] = line.text;
        entry["delivery"] = std::string(to_string(line.delivery));
        entry["takes"] = static_cast<std::uint64_t>(line.takes.size());
        entry["selected_take"] = line.selected_take;
        entry["audio_start"] = static_cast<double>(line.audio_start);
        entry["audio_end"] = static_cast<double>(line.audio_end);
        entry["localization"] = std::string(to_string(line.localization));
        lines_json.push(std::move(entry));
    }
    root["lines"] = std::move(lines_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

}  // namespace bully::formats
