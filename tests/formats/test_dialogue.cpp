#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "bully/core/json.h"
#include "bully/formats/dialogue.h"
#include "test_support.h"

using namespace bully::formats;

namespace {

DialogueDocument make_conversation()
{
    DialogueDocument document;
    document.asset_id = "dlg-1";
    document.name = "intro";
    document.language = "en";
    document.entry_node = "n_start";

    DialogueCharacter alice;
    alice.character_id = "chr.alice";
    alice.display_name = "Alice";
    alice.voice_actor = "actor.kim";
    document.characters.push_back(alice);

    DialogueLine first;
    first.line_id = "line.1";
    first.character = "chr.alice";
    first.text = "Hello there.";
    first.audio_start = 0.0f;
    first.audio_end = 1.0f;
    first.subtitle = SubtitleTiming{0.0f, 1.0f, false};
    VoiceTake take;
    take.take_id = "t01";
    take.audio_asset = "aud.hello";
    take.voice_actor = "actor.kim";
    take.start = 0.0f;
    take.end = 1.0f;
    take.is_selected = true;
    first.takes.push_back(take);
    first.selected_take = "t01";
    document.lines.push_back(first);

    DialogueLine second;
    second.line_id = "line.2";
    second.character = "chr.alice";
    second.text = "This one has no recording yet.";
    document.lines.push_back(second);

    DialogueNode start;
    start.node_id = "n_start";
    start.kind = NodeKind::Line;
    start.line_id = "line.1";
    start.next_target = "n_choice";
    document.nodes.push_back(start);

    DialogueNode choice;
    choice.node_id = "n_choice";
    choice.kind = NodeKind::Choice;
    DialogueBranch branch;
    branch.label = "Continue";
    branch.target_node = "n_end";
    choice.branches.push_back(branch);
    document.nodes.push_back(choice);

    DialogueNode end;
    end.node_id = "n_end";
    end.kind = NodeKind::End;
    document.nodes.push_back(end);

    document.flags.push_back(DialogueFlagUsage{"met_alice", "true", "n_choice"});
    return document;
}

}  // namespace

TEST_CASE("bdialogue round-trips lines, takes and graph nodes", "[formats][dialogue]")
{
    const auto original = make_conversation();
    auto decoded = DialogueDocument{}.deserialize(original.serialize());
    REQUIRE(decoded.has_value());
    const auto& document = decoded.value();
    CHECK(document.name == "intro");
    REQUIRE(document.characters.size() == 1);
    CHECK(document.characters[0].voice_actor == "actor.kim");
    REQUIRE(document.lines.size() == 2);
    REQUIRE(document.lines[0].takes.size() == 1);
    CHECK(document.lines[0].selected() != nullptr);
    CHECK(document.lines[0].selected()->audio_asset == "aud.hello");
    REQUIRE(document.nodes.size() == 3);
    CHECK(document.find_node("n_choice")->kind == NodeKind::Choice);
    CHECK(document.validate().has_value());
    CHECK(document.total_audio_seconds() == Catch::Approx(1.0f));
}

TEST_CASE("Missing recordings report is honest", "[formats][dialogue]")
{
    const auto document = make_conversation();
    const auto missing = document.missing_recordings();
    REQUIRE(missing.size() == 1);
    CHECK(missing[0] == "line.2");
}

TEST_CASE("Subtitle drift is reported beyond tolerance", "[formats][dialogue]")
{
    auto document = make_conversation();
    CHECK(document.subtitle_drift().empty());
    document.lines[0].subtitle.end = 2.5f;
    CHECK(document.subtitle_drift().size() == 1);
    CHECK(document.subtitle_drift(5.0f).empty());
}

TEST_CASE("Dialogue validation catches dangling references", "[formats][dialogue]")
{
    auto document = make_conversation();
    document.nodes[0].next_target = "n_missing";
    CHECK_FALSE(document.validate().has_value());

    // Unknown speakers are a warning (a character table may be authored later),
    // not a hard error - the diagnostic still surfaces in the QA dashboard.
    document = make_conversation();
    document.lines[0].character = "chr.nobody";
    CHECK(document.validate().has_value());

    document = make_conversation();
    document.entry_node = "n_missing";
    CHECK_FALSE(document.validate().has_value());

    document = make_conversation();
    document.lines[1].line_id = document.lines[0].line_id;
    CHECK_FALSE(document.validate().has_value());

    document = make_conversation();
    document.nodes[0].line_id = "line.missing";
    CHECK_FALSE(document.validate().has_value());
}

TEST_CASE("Dialogue source JSON parses through the importer helper", "[formats][dialogue]")
{
    // The test fixture documents the on-disk schema the data importer accepts.
    auto parsed = bully::core::json::parse(bully::test::dialogue_source_json());
    REQUIRE(parsed.has_value());
    CHECK(parsed.value().get_string("name") == "test_conversation");
}
