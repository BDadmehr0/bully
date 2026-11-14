# bully::templates - reusable project templates (folder structure, runtime
# modules, build settings and sample content).
bully_add_library(bully_templates PUBLIC_DEPS bully_core bully_pipeline)
set_target_properties(bully_templates PROPERTIES
    EXPORT_NAME templates
    DESCRIPTION "Project templates and sample project scaffolding")
