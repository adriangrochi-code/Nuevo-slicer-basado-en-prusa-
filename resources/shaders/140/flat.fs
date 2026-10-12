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

uniform vec4 uniform_color;

out vec4 out_color;

void main()
{
    out_color = vec4(uniform_color.rgb * tisma_bed_shadow(tisma_eye_pos), uniform_color.a);
}
