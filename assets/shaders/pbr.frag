#version 460 core

in vec3 v_world_pos;
in vec3 v_normal;
in vec2 v_uv;
in vec4 v_tint;

out vec4 frag_color;

const int MAX_LIGHTS = 16;
const float PI = 3.14159265359;
const vec2 INV_ATAN = vec2(0.15915494309, 0.31830988618);

// Blender light types: 0 sun, 1 point, 2 spot, 3 area.
uniform int   u_light_count;
uniform int   u_light_type[MAX_LIGHTS];
uniform vec3  u_light_position[MAX_LIGHTS];
uniform vec3  u_light_direction[MAX_LIGHTS];
uniform vec3  u_light_color[MAX_LIGHTS];
uniform float u_light_energy[MAX_LIGHTS];
uniform float u_light_radius[MAX_LIGHTS];
uniform vec2  u_light_size[MAX_LIGHTS];
uniform float u_light_spot_cos[MAX_LIGHTS];
uniform float u_light_spot_inner[MAX_LIGHTS];
uniform float u_light_range[MAX_LIGHTS];

uniform int   u_shadow_light;
uniform vec3  u_shadow_dir;
uniform mat4  u_light_vp;
uniform int   u_has_shadows;
uniform sampler2DShadow u_shadow_map;

uniform vec3  u_sun_dir;
uniform vec3  u_sun_color;

uniform vec3  u_camera_pos;
uniform sampler2D u_albedo_tex;
uniform sampler2D u_normal_tex;
uniform sampler2D u_environment_tex;
uniform vec4  u_base_color;
uniform float u_metallic;
uniform float u_roughness;
uniform vec3  u_emissive;
uniform float u_uv_scale;
uniform int   u_use_albedo;
uniform int   u_use_normal;
uniform float u_normal_strength;
uniform int   u_alpha_clip;
uniform int   u_use_environment;
uniform float u_environment_strength;

uniform vec3  u_sky_ambient;
uniform vec3  u_ground_ambient;
uniform vec3  u_env_top;
uniform vec3  u_env_horizon;
uniform vec3  u_env_ground;
uniform vec3  u_fog_color;
uniform float u_fog_density;

vec2 equirect_uv(vec3 dir) {
    // Blender's world mapping: +X is at u = 0.25, -Z (the default view
    // direction) is at u = 0.5 and the zenith is at v = 1.
    return vec2(atan(-dir.x, -dir.z) * INV_ATAN.x + 0.5,
                asin(clamp(dir.y, -1.0, 1.0)) * INV_ATAN.y + 0.5);
}

vec3 environment_radiance(vec3 dir, float lod) {
    if (u_use_environment != 0) {
        return textureLod(u_environment_tex, equirect_uv(dir), lod).rgb * u_environment_strength;
    }
    float up = clamp(dir.y, 0.0, 1.0);
    vec3 base = mix(u_env_horizon, u_env_top, pow(up, 0.70));
    float below = 1.0 - smoothstep(-0.12, 0.04, dir.y);
    base = mix(base, u_env_ground, below);
    float sun = pow(max(dot(dir, normalize(u_sun_dir)), 0.0), 64.0);
    return base + u_sun_color * sun * 0.20;
}

float distribution_ggx(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float d = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * d * d + 1e-7);
}

float geometry_schlick_ggx(float NdotV, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + 1e-7);
}

float geometry_smith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometry_schlick_ggx(max(dot(N, V), 0.0), roughness) *
           geometry_schlick_ggx(max(dot(N, L), 0.0), roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

float shadow_factor(vec3 world_pos, vec3 N) {
    if (u_has_shadows == 0 || u_shadow_light < 0) return 1.0;

    vec3 offset_pos = world_pos + N * 0.035;
    vec4 light_clip = u_light_vp * vec4(offset_pos, 1.0);
    vec3 proj = light_clip.xyz / light_clip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0 || proj.x < 0.0 || proj.x > 1.0 || proj.y < 0.0 || proj.y > 1.0) return 1.0;

    float ndl = max(dot(N, -normalize(u_shadow_dir)), 0.0);
    float bias = max(0.0012 * (1.0 - ndl), 0.0002);
    vec2 texel = 1.0 / vec2(textureSize(u_shadow_map, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            sum += texture(u_shadow_map, vec3(proj.xy + vec2(x, y) * texel, proj.z - bias));
        }
    }
    return sum / 9.0;
}

