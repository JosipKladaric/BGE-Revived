#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include "bge/math/math.h"

namespace bge {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool load(const std::string& vertex_path, const std::string& fragment_path);
    void destroy();
    bool reload_if_changed();
    void use() const;

    void set_int(const char* name, int value);
    void set_float(const char* name, float value);
    void set_bool(const char* name, bool value) { set_int(name, value ? 1 : 0); }
    void set_vec2(const char* name, const Vec2& v);
    void set_vec3(const char* name, const Vec3& v);
    void set_vec4(const char* name, const Vec4& v);
    void set_mat4(const char* name, const Mat4& m);

    void set_int_array(const char* name, const int* values, int count);
    void set_float_array(const char* name, const float* values, int count);
    void set_vec2_array(const char* name, const Vec2* values, int count);
    void set_vec3_array(const char* name, const Vec3* values, int count);

    uint32_t program() const { return program_; }
    bool valid() const { return program_ != 0; }

private:
    bool build(uint32_t& out_program, std::string& error);
    int location(const char* name);

    uint32_t program_ = 0;
    std::unordered_map<std::string, int> uniforms_;
    std::string vertex_path_;
    std::string fragment_path_;
    int64_t vertex_time_ = 0;
    int64_t fragment_time_ = 0;
};

} // namespace bge
