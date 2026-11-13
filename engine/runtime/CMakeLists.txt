# Game runtime subsystems. Everything here is headless-testable: assets, story
# state, the fixed-step clock and the input map have no window, GPU or audio
# device in their dependency graph. apps/game only adds SDL3 on top.
bully_add_library(bully_runtime PUBLIC_DEPS bully_core bully_formats)
target_compile_features(bully_runtime PUBLIC cxx_std_20)
