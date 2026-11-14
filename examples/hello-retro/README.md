# hello-retro

Created by Bully Studio from template **retro-action**.

## Layout

- `assets/` - source assets (never modified by the pipeline)
- `content/` - authored scenes, missions, dialogue and story documents
- `built/` - compiled runtime assets
- `packages/` - distributable `.bpackage` output
- `reports/` - build and QA reports

## Build

```sh
bully_asset_cli build
bully_asset_cli qa
bully_asset_cli package
```
