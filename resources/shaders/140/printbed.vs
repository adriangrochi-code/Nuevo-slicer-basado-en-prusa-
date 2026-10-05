#version 140

uniform mat4 view_model_matrix;
uniform mat4 projection_matrix;

in vec3 v_position;

// Tisma: position in eye space for the shadows.
out vec3 tisma_eye_pos;
in vec2 v_tex_coord;

out vec2 tex_coord;

void main()
{
	tex_coord = v_tex_coord;
    vec4 eye_pos = view_model_matrix * vec4(v_position, 1.0);
    tisma_eye_pos = eye_pos.xyz;
    gl_Position = projection_matrix * eye_pos;
}
