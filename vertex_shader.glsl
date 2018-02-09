#version 150 core
in vec3 position;
in float color_in;

out vec4 vert_color;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;
uniform float color_override;

void main()
{
    vec3 rgb_components;
    if (position.x > 0 && position.z > 0 && position.y > 0) {
        rgb_components = vec3(1,0,0);
    } else if (position.x < 0 && position.z < 0 && position.y > 0) {
        rgb_components = vec3(0,1,0);
    } else {
        rgb_components = vec3(1,1,1)*color_in;
    }
    vert_color = vec4(rgb_components*color_override,1);
    gl_Position = proj * view * model * vec4(position, 1.0);
}
