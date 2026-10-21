#include "bge/render/shader.h"

#include "bge/core/log.h"

#include <glad/gl.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace bge {
namespace {

std::string read_file(const std::string& path, bool& ok) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        ok = false;
        return {};
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    ok = true;
    return ss.str();
}

int64_t file_time(const std::string& path) {
    try {
        return static_cast<int64_t>(std::filesystem::last_write_time(path).time_since_epoch().count());
    } catch (...) {
        return 0;
    }
}

uint32_t compile_stage(uint32_t type, const std::string& source, const std::string& path, std::string& error) {
    uint32_t shader = glCreateShader(type);
    const char* src = source.c_str();
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);

    int success = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        error = path + ":\n" + std::string(log.data());
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}

} // namespace

Shader::~Shader() { destroy(); }

bool Shader::build(uint32_t& out_program, std::string& error) {
    bool ok_v = false, ok_f = false;
    std::string vs_src = read_file(vertex_path_, ok_v);
    std::string fs_src = read_file(fragment_path_, ok_f);
    if (!ok_v || !ok_f) {
        error = "Could not read shader files: " + vertex_path_ + ", " + fragment_path_;
        return false;
    }

    uint32_t vs = compile_stage(GL_VERTEX_SHADER, vs_src, vertex_path_, error);
    if (!vs) return false;
    uint32_t fs = compile_stage(GL_FRAGMENT_SHADER, fs_src, fragment_path_, error);
    if (!fs) { glDeleteShader(vs); return false; }

    uint32_t program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    int success = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        int length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::vector<char> log(static_cast<size_t>(std::max(length, 1)));
        glGetProgramInfoLog(program, length, nullptr, log.data());
        error = "Link failed:\n" + std::string(log.data());
        glDeleteProgram(program);
        return false;
    }

    out_program = program;
    return true;
}

bool Shader::load(const std::string& vertex_path, const std::string& fragment_path) {
    vertex_path_ = vertex_path;
    fragment_path_ = fragment_path;

    uint32_t program = 0;
    std::string error;
    if (!build(program, error)) {
        BGE_ERROR("Shader load failed: %s", error.c_str());
        return false;
    }

    if (program_) glDeleteProgram(program_);
    program_ = program;
    uniforms_.clear();
    vertex_time_ = file_time(vertex_path_);
    fragment_time_ = file_time(fragment_path_);
    BGE_DEBUG("Shader loaded: %s + %s", vertex_path_.c_str(), fragment_path_.c_str());
    return true;
}

void Shader::destroy() {
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    uniforms_.clear();
}

bool Shader::reload_if_changed() {
    if (!program_ || vertex_path_.empty()) return false;
    int64_t vt = file_time(vertex_path_);
    int64_t ft = file_time(fragment_path_);
    if (vt == vertex_time_ && ft == fragment_time_) return false;

    uint32_t program = 0;
    std::string error;
    if (!build(program, error)) {
        BGE_ERROR("Shader hot reload failed, keeping previous program: %s", error.c_str());
        vertex_time_ = vt;
        fragment_time_ = ft;
        return false;
    }
    glDeleteProgram(program_);
    program_ = program;
    uniforms_.clear();
    vertex_time_ = vt;
    fragment_time_ = ft;
    BGE_INFO("Shader reloaded: %s", vertex_path_.c_str());
    return true;
}

void Shader::use() const { glUseProgram(program_); }

int Shader::location(const char* name) {
    auto it = uniforms_.find(name);
    if (it != uniforms_.end()) return it->second;
    int loc = glGetUniformLocation(program_, name);
    uniforms_.emplace(name, loc);
    return loc;
}

void Shader::set_int(const char* name, int value) { glUniform1i(location(name), value); }
void Shader::set_float(const char* name, float value) { glUniform1f(location(name), value); }
void Shader::set_vec2(const char* name, const Vec2& v) { glUniform2f(location(name), v.x, v.y); }
void Shader::set_vec3(const char* name, const Vec3& v) { glUniform3f(location(name), v.x, v.y, v.z); }
void Shader::set_vec4(const char* name, const Vec4& v) { glUniform4f(location(name), v.x, v.y, v.z, v.w); }
void Shader::set_mat4(const char* name, const Mat4& m) { glUniformMatrix4fv(location(name), 1, GL_FALSE, m.m); }
void Shader::set_int_array(const char* name, const int* values, int count) {
    if (count > 0) glUniform1iv(location(name), count, values);
}
void Shader::set_float_array(const char* name, const float* values, int count) {
    if (count > 0) glUniform1fv(location(name), count, values);
}
void Shader::set_vec2_array(const char* name, const Vec2* values, int count) {
    if (count > 0) glUniform2fv(location(name), count, &values[0].x);
}
void Shader::set_vec3_array(const char* name, const Vec3* values, int count) {
    if (count > 0) glUniform3fv(location(name), count, &values[0].x);
}

} // namespace bge
