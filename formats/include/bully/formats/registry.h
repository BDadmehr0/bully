// Bully Studio - format registry.
//
// Tools ask the registry which runtime formats exist instead of hardcoding a
// list, so adding a new .b* format only requires registering it here (and, for
// plugins, in the plugin's descriptor).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "bully/formats/animation.h"
#include "bully/formats/audio.h"
#include "bully/formats/binary.h"
#include "bully/formats/dialogue.h"
#include "bully/formats/material.h"
#include "bully/formats/mission.h"
#include "bully/formats/model.h"
#include "bully/formats/package.h"
#include "bully/formats/scene.h"
#include "bully/formats/texture.h"

namespace bully::formats {

/// All formats known to this build, in display order.
[[nodiscard]] const std::vector<FormatDescriptor>& known_formats();

/// Resolves a format by fourcc magic (0 when unknown).
[[nodiscard]] const FormatDescriptor* format_by_magic(std::uint32_t magic);

/// Resolves a format by file extension (".bmodel" or "bmodel"); nullptr if unknown.
[[nodiscard]] const FormatDescriptor* format_by_extension(std::string_view extension);

/// Probes the header of a file and returns the matching format (nullptr when
/// the magic is unknown). Does not validate the payload.
[[nodiscard]] const FormatDescriptor* sniff_format(const std::vector<std::uint8_t>& bytes);

/// Extracts the {major, minor} version from a file header without decoding.
/// Returns false when the file is too small.
[[nodiscard]] bool sniff_version(const std::vector<std::uint8_t>& bytes, core::Version& version);

/// Human-readable one-line summary for docs/CLI listings.
[[nodiscard]] std::string describe_formats();

}  // namespace bully::formats
