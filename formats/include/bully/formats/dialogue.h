// Bully Studio - .bdialogue : compiled dialogue + voice-over package.
//
// A dialogue document is a conversation graph: lines, character assignment,
// voice takes, subtitle timing, localisation status, branches and conditions.
// Audio is referenced by asset id - never embedded - so voice recordings can be
// re-imported, re-compressed or replaced without recompiling the story.
//
// v1.0 chunks:
//   INFO  document identity, language, default speaker, counters
//   CHRS  character table (id, display name, voice actor, colour)
//   LINE  one chunk per dialogue line (text, take list, timing, emotion)
//   NODE  branching graph nodes (choice hubs, conditions, jumps)
//   FLAG  story flags read/written by this conversation
//   META  JSON metadata (localisation, notes)
//
// Diagnostic codes: BULLY-DLG-0001..0039
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/formats/binary.h"

namespace bully::formats {

inline constexpr FormatDescriptor kDialogueFormat{
    fourcc("BDLG"), "bdialogue", {1, 0, 0}, ".bdialogue",
    "Compiled dialogue: lines, characters, voice takes, subtitles, branching"};

enum class LocalizationStatus : std::uint8_t {
    Missing = 0,
    Draft = 1,
    Translated = 2,
    Reviewed = 3,
    Final = 4,
};

[[nodiscard]] std::string_view to_string(LocalizationStatus status) noexcept;

enum class LineDelivery : std::uint8_t {
    Neutral = 0,
    Angry = 1,
    Happy = 2,
    Sad = 3,
    Fearful = 4,
    Sarcastic = 5,
    Whisper = 6,
    Shout = 7,
    Custom = 8,
};

[[nodiscard]] std::string_view to_string(LineDelivery delivery) noexcept;

/// One recorded take for a line. The studio compares takes by duration,
/// loudness and notes; the exported package ships every take so dubbing
/// studios can re-cut later.
struct VoiceTake {
    std::string take_id;         // stable id within the line, e.g. "t01"
    std::string audio_asset;     // .baudio asset id (never a raw file path)
    std::string voice_actor;     // stable id of the performer
    std::string recording_date;  // ISO-8601 date
    std::string notes;           // director notes
    float start = 0.0f;          // trim start within the audio asset (seconds)
    float end = 0.0f;            // trim end; 0 = to end of clip
    float loudness_lufs = 0.0f;
    bool is_selected = false;
};

/// Subtitle timing: may differ from audio timing (translated text is often
/// longer than the recorded take).
struct SubtitleTiming {
    float start = 0.0f;
    float end = 0.0f;
    bool auto_generated = false;
};

struct DialogueLine {
    std::string line_id;      // stable id, e.g. "dlg.intro.missy.001"
    std::string character;    // CHRS id, empty for narration
    std::string text;         // source-language text
    std::string language = "en";
    LineDelivery delivery = LineDelivery::Neutral;
    std::string emotion_notes;
    std::vector<VoiceTake> takes;
    std::string selected_take;
    SubtitleTiming subtitle;
    float audio_start = 0.0f;  // resolved timeline position in the conversation
    float audio_end = 0.0f;
    LocalizationStatus localization = LocalizationStatus::Missing;
    std::vector<std::string> playback_events;  // e.g. "anim.laugh", "cam.closeup"
    std::string next_node;                     // fallthrough branch target

    [[nodiscard]] const VoiceTake* selected() const;
};

struct DialogueCharacter {
    std::string character_id;   // "chr.missy"
    std::string display_name;   // "Missy"
    std::string voice_actor;    // default performer id
    std::string color_hex = "#FFFFFF";
    std::string notes;
};

enum class NodeKind : std::uint8_t {
    Line = 0,       // references a DialogueLine
    Choice = 1,     // presents player options
    Condition = 2,  // branches on a story flag
    Jump = 3,       // continues elsewhere
    Cutscene = 4,
    End = 5,
};

struct DialogueBranch {
    std::string label;           // player-facing choice text
    std::string target_node;
    std::string required_flag;   // empty = always available
    std::string required_value;  // expected flag value ("1", "true", ...)
};

struct DialogueNode {
    std::string node_id;
    NodeKind kind = NodeKind::Line;
    std::string line_id;                 // for Line nodes
    std::string condition_flag;          // for Condition nodes
    std::string condition_value;
    std::string true_target;
    std::string false_target;
    std::vector<DialogueBranch> branches;  // for Choice nodes
    /// Fallthrough target for Line/Cutscene nodes (mirrors DialogueLine but is
    /// authoritative for graph traversal).
    std::string next_target;
};

struct DialogueFlagUsage {
    std::string flag;
    std::string written_value;  // empty when the flag is only read
    std::string node_id;
};

struct DialogueDocument {
    std::string asset_id;
    std::string name;
    std::string language = "en";
    std::string source_path;      // story source (markdown / .story.json)
    std::string entry_node;
    std::vector<DialogueCharacter> characters;
    std::vector<DialogueLine> lines;
    std::vector<DialogueNode> nodes;
    std::vector<DialogueFlagUsage> flags;
    std::string metadata_json;

    [[nodiscard]] const DialogueLine* find_line(std::string_view line_id) const;
    [[nodiscard]] const DialogueNode* find_node(std::string_view node_id) const;
    [[nodiscard]] float total_audio_seconds() const;

    /// Lines that have no selected take with audio - drives the "missing
    /// voice recordings" report and the recording checklist.
    [[nodiscard]] std::vector<std::string> missing_recordings() const;

    /// Subtitle/audio drift beyond `tolerance` seconds.
    [[nodiscard]] std::vector<std::string> subtitle_drift(float tolerance = 0.15f) const;

    [[nodiscard]] std::vector<std::uint8_t> serialize() const;
    [[nodiscard]] core::Result<DialogueDocument> deserialize(
        const std::vector<std::uint8_t>& bytes) const;

    /// Structural validation: duplicate ids, dangling branch targets,
    /// non-monotonic subtitles, missing speakers.
    [[nodiscard]] core::Status validate() const;
    [[nodiscard]] std::string to_json() const;
};

}  // namespace bully::formats
