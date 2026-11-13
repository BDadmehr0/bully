// Bully Studio - quality assurance dashboard backend.
//
// Runs a suite of checks over a project and produces a structured report the
// Studio QA panel and `bully_asset_cli qa` both render. Checks are independent
// "rules" so plugins can register additional ones.
//
// Built-in checks:
//   asset.import         every asset imports without errors
//   asset.references     dependency targets exist and are buildable
//   asset.orphans        source files referenced by nothing (informational)
//   dialogue.recordings  every dialogue line has a selected voice take
//   dialogue.subtitles   subtitle timing matches audio timing
//   dialogue.graph       branches resolve, no dead ends without an exit
//   animation.references rigs referenced by animations exist
//   scene.references     models / missions referenced by scenes exist
//   mission.dependencies mission dependency graph is acyclic and complete
//   plugin.compatibility plugins declare a compatible API version
//   runtime.load         every compiled asset can be decoded by the runtime
//   package.integrity    existing packages pass their CRC verification
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "bully/core/error.h"
#include "bully/pipeline/asset.h"
#include "bully/pipeline/pipeline.h"
#include "bully/project/asset_registry.h"

namespace bully::pipeline {

struct QaFinding {
    core::Severity severity = core::Severity::Warning;
    std::string rule;
    std::string code;
    std::string message;
    std::string asset_id;
    std::string source;
};

struct QaRuleResult {
    std::string rule;
    std::string description;
    std::uint32_t checked = 0;
    std::uint32_t passed = 0;
    std::uint32_t failed = 0;
    std::uint32_t warnings = 0;
    std::uint32_t duration_ms = 0;
    std::vector<QaFinding> findings;
};

struct QaReport {
    std::string project_name;
    std::string generated_utc;
    std::uint32_t rules_run = 0;
    std::uint32_t errors = 0;
    std::uint32_t warnings = 0;
    std::uint32_t duration_ms = 0;
    std::vector<QaRuleResult> rules;

    [[nodiscard]] bool success() const noexcept { return errors == 0; }
    [[nodiscard]] std::string to_json() const;
    [[nodiscard]] std::string to_markdown() const;
};

/// Context handed to every QA rule.
struct QaContext {
    const ProjectLayout& layout;
    project::AssetRegistry& registry;
    ImporterRegistry& importers;
    Pipeline& pipeline;
    bool headless = true;
};

using QaRule = std::function<QaRuleResult(QaContext&)>;

class QaRunner {
public:
    QaRunner(ProjectLayout layout, project::AssetRegistry& registry, ImporterRegistry& importers,
             Pipeline& pipeline);

    /// Registers an additional rule (used by plugins and tests).
    void add_rule(std::string name, std::string description, QaRule rule);
    /// Registers the built-in rule set.
    void register_builtin_rules();
    /// Names of every registered rule, in execution order.
    [[nodiscard]] std::vector<std::string> rule_names() const;

    /// Runs every registered rule (or those in `only`).
    [[nodiscard]] QaReport run(const std::vector<std::string>& only = {});

    /// Runs the asset import/validation rules for one asset.
    [[nodiscard]] QaRuleResult check_asset(const std::string& asset_id);

private:
    struct Registration {
        std::string name;
        std::string description;
        QaRule rule;
    };
    ProjectLayout layout_;
    project::AssetRegistry& registry_;
    ImporterRegistry& importers_;
    Pipeline& pipeline_;
    std::vector<Registration> rules_;
};

}  // namespace bully::pipeline
