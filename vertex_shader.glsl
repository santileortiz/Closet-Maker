#version 150 core
in vec3 position;
in float color_in;

out vec4 vert_color;

uniform mat4 model;
uniform mat4 view;
uniform mat4 proj;

void main()
{
    if (position.x > 0 && position.z > 0 && position.y > 0) {
        vert_color = vec4(vec3(1,0,0),1);
    } else if (position.x < 0 && position.z < 0 && position.y < 0) {
        vert_color = vec4(vec3(0,1,0),1);
    } else {
        vert_color = vec4(vec3(1,1,1)*color_in,1);
    }
    gl_Position = proj * view * model * vec4(position, 1.0);
}
