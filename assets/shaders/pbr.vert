#version 460 core

layout(location = 0) in vec3 a_position;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec2 a_uv;

layout(location = 4) in vec4 a_model0;
layout(location = 5) in vec4 a_model1;
layout(location = 6) in vec4 a_model2;
layout(location = 7) in vec4 a_model3;

layout(location = 8) in vec3 a_normal0;
layout(location = 9) in vec3 a_normal1;
layout(location = 10) in vec3 a_normal2;
layout(location = 11) in vec4 a_tint;

uniform mat4 u_view_proj;

out vec3 v_world_pos;
out vec3 v_normal;
out vec2 v_uv;
out vec4 v_tint;

void main() {
    mat4 model = mat4(a_model0, a_model1, a_model2, a_model3);
    mat3 normal_matrix = mat3(a_normal0, a_normal1, a_normal2);
    vec4 world = model * vec4(a_position, 1.0);
    v_world_pos = world.xyz;
    v_normal = normal_matrix * a_normal;
    v_uv = a_uv;
    v_tint = a_tint;
    gl_Position = u_view_proj * world;
}
