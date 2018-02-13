#version 150 core

in vec2 position;
in vec2 tex_coord_in;

out vec2 tex_coord;

uniform mat4 transf;

void main ()
{
    tex_coord = tex_coord_in;
    gl_Position = vec4 (position, 0.0, 1.0);
}
