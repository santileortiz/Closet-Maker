/*
 * Copiright (C) 2018 Santiago León O.
 */

#if !defined(OPENGL_UTIL_H)

void debug_message_callback( GLenum source,
                             GLenum type,
                             GLuint id,
                             GLenum severity,
                             GLsizei length,
                             const GLchar* message,
                             const void* userParam )
{
    fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
             ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
             type, severity, message );
}

GLuint gl_program (const char *vertex_shader_source, const char *fragment_shader_source)
{
    bool compilation_failed = false;
    GLuint program_id = 0;
    mem_pool_t pool = {0};

    // Vertex shader
    const char* vertex_source = full_file_read (&pool, vertex_shader_source);

    GLuint vertex_shader = glCreateShader (GL_VERTEX_SHADER);
    glShaderSource (vertex_shader, 1, &vertex_source, NULL);
    glCompileShader (vertex_shader);
    GLint shader_status;
    glGetShaderiv (vertex_shader, GL_COMPILE_STATUS, &shader_status);
    if (shader_status != GL_TRUE) {
        printf ("Compilation of \"%s\" failed.\n", vertex_shader_source);
        char buffer[512];
        glGetShaderInfoLog(vertex_shader, 512, NULL, buffer);
        printf ("%s", buffer);
        compilation_failed = true;
    }

    // Fragment shader
    const char* fragment_source = full_file_read (&pool, fragment_shader_source);

    GLuint fragment_shader = glCreateShader (GL_FRAGMENT_SHADER);
    glShaderSource (fragment_shader, 1, &fragment_source, NULL);
    glCompileShader (fragment_shader);
    glGetShaderiv (fragment_shader, GL_COMPILE_STATUS, &shader_status);
    if (shader_status != GL_TRUE) {
        printf ("Compilation of \"%s\" failed.\n", fragment_shader_source);
        char buffer[512];
        glGetShaderInfoLog(fragment_shader, 512, NULL, buffer);
        printf ("%s", buffer);
        compilation_failed = true;
    }

    // Program creation
    if (!compilation_failed) {
        program_id = glCreateProgram();
        glAttachShader (program_id, vertex_shader);
        glAttachShader (program_id, fragment_shader);
        glBindFragDataLocation (program_id, 0, "out_color");
        glLinkProgram (program_id);
        glUseProgram (program_id);
    }

    mem_pool_destroy (&pool);
    return program_id;
}

void create_color_texture (GLuint *id, float width, float height, int num_samples)
{
    glGenTextures (1, id);
    if (num_samples > 0) {
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, *id);

        glTexImage2DMultisample (
            GL_TEXTURE_2D_MULTISAMPLE, num_samples, GL_RGBA,
            width, height, GL_FALSE
        );
    } else {
        glBindTexture (GL_TEXTURE_2D, *id);

        glTexImage2D (
            GL_TEXTURE_2D, 0, GL_RGBA,
            width, height, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, NULL
        );

        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

void create_depth_texture (GLuint *id, float width, float height, int num_samples)
{
    glGenTextures (1, id);
    if (num_samples > 0) {
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, *id);

        glTexImage2DMultisample (
            GL_TEXTURE_2D_MULTISAMPLE, num_samples, GL_DEPTH_COMPONENT32F,
            width, height, GL_FALSE
        );
    } else {
        glBindTexture (GL_TEXTURE_2D, *id);

        glTexImage2D (
            GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT32F,
            width, height, 0,
            GL_DEPTH_COMPONENT, GL_FLOAT, NULL
        );

        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
}

struct gl_framebuffer_t {
    GLuint fb_id;
    GLuint tex_color_buffer;
    bool multisampled;
    float width;
    float height;
};

struct gl_framebuffer_t create_framebuffer (float width, float height)
{
    struct gl_framebuffer_t framebuffer;
    framebuffer.multisampled = false;
    framebuffer.width = width;
    framebuffer.height = height;
    glGenFramebuffers (1, &framebuffer.fb_id);
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer.fb_id);

    glGenTextures (1, &framebuffer.tex_color_buffer);
    glBindTexture (GL_TEXTURE_2D, framebuffer.tex_color_buffer);

    glTexImage2D (
        GL_TEXTURE_2D, 0, GL_RGBA,
        width, height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, NULL
    );

    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D, framebuffer.tex_color_buffer, 0
    );

    GLuint depth_stencil;
    glGenRenderbuffers (1, &depth_stencil);
    glBindRenderbuffer (GL_RENDERBUFFER, depth_stencil);
    glRenderbufferStorage (
        GL_RENDERBUFFER, GL_DEPTH24_STENCIL8,
        width, height
    );

    glFramebufferRenderbuffer (
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, depth_stencil
    );

    return framebuffer;
}

