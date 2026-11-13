# bully::core - the only mandatory dependency of every other module.
#
# Contains no third-party code: it owns the error model, identifiers, JSON,
# filesystem access, hashing, logging and small utilities.
bully_add_library(bully_core)

set_target_properties(bully_core PROPERTIES
    EXPORT_NAME core
    DESCRIPTION "Bully Studio core services (errors, ids, json, fs, log)")

find_package(Threads REQUIRED)
target_link_libraries(bully_core PUBLIC Threads::Threads)
