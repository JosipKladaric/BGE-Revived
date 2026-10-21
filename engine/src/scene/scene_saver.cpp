#include "bge/scene/scene_saver.h"

#include "bge/core/log.h"
#include "bge/ecs/components.h"
#include "bge/render/renderer.h"
#include "bge/scene/scene.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace bge {
namespace {

namespace fs = std::filesystem;

// A tiny JSON writer: the scene format only needs objects, arrays and numbers.
class JsonWriter {
public:
    void begin_object() {
        separate();
        out_ << "{";
        stack_.push_back(false);
    }
    void end_object() {
        out_ << "}";
        stack_.pop_back();
        mark();
    }
    void begin_array() {
        separate();
        out_ << "[";
        stack_.push_back(false);
    }
    void end_array() {
        out_ << "]";
        stack_.pop_back();
        mark();
    }
    void key(const std::string& name) {
        separate();
        out_ << '"' << name << "\": ";
        pending_key_ = true;
    }
    void value_string(const std::string& value) {
        separate();
        out_ << '"' << escape(value) << '"';
        mark();
    }
    void value_number(double value) {
        separate();
        out_ << format_number(value);
        mark();
    }
    void value_bool(bool value) {
        separate();
        out_ << (value ? "true" : "false");
        mark();
    }

    void member(const std::string& name, const std::string& value) {
        key(name);
        value_string(value);
    }
    void member(const std::string& name, double value) {
        key(name);
        value_number(value);
    }
    void member(const std::string& name, bool value) {
        key(name);
        value_bool(value);
    }
    void member(const std::string& name, int value) {
        key(name);
        value_number(value);
    }
    void member(const std::string& name, size_t value) {
        key(name);
        value_number(static_cast<double>(value));
    }
    void member(const std::string& name, uint32_t value) {
        key(name);
        value_number(static_cast<double>(value));
    }

    std::string str() const { return out_.str(); }

private:
    void separate() {
        if (pending_key_) {
            pending_key_ = false;
            return;
        }
        if (!stack_.empty()) {
            if (stack_.back()) out_ << ",";
            stack_.back() = true;
        }
    }
    void mark() {}

    static std::string escape(const std::string& value) {
        std::string out;
        out.reserve(value.size() + 8);
        for (char c : value) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) {
                        char buffer[8];
                        std::snprintf(buffer, sizeof(buffer), "\\u%04x", c);
                        out += buffer;
                    } else {
                        out += c;
                    }
            }
        }
        return out;
    }

    static std::string format_number(double value) {
        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.9g", value);
        return buffer;
    }

    std::ostringstream out_;
    std::vector<bool> stack_;
    bool pending_key_ = false;
};

void write_vec(JsonWriter& json, const std::string& name, const Vec3& v) {
    json.key(name);
    json.begin_array();
    json.value_number(v.x);
    json.value_number(v.y);
    json.value_number(v.z);
    json.end_array();
}

void write_vec(JsonWriter& json, const std::string& name, const Vec4& v) {
    json.key(name);
    json.begin_array();
    json.value_number(v.x);
    json.value_number(v.y);
    json.value_number(v.z);
    json.value_number(v.w);
    json.end_array();
}

void write_vec(JsonWriter& json, const std::string& name, const Vec2& v) {
    json.key(name);
    json.begin_array();
    json.value_number(v.x);
    json.value_number(v.y);
    json.end_array();
}

void write_mat(JsonWriter& json, const std::string& name, const Mat4& m) {
    json.key(name);
    json.begin_array();
    for (int i = 0; i < 16; ++i) json.value_number(m.m[i]);
    json.end_array();
}

} // namespace