struct gl_framebuffer_t create_multisampled_framebuffer (float width, float height, uint32_t num_samples)
{
    struct gl_framebuffer_t framebuffer;
    framebuffer.multisampled = true;
    framebuffer.width = width;
    framebuffer.height = height;
    glGenFramebuffers (1, &framebuffer.fb_id);
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer.fb_id);

    glGenTextures (1, &framebuffer.tex_color_buffer);
    glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, framebuffer.tex_color_buffer);

    glTexImage2DMultisample (
        GL_TEXTURE_2D_MULTISAMPLE, num_samples, GL_RGBA,
        width, height, GL_FALSE
    );

    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE, framebuffer.tex_color_buffer, 0
    );

#if 0
    // Why does this not work?, We get a FRAMEBUFFER_INCOMPLETE_MULTISAMPLE
    // response from glCheckFramebufferStatus() even though we are using the
    // same number of samples as the color texture.

    GLuint depth_stencil;
    glGenRenderbuffers (1, &depth_stencil);
    glBindRenderbuffer (GL_RENDERBUFFER, depth_stencil);
    glRenderbufferStorageMultisample (
        GL_RENDERBUFFER, num_samples, GL_DEPTH24_STENCIL8,
        width, height
    );

    glFramebufferRenderbuffer (
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER, depth_stencil
    );
#else
    GLuint depth_stencil;
    glGenTextures (1, &depth_stencil);
    glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, depth_stencil);
    glTexImage2DMultisample (
        GL_TEXTURE_2D_MULTISAMPLE, num_samples, GL_DEPTH24_STENCIL8,
        width, height, GL_FALSE
    );

    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, depth_stencil, 0
    );
#endif
    return framebuffer;
}

static inline
void draw_into_full_framebuffer (struct gl_framebuffer_t framebuffer)
{
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer.fb_id);
    glViewport (0, 0, framebuffer.width, framebuffer.height);
    glScissor (0, 0, framebuffer.width, framebuffer.height);
}

static inline
void draw_into_framebuffer_clip (struct gl_framebuffer_t framebuffer,
                                 float x, float y, float width, float height)
{
    glBindFramebuffer (GL_FRAMEBUFFER, framebuffer.fb_id);
    glViewport (x, y, width, height);
    glScissor (x, y, width, height);
}

static inline
void draw_into_window (app_graphics_t *graphics)
{
    glBindFramebuffer (GL_FRAMEBUFFER, 0);
    glViewport (0, 0, graphics->width, graphics->height);
    glScissor (0, 0, graphics->width, graphics->height);
}

struct quad_renderer_t {
    GLuint vao;
    GLuint program_id;
};

struct quad_renderer_t init_quad_renderer ()
{
    struct quad_renderer_t res = {0};
    float quad_v[] = {
       //X  Y  U    V
        -1, 1, 0.0, 1.0,
         1,-1, 1.0, 0.0,
         1, 1, 1.0, 1.0,

        -1, 1, 0.0, 1.0,
        -1,-1, 0.0, 0.0,
         1,-1, 1.0, 0.0
    };

    glGenVertexArrays (1, &res.vao);
    glBindVertexArray (res.vao);

    GLuint quad;
    glGenBuffers (1, &quad);
    glBindBuffer (GL_ARRAY_BUFFER, quad);
    glBufferData (GL_ARRAY_BUFFER, sizeof(quad_v), quad_v, GL_STATIC_DRAW);

    res.program_id = gl_program ("2Dvertex_shader.glsl", "2Dfragment_shader.glsl");
    if (!res.program_id) {
        return res;
    }

    GLuint pos_loc = glGetAttribLocation (res.program_id, "position");
    glEnableVertexAttribArray (pos_loc);
    glVertexAttribPointer (pos_loc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), 0);

    GLuint tex_coord_loc = glGetAttribLocation (res.program_id, "tex_coord_in");
    glEnableVertexAttribArray (tex_coord_loc);
    glVertexAttribPointer (tex_coord_loc, 2, GL_FLOAT, GL_FALSE, 4*sizeof(float), (void*)(2*sizeof(float)));

    GLuint tex_loc = glGetUniformLocation (res.program_id, "tex");
    glUniform1i (tex_loc, 0);

    mat4f transf = {{
         1, 0, 0, 0,
         0, 1, 0, 0,
         0, 0, 1, 0,
         0, 0, 0, 1
    }};
    glUniformMatrix4fv (glGetUniformLocation (res.program_id, "transf"), 1, GL_TRUE, transf.E);
    return res;
}

