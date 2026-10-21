#version 460 core

in vec2 v_uv;
out vec4 frag_color;

uniform sampler2D u_scene;
uniform float u_exposure;
uniform int   u_fxaa;
uniform int   u_view_transform;
uniform int   u_grade;
uniform vec2  u_texel;
uniform float u_vignette;

float luma(vec3 c) {
    return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

vec3 aces(vec3 x) {
    x = max(x, vec3(0.0));
    return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

vec3 srgb_encode(vec3 x) {
    x = max(x, vec3(0.0));
    vec3 low = x * 12.92;
    vec3 high = 1.055 * pow(x, vec3(1.0 / 2.4)) - 0.055;
    return mix(low, high, step(vec3(0.0031308), x));
}

vec3 sample_scene(vec2 uv) {
    return texture(u_scene, uv).rgb * u_exposure;
}

vec3 apply_fxaa(vec3 rgb_m, vec2 uv) {
    vec3 rgb_nw = sample_scene(uv + vec2(-1.0, -1.0) * u_texel);
    vec3 rgb_ne = sample_scene(uv + vec2( 1.0, -1.0) * u_texel);
    vec3 rgb_sw = sample_scene(uv + vec2(-1.0,  1.0) * u_texel);
    vec3 rgb_se = sample_scene(uv + vec2( 1.0,  1.0) * u_texel);

    float luma_m  = luma(rgb_m);
    float luma_nw = luma(rgb_nw);
    float luma_ne = luma(rgb_ne);
    float luma_sw = luma(rgb_sw);
    float luma_se = luma(rgb_se);

    float luma_min = min(luma_m, min(min(luma_nw, luma_ne), min(luma_sw, luma_se)));
    float luma_max = max(luma_m, max(max(luma_nw, luma_ne), max(luma_sw, luma_se)));

    if (luma_max - luma_min < max(0.0312, luma_max * 0.125)) {
        return rgb_m;
    }

    vec2 dir;
    dir.x = -((luma_nw + luma_ne) - (luma_sw + luma_se));
    dir.y =  ((luma_nw + luma_sw) - (luma_ne + luma_se));

    float dir_reduce = max((luma_nw + luma_ne + luma_sw + luma_se) * 0.25 * 0.03125, 0.0078125);
    float rcp_dir_min = 1.0 / (min(abs(dir.x), abs(dir.y)) + dir_reduce);
    dir = clamp(dir * rcp_dir_min, vec2(-8.0), vec2(8.0)) * u_texel;

    vec3 rgb_a = 0.5 * (sample_scene(uv + dir * (1.0 / 3.0 - 0.5)) +
                        sample_scene(uv + dir * (2.0 / 3.0 - 0.5)));
    vec3 rgb_b = rgb_a * 0.5 + 0.25 * (sample_scene(uv + dir * -0.5) +
                                       sample_scene(uv + dir * 0.5));

    float luma_b = luma(rgb_b);
    return (luma_b < luma_min || luma_b > luma_max) ? rgb_a : rgb_b;
}

void main() {
    vec3 color = sample_scene(v_uv);
    if (u_fxaa != 0) color = apply_fxaa(color, v_uv);

    if (u_view_transform == 1) {
        // Blender "Standard": clip to display range, then encode sRGB.
        color = clamp(color, 0.0, 1.0);
    } else {
        // ACES approximation used for AgX/Filmic scenes until a LUT is added.
        color = aces(color);
    }
    color = srgb_encode(color);

    if (u_grade != 0) {
        float l = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(l), color, 1.10);
        color = clamp((color - 0.5) * 1.04 + 0.5, 0.0, 1.0);
    }

    vec2 q = v_uv - 0.5;
    float vignette = 1.0 - u_vignette * dot(q, q) * 2.0;
    color *= clamp(vignette, 0.0, 1.0);

    frag_color = vec4(color, 1.0);
}