// Screen-space tangent frame: adequate for normal maps on imported static meshes
// and avoids adding tangents to the packed vertex format.
vec3 apply_normal_map(vec3 N, vec2 uv) {
    if (u_use_normal == 0) return N;
    vec3 dp1 = dFdx(v_world_pos);
    vec3 dp2 = dFdy(v_world_pos);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);
    vec3 T = dp1 * duv2.y - dp2 * duv1.y;
    T -= N * dot(N, T);
    if (dot(T, T) < 1e-12) return N;
    T = normalize(T);
    vec3 B = normalize(cross(N, T));
    vec3 tangent_normal = texture(u_normal_tex, uv).xyz * 2.0 - 1.0;
    tangent_normal.xy *= u_normal_strength;
    return normalize(T * tangent_normal.x + B * tangent_normal.y + N * tangent_normal.z);
}

void main() {
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_camera_pos - v_world_pos);

    vec4 albedo_sample = vec4(1.0);
    if (u_use_albedo != 0) {
        albedo_sample = texture(u_albedo_tex, v_uv * u_uv_scale);
    }
    float alpha = u_base_color.a * v_tint.a * albedo_sample.a;
    if (u_alpha_clip != 0 && alpha < 0.5) discard;

    N = apply_normal_map(N, v_uv * u_uv_scale);

    vec3 albedo = u_base_color.rgb * v_tint.rgb * albedo_sample.rgb;
    float metallic = clamp(u_metallic, 0.0, 1.0);
    float roughness = clamp(u_roughness, 0.045, 1.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);

    vec3 direct = vec3(0.0);
    for (int i = 0; i < u_light_count; ++i) {
        int type = u_light_type[i];
        vec3 L;
        vec3 radiance = u_light_color[i] * max(u_light_energy[i], 0.0);

        if (type == 0) {
            L = normalize(-u_light_direction[i]);
        } else {
            vec3 to_light = u_light_position[i] - v_world_pos;
            float dist = length(to_light);
            if (dist < 1e-5) continue;
            L = to_light / dist;

            // Blender area lights only emit from their front side.
            if (type == 3 && dot(N, normalize(u_light_direction[i])) > 0.0) continue;

            // Point/spot/area power is radiant watts. The 1/(4*pi*d^2) term
            // makes the result comparable to a sun's W/m^2 irradiance.
            float softness = max(u_light_radius[i], type == 3 ? 0.25 * max(u_light_size[i].x, u_light_size[i].y) : 0.0);
            float d2 = max(dist * dist, softness * softness + 1e-4);
            radiance /= (4.0 * PI * d2);

            if (u_light_range[i] > 0.0) {
                radiance *= 1.0 - smoothstep(u_light_range[i] * 0.75, u_light_range[i], dist);
            }

            if (type == 2) {
                float cos_angle = dot(-L, normalize(u_light_direction[i]));
                float cone = smoothstep(u_light_spot_cos[i], u_light_spot_inner[i], cos_angle);
                radiance *= cone;
            }
        }

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL <= 0.0) continue;

        float rough = roughness;
        if (type == 3) {
            float area = max(u_light_size[i].x, u_light_size[i].y);
            float dist = max(length(u_light_position[i] - v_world_pos), 0.001);
            rough = clamp(roughness + area / (dist * 6.0), 0.045, 1.0);
        }

        vec3 H = normalize(V + L);
        float NdotV = max(dot(N, V), 0.0);
        vec3 F = fresnel_schlick(max(dot(H, V), 0.0), F0);
        float D = distribution_ggx(N, H, rough);
        float G = geometry_smith(N, V, L, rough);
        vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
        vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);

        float shadow = (i == u_shadow_light) ? shadow_factor(v_world_pos, N) : 1.0;
        direct += (kD * albedo / PI + specular) * radiance * NdotL * shadow;
    }

    vec3 ambient;
    if (u_use_environment != 0) {
        // Cheap irradiance approximation: normal direction plus a broad upward
        // term. The specular reflection uses the lat-long map directly.
        ambient = environment_radiance(N, 5.0) * 0.65 +
                  environment_radiance(vec3(0.0, 1.0, 0.0), 6.0) * 0.25 +
                  environment_radiance(vec3(0.0, 0.0, 1.0), 6.0) * 0.10;
    } else {
        ambient = mix(u_ground_ambient, u_sky_ambient, N.y * 0.5 + 0.5);
    }

    vec3 R = reflect(-V, N);
    vec3 env_reflect = environment_radiance(R, roughness * 5.0);
    vec3 ambient_diffuse = ambient * albedo * (1.0 - metallic);
    vec3 ambient_spec = env_reflect * F0 * (1.0 - roughness * 0.8) * 1.2;
    vec3 color = direct + ambient_diffuse + ambient_spec + u_emissive;

    float dist = length(u_camera_pos - v_world_pos);
    float fog = 1.0 - exp(-dist * u_fog_density);
    color = mix(color, u_fog_color, clamp(fog, 0.0, 1.0));

    frag_color = vec4(color, alpha);
}
