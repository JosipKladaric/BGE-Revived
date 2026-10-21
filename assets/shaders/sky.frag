#version 460 core

in vec2 v_uv;
out vec4 frag_color;

uniform vec3  u_forward;
uniform vec3  u_right;
uniform vec3  u_up;
uniform float u_tan_half_fov;
uniform float u_aspect;

uniform vec3 u_sky_top;
uniform vec3 u_sky_horizon;
uniform vec3 u_ground;
uniform vec3 u_sun_dir;
uniform vec3 u_sun_color;
uniform int  u_flat;
uniform sampler2D u_environment_tex;
uniform int  u_use_environment;
uniform float u_environment_strength;

const vec2 INV_ATAN = vec2(0.15915494309, 0.31830988618);

vec2 equirect_uv(vec3 dir) {
    // Blender's world mapping: +X is at u = 0.25, -Z (the default view
    // direction) is at u = 0.5 and the zenith is at v = 1.
    return vec2(atan(-dir.x, -dir.z) * INV_ATAN.x + 0.5,
                asin(clamp(dir.y, -1.0, 1.0)) * INV_ATAN.y + 0.5);
}

void main() {
    if (u_flat != 0 && u_use_environment == 0) {
        frag_color = vec4(u_sky_top, 1.0);
        return;
    }

    vec2 ndc = v_uv * 2.0 - 1.0;
    vec3 ray = normalize(u_forward +
                         u_right * (ndc.x * u_tan_half_fov * u_aspect) +
                         u_up * (ndc.y * u_tan_half_fov));

    vec3 sky;
    if (u_use_environment != 0) {
        sky = texture(u_environment_tex, equirect_uv(ray)).rgb * u_environment_strength;
    } else {
        float height = max(ray.y, 0.0);
        sky = mix(u_sky_horizon, u_sky_top, pow(height, 0.55));
        float below = 1.0 - smoothstep(-0.12, 0.0, ray.y);
        sky = mix(sky, u_ground, below * 0.7);

        vec3 sun_dir = normalize(u_sun_dir);
        float sun_dot = max(dot(ray, sun_dir), 0.0);
        vec3 sun = u_sun_color * (pow(sun_dot, 1400.0) * 18.0 +
                                  pow(sun_dot, 64.0) * 0.28 +
                                  pow(sun_dot, 8.0) * 0.10);
        sky += sun;
    }

    // A tiny hash dither keeps the large gradient from banding in 16-bit HDR.
    float dither = fract(sin(dot(gl_FragCoord.xy, vec2(12.9898, 78.233))) * 43758.5453);
    sky += (dither - 0.5) / 1024.0;

    frag_color = vec4(sky, 1.0);
}
