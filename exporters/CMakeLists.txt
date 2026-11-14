# Exporters: compiled asset -> external format. Used by round-trip tests, the
# Studio's "Export..." action and the CLI's `export` command.
bully_add_library(bully_exporters PUBLIC_DEPS bully_core bully_formats)
target_compile_features(bully_exporters PUBLIC cxx_std_20)
