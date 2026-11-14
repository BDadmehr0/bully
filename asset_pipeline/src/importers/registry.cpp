#include <algorithm>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "bully/core/log.h"
#include "bully/core/strings.h"
#include "bully/pipeline/importer.h"
#include "bully/pipeline/builtin_importers.h"

namespace bully::pipeline {

ImporterRegistry& ImporterRegistry::instance()
{
    static ImporterRegistry registry;
    return registry;
}

bool ImporterRegistry::register_importer(ImporterFactory factory)
{
    if (!factory) return false;
    auto probe = factory();
    if (!probe) {
        core::log_error("pipeline", "importer factory returned null");
        return false;
    }
    const std::string id = probe->id();
    if (id.empty()) {
        core::log_error("pipeline", "importer registered without an id");
        return false;
    }
    for (const auto& existing : factories_) {
        auto instance = existing();
        if (instance && instance->id() == id) {
            core::log_warn("pipeline", "importer id '" + id + "' is already registered");
            return false;
        }
    }
    factories_.push_back(std::move(factory));
    core::log_info("pipeline", "registered importer '" + id + "' v" + probe->version());
    return true;
}

bool ImporterRegistry::add(std::shared_ptr<Importer> importer)
{
    if (!importer) return false;
    std::shared_ptr<Importer> shared = std::move(importer);
    return register_importer([shared]() { return shared; });
}

bool ImporterRegistry::unregister(std::string_view importer_id)
{
    for (auto it = factories_.begin(); it != factories_.end(); ++it) {
        auto instance = (*it)();
        if (instance && instance->id() == importer_id) {
            factories_.erase(it);
            core::log_info("pipeline", "unregistered importer '" + std::string(importer_id) + "'");
            return true;
        }
    }
    return false;
}

std::vector<std::shared_ptr<Importer>> ImporterRegistry::all() const
{
    std::vector<std::shared_ptr<Importer>> importers;
    importers.reserve(factories_.size());
    for (const auto& factory : factories_) {
        if (auto instance = factory()) importers.push_back(std::move(instance));
    }
    return importers;
}

std::shared_ptr<Importer> ImporterRegistry::find(std::string_view importer_id) const
{
    for (const auto& factory : factories_) {
        auto instance = factory();
        if (instance && instance->id() == importer_id) return instance;
    }
    return nullptr;
}

std::shared_ptr<Importer> ImporterRegistry::for_extension(std::string_view extension) const
{
    std::string normalised = core::to_lower(extension);
    if (!normalised.empty() && normalised.front() != '.') normalised.insert(normalised.begin(), '.');
    for (const auto& factory : factories_) {
        auto instance = factory();
        if (!instance) continue;
        for (const auto& candidate : instance->extensions()) {
            if (core::to_lower(candidate) == normalised) return instance;
        }
    }
    return nullptr;
}

std::shared_ptr<Importer> ImporterRegistry::for_filename(std::string_view filename) const
{
    const std::string lowered = core::to_lower(filename);
    std::shared_ptr<Importer> best;
    std::size_t best_length = 0;
    for (const auto& factory : factories_) {
        auto instance = factory();
        if (!instance) continue;
        for (const auto& candidate : instance->extensions()) {
            std::string normalised = core::to_lower(candidate);
            if (!normalised.empty() && normalised.front() != '.') {
                normalised.insert(normalised.begin(), '.');
            }
            if (normalised.size() > lowered.size()) continue;
            if (lowered.compare(lowered.size() - normalised.size(), normalised.size(), normalised) !=
                0) {
                continue;
            }
            if (normalised.size() > best_length) {
                best_length = normalised.size();
                best = instance;
            }
        }
    }
    return best;
}

std::vector<std::string> ImporterRegistry::all_extensions() const
{
    std::vector<std::string> extensions;
    for (const auto& factory : factories_) {
        auto instance = factory();
        if (!instance) continue;
        for (const auto& candidate : instance->extensions()) {
            extensions.push_back(core::to_lower(candidate));
        }
    }
    std::sort(extensions.begin(), extensions.end());
    extensions.erase(std::unique(extensions.begin(), extensions.end()), extensions.end());
    return extensions;
}

void ImporterRegistry::register_builtin()
{
    register_builtin_importers(*this);
}

void ImporterRegistry::clear() { factories_.clear(); }

}  // namespace bully::pipeline
