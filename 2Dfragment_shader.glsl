#version 150 core

in vec2 tex_coord;

out vec4 out_color;

uniform sampler2D tex;
uniform sampler2DMS texMS;
uniform bool ignore_alpha;
uniform bool multisampled_texture;

void main ()
{
    vec4 r_texel;
    if (multisampled_texture) {
        ivec2 sample_coord = ivec2(textureSize (texMS) * tex_coord);
        r_texel = vec4 (0,0,0,0);
        for (int i=0; i<4; i++) {
            r_texel += texelFetch (texMS, sample_coord, i);
        }
        r_texel /= 4;
    } else {
        r_texel = texture (tex, tex_coord);
    }

    if (ignore_alpha) {
        out_color = vec4 (r_texel.r, r_texel.g, r_texel.b, 1);
    } else {
        out_color = r_texel;
    }
}
