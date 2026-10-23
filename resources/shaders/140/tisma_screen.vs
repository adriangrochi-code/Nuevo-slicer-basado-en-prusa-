#version 140

// Tisma Slicer (phase 7c): full screen quad of the passes of the deferred shading.

in vec2 v_position;
in vec2 v_tex_coord;

out vec2 tex_coord;

void main()
{
    tex_coord = v_tex_coord;
    gl_Position = vec4(v_position, 0.0, 1.0);
}
