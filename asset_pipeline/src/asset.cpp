#include "bully/pipeline/asset.h"

#include <cstdlib>

#include "bully/core/fs.h"
#include "bully/core/strings.h"

namespace bully::pipeline {

std::string_view to_string(AssetType type) noexcept
{
    switch (type) {
        case AssetType::Model: return "model";
        case AssetType::Texture: return "texture";
        case AssetType::Material: return "material";
        case AssetType::Animation: return "animation";
        case AssetType::Audio: return "audio";
        case AssetType::Dialogue: return "dialogue";
        case AssetType::Scene: return "scene";
        case AssetType::Mission: return "mission";
        case AssetType::Story: return "story";
        case AssetType::Font: return "font";
        case AssetType::Script: return "script";
        case AssetType::Other: return "other";
        case AssetType::Unknown: return "unknown";
    }
    return "unknown";
}

AssetType asset_type_from_string(std::string_view text) noexcept
{
    const std::string lowered = core::to_lower(text);
    if (lowered == "model") return AssetType::Model;
    if (lowered == "texture") return AssetType::Texture;
    if (lowered == "material") return AssetType::Material;
    if (lowered == "animation") return AssetType::Animation;
    if (lowered == "audio") return AssetType::Audio;
    if (lowered == "dialogue") return AssetType::Dialogue;
    if (lowered == "scene") return AssetType::Scene;
    if (lowered == "mission") return AssetType::Mission;
    if (lowered == "story") return AssetType::Story;
    if (lowered == "font") return AssetType::Font;
    if (lowered == "script") return AssetType::Script;
    if (lowered == "other") return AssetType::Other;
    return AssetType::Unknown;
}

std::string_view runtime_extension(AssetType type) noexcept
{
    switch (type) {
        case AssetType::Model: return ".bmodel";
        case AssetType::Texture: return ".btexture";
        case AssetType::Material: return ".bmaterial";
        case AssetType::Animation: return ".banim";
        case AssetType::Audio: return ".baudio";
        case AssetType::Dialogue: return ".bdialogue";
        case AssetType::Scene: return ".bscene";
        case AssetType::Mission: return ".bmission";
        default: return "";
    }
}

std::string_view to_string(BuildStatus status) noexcept
{
    switch (status) {
        case BuildStatus::UpToDate: return "up_to_date";
        case BuildStatus::Pending: return "pending";
        case BuildStatus::Building: return "building";
        case BuildStatus::Failed: return "failed";
        case BuildStatus::Skipped: return "skipped";
        case BuildStatus::Unknown: return "unknown";
    }
    return "unknown";
}

BuildStatus build_status_from_string(std::string_view text) noexcept
{
    const std::string lowered = core::to_lower(text);
    if (lowered == "up_to_date") return BuildStatus::UpToDate;
    if (lowered == "pending") return BuildStatus::Pending;
    if (lowered == "building") return BuildStatus::Building;
    if (lowered == "failed") return BuildStatus::Failed;
    if (lowered == "skipped") return BuildStatus::Skipped;
    return BuildStatus::Unknown;
}

std::string ImportSettings::get(std::string_view key, std::string fallback) const
{
    for (const auto& setting : values) {
        if (setting.key == key) return setting.value;
    }
    return fallback;
}

bool ImportSettings::get_bool(std::string_view key, bool fallback) const
{
    const std::string value = core::to_lower(get(key));
    if (value.empty()) return fallback;
    return value == "true" || value == "1" || value == "yes" || value == "on";
}

double ImportSettings::get_number(std::string_view key, double fallback) const
{
    const std::string value = get(key);
    if (value.empty()) return fallback;
    try {
        return std::stod(value);
    } catch (...) {
        return fallback;
    }
}

void ImportSettings::set(std::string key, std::string value)
{
    for (auto& setting : values) {
        if (setting.key == key) {
            setting.value = std::move(value);
            return;
        }
    }
    values.push_back(ImportSetting{std::move(key), std::move(value)});
}

core::json::Value ImportSettings::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    root["importer_id"] = importer_id;
    core::json::Value settings = core::json::Value::make_object();
    for (const auto& setting : values) settings[setting.key] = setting.value;
    root["values"] = std::move(settings);
    core::json::Value options = core::json::Value::make_object();
    if (auto parsed = core::json::parse(options_json); parsed.has_value() && parsed.value().is_object()) {
        options = std::move(parsed.value());
    }
    root["options"] = std::move(options);
    return root;
}

