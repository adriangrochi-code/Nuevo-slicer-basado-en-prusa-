#version 140

// Tisma Slicer (phase 7c): box blur of the ambient occlusion along one direction, as in PrusaSlicer 3.0 (ao_hblur.fs,
// ao_vblur.fs, Copyright (c) Prusa Research, AGPLv3).

uniform sampler2D in_tex;
uniform int filter_size;
// (1, 0) horizontal, (0, 1) vertical
uniform vec2 direction;

in vec2 tex_coord;

out vec4 out_color;

void main()
{
    vec2 texel_size = 1.0 / vec2(textureSize(in_tex, 0));
    int half_filter_size = filter_size / 2;
    float result = 0.0;
    for (int i = -half_filter_size; i < -half_filter_size + filter_size; ++i) {
        result += texture(in_tex, tex_coord + float(i) * direction * texel_size).r;
    }
    out_color = vec4(result / float(filter_size));
}