bool save_bge_scene(Scene& scene, const Renderer& renderer, const std::string& path,
                    std::string* error) {
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        BGE_ERROR("Scene save failed: %s", message.c_str());
        return false;
    };

    fs::path scene_path = path;
    if (scene_path.has_parent_path()) {
        std::error_code ec;
        fs::create_directories(scene_path.parent_path(), ec);
    }
    const fs::path base = scene_path.parent_path();

    // Collect the meshes used by the scene, in a stable order.
    std::vector<uint32_t> mesh_handles;
    std::vector<uint32_t> material_handles;
    std::unordered_map<uint32_t, size_t> mesh_lookup;
    std::unordered_map<uint32_t, size_t> material_lookup;

    struct ObjectRecord {
        std::string name;
        size_t mesh = 0;
        size_t material = 0;
        Mat4 matrix{Mat4::identity()};
        bool visible = true;
        bool cast_shadow = true;
    };
    std::vector<ObjectRecord> objects;

    scene.world().each<Transform, MeshRenderer>([&](Entity e, Transform& transform, MeshRenderer& renderer_component) {
        if (renderer_component.mesh == 0) return;
        MeshData data;
        if (!renderer.get_mesh_data(renderer_component.mesh, data)) return;

        size_t mesh_index = 0;
        auto found = mesh_lookup.find(renderer_component.mesh);
        if (found == mesh_lookup.end()) {
            mesh_index = mesh_handles.size();
            mesh_lookup[renderer_component.mesh] = mesh_index;
            mesh_handles.push_back(renderer_component.mesh);
        } else {
            mesh_index = found->second;
        }

        size_t material_index = 0;
        auto material_found = material_lookup.find(renderer_component.material);
        if (material_found == material_lookup.end()) {
            material_index = material_handles.size();
            material_lookup[renderer_component.material] = material_index;
            material_handles.push_back(renderer_component.material);
        } else {
            material_index = material_found->second;
        }

        ObjectRecord record;
        const Name* name = scene.world().get<Name>(e);
        record.name = name ? name->value : "Object";
        record.mesh = mesh_index;
        record.material = material_index;
        record.matrix = transform.world;
        record.visible = renderer_component.visible;
        record.cast_shadow = renderer_component.cast_shadow;
        objects.push_back(record);
    });

    // Binary buffer: vertex data then index data per mesh.
    std::vector<uint8_t> buffer;
    struct MeshRange {
        size_t vertex_offset = 0;
        size_t vertex_count = 0;
        size_t index_offset = 0;
        size_t index_count = 0;
    };
    std::vector<MeshRange> ranges;
    for (uint32_t handle : mesh_handles) {
        MeshData data;
        if (!renderer.get_mesh_data(handle, data)) return fail("mesh data is unavailable");
        MeshRange range;
        range.vertex_offset = buffer.size();
        range.vertex_count = data.vertices.size();
        for (const Vertex& vertex : data.vertices) {
            const float values[8] = {vertex.position.x, vertex.position.y, vertex.position.z,
                                     vertex.normal.x, vertex.normal.y, vertex.normal.z,
                                     vertex.uv.x, vertex.uv.y};
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(values);
            buffer.insert(buffer.end(), bytes, bytes + sizeof(values));
        }
        range.index_offset = buffer.size();
        range.index_count = data.indices.size();
        for (uint32_t index : data.indices) {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&index);
            buffer.insert(buffer.end(), bytes, bytes + sizeof(index));
        }
        ranges.push_back(range);
    }

    // JSON manifest.
    JsonWriter json;
    json.begin_object();
    json.member("format", "bge.scene/1");
    json.member("generator", "BGE Revived runtime scene saver");
    json.member("name", scene.name.empty() ? "Scene" : scene.name);
    json.member("buffer", "scene.bin");
    json.member("coordinate_system", "y_up_right_handed");

    std::string active_camera;
    if (scene.camera_entity.valid()) {
        const Name* n = scene.world().get<Name>(scene.camera_entity);
        if (n) active_camera = n->value;
    }
    json.member("active_camera", active_camera);

    json.key("world");
    json.begin_object();
    write_vec(json, "background_color", scene.sky_color);
    json.member("strength", static_cast<double>(scene.environment_strength));
    json.member("flat_sky", scene.flat_sky);
    json.member("fog_density", static_cast<double>(scene.fog_density));
    json.end_object();

    json.key("render");
    json.begin_object();
    json.member("exposure", std::log2(static_cast<double>(scene.exposure)));
    json.member("view_transform", scene.view_transform == 1 ? "Standard" : "AgX");
    json.end_object();

    json.key("materials");
    json.begin_array();
    for (uint32_t handle : material_handles) {
        MaterialDesc desc;
        if (!renderer.get_material(handle, desc)) continue;
        json.begin_object();
        json.key("base_color");
        json.begin_array();
        json.value_number(desc.base_color.x);
        json.value_number(desc.base_color.y);
        json.value_number(desc.base_color.z);
        json.value_number(desc.base_color.w);
        json.end_array();
        json.member("metallic", static_cast<double>(desc.metallic));
        json.member("roughness", static_cast<double>(desc.roughness));
        write_vec(json, "emissive", desc.emissive);
        json.member("alpha", static_cast<double>(desc.base_color.w));
        json.member("alpha_mode", desc.blend ? "BLEND" : (desc.alpha_clip ? "CLIP" : "OPAQUE"));
        std::string texture_path;
        if (desc.use_albedo && renderer.get_texture_path(desc.albedo, texture_path)) {
            // Absolute paths survive the scene being saved somewhere else.
            json.member("albedo", fs::absolute(texture_path).string());
        }
        if (desc.use_normal && renderer.get_texture_path(desc.normal, texture_path)) {
            json.member("normal", fs::absolute(texture_path).string());
        }
        json.end_object();
    }
    json.end_array();

    json.key("meshes");
    json.begin_array();
    for (size_t i = 0; i < ranges.size(); ++i) {
        json.begin_object();
        json.member("name", "Mesh" + std::to_string(i));
        json.member("vertex_byte_offset", ranges[i].vertex_offset);
        json.member("vertex_count", ranges[i].vertex_count);
        json.member("index_byte_offset", ranges[i].index_offset);
        json.member("index_count", ranges[i].index_count);
        json.end_object();
    }
    json.end_array();

    json.key("objects");
    json.begin_array();
    for (const ObjectRecord& record : objects) {
        json.begin_object();
        json.member("name", record.name);
        json.member("mesh", static_cast<int>(record.mesh));
        json.member("material", static_cast<int>(record.material));
        write_mat(json, "matrix", record.matrix);
        json.member("visible", record.visible);
        json.member("cast_shadow", record.cast_shadow);
        json.end_object();
    }
    json.end_array();

    json.key("lights");
    json.begin_array();
    scene.world().each<Transform, Light>([&](Entity e, Transform& transform, Light& light) {
        json.begin_object();
        const Name* n = scene.world().get<Name>(e);
        json.member("name", n ? n->value : "Light");
        json.member("type", static_cast<int>(light.type));
        write_mat(json, "matrix", transform.world);
        write_vec(json, "direction", light.direction);
        write_vec(json, "color", light.color);
        json.member("energy", static_cast<double>(light.intensity));
        json.member("radius", static_cast<double>(light.radius));
        write_vec(json, "size", light.size);
        json.member("spot_size", static_cast<double>(light.spot_size));
        json.member("spot_blend", static_cast<double>(light.spot_blend));
        json.member("cast_shadow", light.cast_shadow);
        json.member("range", static_cast<double>(light.range));
        json.end_object();
    });
    json.end_array();

    json.key("cameras");
    json.begin_array();
    scene.world().each<Transform, Camera>([&](Entity e, Transform& transform, Camera& camera) {
        json.begin_object();
        const Name* n = scene.world().get<Name>(e);
        json.member("name", n ? n->value : "Camera");
        write_mat(json, "matrix", transform.world);
        // The explicit basis keeps the camera orientation unambiguous.
        Vec3 forward = normalize(transform.world.column(2) * -1.0f);
        Vec3 up = normalize(transform.world.column(1));
        write_vec(json, "position", transform.world.translation());
        write_vec(json, "forward", forward);
        write_vec(json, "up", up);
        json.member("fov_y", static_cast<double>(camera.fov_y));
        json.member("near", static_cast<double>(camera.near_plane));
        json.member("far", static_cast<double>(camera.far_plane));
        json.member("orthographic", camera.orthographic);
        json.member("ortho_width", static_cast<double>(camera.ortho_width));
        json.member("ortho_height", static_cast<double>(camera.ortho_height));
        json.member("shift_x", static_cast<double>(camera.shift_x));
        json.member("shift_y", static_cast<double>(camera.shift_y));
        json.member("primary", camera.primary);
        json.end_object();
    });
    json.end_array();

    json.end_object();

    // Write the files.
    std::error_code ec;
    {
        std::ofstream file(base / "scene.bin", std::ios::binary);
        if (!file) return fail("could not open the binary buffer for writing");
        file.write(reinterpret_cast<const char*>(buffer.data()),
                   static_cast<std::streamsize>(buffer.size()));
    }
    {
        std::ofstream file(scene_path, std::ios::binary);
        if (!file) return fail("could not open the scene file for writing");
        file << json.str();
    }

    BGE_INFO("Saved scene '%s': %zu objects, %zu meshes, %zu materials", scene_path.string().c_str(),
             objects.size(), mesh_handles.size(), material_handles.size());
    return true;
}

} // namespace bge
