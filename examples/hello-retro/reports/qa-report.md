# QA report - hello-retro

- Generated: 2026-11-13T12:54:37Z
- Rules: 13
- Errors: 0, warnings: 5
- Result: PASS

| Rule | Checked | Passed | Failed | Warnings | ms |
|---|---|---|---|---|---|
| asset.references | 4 | 4 | 0 | 0 | 0 |
| asset.import | 4 | 4 | 0 | 2 | 3 |
| runtime.load | 4 | 4 | 0 | 0 | 1 |
| dialogue.recordings | 1 | 0 | 1 | 3 | 0 |
| mission.dependencies | 1 | 1 | 0 | 0 | 0 |
| asset.orphans | 1 | 1 | 0 | 0 | 0 |
| asset.outputs | 4 | 4 | 0 | 0 | 0 |
| dialogue.subtitles | 1 | 1 | 0 | 0 | 0 |
| dialogue.graph | 1 | 1 | 0 | 0 | 0 |
| animation.references | 0 | 0 | 0 | 0 | 0 |
| scene.references | 1 | 1 | 0 | 0 | 0 |
| plugin.compatibility | 0 | 0 | 0 | 0 | 0 |
| package.integrity | 0 | 0 | 0 | 0 | 0 |

## asset.import

- [warning] BULLY-DLG-0030: 1 voice take(s) have no audio assigned; the recording checklist will list them (content/dialogue/intro.dialogue.json)
- [warning] BULLY-DLG-0030: 1 voice take(s) have no audio assigned; the recording checklist will list them (content/dialogue/intro.dialogue.json)

## dialogue.recordings

- [warning] BULLY-QA-0020: line 'dlg.intro.gary.001' has no voice recording (content/dialogue/intro.dialogue.json)
- [warning] BULLY-QA-0020: line 'dlg.intro.jimmy.001' has no voice recording (content/dialogue/intro.dialogue.json)
- [warning] BULLY-QA-0020: line 'dlg.intro.jimmy.002' has no voice recording (content/dialogue/intro.dialogue.json)