// Sets the square (in texture coordinates) from the texture with which to fill
// the quad rendered by quad_prog.
//
// TODO: Check this is pixel accurate if we care about pixel perfect texture
// blitting. For example a when blending texture drawn with cairo.
void set_texture_clip (struct quad_renderer_t *quad_prog,
                       float texture_width, float texture_height,
                       float x, float y, float width, float height)
{
    glUseProgram (quad_prog->program_id);
    dvec3 s1 = DVEC3(-1 + 2*x/texture_width, -1 + 2*y/texture_height, 0);
    dvec3 s2 = DVEC3(s1.x + 2*width/texture_width, s1.y + 2*height/texture_height, 0);
    mat4f transf = transform_from_2_points (s1, s2, DVEC3(-1,-1,0), DVEC3(1,1,0));
    glUniformMatrix4fv (glGetUniformLocation (quad_prog->program_id, "transf"), 1, GL_TRUE, transf.E);
}

// Sets the square (in texture coordinates) from the framebuffer with which to
// fill the quad rendered by quad_prog.
void set_framebuffer_clip (struct quad_renderer_t *quad_prog,
                           struct gl_framebuffer_t *fb,
                           float x, float y, float width, float height)
{
    set_texture_clip (quad_prog, fb->width, fb->height, x, y, width, height);
}

void blend_premul_quad (struct quad_renderer_t *quad_prog,
                        GLuint texture, bool multisampled,
                        app_graphics_t *graphics,
                        float x, float y, float width_px, float height_px)
{
    glBindVertexArray (quad_prog->vao);
    glUseProgram (quad_prog->program_id);
    glDisable (GL_DEPTH_TEST);
    glActiveTexture (GL_TEXTURE0);

    if (multisampled) {
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, texture);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "texMS"), 0);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "multisampled_texture"), 1);
    } else {
        glBindTexture (GL_TEXTURE_2D, texture);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "tex"), 0);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "multisampled_texture"), 0);
    }

    glViewport (x, graphics->height - y - height_px, width_px, height_px);
    glScissor (x, graphics->height - y - height_px, width_px, height_px);

    glUniform1i (glGetUniformLocation (quad_prog->program_id, "ignore_alpha"), 0);
    glDrawArrays (GL_TRIANGLES, 0, 6);
}

void render_opaque_quad (struct quad_renderer_t *quad_prog,
                         GLuint texture, bool multisampled,
                         app_graphics_t *graphics,
                         float x, float y, float width_px, float height_px)
{
    glBindVertexArray (quad_prog->vao);
    glUseProgram (quad_prog->program_id);
    glDisable (GL_DEPTH_TEST);
    glActiveTexture (GL_TEXTURE0);

    if (multisampled) {
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, texture);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "texMS"), 0);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "multisampled_texture"), 1);
    } else {
        glBindTexture (GL_TEXTURE_2D, texture);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "tex"), 0);
        glUniform1i (glGetUniformLocation (quad_prog->program_id, "multisampled_texture"), 0);
    }

    glViewport (x, graphics->height - y - height_px, width_px, height_px);
    glScissor (x, graphics->height - y - height_px, width_px, height_px);

    glUniform1i (glGetUniformLocation (quad_prog->program_id, "ignore_alpha"), 1);
    glDrawArrays (GL_TRIANGLES, 0, 6);
}

void render_framebuffer (struct quad_renderer_t *quad_prog,
                         struct gl_framebuffer_t *fb, bool blend,
                         app_graphics_t *graphics,
                         float x, float y, float width_px, float height_px)
{
    if (blend) {
        blend_premul_quad (quad_prog, fb->tex_color_buffer, fb->multisampled, graphics,
                           x, y, width_px, height_px);
    } else {
        glDisable (GL_BLEND);
        render_opaque_quad (quad_prog, fb->tex_color_buffer, fb->multisampled, graphics,
                           x, y, width_px, height_px);
    }
}

#define OPENGL_UTIL_H
#endif
