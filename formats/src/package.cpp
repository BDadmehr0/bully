#include "bully/formats/package.h"

#include <algorithm>
#include <set>

#include "bully/core/json.h"
#include "bully/core/strings.h"

namespace bully::formats {

namespace {

constexpr std::uint32_t kChunkInfo = fourcc("INFO");
constexpr std::uint32_t kChunkEntr = fourcc("ENTR");
constexpr std::uint32_t kChunkData = fourcc("DATA");
constexpr std::uint32_t kChunkMeta = fourcc("META");

void write_entry(BinaryWriter& w, const PackageEntry& entry)
{
    w.string(entry.asset_id);
    w.u8(static_cast<std::uint8_t>(entry.kind));
    w.string(entry.logical_path);
    w.u32(entry.offset);
    w.u32(entry.size);
    w.u32(entry.crc);
    w.u32(entry.flags);
}

PackageEntry read_entry(BinaryReader& r)
{
    PackageEntry entry;
    entry.asset_id = r.string();
    entry.kind = static_cast<PackageEntryKind>(r.u8());
    entry.logical_path = r.string();
    entry.offset = r.u32();
    entry.size = r.u32();
    entry.crc = r.u32();
    entry.flags = r.u32();
    return entry;
}

}  // namespace

std::string_view to_string(PackageEntryKind kind) noexcept
{
    switch (kind) {
        case PackageEntryKind::Model: return "model";
        case PackageEntryKind::Texture: return "texture";
        case PackageEntryKind::Material: return "material";
        case PackageEntryKind::Animation: return "animation";
        case PackageEntryKind::Audio: return "audio";
        case PackageEntryKind::Dialogue: return "dialogue";
        case PackageEntryKind::Scene: return "scene";
        case PackageEntryKind::Mission: return "mission";
        case PackageEntryKind::Other: return "other";
    }
    return "other";
}

PackageEntryKind package_kind_from_extension(std::string_view extension) noexcept
{
    const std::string ext = core::to_lower(extension);
    if (ext == ".bmodel" || ext == "bmodel") return PackageEntryKind::Model;
    if (ext == ".btexture" || ext == "btexture") return PackageEntryKind::Texture;
    if (ext == ".bmaterial" || ext == "bmaterial") return PackageEntryKind::Material;
    if (ext == ".banim" || ext == "banim") return PackageEntryKind::Animation;
    if (ext == ".baudio" || ext == "baudio") return PackageEntryKind::Audio;
    if (ext == ".bdialogue" || ext == "bdialogue") return PackageEntryKind::Dialogue;
    if (ext == ".bscene" || ext == "bscene") return PackageEntryKind::Scene;
    if (ext == ".bmission" || ext == "bmission") return PackageEntryKind::Mission;
    return PackageEntryKind::Other;
}

const PackageEntry* PackageDocument::find(std::string_view lookup_id) const
{
    for (const auto& entry : entries) {
        if (entry.asset_id == lookup_id) return &entry;
    }
    return nullptr;
}

core::Result<std::vector<std::uint8_t>> PackageDocument::extract(std::string_view lookup_id) const
{
    core::DiagnosticList diagnostics;
    const PackageEntry* entry = find(lookup_id);
    if (entry == nullptr) {
        diagnostics.error("BULLY-PKG-0001", "package does not contain asset '" + std::string(lookup_id) + "'",
                          name);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    const std::uint64_t end = static_cast<std::uint64_t>(entry->offset) + entry->size;
    if (end > data.size()) {
        diagnostics.error("BULLY-PKG-0002",
                          "entry '" + entry->logical_path + "' points outside the data block", name);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    std::vector<std::uint8_t> bytes(
        data.begin() + static_cast<std::ptrdiff_t>(entry->offset),
        data.begin() + static_cast<std::ptrdiff_t>(end));
    const std::uint32_t actual = crc32(bytes.data(), bytes.size());
    if (actual != entry->crc) {
        diagnostics.error("BULLY-PKG-0003",
                          "entry '" + entry->logical_path + "' (asset '" + entry->asset_id +
                              "') failed its checksum",
                          name);
        return core::Result<std::vector<std::uint8_t>>(std::move(diagnostics));
    }
    return bytes;
}

core::Status PackageDocument::verify() const
{
    core::DiagnosticList diagnostics;
    for (const auto& entry : entries) {
        const std::uint64_t end = static_cast<std::uint64_t>(entry.offset) + entry.size;
        if (end > data.size()) {
            diagnostics.error("BULLY-PKG-0002",
                              "entry '" + entry.logical_path + "' points outside the data block", name);
            continue;
        }
        const std::uint32_t actual = crc32(data.data() + entry.offset, entry.size);
        if (actual != entry.crc) {
            diagnostics.error("BULLY-PKG-0003",
                              "entry '" + entry.logical_path + "' (asset '" + entry.asset_id +
                                  "') failed its checksum",
                              name);
        }
    }
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    core::Status combined{};
    combined.attach_diagnostics(std::move(diagnostics));
    return combined;
}

core::Status PackageDocument::add(PackageEntryKind kind, std::string logical_path,
                                  std::string new_asset_id, const std::vector<std::uint8_t>& bytes)
{
    core::DiagnosticList diagnostics;
    if (new_asset_id.empty()) {
        diagnostics.error("BULLY-PKG-0004", "cannot add a resource without an asset id", logical_path);
        return core::Status(std::move(diagnostics));
    }
    if (find(new_asset_id) != nullptr) {
        diagnostics.error("BULLY-PKG-0005",
                          "asset id '" + new_asset_id + "' is already present in the package",
                          logical_path);
        return core::Status(std::move(diagnostics));
    }
    if (bytes.empty()) {
        diagnostics.error("BULLY-PKG-0013",
                          "cannot add an empty resource for asset '" + new_asset_id + "'",
                          logical_path);
        return core::Status(std::move(diagnostics));
    }
    PackageEntry entry;
    entry.asset_id = std::move(new_asset_id);
    entry.kind = kind;
    entry.logical_path = std::move(logical_path);
    entry.offset = static_cast<std::uint32_t>(data.size());
    entry.size = static_cast<std::uint32_t>(bytes.size());
    entry.crc = crc32(bytes.data(), bytes.size());
    data.insert(data.end(), bytes.begin(), bytes.end());
    entries.push_back(std::move(entry));
    return core::Status{};
}

std::vector<std::uint8_t> PackageDocument::serialize() const
{
    BinaryWriter w;
    w.chunk(kChunkInfo, [&](BinaryWriter& body) {
        body.string(asset_id);
        body.string(name);
        body.string(target_platform);
        body.string(build_profile);
        body.u32(content_version.major);
        body.u32(content_version.minor);
        body.u32(content_version.patch);
        body.u32(static_cast<std::uint32_t>(entries.size()));
        body.u64(static_cast<std::uint64_t>(data.size()));
    });
    w.chunk(kChunkEntr, [&](BinaryWriter& body) {
        body.u32(static_cast<std::uint32_t>(entries.size()));
        for (const auto& entry : entries) write_entry(body, entry);
    });
    w.chunk(kChunkData, [&](BinaryWriter& body) {
        for (std::uint8_t byte : data) body.u8(byte);
    });
    w.chunk(kChunkMeta, [&](BinaryWriter& body) { body.string(manifest_json); });
    return encode_document(kPackageFormat, 0, w.bytes());
}

core::Result<PackageDocument> PackageDocument::deserialize(
    const std::vector<std::uint8_t>& bytes) const
{
    core::DiagnosticList diagnostics;
    auto payload = decode_document(kPackageFormat, bytes);
    if (!payload) return core::Result<PackageDocument>(payload.diagnostics());
    diagnostics.append(payload.diagnostics());

    PackageDocument document;
    BinaryReader reader(payload.value().data(), payload.value().size(), &diagnostics);
    while (reader.ok()) {
        std::uint32_t chunk_id = 0;
        std::uint32_t chunk_size = 0;
        if (!reader.next_chunk(chunk_id, chunk_size)) break;
        const std::size_t chunk_end = reader.position() + chunk_size;
        if (chunk_id == kChunkInfo) {
            document.asset_id = reader.string();
            document.name = reader.string();
            document.target_platform = reader.string();
            document.build_profile = reader.string();
            document.content_version.major = reader.u32();
            document.content_version.minor = reader.u32();
            document.content_version.patch = reader.u32();
            (void)reader.u32();
            (void)reader.u64();
        } else if (chunk_id == kChunkEntr) {
            const std::uint32_t count = std::min<std::uint32_t>(reader.u32(), 1u << 20);
            document.entries.resize(count);
            for (auto& entry : document.entries) entry = read_entry(reader);
        } else if (chunk_id == kChunkData) {
            document.data.resize(chunk_size);
            for (auto& byte : document.data) byte = reader.u8();
        } else if (chunk_id == kChunkMeta) {
            document.manifest_json = reader.string();
        } else {
            diagnostics.info("BULLY-PKG-0006",
                             "skipping unknown package chunk '" + fourcc_to_string(chunk_id) + "'");
        }
        if (reader.ok()) reader.seek(chunk_end);
    }
    if (!reader.ok() || diagnostics.has_errors()) {
        if (diagnostics.empty()) diagnostics.error("BULLY-PKG-0007", "failed to decode package");
        return core::Result<PackageDocument>(std::move(diagnostics));
    }
    auto decoded = core::Result<PackageDocument>(std::move(document));
    // Warnings collected while decoding (unknown chunks, newer minor versions)
    // travel with the successful result so callers can surface them.
    decoded.attach_diagnostics(std::move(diagnostics));
    return decoded;
}

core::Status PackageDocument::validate() const
{
    core::DiagnosticList diagnostics;
    if (asset_id.empty()) diagnostics.error("BULLY-PKG-0008", "package has no asset id");
    std::set<std::string> ids;
    std::set<std::string> paths;
    for (const auto& entry : entries) {
        if (!ids.insert(entry.asset_id).second) {
            diagnostics.error("BULLY-PKG-0009", "duplicate asset id '" + entry.asset_id + "'", name);
        }
        if (!paths.insert(entry.logical_path).second) {
            diagnostics.warn("BULLY-PKG-0010", "duplicate logical path '" + entry.logical_path + "'", name);
        }
        if (entry.logical_path.empty()) {
            diagnostics.warn("BULLY-PKG-0011", "entry '" + entry.asset_id + "' has no logical path", name);
        }
    }
    auto status = verify();
    diagnostics.append(status.diagnostics());
    if (diagnostics.has_errors()) return core::Status(std::move(diagnostics));
    // Warnings are part of the report even when the document is valid.
    core::Status combined{};
    combined.attach_diagnostics(std::move(diagnostics));
    return combined;
}

std::string PackageDocument::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["format"] = "bpackage";
    root["version"] = kPackageFormat.version.to_string();
    root["asset_id"] = asset_id;
    root["name"] = name;
    root["target_platform"] = target_platform;
    root["build_profile"] = build_profile;
    root["content_version"] = content_version.to_string();
    root["entry_count"] = static_cast<std::uint64_t>(entries.size());
    root["data_bytes"] = static_cast<std::uint64_t>(data.size());
    Value entries_json = Value::make_array();
    for (const auto& entry : entries) {
        Value entry_json = Value::make_object();
        entry_json["asset_id"] = entry.asset_id;
        entry_json["kind"] = std::string(to_string(entry.kind));
        entry_json["path"] = entry.logical_path;
        entry_json["size"] = static_cast<std::uint64_t>(entry.size);
        entries_json.push(std::move(entry_json));
    }
    root["entries"] = std::move(entries_json);
    root["valid"] = validate().has_value();
    return root.dump(2);
}

// ------------------------------------------------------------- Manifest -----

std::string PackageManifest::to_json() const
{
    using core::json::Value;
    Value root = Value::make_object();
    root["package_name"] = package_name;
    root["target_platform"] = target_platform;
    root["build_profile"] = build_profile;
    root["created_utc"] = created_utc;
    root["tool_version"] = tool_version;
    root["deterministic"] = deterministic;
    Value entries_json = Value::make_array();
    for (const auto& entry : entries) {
        Value entry_json = Value::make_object();
        entry_json["asset_id"] = entry.asset_id;
        entry_json["kind"] = std::string(to_string(entry.kind));
        entry_json["path"] = entry.logical_path;
        entry_json["size"] = static_cast<std::uint64_t>(entry.size);
        entry_json["crc"] = static_cast<std::uint64_t>(entry.crc);
        entries_json.push(std::move(entry_json));
    }
    root["entries"] = std::move(entries_json);
    Value metadata_json = Value::make_object();
    for (const auto& [key, value] : metadata) metadata_json[key] = value;
    root["metadata"] = std::move(metadata_json);
    return root.dump(2);
}

core::Result<PackageManifest> PackageManifest::from_json(std::string_view text)
{
    core::DiagnosticList diagnostics;
    auto parsed = core::json::parse(text);
    if (!parsed) {
        diagnostics.error("BULLY-PKG-0012", "package manifest is not valid JSON");
        return core::Result<PackageManifest>(std::move(diagnostics));
    }
    const auto& root = parsed.value();
    PackageManifest manifest;
    manifest.package_name = root.get_string("package_name");
    manifest.target_platform = root.get_string("target_platform", "linux-x86_64");
    manifest.build_profile = root.get_string("build_profile", "release");
    manifest.created_utc = root.get_string("created_utc");
    manifest.tool_version = root.get_string("tool_version");
    manifest.deterministic = root.get_bool("deterministic", true);
    const core::json::Value* entries = root.find("entries");
    if (entries != nullptr && entries->is_array()) {
        for (const auto& entry_json : entries->items()) {
            PackageEntry entry;
            entry.asset_id = entry_json.get_string("asset_id");
            entry.logical_path = entry_json.get_string("path");
            entry.size = static_cast<std::uint32_t>(entry_json.get_int("size", 0));
            entry.crc = static_cast<std::uint32_t>(entry_json.get_int("crc", 0));
            const std::string kind = entry_json.get_string("kind", "other");
            entry.kind = package_kind_from_extension("." + kind);
            if (entry.kind == PackageEntryKind::Other) {
                if (kind == "model") entry.kind = PackageEntryKind::Model;
                else if (kind == "texture") entry.kind = PackageEntryKind::Texture;
                else if (kind == "material") entry.kind = PackageEntryKind::Material;
                else if (kind == "animation") entry.kind = PackageEntryKind::Animation;
                else if (kind == "audio") entry.kind = PackageEntryKind::Audio;
                else if (kind == "dialogue") entry.kind = PackageEntryKind::Dialogue;
                else if (kind == "scene") entry.kind = PackageEntryKind::Scene;
                else if (kind == "mission") entry.kind = PackageEntryKind::Mission;
            }
            manifest.entries.push_back(std::move(entry));
        }
    }
    const core::json::Value* metadata = root.find("metadata");
    if (metadata != nullptr && metadata->is_object()) {
        for (const auto& [key, value] : metadata->object().entries()) {
            manifest.metadata[key] = value.as_string();
        }
    }
    return manifest;
}

}  // namespace bully::formats
