#version 140

// Tisma Slicer (phase 7c): lighting pass of the deferred shading (see src/slic3r/GUI/TismaShading.cpp). Reads the
// G-buffer (depth, eye normal, color and material) and lights every pixel with the lights of PrusaSlicer 2.9 (Phong) or
// with the physically based model of PrusaSlicer 3.0 (Cook-Torrance: GGX, Smith, Fresnel-Schlick, Reinhard tonemapping;
// ao_lighting.fs, Copyright (c) Prusa Research, AGPLv3), with the shadows of the main light and the ambient occlusion on
// the ambient light. Writes the depth of the G-buffer, so the rest of the scene is rendered over it.

#define PI 3.1415926535897932384626433832795
#define MAX_MATERIALS 16

#define INTENSITY_CORRECTION 0.6
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
#define INTENSITY_AMBIENT    0.3

uniform sampler2D g_depth;
uniform sampler2D g_normal;
uniform sampler2D g_color;
uniform sampler2D ssao;
uniform bool apply_ao;
uniform bool apply_pbr;
uniform float pbr_intensity;
uniform float pbr_ambient;
uniform mat4 inverse_projection_matrix;

// x = metalness, y = roughness, z = index of refraction, w = emission
uniform vec4 materials[MAX_MATERIALS];
// x = strength of the shadows, y = 1 lit / 0 only shadowed (Phong), z = specular tinted by the color (Phong)
uniform vec4 material_flags[MAX_MATERIALS];
uniform int materials_count;

uniform bool tisma_shadows;
uniform mat4 tisma_eye_to_shadow;
uniform sampler2D tisma_shadow_map;
uniform float tisma_shadow_intensity;

in vec2 tex_coord;

out vec4 out_color;

vec3 eye_position_from_depth(vec2 uv, float depth)
{
    vec4 eye = inverse_projection_matrix * vec4(vec3(uv, depth) * 2.0 - vec3(1.0), 1.0);
    return eye.xyz / eye.w;
}

vec3 decode_normal(vec2 f)
{
    vec3 n = vec3(f.xy, 1.0 - abs(f.x) - abs(f.y));
    vec2 cond = step(n.z, vec2(0.0));
    n.xy = mix(n.xy, (1.0 - abs(n.yx)) * sign(n.xy), cond.x);
    return normalize(n);
}

float shadow_factor(vec3 eye_pos, float NdotL)
{
    if (!tisma_shadows)
        return 1.0;
    vec4 p = tisma_eye_to_shadow * vec4(eye_pos, 1.0);
    vec3 c = p.xyz / p.w * 0.5 + 0.5;
    if (c.x < 0.0 || c.x > 1.0 || c.y < 0.0 || c.y > 1.0 || c.z < 0.0)
        return 1.0;
    float bias = max(0.0025 * (1.0 - NdotL), 0.0005);
    // behind the far plane of the light the shadow map is a silhouette
    float depth = (c.z > 1.0) ? 1.0 : c.z - bias;
    vec2 texel = 1.0 / vec2(textureSize(tisma_shadow_map, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            shadow += (depth > texture(tisma_shadow_map, c.xy + vec2(x, y) * texel).r) ? 1.0 : 0.0;
        }
    }
    return 1.0 - tisma_shadow_intensity * shadow / 9.0;
}

vec3 lighting_phong(vec3 color, vec3 eye_pos, vec3 n, vec4 mat, vec4 flags, float ao)
{
    float NdotL = max(dot(n, LIGHT_TOP_DIR), 0.0);
    float shadow = mix(1.0, shadow_factor(eye_pos, NdotL), flags.x);
    if (flags.y < 0.5)
        // only shadowed (the texture of the bed)
        return color * shadow * ao;
    float diffuse = INTENSITY_AMBIENT * ao + mat.w + shadow * NdotL * LIGHT_TOP_DIFFUSE + max(dot(n, LIGHT_FRONT_DIR), 0.0) * LIGHT_FRONT_DIFFUSE;
    float specular = shadow * LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(eye_pos), reflect(-LIGHT_TOP_DIR, n)), 0.0), LIGHT_TOP_SHININESS);
    return color * diffuse + (flags.z > 0.5 ? color : vec3(1.0)) * specular;
}

