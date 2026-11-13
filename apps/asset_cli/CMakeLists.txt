# bully_asset_cli - headless pipeline/QA/packaging tool.
#
# Deliberately Qt-free so it can run in CI containers and be scripted.
bully_add_executable(bully_asset_cli
    DEPS bully_core bully_formats bully_pipeline bully_project bully_templates bully_plugins)
set_target_properties(bully_asset_cli PROPERTIES OUTPUT_NAME bully_asset_cli)
