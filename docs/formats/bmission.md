# `.bmission` - mission / quest

Magic `BMSN`, version **1.0**. Authored as `*.mission.json` by the Mission &
Quest Editor.

| Chunk | Body |
|---|---|
| `INFO` | identity, giver NPC, dependencies, reward summary, flags |
| `OBJ` | one chunk per objective (kind, target, count, optional dialogue) |
| `NODE` | graph nodes with typed edges (sequential, parallel, optional, on-failure) |
| `CND` | completion/failure conditions (flag, objective state, timer, player stat) |
| `META` | authoring notes, scene bindings |

## Objective kinds

`GoTo`, `TalkTo`, `Collect`, `Defeat`, `Escort`, `UseObject`, `Survive`, `Custom`.
Each objective carries `required_count`, `optional`, `hidden`, an optional
completion dialogue line, and reward item/currency.

## Graph validation

| Code | Meaning |
|---|---|
| `BULLY-MSN-0006` | duplicate objective id |
| `BULLY-MSN-0012` | node references a missing objective |
| `BULLY-MSN-0013` | edge points at a missing node |
| `BULLY-MSN-0014` | edge gated by an unknown condition |
| `BULLY-MSN-0016` | start node does not exist |
| `BULLY-MSN-0017` | node unreachable from the start (warning) |
| `BULLY-MSN-0018` | timed mission without a positive limit |
| `BULLY-MSN-0019` | mission depends on itself |

The `mission.dependencies` QA rule additionally walks cross-mission dependencies
and fails on cycles.
