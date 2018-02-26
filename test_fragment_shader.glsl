#version 150 core
flat in vec3 normal;

out vec4 out_color;

void main()
{
    vec4 res = vec4(0,0,0,0);
    if (normal.x != 0) {
        if (normal.x == 1) {
            res = vec4(1,0,0,1);
        } else {
            res = vec4(1,1,0,1);
        }
    } else if (normal.y != 0) {
        if (normal.y == 1) {
            res = vec4(0,1,0,1);
        } else {
            res = vec4(0,1,1,1);
        }
    } else if (normal.z != 0) {
        if (normal.z == 1) {
            res = vec4(0,0,1,1);
        } else {
            res = vec4(1,0,1,1);
        }
    }

    out_color = res;
}
