#version 140

const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);

// Tisma Slicer (phase 7): shadows of the main light (see src/slic3r/GUI/TismaShading.cpp), disabled by default.
uniform bool tisma_shadows;
uniform mat4 tisma_eye_to_shadow;
uniform sampler2D tisma_shadow_map;
uniform float tisma_shadow_intensity;

float tisma_shadow(vec3 eye_pos, float NdotL)
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

// Tisma: darkening of the bed in the shadows.
#define TISMA_BED_SHADOW 0.6
float tisma_bed_shadow(vec3 eye_pos)
{
    if (!tisma_shadows)
        return 1.0;
    vec3 n = normalize(cross(dFdx(eye_pos), dFdy(eye_pos)));
    float NdotL = abs(dot(n, LIGHT_TOP_DIR));
    return mix(1.0, tisma_shadow(eye_pos, NdotL), TISMA_BED_SHADOW);
}

in vec3 tisma_eye_pos;

// Tisma Slicer (phase 7c): output to the G-buffer of the deferred shading (see src/slic3r/GUI/TismaShading.cpp), disabled
// by default: the color before lighting, the material in the alpha and the eye normal. Octahedral normal encoding of
// PrusaSlicer 3.0 (gbuffer_ao.fs, Copyright (c) Prusa Research, AGPLv3).
uniform bool tisma_gbuffer;
uniform float tisma_material;
out vec2 tisma_out_normal;

vec2 tisma_encode_normal(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    return (n.z >= 0.0) ? n.xy : (1.0 - abs(n.yx)) * vec2(n.x >= 0.0 ? 1.0 : -1.0, n.y >= 0.0 ? 1.0 : -1.0);
}

// Color of the bed model under the texture (DEFAULT_MODEL_COLOR of src/slic3r/GUI/3DBed.cpp): in the G-buffer the
// texture is composed over it, without blending.
const vec3 TISMA_BED_MODEL_COLOR = vec3(0.169, 0.169, 0.192);

const vec3 back_color_dark  = vec3(0.235, 0.235, 0.235);
const vec3 back_color_light = vec3(0.365, 0.365, 0.365);

uniform sampler2D in_texture;
uniform bool transparent_background;
uniform bool svg_source;

in vec2 tex_coord;

out vec4 out_color;

vec4 svg_color()
{
    // takes foreground from texture
    vec4 fore_color = texture(in_texture, tex_coord);

    // calculates radial gradient
    vec3 back_color = vec3(mix(back_color_light, back_color_dark, smoothstep(0.0, 0.5, length(abs(tex_coord.xy) - vec2(0.5)))));

    // blends foreground with background
    return vec4(mix(back_color, fore_color.rgb, fore_color.a), transparent_background ? fore_color.a : 1.0);
}

vec4 non_svg_color()
{
    // takes foreground from texture
    vec4 color = texture(in_texture, tex_coord);
    return vec4(color.rgb, transparent_background ? color.a * 0.25 : color.a);
}

void main()
{
	vec4 color = svg_source ? svg_color() : non_svg_color();
    if (tisma_gbuffer) {
        vec3 n = normalize(cross(dFdx(tisma_eye_pos), dFdy(tisma_eye_pos)));
        if (dot(n, tisma_eye_pos) > 0.0)
            n = -n;
        out_color = vec4(mix(TISMA_BED_MODEL_COLOR, color.rgb, color.a), tisma_material);
        tisma_out_normal = tisma_encode_normal(n);
        return;
    }
	color.a = transparent_background ? color.a * 0.5 : color.a;
	color.rgb *= tisma_bed_shadow(tisma_eye_pos);
	out_color = color;
}