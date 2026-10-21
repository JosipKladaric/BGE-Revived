#include "bge/scene/scene_loader.h"

#include "bge/core/log.h"
#include "bge/render/renderer.h"
#include "bge/scene/json.h"
#include "bge/scene/scene.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <vector>

namespace bge {
namespace {

namespace fs = std::filesystem;

bool read_bytes(const fs::path& path, std::vector<uint8_t>& out) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    file.seekg(0, std::ios::end);
    std::streamoff size = file.tellg();
    if (size < 0) return false;
    file.seekg(0, std::ios::beg);
    out.resize(static_cast<size_t>(size));
    if (size > 0) file.read(reinterpret_cast<char*>(out.data()), size);
    return static_cast<bool>(file);
}

std::string read_text(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return {};
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

Vec2 json_vec2(const Json& j, Vec2 fallback) {
    if (!j.is_array() || j.as_array().size() < 2) return fallback;
    return {j.at(0).as_float(), j.at(1).as_float()};
}

Vec3 json_vec3(const Json& j, Vec3 fallback) {
    if (!j.is_array() || j.as_array().size() < 3) return fallback;
    return {j.at(0).as_float(), j.at(1).as_float(), j.at(2).as_float()};
}

Vec4 json_vec4(const Json& j, Vec4 fallback) {
    if (!j.is_array() || j.as_array().size() < 4) return fallback;
    return {j.at(0).as_float(), j.at(1).as_float(), j.at(2).as_float(), j.at(3).as_float()};
}

Mat4 json_mat4(const Json& j) {
    Mat4 m = Mat4::identity();
    if (!j.is_array() || j.as_array().size() < 16) return m;
    for (int i = 0; i < 16; ++i) m.m[i] = j.at(static_cast<size_t>(i)).as_float();
    return m;
}

float json_float(const Json& object, std::string_view key, float fallback = 0.0f) {
    return object[key].as_float(fallback);
}

int json_int(const Json& object, std::string_view key, int fallback = 0) {
    return object[key].as_int(fallback);
}

bool json_bool(const Json& object, std::string_view key, bool fallback = false) {
    return object[key].as_bool(fallback);
}

std::string json_string(const Json& object, std::string_view key, std::string fallback = {}) {
    auto value = object[key].as_string();
    return value.empty() ? std::move(fallback) : std::string(value);
}

TextureHandle load_texture_if_present(Renderer& renderer, const fs::path& base, const std::string& rel,
                                      bool srgb) {
    if (rel.empty()) return 0;
    fs::path path = rel;
    if (path.is_relative()) path = base / path;
    if (!fs::exists(path)) {
        BGE_WARN("Imported texture missing: %s", path.string().c_str());
        return 0;
    }
    return renderer.load_texture(path.string(), srgb);
}

TextureHandle load_environment_if_present(Renderer& renderer, const fs::path& base, const std::string& rel) {
    if (rel.empty()) return 0;
    fs::path path = rel;
    if (path.is_relative()) path = base / path;
    if (!fs::exists(path)) {
        BGE_WARN("Imported environment texture missing: %s", path.string().c_str());
        return 0;
    }
    return renderer.load_environment_texture(path.string());
}

LightType light_type_from_json(int value) {
    switch (value) {
        case 1: return LightType::Point;
        case 2: return LightType::Spot;
        case 3: return LightType::Area;
        default: return LightType::Directional;
    }
}

} // namespace

bool load_bge_scene(Scene& scene, Renderer& renderer, const std::string& path, std::string* error) {
    fs::path scene_path = path;
    if (!fs::exists(scene_path)) {
        if (error) *error = "scene file does not exist: " + path;
        BGE_ERROR("Scene import failed: scene file does not exist: %s", path.c_str());
        return false;
    }
    std::string text = read_text(scene_path);
    if (text.empty()) {
        if (error) *error = "scene file is empty: " + path;
        BGE_ERROR("Scene import failed: scene file is empty: %s", path.c_str());
        return false;
    }

    fs::path buffer_path = scene_path.parent_path() / "scene.bin";
    std::vector<uint8_t> buffer;
    if (!read_bytes(buffer_path, buffer)) {
        // The manifest may name a different buffer file.
        Json root;
        std::string parse_error;
        if (Json::parse(text, root, parse_error)) {
            const std::string name(root["buffer"].as_string("scene.bin"));
            if (!name.empty()) {
                fs::path candidate = scene_path.parent_path() / name;
                read_bytes(candidate, buffer);
            }
        }
    }

    const std::string base = scene_path.parent_path().string();
    return load_bge_scene_data(scene, renderer, text, buffer, base, error);
}

bool load_bge_scene_data(Scene& scene, Renderer& renderer, const std::string& text,
                          const std::vector<uint8_t>& buffer, const std::string& base_path,
                          std::string* error) {
    const fs::path base(base_path);
    auto fail = [&](const std::string& message) {
        if (error) *error = message;
        BGE_ERROR("Scene import failed: %s", message.c_str());
        return false;
    };

    if (text.empty()) return fail("the scene manifest is empty");

    Json root;
    std::string parse_error;
    if (!Json::parse(text, root, parse_error)) return fail("JSON: " + parse_error);
    if (!root.is_object()) return fail("root must be an object");

    auto format = root["format"].as_string();
    if (format != "bge.scene/1") {
        BGE_WARN("Unexpected scene format '%.*s' (expected bge.scene/1)",
                 static_cast<int>(format.size()), format.data());
    }

    // The binary buffer is supplied by the caller (read from disk or pushed
    // over the network), so the manifest can name it for documentation only.
    (void)json_string(root, "buffer", "scene.bin");

    // Materials first: meshes refer to them, objects refer to meshes.
    std::vector<MaterialHandle> materials;
    const Json& material_array = root["materials"];
    materials.reserve(material_array.as_array().size());
    for (const Json& m : material_array.as_array()) {
        MaterialDesc desc;
        desc.base_color = json_vec4(m["base_color"], {1, 1, 1, 1});
        desc.base_color.w = clampf(json_float(m, "alpha", desc.base_color.w), 0.0f, 1.0f);
        desc.metallic = clampf(json_float(m, "metallic", 0.0f), 0.0f, 1.0f);
        desc.roughness = clampf(json_float(m, "roughness", 0.5f), 0.045f, 1.0f);
        desc.emissive = json_vec3(m["emissive"], {0, 0, 0});
        desc.uv_scale = json_float(m, "uv_scale", 1.0f);
        std::string alpha_mode = json_string(m, "alpha_mode", "OPAQUE");
        desc.blend = alpha_mode == "BLEND" && desc.base_color.w < 0.999f;
        desc.alpha_clip = alpha_mode == "CLIP";
        std::string albedo = json_string(m, "albedo");
        if (!albedo.empty()) {
            desc.albedo = load_texture_if_present(renderer, base, albedo, true);
            desc.use_albedo = desc.albedo != 0;
        }
        std::string normal = json_string(m, "normal");
        if (!normal.empty()) {
            desc.normal = load_texture_if_present(renderer, base, normal, false);
            desc.use_normal = desc.normal != 0;
            desc.normal_strength = json_float(m, "normal_strength", 1.0f);
        }
        materials.push_back(renderer.create_material(desc));
    }

    std::vector<MeshHandle> meshes;
    const Json& mesh_array = root["meshes"];
    meshes.reserve(mesh_array.as_array().size());
    for (const Json& mesh_json : mesh_array.as_array()) {
        const size_t vertex_offset = static_cast<size_t>(mesh_json["vertex_byte_offset"].as_number());
        const size_t vertex_count = static_cast<size_t>(mesh_json["vertex_count"].as_number());
        const size_t index_offset = static_cast<size_t>(mesh_json["index_byte_offset"].as_number());
        const size_t index_count = static_cast<size_t>(mesh_json["index_count"].as_number());
        constexpr size_t stride = sizeof(float) * 8; // position(3), normal(3), uv(2)

        if (vertex_offset + vertex_count * stride > buffer.size()) {
            return fail("mesh vertex data is out of bounds");
        }
        if (index_offset + index_count * sizeof(uint32_t) > buffer.size()) {
            return fail("mesh index data is out of bounds");
        }
        if (vertex_count == 0 || index_count < 3 || index_count % 3 != 0) {
            BGE_WARN("Skipping empty or invalid mesh primitive");
            meshes.push_back(0);
            continue;
        }

        MeshData data;
        data.vertices.resize(vertex_count);
        for (size_t i = 0; i < vertex_count; ++i) {
            const uint8_t* ptr = buffer.data() + vertex_offset + i * stride;
            float values[8];
            std::memcpy(values, ptr, sizeof(values));
            data.vertices[i] = {{values[0], values[1], values[2]},
                                {values[3], values[4], values[5]},
                                {values[6], values[7]}};
        }
        data.indices.resize(index_count);
        std::memcpy(data.indices.data(), buffer.data() + index_offset,
                    index_count * sizeof(uint32_t));
        data.compute_bounds();
        meshes.push_back(renderer.create_mesh(data));
    }

    std::string active_camera = json_string(root, "active_camera");

    const Json& camera_array = root["cameras"];
    for (const Json& c : camera_array.as_array()) {
        std::string name = json_string(c, "name", "Camera");
        Entity e = scene.create_entity(name);
        Transform* t = scene.world().get<Transform>(e);
        if (!t) continue;
        Mat4 world = json_mat4(c["matrix"]);
        decompose_trs(world, t->position, t->rotation, t->scale);

        // Camera local axes need a basis conversion, not a matrix conjugate:
        // Blender's camera looks down local -Z with +Y up, exactly like BGE, but
        // the local frame itself is expressed in Blender's Z-up convention.
        // The basis is only used when the file actually carries it; otherwise
        // the decomposed matrix is the authority.
        Vec3 position = json_vec3(c["position"], t->position);
        if (c["forward"].is_array() && c["up"].is_array()) {
            Vec3 forward = normalize(json_vec3(c["forward"], {0, 0, -1}));
            Vec3 up = normalize(json_vec3(c["up"], {0, 1, 0}));
            if (length_sq(forward) > 1e-8f && length_sq(up) > 1e-8f) {
                Vec3 right = normalize(cross(forward, up));
                Vec3 corrected_up = normalize(cross(right, forward));
                Mat4 basis = Mat4::identity();
                basis.m[0] = right.x; basis.m[1] = right.y; basis.m[2] = right.z;
                basis.m[4] = corrected_up.x; basis.m[5] = corrected_up.y; basis.m[6] = corrected_up.z;
                basis.m[8] = -forward.x; basis.m[9] = -forward.y; basis.m[10] = -forward.z;
                t->position = position;
                t->rotation = quat_from_rotation(basis);
                t->scale = {1, 1, 1};
            } else {
                t->position = position;
            }
        }

        Camera camera;
        camera.fov_y = json_float(c, "fov_y", radians(50.0f));
        camera.near_plane = json_float(c, "near", 0.1f);
        camera.far_plane = json_float(c, "far", 1000.0f);
        camera.orthographic = json_bool(c, "orthographic", false);
        camera.ortho_width = json_float(c, "ortho_width", 6.0f);
        camera.ortho_height = json_float(c, "ortho_height", 6.0f);
        camera.shift_x = json_float(c, "shift_x", 0.0f);
        camera.shift_y = json_float(c, "shift_y", 0.0f);
        camera.primary = json_bool(c, "primary", true);
        scene.world().add(e, camera);

        if (scene.camera_entity.index == Entity::invalid_index ||
            (!active_camera.empty() && name == active_camera)) {
            scene.camera_entity = e;
        }
    }

    const Json& light_array = root["lights"];
    for (const Json& l : light_array.as_array()) {
        std::string name = json_string(l, "name", "Light");
        Entity e = scene.create_entity(name);
        Transform* t = scene.world().get<Transform>(e);
        if (!t) continue;
        Mat4 world = json_mat4(l["matrix"]);
        decompose_trs(world, t->position, t->rotation, t->scale);

        Light light;
        light.type = light_type_from_json(json_int(l, "type", 0));
        light.direction = json_vec3(l["direction"], {0, -1, 0});
        if (length_sq(light.direction) < 1e-8f) {
            light.direction = normalize(-world.column(2));
        }
        light.color = json_vec3(l["color"], {1, 1, 1});
        light.intensity = json_float(l, "energy", 1.0f);
        light.radius = json_float(l, "radius", 0.1f);
        light.size = json_vec2(l["size"], {1, 1});
        light.spot_size = json_float(l, "spot_size", radians(45.0f));
        light.spot_blend = json_float(l, "spot_blend", 0.15f);
        light.range = json_float(l, "range", 0.0f);
        light.cast_shadow = json_bool(l, "cast_shadow", true);
        scene.world().add(e, light);
    }

    const Json& object_array = root["objects"];
    size_t imported_objects = 0;
    for (const Json& o : object_array.as_array()) {
        if (!json_bool(o, "visible", true)) continue;
        int mesh_index = json_int(o, "mesh", -1);
        int material_index = json_int(o, "material", -1);
        if (mesh_index < 0 || static_cast<size_t>(mesh_index) >= meshes.size()) continue;
        MeshHandle mesh = meshes[static_cast<size_t>(mesh_index)];
        if (!mesh) continue;
        MaterialHandle material = 0;
        if (material_index >= 0 && static_cast<size_t>(material_index) < materials.size()) {
            material = materials[static_cast<size_t>(material_index)];
        }

        std::string name = json_string(o, "name", "Mesh");
        Entity e = scene.create_mesh_entity(name, mesh, material);
        Transform* t = scene.world().get<Transform>(e);
        if (!t) continue;
        Mat4 world = json_mat4(o["matrix"]);
        decompose_trs(world, t->position, t->rotation, t->scale);
        MeshRenderer* r = scene.world().get<MeshRenderer>(e);
        if (r) r->cast_shadow = json_bool(o, "cast_shadow", true);
        ++imported_objects;
    }

    const Json& world = root["world"];
    if (world.is_object()) {
        Vec3 background = json_vec3(world["background_color"], {0.05f, 0.05f, 0.05f});
        float strength = json_float(world, "strength", 1.0f);
        Vec3 env = background * strength;
        scene.flat_sky = json_bool(world, "flat_sky", true);
        scene.sky_color = env;
        scene.horizon_color = env;
        scene.ground_color = env * 0.35f;
        scene.fog_color = env;
        scene.fog_density = json_float(world, "fog_density", 0.0f);
        scene.ambient_intensity = 1.0f;
        scene.environment_strength = strength;

        std::string env_tex = json_string(world, "environment_texture");
        if (!env_tex.empty()) {
            scene.environment_texture = load_environment_if_present(renderer, base, env_tex);
            if (scene.environment_texture) scene.flat_sky = false;
        }
    }

    if (root["render"].is_object()) {
        float ev = json_float(root["render"], "exposure", 0.0f);
        scene.exposure = std::pow(2.0f, ev);
        std::string view_transform = json_string(root["render"], "view_transform", "AgX");
        scene.view_transform = (view_transform == "Standard") ? 1 : 0;
        scene.vignette = 0.0f;
        scene.post_grade = false;
    }

    scene.name = json_string(root, "name", base.filename().string());
    scene.source_path = fs::absolute(base).string();
    scene.update_transforms();
    scene.apply_camera_component();

    BGE_INFO("Imported Blender scene '%s': %zu objects, %zu meshes, %zu materials, %zu lights, %zu cameras",
             scene.name.c_str(), imported_objects, meshes.size(), materials.size(),
             light_array.as_array().size(), camera_array.as_array().size());
    return true;
}

} // namespace bge
