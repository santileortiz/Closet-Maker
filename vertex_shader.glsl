#version 150 core
in vec3 position;
in vec3 in_normal;

out vec3 normal;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

void main()
{
    normal = in_normal;
    gl_Position = proj * view * model * vec4(position, 1.0);
}
