#version 460 core

layout(location = 0) in vec3 a_position;

layout(location = 4) in vec4 a_model0;
layout(location = 5) in vec4 a_model1;
layout(location = 6) in vec4 a_model2;
layout(location = 7) in vec4 a_model3;

uniform mat4 u_light_vp;

void main() {
    mat4 model = mat4(a_model0, a_model1, a_model2, a_model3);
    gl_Position = u_light_vp * model * vec4(a_position, 1.0);
}
