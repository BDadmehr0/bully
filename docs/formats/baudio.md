# `.baudio` - audio asset

Magic `BAUD`, version **1.0**, produced by the WAV importer and the Audio
Production Studio.

| Chunk | Body |
|---|---|
| `INFO` | identity, sample rate, channels, duration, loudness, peak, loop points |
| `WAVE` | optional embedded PCM (16-bit LE, interleaved) |
| `SPAT` | optional spatial configuration |
| `EVNT` | optional event triggers (footsteps, ambience swaps) |
| `META` | source application, export profile, notes |

## Two modes

* **Reference mode** (default for large libraries): metadata only. `source_path`
  points at the original WAV/OGG/FLAC, which is never modified.
* **Embedded mode**: the pipeline baked decoded PCM into the file so a package is
  self-contained. `is_embedded()` distinguishes them.

## Analysis

`analyse()` computes peak, RMS and crest factor from the embedded PCM; the WAV
importer fills `peak` and `loudness_lufs` from it. `normalize(target)` scales the
payload linearly without clipping and reports a diagnostic when there is no PCM
to normalise.

## Loop points

`loop_start_samples` / `loop_end_samples` are sample-accurate. When looping is
enabled and the end is left at 0, the importer sets it to `frame_count()`.
