#version 400 core
flat in vec3 normal;

out vec4 out_color;

uniform vec4 color;

uniform sampler2DMS peel_depth_map;
uniform sampler2DMS opaque_depth_map;
vec4 apply_depth_peeling (const in vec4 color)
{
    float peel_depth = texelFetch (peel_depth_map, ivec2(gl_FragCoord.xy), gl_SampleID).r;
    float opaque_depth = texelFetch (opaque_depth_map, ivec2(gl_FragCoord.xy), gl_SampleID).r;

    if (gl_FragCoord.z <= peel_depth || gl_FragCoord.z >= opaque_depth) {
        discard;
    } else {
        return color;
    }
}

void main()
{
    float value = 0.9;
    if (normal.x != 0) {
        value *= 0.8;
    } else if (normal.z != 0) {
        value *= 0.6;
    }

    out_color = apply_depth_peeling (vec4(color.rgb * value * color.a, color.a));
}
