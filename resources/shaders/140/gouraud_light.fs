#version 140

#define INTENSITY_CORRECTION 0.6
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0
const vec3 LIGHT_FRONT_DIR = vec3(0.6985074, 0.1397015, 0.6985074);
#define LIGHT_FRONT_DIFFUSE  (0.3 * INTENSITY_CORRECTION)
#define INTENSITY_AMBIENT    0.3

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

// Tisma Slicer (phase 7): lighting computed per pixel (the same lights as the vertex shader), disabled by default.
uniform bool tisma_per_pixel;

// x = diffuse, y = specular
vec2 tisma_lighting(vec3 eye_normal, vec3 eye_pos)
{
    vec3 n = normalize(eye_normal);
    float NdotL = max(dot(n, LIGHT_TOP_DIR), 0.0);
    float shadow = tisma_shadow(eye_pos, NdotL);
    float diffuse = INTENSITY_AMBIENT + shadow * NdotL * LIGHT_TOP_DIFFUSE + max(dot(n, LIGHT_FRONT_DIR), 0.0) * LIGHT_FRONT_DIFFUSE;
    float specular = shadow * LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(eye_pos), reflect(-LIGHT_TOP_DIR, n)), 0.0), LIGHT_TOP_SHININESS);
    return vec2(diffuse, specular);
}

uniform vec4 uniform_color;
uniform float emission_factor;

// x = tainted, y = specular;
in vec2 intensity;
in vec3 tisma_eye_normal;
in vec3 tisma_eye_pos;

out vec4 out_color;

void main()
{
    vec2 light = (tisma_per_pixel || tisma_shadows) ? tisma_lighting(tisma_eye_normal, tisma_eye_pos) : intensity;
    out_color = vec4(vec3(light.y) + uniform_color.rgb * (light.x + emission_factor), uniform_color.a);
}
