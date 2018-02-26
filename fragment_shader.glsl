#version 150 core
flat in vec3 normal;

out vec4 out_color;

uniform vec4 color;

void main()
{
    float value = 0.9;
    if (normal.x != 0) {
        value *= 0.8;
    } else if (normal.z != 0) {
        value *= 0.6;
    }

    out_color = vec4 (color.r * value * color.a,
                      color.g * value * color.a,
                      color.b * value * color.a,
                      color.a);
}
