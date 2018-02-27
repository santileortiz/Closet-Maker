#version 400 core
flat in vec3 normal;

out vec4 out_color;

uniform vec4 color;
uniform sampler2DMS peel_depth_map;

void main()
{
    float value = 0.9;
    if (normal.x != 0) {
        value *= 0.8;
    } else if (normal.z != 0) {
        value *= 0.6;
    }

    float sample_depth = texelFetch (peel_depth_map, ivec2(gl_FragCoord.xy), gl_SampleID).r;

    if (gl_FragCoord.z <= sample_depth) {
        discard;
    } else {
        out_color = vec4(color.rgb * value * color.a, color.a);
    }
}
