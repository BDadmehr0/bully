# `.bdialogue` - dialogue and voice-over graph

Magic `BDLG`, version **1.0**. Authored as `*.dialogue.json` (or Markdown story
sources), compiled by `com.bully.importer.dialogue_json`.

| Chunk | Body |
|---|---|
| `INFO` | identity, language, entry node, counters |
| `CHRS` | character table (id, display name, voice actor, colour) |
| `LINE` | one chunk per line: text, delivery, takes, subtitle timing |
| `NODE` | branching graph nodes (line, choice, condition, jump, cutscene, end) |
| `FLAG` | story flags read/written by the conversation |
| `META` | localisation metadata, authoring notes |

Audio is referenced by `.baudio` **asset id**, never embedded, so recordings can
be replaced without recompiling the story.

## Takes

A line can carry several `VoiceTake` records (different performers, alternate
reads). Each take stores the audio asset id, performer id, recording date,
director notes, trim window, loudness and a selected flag. `selected()` resolves
`selected_take` for the studio and the runtime. Exported packages ship **every**
take so a dubbing studio can re-cut later.

## Reporting helpers

* `missing_recordings()` - lines with text but no selected take carrying audio.
  Drives the recording checklist and the `dialogue.recordings` QA rule.
* `subtitle_drift(tolerance)` - lines whose subtitle timing differs from the
  audio window by more than `tolerance` seconds (default 0.15 s).
* `total_audio_seconds()` - conversation length.

## Validation

Dangling branch targets, unknown entry nodes, unknown line references and
inverted timings are errors; unknown speakers, choice nodes with no
unconditional branch and lines continuing into missing nodes are warnings.
