#version 140

const vec3 ZERO = vec3(0.0, 0.0, 0.0);
const float EPSILON = 0.0001;

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

struct PrintVolumeDetection
{
	// 0 = rectangle, 1 = circle, 2 = custom, 3 = invalid
	int type;
    // type = 0 (rectangle):
    // x = min.x, y = min.y, z = max.x, w = max.y
    // type = 1 (circle):
    // x = center.x, y = center.y, z = radius
	vec4 xy_data;
    // x = min z, y = max z
	vec2 z_data;
};

struct SlopeDetection
{
    bool actived;
	float normal_z;
    mat3 volume_world_normal_matrix;
};

uniform vec4 uniform_color;
uniform bool use_color_clip_plane;
uniform vec4 uniform_color_clip_plane_1;
uniform vec4 uniform_color_clip_plane_2;
uniform SlopeDetection slope;

#ifdef ENABLE_ENVIRONMENT_MAP
    uniform sampler2D environment_tex;
    uniform bool use_environment_tex;
#endif // ENABLE_ENVIRONMENT_MAP

uniform PrintVolumeDetection print_volume;

in vec3 clipping_planes_dots;
in float color_clip_plane_dot;

// x = diffuse, y = specular;
in vec2 intensity;

in vec4 world_pos;
in float world_normal_z;
in vec3 eye_normal;
in vec3 tisma_eye_pos;

out vec4 out_color;

void main()
{
    if (any(lessThan(clipping_planes_dots, ZERO)))
        discard;

    vec2 light = (tisma_per_pixel || tisma_shadows) ? tisma_lighting(eye_normal, tisma_eye_pos) : intensity;

    vec4 color;
	if (use_color_clip_plane) {
		color.rgb = (color_clip_plane_dot < 0.0) ? uniform_color_clip_plane_1.rgb : uniform_color_clip_plane_2.rgb;
		color.a = uniform_color.a;
    }
    else
	    color = uniform_color;

    if (slope.actived && world_normal_z < slope.normal_z - EPSILON) {
        color.rgb = vec3(0.7, 0.7, 1.0);
        color.a = 1.0;
    }
	
    // if the fragment is outside the print volume -> use darker color
	vec3 pv_check_min = ZERO;
	vec3 pv_check_max = ZERO;
    if (print_volume.type == 0) {
		// rectangle
		pv_check_min = world_pos.xyz - vec3(print_volume.xy_data.x, print_volume.xy_data.y, print_volume.z_data.x);
		pv_check_max = world_pos.xyz - vec3(print_volume.xy_data.z, print_volume.xy_data.w, print_volume.z_data.y);
	}
	else if (print_volume.type == 1) {
		// circle
		float delta_radius = print_volume.xy_data.z - distance(world_pos.xy, print_volume.xy_data.xy);
		pv_check_min = vec3(delta_radius, 0.0, world_pos.z - print_volume.z_data.x);
		pv_check_max = vec3(0.0, 0.0, world_pos.z - print_volume.z_data.y);
	}
	color.rgb = (any(lessThan(pv_check_min, ZERO)) || any(greaterThan(pv_check_max, ZERO))) ? mix(color.rgb, ZERO, 0.3333) : color.rgb;
	
#ifdef ENABLE_ENVIRONMENT_MAP
    if (use_environment_tex)
        out_color = vec4(0.45 * texture(environment_tex, normalize(eye_normal).xy * 0.5 + 0.5).xyz + 0.8 * color.rgb * light.x, color.a);
    else
#endif
        out_color = vec4(vec3(light.y) + color.rgb * light.x, color.a);
}
