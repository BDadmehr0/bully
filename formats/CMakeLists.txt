# bully::formats - versioned runtime resource formats.
#
# Every format is a separate translation unit but they share the binary
# document envelope and the compatibility policy in binary.h. The registry
# below lets tools enumerate supported formats without hardcoding extensions.
bully_add_library(bully_formats PUBLIC_DEPS bully_core)

set_target_properties(bully_formats PROPERTIES
    EXPORT_NAME formats
    DESCRIPTION "Versioned binary resource formats (.bmodel, .btexture, ...)")