float ior_to_f0(float ior)
{
    float num = ior - 1.0;
    float denom = ior + 1.0;
    return num * num / (denom * denom);
}

float distribution_ggx(vec3 n, vec3 h, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(n, h), 0.0);
    float denom = (NdotH * NdotH * (a2 - 1.0) + 1.0);
    return a2 / (PI * denom * denom);
}

float geometry_schlick_ggx(float cos_theta, float roughness)
{
    float r = roughness + 1.0;
    float k = r * r / 8.0;
    return cos_theta / (cos_theta * (1.0 - k) + k);
}

float geometry_smith(vec3 n, vec3 v, vec3 l, float roughness)
{
    return geometry_schlick_ggx(max(dot(n, v), 0.0), roughness) * geometry_schlick_ggx(max(dot(n, l), 0.0), roughness);
}

vec3 fresnel_schlick(float cos_theta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cos_theta, 0.0, 1.0), 5.0);
}

vec3 light_radiance(vec3 F0, vec3 v, vec3 n, vec3 l, float radiance, vec4 mat, vec3 color)
{
    vec3 h = normalize(v + l);
    float NDF = distribution_ggx(n, h, mat.y);
    float G = geometry_smith(n, v, l, mat.y);
    vec3 F = fresnel_schlick(clamp(dot(h, v), 0.0, 1.0), F0);
    float NdotL = max(dot(n, l), 0.0);
    vec3 specular = NDF * G * F / (4.0 * max(dot(n, v), 0.0) * NdotL + 0.0001);
    // energy conservation: only non metals have diffuse light
    vec3 kD = (vec3(1.0) - F) * (1.0 - mat.x);
    return (kD * color / PI + specular) * radiance * NdotL;
}

vec3 lighting_pbr(vec3 in_color, vec3 eye_pos, vec3 n, vec4 mat, vec4 flags, float ao)
{
    vec3 color = pow(in_color, vec3(2.2));
    vec3 F0 = mix(vec3(ior_to_f0(mat.z)), color, mat.x);
    vec3 v = -normalize(eye_pos);
    float shadow = mix(1.0, shadow_factor(eye_pos, max(dot(n, LIGHT_TOP_DIR), 0.0)), flags.x);
    vec3 lo = shadow * light_radiance(F0, v, n, LIGHT_TOP_DIR, pbr_intensity * LIGHT_TOP_DIFFUSE, mat, color) +
                       light_radiance(F0, v, n, LIGHT_FRONT_DIR, pbr_intensity * LIGHT_FRONT_DIFFUSE, mat, color);
    vec3 pbr_color = (vec3(pbr_ambient + mat.w) * color + lo) * ao;
    // HDR tonemapping (Reinhard)
    return pbr_color / (pbr_color + vec3(1.0));
}

void main()
{
    float depth = texture(g_depth, tex_coord).r;
    if (depth >= 1.0)
        discard;
    vec4 color = texture(g_color, tex_coord);
    int material_id = int(color.a * 255.0 + 0.5);
    gl_FragDepth = depth;
    if (material_id >= materials_count) {
        // rendered without a material (gizmos, contours): already lit
        out_color = vec4(color.rgb, 1.0);
        return;
    }
    vec3 eye_pos = eye_position_from_depth(tex_coord, depth);
    vec3 n = decode_normal(texture(g_normal, tex_coord).xy);
    float ao = apply_ao ? texture(ssao, tex_coord).r : 1.0;
    vec4 mat = materials[material_id];
    vec4 flags = material_flags[material_id];
    out_color = vec4(apply_pbr ? lighting_pbr(color.rgb, eye_pos, n, mat, flags, ao) : lighting_phong(color.rgb, eye_pos, n, mat, flags, ao), 1.0);
}
