
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include "bully/pipeline/importer.h"

namespace bully::pipeline::settings {

namespace {

ImportSettingDescriptor make(std::string key, std::string label, ImportSettingDescriptor::Kind kind,
                             std::string default_value, std::string help)
{
    ImportSettingDescriptor descriptor;
    descriptor.key = std::move(key);
    descriptor.label = std::move(label);
    descriptor.kind = kind;
    descriptor.default_value = std::move(default_value);
    descriptor.help = std::move(help);
    return descriptor;
}

}  // namespace

ImportSettingDescriptor boolean(std::string key, std::string label, bool default_value,
                                std::string help)
{
    return make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::Bool,
                default_value ? "true" : "false", std::move(help));
}

ImportSettingDescriptor integer(std::string key, std::string label, std::int64_t default_value,
                                double minimum, double maximum, std::string help)
{
    auto descriptor = make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::Int,
                           std::to_string(default_value), std::move(help));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

ImportSettingDescriptor number(std::string key, std::string label, double default_value,
                               double minimum, double maximum, std::string help)
{
    auto descriptor = make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::Float,
                           std::to_string(default_value), std::move(help));
    descriptor.minimum = minimum;
    descriptor.maximum = maximum;
    return descriptor;
}

ImportSettingDescriptor text(std::string key, std::string label, std::string default_value,
                             std::string help)
{
    return make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::Text,
                std::move(default_value), std::move(help));
}

ImportSettingDescriptor enumeration(std::string key, std::string label, std::string default_value,
                                    std::vector<std::string> values, std::string help)
{
    auto descriptor = make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::Enum,
                           std::move(default_value), std::move(help));
    descriptor.enum_values = std::move(values);
    return descriptor;
}

ImportSettingDescriptor file_path(std::string key, std::string label, std::string help)
{
    auto descriptor = make(std::move(key), std::move(label), ImportSettingDescriptor::Kind::FilePath,
                           {}, std::move(help));
    return descriptor;
}

}  // namespace bully::pipeline::settings
