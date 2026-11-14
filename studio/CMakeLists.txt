# Studio layer - library code shared by the Qt desktop shell, the CLI and tests.
#
# `project` holds project metadata (SQLite) and asset registry. Panels and
# editor workspaces are added in later phases; they stay Qt-free here so they
# can be unit tested headlessly, with thin Qt widgets in apps/studio.
add_subdirectory(project)
