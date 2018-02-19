#version 150 core
in vec3 normal;

out vec4 out_color;

void main()
{
    float value = 0.9;
    if (normal.x != 0) {
        value *= 0.8;
    } else if (normal.z != 0) {
        value *= 0.6;
    }
    out_color = vec4 (value,value,value,1);
}
