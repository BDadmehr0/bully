bully_add_library(bully_project
    PUBLIC_DEPS bully_core bully_formats bully_asset_model
    PRIVATE_DEPS bully_sqlite3)

set_target_properties(bully_project PROPERTIES
    EXPORT_NAME project
    DESCRIPTION "Project metadata: SQLite database, asset registry")
