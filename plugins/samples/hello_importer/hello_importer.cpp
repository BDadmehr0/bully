// Sample plugin: registers a ".hello" importer and a QA rule, proving the whole
// plugin path (discovery, ABI validation, initialize, importer execution).
//
// The importer converts a tiny text format:
//
//   name: Greeting Object
//   vertices: 1.0,2.0,3.0; 4.0,5.0,6.0; 0.0,0.0,0.0
//
// into a valid .bmodel using the host's public format library (which the sample
// links against; third-party plugins may equally implement their own writer).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "bully/formats/model.h"
#include "bully/plugin_api.h"
#include "hello_importer.h"

namespace {

const bully_host_api* g_host = nullptr;

std::string from_view(bully_str text)
{
    if (text.data == nullptr) return {};
    return std::string(text.data, text.size);
}

bully_str to_view(const std::string& text)
{
    bully_str view;
    view.data = text.data();
    view.size = text.size();
    return view;
}

const bully_plugin_descriptor* descriptor()
{
    static const std::string kId = "com.bully.samples.hello_importer";
    static const std::string kName = "Hello Importer";
    static const std::string kVersion = "1.0.0";
    static const std::string kVendor = "Bully Studio";
    static const std::string kDescription = "Sample plugin demonstrating the stable C plugin ABI";
    static const bully_plugin_descriptor kDescriptor{
        bully_str{kId.data(), kId.size()},
        bully_str{kName.data(), kName.size()},
        bully_str{kVersion.data(), kVersion.size()},
        bully_str{kVendor.data(), kVendor.size()},
        bully_str{kDescription.data(), kDescription.size()},
        BULLY_PLUGIN_ABI_MAJOR,
        BULLY_PLUGIN_ABI_MINOR,
        (1u << BULLY_CAP_IMPORTER) | (1u << BULLY_CAP_QA_RULE),
        1,  // reload_safe
    };
    return &kDescriptor;
}

bully_result initialize(const bully_host_api* host, void* /*plugin_state*/)
{
    if (host == nullptr || host->struct_size < sizeof(bully_host_api)) return BULLY_ERR_VERSION;
    if (host->abi_major != BULLY_PLUGIN_ABI_MAJOR) return BULLY_ERR_VERSION;
    g_host = host;
    if (host->log != nullptr) {
        host->log(host->host_handle, BULLY_LOG_INFO, to_view("samples"),
                  to_view("hello importer initialized"));
    }
    if (host->register_importer != nullptr) {
        const bully_result result = host->register_importer(
            host->host_handle, to_view("com.bully.samples.hello_importer.hello"),
            to_view("Hello Point List"), to_view("1.0.0"), to_view(".hello,.hellox"), nullptr);
        if (result != BULLY_OK) return result;
    }
    if (host->register_qa_rule != nullptr) {
        (void)host->register_qa_rule(host->host_handle, to_view(R"({"name":"samples.hello_ok"})"),
                                     nullptr);
    }
    return BULLY_OK;
}

void shutdown() { g_host = nullptr; }

bully_result run_import(bully_str /*importer_id*/, const bully_import_request* request,
                        bully_import_response* response)
{
    if (request == nullptr || response == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
    std::string source;
    std::string relative;
    if (g_host != nullptr && g_host->read_project_file != nullptr) {
        // The host resolves absolute paths for plugins; the path handed in the
        // request is already absolute, so read it through stdio.
    }
    {
        FILE* file = std::fopen(from_view(request->source_path).c_str(), "rb");
        if (file == nullptr) return BULLY_ERR_IO;
        char buffer[4096];
        const std::size_t read = std::fread(buffer, 1, sizeof(buffer), file);
        std::fclose(file);
        source.assign(buffer, read);
    }
    relative = from_view(request->relative_path);

    // Parse the toy format.
    std::string name = "hello";
    std::vector<bully::formats::ModelVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::size_t cursor = 0;
    while (cursor < source.size()) {
        const std::size_t end = source.find('\n', cursor);
        const std::string line = source.substr(cursor, end == std::string::npos ? std::string::npos
                                                                               : end - cursor);
        cursor = end == std::string::npos ? source.size() : end + 1;
        if (line.rfind("name:", 0) == 0) {
            name = line.substr(5);
            while (!name.empty() && (name.front() == ' ')) name.erase(name.begin());
        } else if (line.rfind("vertices:", 0) == 0) {
            std::string body = line.substr(9);
            std::size_t start = 0;
            while (start < body.size()) {
                const std::size_t separator = body.find(';', start);
                const std::string triple = body.substr(
                    start, separator == std::string::npos ? std::string::npos : separator - start);
                float x = 0.0f;
                float y = 0.0f;
                float z = 0.0f;
                if (std::sscanf(triple.c_str(), " %f , %f , %f", &x, &y, &z) == 3) {
                    bully::formats::ModelVertex vertex;
                    vertex.position = bully::formats::Vec3f{x, y, z};
                    vertex.normal = bully::formats::Vec3f{0.0f, 1.0f, 0.0f};
                    vertices.push_back(vertex);
                }
                if (separator == std::string::npos) break;
                start = separator + 1;
            }
        }
    }
    if (vertices.size() < 3) return BULLY_ERR_PARSE;
    indices = {0, 1, 2};

    bully::formats::ModelDocument document;
    document.asset_id = from_view(request->asset_id);
    document.name = name;
    document.source_path = relative;
    document.source_format = "hello";
    document.importer = "com.bully.samples.hello_importer.hello";
    document.importer_version = "1.0.0";
    bully::formats::ModelMesh mesh;
    mesh.name = name;
    mesh.vertices = vertices;
    mesh.indices = indices;
    document.meshes.push_back(std::move(mesh));
    const auto bytes = document.serialize();

    auto* buffer = static_cast<std::uint8_t*>(
        g_host != nullptr && g_host->alloc != nullptr ? g_host->alloc(g_host->host_handle, bytes.size())
                                                      : std::malloc(bytes.size()));
    if (buffer == nullptr) return BULLY_ERR_INTERNAL;
    std::memcpy(buffer, bytes.data(), bytes.size());
    response->output.data = buffer;
    response->output.size = bytes.size();
    response->output.capacity = bytes.size();
    const std::string summary = "hello importer produced " + std::to_string(vertices.size()) +
                                " vertices from " + relative;
    response->summary = to_view(summary);
    return BULLY_OK;
}

bully_result run_qa_rule(bully_str /*rule_name*/, bully_str /*project_root*/, bully_buf* out)
{
    if (out == nullptr) return BULLY_ERR_INVALID_ARGUMENT;
    const std::string json = R"({"rule":"samples.hello_ok","findings":[]})";
    auto* buffer = static_cast<std::uint8_t*>(
        g_host != nullptr && g_host->alloc != nullptr ? g_host->alloc(g_host->host_handle, json.size())
                                                      : std::malloc(json.size()));
    if (buffer == nullptr) return BULLY_ERR_INTERNAL;
    std::memcpy(buffer, json.data(), json.size());
    out->data = buffer;
    out->size = json.size();
    out->capacity = json.size();
    return BULLY_OK;
}

const bully_plugin_api* api_table()
{
    static const bully_plugin_api kApi{
        sizeof(bully_plugin_api), BULLY_PLUGIN_ABI_MAJOR, BULLY_PLUGIN_ABI_MINOR,
        descriptor,   initialize,  shutdown,   run_import,   nullptr,
        run_qa_rule,  nullptr};
    return &kApi;
}

}  // namespace

extern "C" {

__attribute__((visibility("default"))) const bully_plugin_api* bully_plugin_describe(void)
{
    return api_table();
}
}
