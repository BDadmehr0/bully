#include "bully/formats/registry.h"

#include "bully/core/strings.h"

namespace bully::formats {

const std::vector<FormatDescriptor>& known_formats()
{
    static const std::vector<FormatDescriptor> formats{
        kModelFormat,  kTextureFormat, kMaterialFormat, kAnimationFormat,
        kAudioFormat,  kDialogueFormat, kSceneFormat,   kMissionFormat,
        kPackageFormat};
    return formats;
}

const FormatDescriptor* format_by_magic(std::uint32_t magic)
{
    for (const auto& format : known_formats()) {
        if (format.magic == magic) return &format;
    }
    return nullptr;
}

const FormatDescriptor* format_by_extension(std::string_view extension)
{
    std::string normalised = core::to_lower(extension);
    if (!normalised.empty() && normalised.front() != '.') normalised.insert(normalised.begin(), '.');
    for (const auto& format : known_formats()) {
        if (normalised == format.extension) return &format;
    }
    return nullptr;
}

const FormatDescriptor* sniff_format(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() < 4) return nullptr;
    // The document header stores every integer little-endian, including the
    // four-character magic, so the magic is assembled the same way here.
    const std::uint32_t magic = static_cast<std::uint32_t>(bytes[0]) |
                                (static_cast<std::uint32_t>(bytes[1]) << 8) |
                                (static_cast<std::uint32_t>(bytes[2]) << 16) |
                                (static_cast<std::uint32_t>(bytes[3]) << 24);
    return format_by_magic(magic);
}

bool sniff_version(const std::vector<std::uint8_t>& bytes, core::Version& version)
{
    if (bytes.size() < 8) return false;
    version.major = static_cast<std::uint32_t>(bytes[4] | (bytes[5] << 8));
    version.minor = static_cast<std::uint32_t>(bytes[6] | (bytes[7] << 8));
    version.patch = 0;
    return true;
}

std::string describe_formats()
{
    std::string out = "Bully Studio runtime formats (all little-endian, CRC-32 protected)\n";
    for (const auto& format : known_formats()) {
        out += "  ";
        out += format.extension;
        out += std::string(14 - std::string(format.extension).size(), ' ');
        out += "v";
        out += format.version.to_string();
        out += "  ";
        out += format.description;
        out += "\n";
    }
    return out;
}

}  // namespace bully::formats
