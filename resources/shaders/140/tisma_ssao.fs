#version 140

// Tisma Slicer (phase 7c): screen space ambient occlusion from the depth and the normals of the G-buffer.
// The method and its parameters are the ones of PrusaSlicer 3.0 (ao_texture.fs, Copyright (c) Prusa Research, AGPLv3):
// a hemisphere of samples around the normal, rotated by a 4x4 noise texture, compared with the depth buffer.

uniform float intensity;
uniform float radius;
uniform float bias;
uniform float z_threshold;
uniform int kernel_size;
uniform vec2 noise_scale;
uniform mat4 projection_matrix;
uniform mat4 inverse_projection_matrix;
uniform vec3 kernel[64];

uniform sampler2D g_depth;
uniform sampler2D g_normal;
uniform sampler2D tex_noise;

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

void main()
{
    float depth = texture(g_depth, tex_coord).r;
    if (depth >= 1.0) {
        out_color = vec4(1.0);
        return;
    }
    vec3 eye_position = eye_position_from_depth(tex_coord, depth);
    vec3 eye_normal = decode_normal(texture(g_normal, tex_coord).xy);
    vec3 random = normalize(texture(tex_noise, tex_coord * noise_scale).xyz);

    // from the tangent space of the normal to the eye space
    vec3 tangent = normalize(random - eye_normal * dot(random, eye_normal));
    vec3 bitangent = cross(eye_normal, tangent);
    mat3 TBN = mat3(tangent, bitangent, eye_normal);

    float occlusion = 0.0;
    for (int i = 0; i < kernel_size; ++i) {
        vec3 sample_ref_pos = eye_position + TBN * kernel[i] * radius;
        vec4 offset = projection_matrix * vec4(sample_ref_pos, 1.0);
        offset.xyz /= offset.w;
        offset.xyz = offset.xyz * 0.5 + 0.5;
        float sample_depth = texture(g_depth, offset.xy).r;
        vec3 sample_eye_position = eye_position_from_depth(offset.xy, sample_depth);
        float range_check = smoothstep(0.0, 1.0, z_threshold / abs(eye_position.z - sample_eye_position.z));
        occlusion += (sample_eye_position.z >= sample_ref_pos.z + bias ? 1.0 : 0.0) * range_check;
    }

    out_color = vec4(pow(1.0 - occlusion / float(kernel_size), intensity));
}