ImportSettings ImportSettings::from_json(const core::json::Value& value)
{
    ImportSettings settings;
    settings.importer_id = value.get_string("importer_id");
    if (const core::json::Value* values = value.find("values"); values != nullptr && values->is_object()) {
        for (const auto& [key, entry] : values->object().entries()) {
            settings.values.push_back(ImportSetting{key, entry.as_string()});
        }
    }
    if (const core::json::Value* options = value.find("options"); options != nullptr) {
        settings.options_json = options->dump(0);
    }
    return settings;
}

core::json::Value AssetRecord::to_json() const
{
    core::json::Value root = core::json::Value::make_object();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["type"] = std::string(to_string(type));
    root["source_path"] = source_path;
    root["runtime_path"] = runtime_path;
    root["settings"] = settings.to_json();
    core::json::Value deps = core::json::Value::make_array();
    for (const auto& dependency : dependencies) {
        core::json::Value entry = core::json::Value::make_object();
        entry["asset_id"] = dependency.asset_id;
        entry["required"] = dependency.required;
        entry["reason"] = dependency.reason;
        deps.push(std::move(entry));
    }
    root["dependencies"] = std::move(deps);
    root["version"] = version;
    root["source_hash"] = source_hash;
    root["settings_hash"] = settings_hash;
    root["output_hash"] = output_hash;
    root["status"] = std::string(to_string(status));
    root["last_error"] = last_error;
    root["last_built_unix"] = last_built_unix;
    root["modified_unix"] = modified_unix;
    core::json::Value metadata = core::json::Value::make_object();
    if (auto parsed = core::json::parse(metadata_json);
        parsed.has_value() && parsed.value().is_object()) {
        metadata = std::move(parsed.value());
    }
    root["metadata"] = std::move(metadata);
    return root;
}

AssetRecord AssetRecord::from_json(const core::json::Value& value)
{
    AssetRecord record;
    record.asset_id = value.get_string("asset_id");
    record.name = value.get_string("name");
    record.type = asset_type_from_string(value.get_string("type", "unknown"));
    record.source_path = value.get_string("source_path");
    record.runtime_path = value.get_string("runtime_path");
    if (const core::json::Value* settings = value.find("settings")) {
        record.settings = ImportSettings::from_json(*settings);
    }
    if (const core::json::Value* deps = value.find("dependencies"); deps != nullptr && deps->is_array()) {
        for (const auto& entry : deps->items()) {
            AssetDependency dependency;
            dependency.asset_id = entry.get_string("asset_id");
            dependency.required = entry.get_bool("required", true);
            dependency.reason = entry.get_string("reason");
            record.dependencies.push_back(std::move(dependency));
        }
    }
    record.version = value.get_string("version");
    record.source_hash = value.get_string("source_hash");
    record.settings_hash = value.get_string("settings_hash");
    record.output_hash = value.get_string("output_hash");
    record.status = build_status_from_string(value.get_string("status", "unknown"));
    record.last_error = value.get_string("last_error");
    record.last_built_unix = value.get_int("last_built_unix", 0);
    record.modified_unix = value.get_int("modified_unix", 0);
    record.metadata_json = value.contains("metadata") ? value.at("metadata").dump(0) : "{}";
    return record;
}

std::string AssetRecord::build_key() const
{
    return version + "|" + source_hash + "|" + settings_hash;
}

std::string hash_hex(const std::vector<std::uint8_t>& bytes)
{
    return core::FileSystem::hash_to_hex(core::FileSystem::hash_bytes(bytes));
}

std::string hash_string_hex(std::string_view text)
{
    return core::FileSystem::hash_to_hex(
        core::FileSystem::hash_bytes(std::vector<std::uint8_t>(text.begin(), text.end())));
}

}  // namespace bully::pipeline
