#version 150 core
flat in vec3 normal;

out vec4 out_color;

uniform sampler2D depth_map;

void main()
{
    vec3 res = vec3(0,0,0);
#if 1
    float alpha = 0.7;
    if (normal.x != 0) {
        res = vec3(1,0,0);
    } else if (normal.y != 0) {
        res = vec3(0,1,0);
    } else if (normal.z != 0) {
        res = vec3(0,0,1);
    }

#else
    float alpha = 0.7;
    if (normal.x != 0) {
        if (normal.x == 1) {
            res = vec3(1,0,0);
        } else {
            res = vec3(1,1,0);
        }
    } else if (normal.y != 0) {
        if (normal.y == 1) {
            res = vec3(0,1,0);
        } else {
            res = vec3(0,1,1);
        }
    } else if (normal.z != 0) {
        if (normal.z == 1) {
            res = vec3(0,0,1);
        } else {
            res = vec3(1,0,1);
        }
    }
#endif

    float prev_depth = texelFetch (depth_map, ivec2(gl_FragCoord.xy), 0).r;
    if (gl_FragCoord.z <= prev_depth) {
        discard;
    } else {
        out_color = vec4(res * alpha, alpha);
    }
}
