# `.banim` - skeletal animation clip

Magic `BANI`, version **1.0**, produced by the glTF importer and the animation
importer (`*.banim.json`).

| Chunk | Body |
|---|---|
| `INFO` | identity, duration, sample rate, loop mode, root motion, events |
| `BONE` | static bone table with rest pose |
| `TRKS` | per-bone translation/rotation/scale tracks |
| `FACE` | facial blend-shape weight tracks |
| `META` | importer settings + annotations |

Time is stored in seconds as `f32`. Rotations are quaternions `(x,y,z,w)`.

## Interpolation

`Linear`, `Step`, `CubicSpline`. `sample_translation/rotation/scale()` clamp to
the first/last key, so scrubbing in the studio and playback in the runtime agree
exactly.

## Root motion

`enabled`, `bone`, `strips_translation`, `strips_rotation`. Enabling root motion
without naming a root bone is an error (`BULLY-ANIM-0010`).

## Events

`AnimationEvent{name, payload_json, time}`. Names are namespaced strings
(`footstep`, `sfx.grab`, `dialog.cue`) so plugins can add event kinds without a
format revision.

## Validation

| Code | Meaning |
|---|---|
| `BULLY-ANIM-0005` | non-positive duration (warning) |
| `BULLY-ANIM-0007` | non-monotonic keyframe times |
| `BULLY-ANIM-0009` | event outside the clip duration |
| `BULLY-ANIM-0010` | root motion without a root bone |
| `BULLY-ANIM-0011` | negative duration (error) |
| `BULLY-ANIM-0012` | track targets a bone missing from the clip's table |
