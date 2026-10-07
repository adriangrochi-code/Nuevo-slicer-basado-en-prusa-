#version 140

#define INTENSITY_CORRECTION 0.6

// normalized values for (-0.6/1.31, 0.6/1.31, 1./1.31)
const vec3 LIGHT_TOP_DIR = vec3(-0.4574957, 0.4574957, 0.7624929);
#define LIGHT_TOP_DIFFUSE    (0.8 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SPECULAR   (0.125 * INTENSITY_CORRECTION)
#define LIGHT_TOP_SHININESS  20.0

// normalized values for (1./1.43, 0.2/1.43, 1./1.43)
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

const vec3  ZERO    = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;

uniform vec4 uniform_color;

uniform bool volume_mirrored;

uniform mat4 view_model_matrix;
uniform mat3 view_normal_matrix;

in vec3 clipping_planes_dots;
in vec4 model_pos;

out vec4 out_color;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;
    vec3  color = uniform_color.rgb;
    float alpha = uniform_color.a;

    vec3 triangle_normal = normalize(cross(dFdx(model_pos.xyz), dFdy(model_pos.xyz)));
#ifdef FLIP_TRIANGLE_NORMALS
    triangle_normal = -triangle_normal;
#endif

    if (volume_mirrored)
        triangle_normal = -triangle_normal;

    // First transform the normal into camera space and normalize the result.
    vec3 eye_normal = normalize(view_normal_matrix * triangle_normal);

    // Compute the cos of the angle between the normal and lights direction. The light is directional so the direction is constant for every vertex.
    // Since these two are normalized the cosine is the dot product. We also need to clamp the result to the [0,1] range.
    float NdotL = max(dot(eye_normal, LIGHT_TOP_DIR), 0.0);

    // x = diffuse, y = specular;
    vec2 intensity = vec2(0.0);
    vec3 position = (view_model_matrix * model_pos).xyz;
    float shadow = tisma_shadow(position, NdotL);
    intensity.x = INTENSITY_AMBIENT + shadow * NdotL * LIGHT_TOP_DIFFUSE;
    intensity.y = shadow * LIGHT_TOP_SPECULAR * pow(max(dot(-normalize(position), reflect(-LIGHT_TOP_DIR, eye_normal)), 0.0), LIGHT_TOP_SHININESS);

    // Perform the same lighting calculation for the 2nd light source (no specular applied).
    NdotL = max(dot(eye_normal, LIGHT_FRONT_DIR), 0.0);
    intensity.x += NdotL * LIGHT_FRONT_DIFFUSE;

    out_color = vec4(vec3(intensity.y) + color * intensity.x, alpha);
}
