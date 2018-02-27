/*
 * Copyright (C) 2017 Santiago León O. <santileortiz@gmail.com>
 */

// Dependencies:
// sudo apt-get install libxcb1-dev libcairo2-dev
#include <X11/Xlib-xcb.h>
#include <xcb/sync.h>
#include <xcb/randr.h>
#include <X11/Xutil.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define GL_GLEXT_PROTOTYPES
#include <GL/glx.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

#include <inttypes.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
//#define NDEBUG
#include <assert.h>
#include <errno.h>

#include "common.h"
#include "gui.h"
#include "slo_timers.h"

#define WINDOW_HEIGHT 700
#define WINDOW_WIDTH 700

#include "app_api.h"

void MessageCallback( GLenum source,
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
        printf ("%s\n", buffer);
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
        glGetShaderInfoLog(vertex_shader, 512, NULL, buffer);
        printf ("%s\n", buffer);
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

// NOTE: Transpose before uploading to OpenGL!!
typedef union {
    float E[16];
    float M[4][4]; // [down][right]
} mat4f;

// NOTE: Camera will be looking towards -Cz.
static inline
mat4f camera_matrix (vect3_t Cx, vect3_t Cy, vect3_t Cz, vect3_t Cpos)
{
    mat4f matrix;
    int i = 0, j;
    for (j=0; j<3; j++) {
        matrix.E[i*4+j] = Cx.E[j];
    }

    matrix.E[i*4+j] = -vect3_dot (Cpos, Cx);

    i = 1;
    for (j=0; j<3; j++) {
        matrix.E[i*4+j] = Cy.E[j];
    }
    matrix.E[i*4+j] = -vect3_dot (Cpos, Cy);

    i = 2;
    for (j=0; j<3; j++) {
        matrix.E[i*4+j] = -Cz.E[j];
    }
    matrix.E[i*4+j] = vect3_dot (Cpos, Cz);

    i = 3;
    for (j=0; j<3; j++) {
        matrix.E[i*4+j] = 0;
    }
    matrix.E[i*4+j] = 1;
    return matrix;
}

static inline
mat4f look_at (vect3_t camera, vect3_t target, vect3_t up)
{
    vect3_t Cz = vect3_normalize (vect3_subs (camera, target));
    vect3_t Cx = vect3_normalize (vect3_cross (up, Cz));
    vect3_t Cy = vect3_cross (Cz, Cx);
    return camera_matrix (Cx, Cy, Cz, camera);
}

static inline
mat4f rotation_x (float angle_r)
{
    float s = sinf(angle_r);
    float c = cosf(angle_r);
    mat4f res = {{
         1, 0, 0, 0,
         0, c,-s, 0,
         0, s, c, 0,
         0, 0, 0, 1
    }};
    return res;
}

static inline
mat4f rotation_y (float angle_r)
{
    float s = sinf(angle_r);
    float c = cosf(angle_r);
    mat4f res = {{
         c, 0, s, 0,
         0, 1, 0, 0,
        -s, 0, c, 0,
         0, 0, 0, 1
    }};
    return res;
}

static inline
mat4f rotation_z (float angle_r)
{
    float s = sinf(angle_r);
    float c = cosf(angle_r);
    mat4f res = {{
         c,-s, 0, 0,
         s, c, 0, 0,
         0, 0, 1, 0,
         0, 0, 0, 1
    }};
    return res;
}

void mat4f_print (mat4f mat)
{
    int i;
    for (i=0; i<4; i++) {
        int j;
        for (j=0; j<4; j++) {
            printf ("%f, ", mat.E[i*4+j]);
        }
        printf ("\n");
    }
    printf ("\n");
}

static inline
mat4f perspective_projection (float left, float right, float bottom, float top, float near, float far)
{
    // NOTE: There are several conventions for the semantics of the near and far
    // arguments to this function:
    //  - OpenGL assumes them to be distances to the camera and fails if either
    //    near or far is negative. This may be confusing because the other
    //    values are assumed to be coordinates and near and far are not,
    //    otherwise they would be negative because OpenGL uses RH coordinates.
    //  - We can make all argumets be the coordinates, then if the near plane or
    //    far plane coordinates are positive we throw an error.
    //  - A third approach is to mix both of them by taking the absolute value
    //    of the near and far plane and never fail. This works fine if they are
    //    interpreted as being Z coordinates, or distances to the camera at the
    //    cost of computing two absolute values.
    near = fabs (near);
    far = fabs (far);

    float a = 2*near/(right-left);
    float b = -(right+left)/(right-left);

    float c = 2*near/(top-bottom);
    float d = -(top+bottom)/(top-bottom);

    float e = (near+far)/(far-near);
    float f = -2*far*near/(far-near);

    mat4f res = {{
        a, 0, b, 0,
        0, c, d, 0,
        0, 0, e, f,
        0, 0, 1, 0,
    }};
    return res;
}

static inline
mat4f mat4f_mult (mat4f mat1, mat4f mat2)
{
    mat4f res = ZERO_INIT(mat4f);
    int i;
    for (i=0; i<4; i++) {
        int j;
        for (j=0; j<4; j++) {
            int k;
            for (k=0; k<4; k++) {
                res.M[i][j] += mat1.M[i][k] * mat2.M[k][j];
            }
        }
    }
    return res;
}

static inline
vect3_t mat4f_times_point (mat4f mat, vect3_t p)
{
    vect3_t res = VECT3(0,0,0);
    int i;
    for (i=0; i<3; i++) {
        int j;
        for (j=0; j<3; j++) {
            res.E[i] += mat.M[i][j] * p.E[j];
        }
    }

    for (i=0; i<3; i++) {
        res.E[i] += mat.M[i][3];
    }
    return res;
}

struct camera_t {
    float width_m;
    float height_m;
    float near_plane;
    float far_plane;
    float pitch;
    float yaw;
    float distance;
};

vect3_t camera_compute_pos (struct camera_t *camera)
{
    camera->pitch = CLAMP (camera->pitch, -M_PI/2 + 0.0001, M_PI/2 - 0.0001);
    camera->yaw = WRAP (camera->yaw, -M_PI, M_PI);
    camera->distance = LOW_CLAMP (camera->distance, camera->near_plane);

    return VECT3 (cos(camera->pitch)*sin(camera->yaw)*camera->distance,
                  sin(camera->pitch)*camera->distance,
                  cos(camera->pitch)*cos(camera->yaw)*camera->distance);
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

static inline
float px_to_m_x (app_graphics_t *graphics, float x_val_in_px)
{
    return x_val_in_px / (1000 * (float)graphics->x_dpi);
}

static inline
float px_to_m_y (app_graphics_t *graphics, float y_val_in_px)
{
    return y_val_in_px / (1000 * (float)graphics->y_dpi);
}

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

// The resulting transform sends s1 and s2 to d1 and d2 respectiveley.
//   s1 = res * d1
//   s2 = res * d2
static inline
mat4f transform_from_2_points (vect3_t s1, vect3_t s2, vect3_t d1, vect3_t d2)
{
    float xs, x0, ys, y0, zs, z0;
    if (s1.x != s2.x) {
        xs = (d1.x - d2.x)/(s1.x - s2.x);
        x0 = (d2.x*s1.x - d2.x*s2.x - d1.x*s2.x + d2.x*s2.x)/(s1.x - s2.x);
    } else {
        xs = 1;
        x0 = 0;
    }

    if (s1.y != s2.y) {
        ys = (d1.y - d2.y)/(s1.y - s2.y);
        y0 = (d2.y*s1.y - d2.y*s2.y - d1.y*s2.y + d2.y*s2.y)/(s1.y - s2.y);
    } else {
        ys = 1;
        y0 = 0;
    }

    if (s1.z != s2.z) {
        zs = (d1.z - d2.z)/(s1.z - s2.z);
        z0 = (d2.z*s1.z - d2.z*s2.z - d1.z*s2.z + d2.z*s2.z)/(s1.z - s2.z);
    } else {
        zs = 1;
        z0 = 0;
    }

    mat4f res = {{
        xs, 0, 0,x0,
         0,ys, 0,y0,
         0, 0,zs,z0,
         0, 0, 0, 1
    }};
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
    vect3_t s1 = VECT3(-1 + 2*x/texture_width, -1 + 2*y/texture_height, 0);
    vect3_t s2 = VECT3(s1.x + 2*width/texture_width, s1.y + 2*height/texture_height, 0);
    mat4f transf = transform_from_2_points (s1, s2, VECT3(-1,-1,0), VECT3(1,1,0));
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

enum faces_t {
    RIGHT_FACE, //  X
    LEFT_FACE,  // -X
    UP_FACE,    //  Y
    DOWN_FACE,  // -Y
    FRONT_FACE, //  Z
    BACK_FACE   // -Z
};

static inline
enum faces_t opposite_face (enum faces_t face)
{
    if (face % 2 == 0) {
        return face + 1;
    } else {
        return face - 1;
    }
}

// NOTE: Naming is based on the first letter of the 3 faces that contain the
// vertex in XYZ order.
// NOTE: Ordering is lexicographic assuming it's a unit cube with LDB point
// located at (0,0,0). Then coordinates in binary are ordered lexicographically.

enum cube_vertices_t {
    LDB, // 000
    LDF, // 001
    LUB, // 010
    LUF, // 011
    RDB, // 100
    RDF, // 101
    RUB, // 110
    RUF  // 111
};

#define VERT_FACE_X(vert) ((vert&0x4)?RIGHT_FACE:LEFT_FACE)
#define VERT_FACE_Y(vert) ((vert&0x2)?UP_FACE:DOWN_FACE)
#define VERT_FACE_Z(vert) ((vert&0x1)?FRONT_FACE:BACK_FACE)

struct cuboid_t {
    vec3f v[8];
};

#define UNIT_CUBE (struct cuboid_t){{\
                    VEC3F(-1,-1,-1), \
                    VEC3F(-1,-1, 1), \
                    VEC3F(-1, 1,-1), \
                    VEC3F(-1, 1, 1), \
                    VEC3F( 1,-1,-1), \
                    VEC3F( 1,-1, 1), \
                    VEC3F( 1, 1,-1), \
                    VEC3F( 1, 1, 1), \
                   }}

#define CUBOID_SIZE_X(c) ((c).v[4].x - (c).v[0].x)
#define CUBOID_SIZE_Y(c) ((c).v[2].y - (c).v[0].y)
#define CUBOID_SIZE_Z(c) ((c).v[1].z - (c).v[0].z)

void cuboid_init (vec3f dim, struct cuboid_t *res)
{
    *res = UNIT_CUBE;

    int i;
    for (i=0; i<8; i++) {
        res->v[i].x = res->v[i].x * dim.x/2;
        res->v[i].y = res->v[i].y * dim.y/2;
        res->v[i].z = res->v[i].z * dim.z/2;
    }
}

void cuboid_print (struct cuboid_t *cb)
{
    int i;
    for (i=0; i<8; i++) {
        vec3f_print (cb->v[i]);
    }
}

static inline
float cuboid_face_coord (struct cuboid_t c, enum faces_t face)
{
    float res;
    switch (face) {
        case RIGHT_FACE:
            res = c.v[7].x;
            break;
        case LEFT_FACE:
            res = c.v[0].x;
            break;
        case UP_FACE:
            res = c.v[7].y;
            break;
        case DOWN_FACE:
            res = c.v[0].y;
            break;
        case BACK_FACE:
            res = c.v[7].z;
            break;
        case FRONT_FACE:
            res = c.v[0].z;
            break;
    }
    return res;
}

static inline
void face_vert_ids (enum faces_t face, uint8_t *face_ids, uint8_t *opposite_face_ids)
{
    uint8_t mask;
    uint8_t axis_1_cnt = 0;
    uint8_t *axis_1 = face_ids;
    uint8_t axis_0_cnt = 0;
    uint8_t *axis_0 = opposite_face_ids;

    switch (face) {
        case LEFT_FACE:
            axis_0 = face_ids;
            axis_1 = opposite_face_ids;
        case RIGHT_FACE:
            mask = 0x4;
            break;

        case DOWN_FACE:
            axis_0 = face_ids;
            axis_1 = opposite_face_ids;
        case UP_FACE:
            mask = 0x2;
            break;

        case BACK_FACE:
            axis_0 = face_ids;
            axis_1 = opposite_face_ids;
        case FRONT_FACE:
            mask = 0x1;
            break;
    }

    uint8_t i;
    for (i=0; i<8; i++) {
        if (i & mask) {
            if (axis_1 != NULL) {
                axis_1[axis_1_cnt] = i;
                axis_1_cnt++;
            }
        } else {
            if (axis_0 != NULL) {
                axis_0[axis_0_cnt] = i;
                axis_0_cnt++;
            }
        }
    }
}

#define NUM_HOLES 30
#define NUM_SEPARATORS (5*NUM_HOLES)
#define NUM_SEPARATOR_PARTS (23*NUM_HOLES)
#define DEFAULT_SEPARATION 0.025f

struct relative_dimension_t {
    uint32_t hole_id;
    enum faces_t face;
};

// A hole's size in some axis can be specified in 3 ways:
//
//   DIM_DIRECT means we have a specific value.
//
//   DIM_COPY means we copy the size from the base hole.
//
//   DIM_RELATIVE means the moving face will match with a parallel face of
//   another hole.

enum dimension_type_t {
    DIMENSION_DIRECT,
    DIMENSION_COPY,
    DIMENSION_RELATIVE
};

struct hole_dimension_t {
    enum dimension_type_t type;
    union {
        float val;
        struct relative_dimension_t rval;
    };
};

#define DIM_F(n) (struct hole_dimension_t){DIMENSION_DIRECT,{n}}
#define DIM_COPY (struct hole_dimension_t){DIMENSION_COPY,{0}}
#define DIM_UNTIL(hole_id,face) (struct hole_dimension_t){DIMENSION_RELATIVE, \
    {.rval = (struct relative_dimension_t){hole_id,face}}}

struct hole_dimensions_t {
    struct hole_dimension_t x;
    struct hole_dimension_t y;
    struct hole_dimension_t z;
};

#define HOLE_DIM_F(x,y,z) (struct hole_dimensions_t){DIM_F(x),DIM_F(y),DIM_F(z)}
#define HOLE_DIM(x,y,z) (struct hole_dimensions_t){x,y,z}

static inline
vec3f hole_dim_direct_to_vec3f (struct hole_dimensions_t *dim)
{
    return VEC3F (dim->x.val, dim->y.val, dim->z.val);
}

struct separator_part_t {
    struct cuboid_t c;
    vec3f color;
};

struct sep_part_list_t {
    struct sep_part_list_t *next;
    struct separator_part_t *part;
};

struct separator_t {
    struct sep_part_list_t *parts;
    float thickness;
};

struct hole_t {
    struct cuboid_t h;
    struct separator_t *separators[6];
};

struct closet_t {
    mem_pool_t pool;

    uint32_t num_holes;
    uint32_t size_holes;
    struct hole_t *holes;

    uint32_t num_seps;
    uint32_t size_separators;
    struct separator_t *separators;

    uint32_t num_sep_parts;
    uint32_t size_sep_parts;
    struct separator_part_t *sep_parts;
};

struct closet_scene_t {
    GLuint program_id;
    GLuint model_loc;
    GLuint view_loc;
    GLuint proj_loc;
    GLuint color_loc;
    GLuint alpha_loc;

    uint32_t holes_vao_size;
    GLuint holes_vao;
    uint32_t seps_vao_size;
    GLuint seps_vao;
};

#define VA_CUBOID_SIZE (36*6*sizeof(float))

float* put_cuboid_in_vertex_array (struct cuboid_t *cuboid, float *dest)
{
    vec3f ldb = cuboid->v[0];
    vec3f ldf = cuboid->v[1];
    vec3f lub = cuboid->v[2];
    vec3f luf = cuboid->v[3];
    vec3f rdb = cuboid->v[4];
    vec3f rdf = cuboid->v[5];
    vec3f rub = cuboid->v[6];
    vec3f ruf = cuboid->v[7];

    float vertex_array[] = {
     // Coords                Normals
        rdb.x, rdb.y, rdb.z,  0.0f,  0.0f, -1.0f,
        ldb.x, ldb.y, ldb.z,  0.0f,  0.0f, -1.0f,
        rub.x, rub.y, rub.z,  0.0f,  0.0f, -1.0f,
        lub.x, lub.y, lub.z,  0.0f,  0.0f, -1.0f,
        rub.x, rub.y, rub.z,  0.0f,  0.0f, -1.0f,
        ldb.x, ldb.y, ldb.z,  0.0f,  0.0f, -1.0f,

        ldf.x, ldf.y, ldf.z,  0.0f,  0.0f,  1.0f,
        rdf.x, rdf.y, rdf.z,  0.0f,  0.0f,  1.0f,
        ruf.x, ruf.y, ruf.z,  0.0f,  0.0f,  1.0f,
        ruf.x, ruf.y, ruf.z,  0.0f,  0.0f,  1.0f,
        luf.x, luf.y, luf.z,  0.0f,  0.0f,  1.0f,
        ldf.x, ldf.y, ldf.z,  0.0f,  0.0f,  1.0f,

        luf.x, luf.y, luf.z, -1.0f,  0.0f,  0.0f,
        lub.x, lub.y, lub.z, -1.0f,  0.0f,  0.0f,
        ldb.x, ldb.y, ldb.z, -1.0f,  0.0f,  0.0f,
        ldb.x, ldb.y, ldb.z, -1.0f,  0.0f,  0.0f,
        ldf.x, ldf.y, ldf.z, -1.0f,  0.0f,  0.0f,
        luf.x, luf.y, luf.z, -1.0f,  0.0f,  0.0f,

        rub.x, rub.y, rub.z,  1.0f,  0.0f,  0.0f,
        ruf.x, ruf.y, ruf.z,  1.0f,  0.0f,  0.0f,
        rdb.x, rdb.y, rdb.z,  1.0f,  0.0f,  0.0f,
        rdf.x, rdf.y, rdf.z,  1.0f,  0.0f,  0.0f,
        rdb.x, rdb.y, rdb.z,  1.0f,  0.0f,  0.0f,
        ruf.x, ruf.y, ruf.z,  1.0f,  0.0f,  0.0f,

        ldb.x, ldb.y, ldb.z,  0.0f, -1.0f,  0.0f,
        rdb.x, rdb.y, rdb.z,  0.0f, -1.0f,  0.0f,
        rdf.x, rdf.y, rdf.z,  0.0f, -1.0f,  0.0f,
        rdf.x, rdf.y, rdf.z,  0.0f, -1.0f,  0.0f,
        ldf.x, ldf.y, ldf.z,  0.0f, -1.0f,  0.0f,
        ldb.x, ldb.y, ldb.z,  0.0f, -1.0f,  0.0f,

        lub.x, lub.y, lub.z,  0.0f,  1.0f,  0.0f,
        ruf.x, ruf.y, ruf.z,  0.0f,  1.0f,  0.0f,
        rub.x, rub.y, rub.z,  0.0f,  1.0f,  0.0f,
        luf.x, luf.y, luf.z,  0.0f,  1.0f,  0.0f,
        ruf.x, ruf.y, ruf.z,  0.0f,  1.0f,  0.0f,
        lub.x, lub.y, lub.z,  0.0f,  1.0f,  0.0f,
    };

    memcpy (dest, vertex_array, sizeof(vertex_array));
    return (float*)((uint8_t*)dest + sizeof (vertex_array));
}

void compute_hole_first (vec3f dim, struct cuboid_t *res)
{
    *res = UNIT_CUBE;

    int i;
    for (i=0; i<8; i++) {
        res->v[i].x = res->v[i].x * dim.x/2;
        res->v[i].y = res->v[i].y * dim.y/2;
        res->v[i].z = res->v[i].z * dim.z/2;
    }
}

void compute_cuboid (vec3f dim,
                     enum cube_vertices_t anchor_id, vec3f anchor_pos,
                     struct cuboid_t *res)
{
    *res = UNIT_CUBE;

    vec3f new_anchor = res->v[anchor_id];
    new_anchor.x *= dim.x/2;
    new_anchor.y *= dim.y/2;
    new_anchor.z *= dim.z/2;

    vec3f disp = vec3f_subs (anchor_pos, new_anchor);

    int i;
    for (i=0; i<8; i++) {
        res->v[i].x = res->v[i].x * dim.x/2 + disp.x;
        res->v[i].y = res->v[i].y * dim.y/2 + disp.y;
        res->v[i].z = res->v[i].z * dim.z/2 + disp.z;
    }
}

struct separator_part_t* next_sep_part (struct closet_t *cl)
{
    assert (cl->num_sep_parts < cl->size_sep_parts - 1);
    struct separator_part_t *res = &cl->sep_parts[cl->num_sep_parts++];
    res->color = VEC3F (1, 1, 0);
    return res;
}

struct separator_t* next_separator (struct closet_t *cl)
{
    assert (cl->num_seps < cl->size_separators - 1);
    return &cl->separators[cl->num_seps++];
}

struct hole_t* next_hole (struct closet_t *cl)
{
    assert (cl->num_holes < cl->size_holes - 1);
    return &cl->holes[cl->num_holes++];
}

void compute_face_separator_part (struct cuboid_t *base, enum faces_t face,
                                  struct cuboid_t *res, float thickness)
{
    uint8_t face_v[4];
    uint8_t opposite_face_v[4];
    face_vert_ids (face, face_v, opposite_face_v);

    int i;
    for (i=0; i<4; i++) {
        res->v[face_v[i]] = res->v[opposite_face_v[i]] = base->v[face_v[i]];
    }

    uint8_t axis_idx;
    switch (face) {
        case LEFT_FACE:
            thickness = -thickness;
        case RIGHT_FACE:
            axis_idx = 0;
            break;

        case DOWN_FACE:
            thickness = -thickness;
        case UP_FACE:
            axis_idx = 1;
            break;

        case BACK_FACE:
            thickness = -thickness;
        case FRONT_FACE:
            axis_idx = 2;
            break;
    }

    for (i=0; i<4; i++) {
        res->v[face_v[i]].E[axis_idx] += thickness;
    }
}

void set_new_separator (struct closet_t *cl, struct hole_t *hole, enum faces_t face, float thickness)
{
    struct separator_t *new_sep = next_separator (cl);
    new_sep->thickness = thickness;
    hole->separators[face] = new_sep;

    struct separator_part_t *part = next_sep_part (cl);
    struct cuboid_t *base = &hole->h;
    compute_face_separator_part (base, face, &part->c, thickness);

    struct sep_part_list_t *list_node = mem_pool_push_size (&cl->pool, sizeof(struct sep_part_list_t));
    new_sep->parts = list_node;
    list_node->part = part;
    list_node->next = NULL;
}

void extend_separator (struct closet_t *cl, struct hole_t *hole, enum faces_t face, struct separator_t *sep)
{
    struct separator_part_t *part = next_sep_part (cl);
    compute_face_separator_part (&hole->h, face, &part->c, sep->thickness);
    hole->separators[face] = sep;

    struct sep_part_list_t *list_node = mem_pool_push_size (&cl->pool, sizeof(struct sep_part_list_t));
    list_node->part = part;
    list_node->next = sep->parts;
    sep->parts = list_node;
}

static inline
void color_separator (struct separator_t *sep, vec3f color)
{
    struct sep_part_list_t *curr_list_node = sep->parts;
    while (curr_list_node != NULL) {
        struct separator_part_t *part = curr_list_node->part;
        part->color = color;
        curr_list_node = curr_list_node->next;
    }
}

struct closet_t new_closet (struct hole_dimensions_t *dim)
{
    assert (dim->x.type == DIMENSION_DIRECT &&
            dim->y.type == DIMENSION_DIRECT &&
            dim->z.type == DIMENSION_DIRECT);

    struct closet_t res = {0};
    res.size_holes = NUM_HOLES;
    res.holes = mem_pool_push_size (&res.pool,
                                    NUM_HOLES*sizeof(struct hole_t));

    res.size_separators = NUM_SEPARATORS;
    res.separators = mem_pool_push_size (&res.pool,
                                         NUM_SEPARATORS*sizeof(struct separator_t));

    res.size_sep_parts = NUM_SEPARATOR_PARTS;
    res.sep_parts = mem_pool_push_size (&res.pool,
                                         NUM_SEPARATOR_PARTS*sizeof(struct separator_part_t));

    vec3f hole_size = hole_dim_direct_to_vec3f (dim);
    struct hole_t *new_hole = next_hole (&res);
    compute_hole_first (hole_size, &new_hole->h);

    set_new_separator (&res, new_hole, UP_FACE, DEFAULT_SEPARATION);
    set_new_separator (&res, new_hole, DOWN_FACE, DEFAULT_SEPARATION);
    set_new_separator (&res, new_hole, RIGHT_FACE, DEFAULT_SEPARATION);
    set_new_separator (&res, new_hole, LEFT_FACE, DEFAULT_SEPARATION);
    set_new_separator (&res, new_hole, FRONT_FACE, DEFAULT_SEPARATION);
    set_new_separator (&res, new_hole, BACK_FACE, DEFAULT_SEPARATION);

    return res;
}

void push_hole (struct closet_t *cl,
                struct hole_dimensions_t *dim, uint32_t base_id,
                enum faces_t face, enum cube_vertices_t base_anchor_id,
                float separation)
{
    assert (cl->num_holes > 0);

    // TODO: Grow the hole array instead of failing
    assert (cl->num_holes < cl->size_holes - 1);

    struct cuboid_t *base_hole_cuboid = &cl->holes[base_id].h;

    // Ensure the base_anchor_id is in the face received as argument. If it's
    // not we choose the closest vertex that is in it.
    switch (face) {
        case RIGHT_FACE:
            base_anchor_id |= 0x4;
            break;
        case LEFT_FACE:
            base_anchor_id &= ~0x4;
            break;
        case UP_FACE:
            base_anchor_id |= 0x2;
            break;
        case DOWN_FACE:
            base_anchor_id &= ~0x2;
            break;
        case FRONT_FACE:
            base_anchor_id |= 0x1;
            break;
        case BACK_FACE:
            base_anchor_id &= ~0x1;
            break;
    }

    // Compute the position and id for the anchor vertex in the new cuboid
    enum cube_vertices_t anchor_id = base_anchor_id;
    vec3f anchor_pos;
    {
        anchor_pos = base_hole_cuboid->v[anchor_id];
        switch (face) {
            case RIGHT_FACE:
                anchor_pos.x += separation;
                break;
            case LEFT_FACE:
                anchor_pos.x -= separation;
                break;
            case UP_FACE:
                anchor_pos.y += separation;
                break;
            case DOWN_FACE:
                anchor_pos.y -= separation;
                break;
            case FRONT_FACE:
                anchor_pos.z += separation;
                break;
            case BACK_FACE:
                anchor_pos.z -= separation;
                break;
            default:
                invalid_code_path;
        }

        switch (face) {
            case RIGHT_FACE:
            case LEFT_FACE:
                anchor_id = (anchor_id & ~0x04) | ((anchor_id ^ 0xFF) & 0x4);
                break;
            case UP_FACE:
            case DOWN_FACE:
                anchor_id = (anchor_id & ~0x02) | ((anchor_id ^ 0xFF) & 0x2);
                break;
            case FRONT_FACE:
            case BACK_FACE:
                anchor_id = (anchor_id & ~0x01) | ((anchor_id ^ 0xFF) & 0x1);
                break;
            default:
                invalid_code_path;
        }
    }

    // Compute the size of the new hole
    vec3f dim_vec = VEC3F(0,0,0);
    enum cube_vertices_t moving_vertex_id = anchor_id^0x7;
    {

        switch (dim->x.type) {
            case DIMENSION_DIRECT:
                dim_vec.x = dim->x.val;
                break;
            case DIMENSION_COPY:
                dim_vec.x = CUBOID_SIZE_X (*base_hole_cuboid);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->x.rval;
                    if (VERT_FACE_X(moving_vertex_id) == rval.face) {
                        float face_coord = cuboid_face_coord (cl->holes[rval.hole_id].h, rval.face);
                        dim_vec.x = fabs (anchor_pos.x - face_coord);
                    } else {
                        printf ("Invalid face for relative dimension.\n");
                    }
                } break;
                break;
            default:
                invalid_code_path;
        }

        switch (dim->y.type) {
            case DIMENSION_DIRECT:
                dim_vec.y = dim->y.val;
                break;
            case DIMENSION_COPY:
                dim_vec.y = CUBOID_SIZE_Y (*base_hole_cuboid);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->y.rval;
                    if (VERT_FACE_Y(moving_vertex_id) == rval.face) {
                        float face_coord = cuboid_face_coord (cl->holes[rval.hole_id].h, rval.face);
                        dim_vec.y = fabs (anchor_pos.y - face_coord);
                    } else {
                        printf ("Invalid face for relative dimension.\n");
                    }
                } break;
            default:
                invalid_code_path;
        }

        switch (dim->z.type) {
            case DIMENSION_DIRECT:
                dim_vec.z = dim->z.val;
                break;
            case DIMENSION_COPY:
                dim_vec.z = CUBOID_SIZE_Z (*base_hole_cuboid);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->z.rval;
                    if (VERT_FACE_Z(moving_vertex_id) == rval.face) {
                        float face_coord = cuboid_face_coord (cl->holes[rval.hole_id].h, rval.face);
                        dim_vec.z = fabs (anchor_pos.z - face_coord);
                    } else {
                        printf ("Invalid face for relative dimension.\n");
                    }
                } break;
            default:
                invalid_code_path;
        }
    }

    // Compute new hole
    struct hole_t *new_hole = next_hole (cl);
    compute_cuboid (dim_vec, anchor_id, anchor_pos, &new_hole->h);

    // Resolve separators
    struct hole_t *base_hole = &cl->holes[base_id];
    new_hole->separators[opposite_face (face)] = base_hole->separators[face];

    if (VERT_FACE_X(anchor_id) != opposite_face (face)) {
        extend_separator (cl, new_hole, VERT_FACE_X(anchor_id), base_hole->separators[VERT_FACE_X(anchor_id)]);
    }

    if (VERT_FACE_Y(anchor_id) != opposite_face (face)) {
        extend_separator (cl, new_hole, VERT_FACE_Y(anchor_id), base_hole->separators[VERT_FACE_Y(anchor_id)]);
    }

    if (VERT_FACE_Z(anchor_id) != opposite_face (face)) {
        extend_separator (cl, new_hole, VERT_FACE_Z(anchor_id), base_hole->separators[VERT_FACE_Z(anchor_id)]);
    }

    if (VERT_FACE_X(moving_vertex_id) != face) {
        switch (dim->x.type) {
            case DIMENSION_DIRECT:
                set_new_separator (cl, new_hole, VERT_FACE_X(moving_vertex_id), DEFAULT_SEPARATION);
                break;
            case DIMENSION_COPY:
                extend_separator (cl, new_hole,
                                  VERT_FACE_X(moving_vertex_id),
                                  base_hole->separators[VERT_FACE_X(moving_vertex_id)]);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->x.rval;
                    struct separator_t *rel_sep = cl->holes[rval.hole_id].separators[rval.face];
                    extend_separator (cl, new_hole, VERT_FACE_X(moving_vertex_id), rel_sep);
                } break;
            default:
                invalid_code_path;
        }
    }

    if (VERT_FACE_Y(moving_vertex_id) != face) {
        switch (dim->y.type) {
            case DIMENSION_DIRECT:
                set_new_separator (cl, new_hole, VERT_FACE_Y(moving_vertex_id), DEFAULT_SEPARATION);
                break;
            case DIMENSION_COPY:
                extend_separator (cl, new_hole,
                                  VERT_FACE_Y(moving_vertex_id),
                                  base_hole->separators[VERT_FACE_Y(moving_vertex_id)]);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->y.rval;
                    struct separator_t *rel_sep = cl->holes[rval.hole_id].separators[rval.face];
                    extend_separator (cl, new_hole, VERT_FACE_Y(moving_vertex_id), rel_sep);
                } break;
            default:
                invalid_code_path;
        }
    }

    if (VERT_FACE_Z(moving_vertex_id) != face) {
        switch (dim->z.type) {
            case DIMENSION_DIRECT:
                set_new_separator (cl, new_hole, VERT_FACE_Z(moving_vertex_id), DEFAULT_SEPARATION);
                break;
            case DIMENSION_COPY:
                extend_separator (cl, new_hole,
                                  VERT_FACE_Z(moving_vertex_id),
                                  base_hole->separators[VERT_FACE_Z(moving_vertex_id)]);
                break;
            case DIMENSION_RELATIVE:
                {
                    struct relative_dimension_t rval = dim->z.rval;
                    struct separator_t *rel_sep = cl->holes[rval.hole_id].separators[rval.face];
                    extend_separator (cl, new_hole, VERT_FACE_Z(moving_vertex_id), rel_sep);
                } break;
            default:
                invalid_code_path;
        }
    }

    // TODO: What happens if the distance between the faces of new_hole parallel
    // to face was set using relative dimensioning? Then the separator may
    // already exist, in which case we want to use extend_separator() here.
    set_new_separator (cl, new_hole, face, DEFAULT_SEPARATION);
}

struct closet_scene_t init_closet_scene ()
{
    struct closet_scene_t scene = {0};

    scene.program_id = gl_program ("vertex_shader.glsl", "fragment_shader.glsl");
    if (!scene.program_id) {
        return scene;
    }

    glGenVertexArrays (1, &scene.holes_vao);
    glGenVertexArrays (1, &scene.seps_vao);

    scene.model_loc = glGetUniformLocation (scene.program_id, "model");
    scene.view_loc = glGetUniformLocation (scene.program_id, "view");
    scene.proj_loc = glGetUniformLocation (scene.program_id, "proj");
    scene.color_loc = glGetUniformLocation (scene.program_id, "color");

    return scene;
}

void update_closet_scene (struct closet_scene_t *scene, struct closet_t *cl)
{
    mem_pool_t pool = {0};
    float *vertices = mem_pool_push_size (&pool, VA_CUBOID_SIZE*cl->num_holes);
    float *vert_ptr = vertices;
    int i;
    for (i = 0; i<cl->num_holes; i++) {
        vert_ptr = put_cuboid_in_vertex_array (&cl->holes[i].h, vert_ptr);
        scene->holes_vao_size += 36;
    }

    GLint pos_attr = glGetAttribLocation (scene->program_id, "position");
    GLint normal_attr = glGetAttribLocation (scene->program_id, "in_normal");

    glBindVertexArray (scene->holes_vao);

      GLuint vbo;
      glGenBuffers (1, &vbo);
      glBindBuffer (GL_ARRAY_BUFFER, vbo);
      glBufferData (GL_ARRAY_BUFFER, VA_CUBOID_SIZE*cl->num_holes, vertices, GL_STATIC_DRAW);

      glEnableVertexAttribArray (pos_attr);
      glVertexAttribPointer (pos_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);

      glEnableVertexAttribArray (normal_attr);
      glVertexAttribPointer (normal_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

    vertices = mem_pool_push_size (&pool, VA_CUBOID_SIZE*cl->num_sep_parts);
    vert_ptr = vertices;
    for (i = 0; i<cl->num_sep_parts; i++) {
        vert_ptr = put_cuboid_in_vertex_array (&cl->sep_parts[i].c, vert_ptr);
        scene->seps_vao_size += 36;
    }

    glBindVertexArray (scene->seps_vao);

      glGenBuffers (1, &vbo);
      glBindBuffer (GL_ARRAY_BUFFER, vbo);
      glBufferData (GL_ARRAY_BUFFER, VA_CUBOID_SIZE*cl->num_sep_parts, vertices, GL_STATIC_DRAW);

      glEnableVertexAttribArray (pos_attr);
      glVertexAttribPointer (pos_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);

      glEnableVertexAttribArray (normal_attr);
      glVertexAttribPointer (normal_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

    mem_pool_destroy (&pool);
}

void closet_scene_set_camera (struct closet_scene_t *closet_scene, struct camera_t *camera)
{
    glUseProgram (closet_scene->program_id);

    mat4f model = rotation_y (0);
    glUniformMatrix4fv (closet_scene->model_loc, 1, GL_TRUE, model.E);

    vect3_t camera_pos = camera_compute_pos (camera);
    mat4f view = look_at (camera_pos,
                          VECT3(0,0,0),
                          VECT3(0,1,0));
    glUniformMatrix4fv (closet_scene->view_loc, 1, GL_TRUE, view.E);

    mat4f projection = perspective_projection (-camera->width_m/2, camera->width_m/2,
                                               -camera->height_m/2, camera->height_m/2,
                                               camera->near_plane, camera->far_plane);
    glUniformMatrix4fv (closet_scene->proj_loc, 1, GL_TRUE, projection.E);
}

void render_closet_opaque (struct closet_scene_t *closet_scene)
{
    glUseProgram (closet_scene->program_id);

    glEnable (GL_DEPTH_TEST);
    glBindVertexArray (closet_scene->holes_vao);
    glUniform4f (closet_scene->color_loc, 1, 1, 1, 1);
    glDrawArrays (GL_TRIANGLES, 0, closet_scene->holes_vao_size);
}

void render_closet_transparent (struct closet_scene_t *closet_scene, struct closet_t *cl)
{
    glUseProgram (closet_scene->program_id);

    glBindVertexArray (closet_scene->seps_vao);

    int i;
    for (i=0; i<cl->num_sep_parts; i++) {
        vec3f c = cl->sep_parts[i].color;
        glUniform4f (closet_scene->color_loc, c.r, c.g, c.b, 0.95);
        glDrawArrays (GL_TRIANGLES, i*36, 36);
    }
}

struct cube_test_scene_t {
    GLuint program_id;
    GLuint model_loc;
    GLuint view_loc;
    GLuint proj_loc;
    GLuint color_loc;
    GLuint alpha_loc;

    uint32_t vao_size;
    GLuint vao;
};

struct cube_test_scene_t init_cube_test ()
{
    struct cube_test_scene_t scene = {0};

    scene.program_id = gl_program ("vertex_shader.glsl", "test_fragment_shader.glsl");
    if (!scene.program_id) {
        return scene;
    }

    glGenVertexArrays (1, &scene.vao);

    scene.model_loc = glGetUniformLocation (scene.program_id, "model");
    scene.view_loc = glGetUniformLocation (scene.program_id, "view");
    scene.proj_loc = glGetUniformLocation (scene.program_id, "proj");
    scene.color_loc = glGetUniformLocation (scene.program_id, "color");

    float vertices[VA_CUBOID_SIZE];
    struct cuboid_t cube;
    cuboid_init (VEC3F (1,1,1), &cube);
    put_cuboid_in_vertex_array (&cube, vertices);

    glBindVertexArray (scene.vao);

      GLuint vbo;
      glGenBuffers (1, &vbo);
      glBindBuffer (GL_ARRAY_BUFFER, vbo);
      glBufferData (GL_ARRAY_BUFFER, VA_CUBOID_SIZE, vertices, GL_STATIC_DRAW);

      GLuint pos_attr = glGetAttribLocation (scene.program_id, "position");
      glEnableVertexAttribArray (pos_attr);
      glVertexAttribPointer (pos_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), 0);

      GLuint normal_attr = glGetAttribLocation (scene.program_id, "in_normal");
      glEnableVertexAttribArray (normal_attr);
      glVertexAttribPointer (normal_attr, 3, GL_FLOAT, GL_FALSE, 6*sizeof(float), (void*)(3*sizeof(float)));

    return scene;
}

void scene_update_camera (struct cube_test_scene_t *scene, struct camera_t *camera)
{
    glUseProgram (scene->program_id);

    mat4f model = rotation_y (0);
    glUniformMatrix4fv (scene->model_loc, 1, GL_TRUE, model.E);

    vect3_t camera_pos = camera_compute_pos (camera);
    mat4f view = look_at (camera_pos,
                          VECT3(0,0,0),
                          VECT3(0,1,0));
    glUniformMatrix4fv (scene->view_loc, 1, GL_TRUE, view.E);

    mat4f projection = perspective_projection (-camera->width_m/2, camera->width_m/2,
                                               -camera->height_m/2, camera->height_m/2,
                                               camera->near_plane, camera->far_plane);
    glUniformMatrix4fv (scene->proj_loc, 1, GL_TRUE, projection.E);
}

void render_cube_test (struct cube_test_scene_t *scene)
{
    glBindVertexArray (scene->vao);
    glDrawArrays (GL_TRIANGLES, 0, 36);
}

void set_depth_textures (GLuint program_id, GLuint depth_texture, GLuint peel_depth_map)
{
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
    );

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map);
    glUniform1i (glGetUniformLocation (program_id, "peel_depth_map"), 0);
}

vec3f undefined_color = VEC3F (1,1,0);
vec3f selected_color = VEC3F(0.93,0.5,0.1);

#if 1
bool update_and_render (struct app_state_t *st, app_graphics_t *graphics, app_input_t input)
{
    bool blit_needed = false;
    st->gui_st.gr = *graphics;
    if (!st->is_initialized) {
        st->end_execution = false;
        st->is_initialized = true;
    }

    update_input (&st->gui_st, input);

    switch (st->gui_st.input.keycode) {
        case 24: //KEY_Q
            st->end_execution = true;
            break;
        default:
            //if (input.keycode >= 8) {
            //    printf ("%" PRIu8 "\n", input.keycode);
            //    //printf ("%" PRIu16 "\n", input.modifiers);
            //}
            break;
    }

    static struct closet_scene_t closet_scene;
    static struct quad_renderer_t quad_renderer;
    static struct closet_t cl;
    static bool run_once = false;
    static struct camera_t main_camera;

    static GLuint fb;
    static GLuint color_texture;
    static GLuint depth_texture;
    static GLuint peel_depth_map;

    if (!run_once) {
        run_once = true;

        closet_scene = init_closet_scene ();
        if (closet_scene.program_id == 0) {
            st->end_execution = true;
            return blit_needed;
        }

        float width = graphics->screen_width;
        float height = graphics->screen_height;
        // Framebuffer creation
        glGenFramebuffers (1, &fb);
        glBindFramebuffer (GL_FRAMEBUFFER, fb);

        create_color_texture (&color_texture, width, height, 4);

        glFramebufferTexture2D (
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D_MULTISAMPLE, color_texture, 0
        );

        create_depth_texture (&peel_depth_map, width, height, 4);
        create_depth_texture (&depth_texture, width, height, 4);

        glFramebufferTexture2D (
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
        );

        quad_renderer = init_quad_renderer ();

        float separation = 0.025;
        struct hole_dimensions_t dim = HOLE_DIM_F (0.9, 0.4, 0.7);
        cl = new_closet (&dim);

        dim = HOLE_DIM (DIM_COPY, DIM_COPY, DIM_COPY);
        push_hole (&cl, &dim, 0, UP_FACE, RUF, separation);

        dim = HOLE_DIM (DIM_F(0.3), DIM_UNTIL(0, DOWN_FACE), DIM_COPY);
        push_hole (&cl, &dim, 1, RIGHT_FACE, RUF, separation);

        update_closet_scene (&closet_scene, &cl);
        color_separator (&cl.separators[0], selected_color);

        main_camera.near_plane = 0.1;
        main_camera.far_plane = 100;
        main_camera.pitch = M_PI/4;
        main_camera.yaw = M_PI/4;
        main_camera.distance = 4.5;
    }

    static int selected_separator = 0;
    switch (st->gui_st.input.keycode) {
        case 23: //KEY_TAB
            color_separator (&cl.separators[selected_separator], undefined_color);
            selected_separator++;
            selected_separator = WRAP (selected_separator, 0, cl.num_seps - 1);
            color_separator (&cl.separators[selected_separator], selected_color);
            break;
        case 9: //KEY_ESC
            color_separator (&cl.separators[selected_separator], undefined_color);
            selected_separator = -1;
            break;
        default:
            break;
    }

    if (st->gui_st.dragging[0]) {
        vect2_t change = st->gui_st.ptr_delta;
        main_camera.pitch += 0.01 * change.y;
        main_camera.yaw -= 0.01 * change.x;
    }

    if (input.wheel != 1) {
        main_camera.distance -= (input.wheel - 1)*main_camera.distance*0.7;
    }

    main_camera.width_m = px_to_m_x (graphics, graphics->width);
    main_camera.height_m = px_to_m_y (graphics, graphics->height);

    closet_scene_set_camera (&closet_scene, &main_camera);

    int num_pass = 8;

    glEnable (GL_SAMPLE_SHADING);
    glMinSampleShading (1.0);
    glEnable (GL_DEPTH_TEST);
    glBindFramebuffer (GL_FRAMEBUFFER, fb);
    glViewport (0, 0, graphics->width, graphics->height);
    glScissor (0, 0, graphics->width, graphics->height);

    // Clear peel_depth_map
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map, 0
    );

    glClearDepth (0);
    glClear (GL_DEPTH_BUFFER_BIT);
    glClearDepth (1);

    set_depth_textures (closet_scene.program_id, depth_texture, peel_depth_map);

    // Clear framebuffer
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render without blending to overwrite color buffer
    glDisable (GL_BLEND);
    render_closet_transparent (&closet_scene, &cl);

    glEnable (GL_BLEND);
    int i;
    for (i = 0; i < num_pass-1; i++) {
        GLuint tmp = peel_depth_map;
        peel_depth_map = depth_texture;
        depth_texture = tmp;

        set_depth_textures (closet_scene.program_id, depth_texture, peel_depth_map);

        glClear(GL_DEPTH_BUFFER_BIT);

        // Render scene using UNDER blending operator
        glBlendFunc (GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
        render_closet_transparent (&closet_scene, &cl);
    }

    // Blend resulting color buffer into the window using the OVER operator
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_into_window (graphics);
    glClearColor(0.164f, 0.203f, 0.223f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    set_texture_clip (&quad_renderer, graphics->screen_width, graphics->screen_height,
                      0, 0, graphics->width, graphics->height);
    blend_premul_quad (&quad_renderer, color_texture, true, graphics,
                        0, 0, graphics->width, graphics->height);

    return true;
}
#else
bool update_and_render (struct app_state_t *st, app_graphics_t *graphics, app_input_t input)
{
    bool blit_needed = false;
    st->gui_st.gr = *graphics;
    if (!st->is_initialized) {
        st->end_execution = false;
        st->is_initialized = true;
    }

    update_input (&st->gui_st, input);

    switch (st->gui_st.input.keycode) {
        case 24: //KEY_Q
            st->end_execution = true;
            break;
        default:
            //if (input.keycode >= 8) {
            //    printf ("%" PRIu8 "\n", input.keycode);
            //    //printf ("%" PRIu16 "\n", input.modifiers);
            //}
            break;
    }

    static struct quad_renderer_t quad_renderer;
    static struct cube_test_scene_t test_scene;
    static bool run_once = false;
    static struct camera_t main_camera;

    static GLuint fb;
    static GLuint color_texture;
    static GLuint depth_texture;
    static GLuint peel_depth_map;

    if (!run_once) {
        run_once = true;

        test_scene = init_cube_test ();
        if (test_scene.program_id == 0) {
            st->end_execution = true;
            return blit_needed;
        }

        float width = graphics->screen_width;
        float height = graphics->screen_height;
        // Framebuffer creation
        glGenFramebuffers (1, &fb);
        glBindFramebuffer (GL_FRAMEBUFFER, fb);

        create_color_texture (&color_texture, width, height, 4);

        glFramebufferTexture2D (
            GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
            GL_TEXTURE_2D_MULTISAMPLE, color_texture, 0
        );

        create_depth_texture (&peel_depth_map, width, height, 4);
        create_depth_texture (&depth_texture, width, height, 4);

        glFramebufferTexture2D (
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
        );

        quad_renderer = init_quad_renderer ();

        main_camera.near_plane = 0.1;
        main_camera.far_plane = 100;
        main_camera.pitch = M_PI/4;
        main_camera.yaw = M_PI/4;
        main_camera.distance = 4.5;
    }

    if (st->gui_st.dragging[0]) {
        vect2_t change = st->gui_st.ptr_delta;
        main_camera.pitch += 0.01 * change.y;
        main_camera.yaw -= 0.01 * change.x;
    }

    if (input.wheel != 1) {
        main_camera.distance -= (input.wheel - 1)*main_camera.distance*0.7;
    }

    main_camera.width_m = px_to_m_x (graphics, graphics->width);
    main_camera.height_m = px_to_m_y (graphics, graphics->height);

    scene_update_camera (&test_scene, &main_camera);

    int num_pass = 2;

    glEnable (GL_DEPTH_TEST);
    glBindFramebuffer (GL_FRAMEBUFFER, fb);
    glViewport (0, 0, graphics->width, graphics->height);
    glScissor (0, 0, graphics->width, graphics->height);

    // Clear peel_depth_map
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map, 0
    );

    glClearDepth (0);
    glClear (GL_DEPTH_BUFFER_BIT);
    glClearDepth (1);

    set_depth_textures (&test_scene, depth_texture, peel_depth_map);

    // Clear framebuffer
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render without blending to overwrite color buffer
    glDisable (GL_BLEND);
    render_cube_test (&test_scene);

    glEnable (GL_BLEND);
    int i;
    for (i = 0; i < num_pass-1; i++) {
        GLuint tmp = peel_depth_map;
        peel_depth_map = depth_texture;
        depth_texture = tmp;

        set_depth_textures (&test_scene, depth_texture, peel_depth_map);

        glClear(GL_DEPTH_BUFFER_BIT);

        // Render scene using UNDER blending operator
        glBlendFunc (GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
        render_cube_test (&test_scene);
    }

    // Blend resulting color buffer into the window using the OVER operator
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_into_window (graphics);
    glClearColor(0.164f, 0.203f, 0.223f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    set_texture_clip (&quad_renderer, graphics->screen_width, graphics->screen_height,
                      0, 0, graphics->width, graphics->height);
    blend_premul_quad (&quad_renderer, color_texture, true, graphics,
                        0, 0, graphics->width, graphics->height);

    return true;
}
#endif

struct x_state {
    xcb_connection_t *xcb_c;
    Display *xlib_dpy;

    xcb_screen_t *screen;
    uint8_t depth;
    xcb_visualid_t visual_id;

    xcb_drawable_t window;
    xcb_pixmap_t backbuffer;

    xcb_gcontext_t gc;

    xcb_timestamp_t last_timestamp; // Last server time received in an event.
    xcb_sync_counter_t counters[2];
    xcb_sync_int64_t counter_val;
    mem_pool_temp_marker_t transient_pool_flush;
    mem_pool_t transient_pool;

    xcb_timestamp_t clipboard_ownership_timestamp;
    bool have_clipboard_ownership;
};

struct x_state global_x11_state;

xcb_atom_t get_x11_atom (xcb_connection_t *c, const char *value)
{
    xcb_atom_t res = XCB_ATOM_NONE;
    xcb_generic_error_t *err = NULL;
    xcb_intern_atom_cookie_t ck = xcb_intern_atom (c, 0, strlen(value), value);
    xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply (c, ck, &err);
    if (err != NULL) {
        printf ("Error while requesting atom.\n");
        free (err);
        return res;
    }
    res = reply->atom;
    free(reply);
    return res;
}

enum cached_atom_names_t {
    // WM_DELETE_WINDOW protocol
    LOC_ATOM_WM_DELETE_WINDOW,

    // _NET_WM_SYNC_REQUEST protocol
    LOC_ATOM__NET_WM_SYNC_REQUEST,
    LOC_ATOM__NET_WM_SYNC__REQUEST_COUNTER,
    LOC_ATOM__NET_WM_SYNC,
    LOC_ATOM__NET_WM_FRAME_DRAWN,
    LOC_ATOM__NET_WM_FRAME_TIMINGS,

    LOC_ATOM_WM_PROTOCOLS,

    // Related to clipboard management
    LOC_ATOM_CLIPBOARD,
    LOC_ATOM__CLIPBOARD_CONTENT,
    LOC_ATOM_TARGETS,
    LOC_ATOM_TIMESTAMP,
    LOC_ATOM_MULTIPLE,
    LOC_ATOM_UTF8_STRING,
    LOC_ATOM_TEXT,
    LOC_ATOM_TEXT_MIME,
    LOC_ATOM_TEXT_MIME_CHARSET,
    LOC_ATOM_ATOM_PAIR,

    NUM_ATOMS_CACHE
};                       

xcb_atom_t xcb_atoms_cache[NUM_ATOMS_CACHE];

struct atom_enum_name_t {
    enum cached_atom_names_t id;
    const char *name;
};

void init_x11_atoms (struct x_state *x_st)
{
    struct atom_enum_name_t atom_arr[] = {
        {LOC_ATOM_WM_DELETE_WINDOW, "WM_DELETE_WINDOW"},
        {LOC_ATOM__NET_WM_SYNC_REQUEST, "_NET_WM_SYNC_REQUEST"},
        {LOC_ATOM__NET_WM_SYNC__REQUEST_COUNTER, "_NET_WM_SYNC__REQUEST_COUNTER"},
        {LOC_ATOM__NET_WM_SYNC, "_NET_WM_SYNC"},
        {LOC_ATOM__NET_WM_FRAME_DRAWN, "_NET_WM_FRAME_DRAWN"},
        {LOC_ATOM__NET_WM_FRAME_TIMINGS, "_NET_WM_FRAME_TIMINGS"},
        {LOC_ATOM_WM_PROTOCOLS, "WM_PROTOCOLS"},
        {LOC_ATOM_CLIPBOARD, "CLIPBOARD"},
        {LOC_ATOM__CLIPBOARD_CONTENT, "_CLIPBOARD_CONTENT"},
        {LOC_ATOM_TARGETS, "TARGETS"},
        {LOC_ATOM_TIMESTAMP, "TIMESTAMP"},
        {LOC_ATOM_MULTIPLE, "MULTIPLE"},
        {LOC_ATOM_UTF8_STRING, "UTF8_STRING"},
        {LOC_ATOM_TEXT, "TEXT"},
        {LOC_ATOM_TEXT_MIME, "text/plain"},
        {LOC_ATOM_TEXT_MIME_CHARSET, "text/plain;charset=utf-8"},
        {LOC_ATOM_ATOM_PAIR, "ATOM_PAIR"}
    };

    xcb_intern_atom_cookie_t cookies[ARRAY_SIZE(atom_arr)];

    uint32_t i;
    for (i=0; i<ARRAY_SIZE(atom_arr); i++) {
        const char *name = atom_arr[i].name;
        cookies[i] = xcb_intern_atom (x_st->xcb_c, 0, strlen(name), name);
    }

    for (i=0; i<ARRAY_SIZE(atom_arr); i++) {
        xcb_generic_error_t *err = NULL;
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply (x_st->xcb_c, cookies[i], &err);
        if (err != NULL) {
            printf ("Error while requesting atom in batch.\n");
            free (err);
            continue;
        }
        xcb_atoms_cache[atom_arr[i].id] = reply->atom;
        free (reply);
    }
}

char* get_x11_atom_name (xcb_connection_t *c, xcb_atom_t atom, mem_pool_t *pool)
{
    if (atom == XCB_ATOM_NONE) {
        return NULL;
    }

    xcb_get_atom_name_cookie_t ck = xcb_get_atom_name (c, atom);
    xcb_generic_error_t *err = NULL;
    xcb_get_atom_name_reply_t *reply = xcb_get_atom_name_reply (c, ck, &err);
    if (err != NULL) {
        printf ("Error while requesting atom's name.\n");
        free (err);
    }

    char *name = xcb_get_atom_name_name (reply);
    size_t len = xcb_get_atom_name_name_length (reply);

    char *res = (char*)mem_pool_push_size (pool, len+1);
    memcpy (res, name, len);
    res[len] = '\0';

    return res;
}

void get_x11_property_part (xcb_connection_t *c, xcb_drawable_t window,
                            xcb_atom_t property, uint32_t offset, uint32_t len,
                            xcb_get_property_reply_t **reply)
{
    xcb_get_property_cookie_t ck =
        xcb_get_property (c, 0, window, property, XCB_GET_PROPERTY_TYPE_ANY,
                          offset, len);
    xcb_generic_error_t *err = NULL;
    *reply = xcb_get_property_reply (c, ck, &err);
    if (err != NULL) {
        printf ("Error reading property.\n");
        free (err);
    }
}

char* get_x11_text_property (xcb_connection_t *c, mem_pool_t *pool,
                             xcb_drawable_t window, xcb_atom_t property,
                             size_t *len)
{

    xcb_get_property_reply_t *reply_1, *reply_2 = NULL;
    uint32_t first_request_size = 10;
    get_x11_property_part (c, window, property, 0, first_request_size, &reply_1);

    uint32_t len_1, len_total;
    len_1 = len_total = xcb_get_property_value_length (reply_1);

    if (reply_1->type == XCB_ATOM_NONE) {
        *len = 0;
        return NULL;
    }

    if (reply_1->type != xcb_atoms_cache[LOC_ATOM_UTF8_STRING] &&
        reply_1->type != XCB_ATOM_STRING && 
        reply_1->type != xcb_atoms_cache[LOC_ATOM_TEXT_MIME_CHARSET] &&
        reply_1->type != xcb_atoms_cache[LOC_ATOM_TEXT_MIME]) {
        mem_pool_t temp_pool = {0};
        printf ("Invalid text property (%s)\n", get_x11_atom_name (c, reply_1->type, &temp_pool));
        mem_pool_destroy (&temp_pool);
        return NULL;
    }

    if (reply_1->bytes_after != 0) {
        get_x11_property_part (c, window, property, first_request_size,
                               I_CEIL_DIVIDE(reply_1->bytes_after,4), &reply_2);
        len_total += xcb_get_property_value_length (reply_2);
    }

    char *res = (char*)pom_push_size (pool, len_total+1);

    memcpy (res, xcb_get_property_value (reply_1), len_1);
    if (reply_1->bytes_after != 0) {
        memcpy ((char*)res+len_1, xcb_get_property_value (reply_2), len_total-len_1);
        free (reply_2);
    }
    free (reply_1);
    res[len_total] = '\0';

    if (len != NULL) {
        *len = len_total;
    }

    return res;
}

void* get_x11_property (xcb_connection_t *c, mem_pool_t *pool, xcb_drawable_t window,
                        xcb_atom_t property, size_t *len, xcb_atom_t *type)
{

    xcb_get_property_reply_t *reply_1, *reply_2 = NULL;
    uint32_t first_request_size = 10;
    get_x11_property_part (c, window, property, 0, first_request_size, &reply_1);
    uint32_t len_1 = *len = xcb_get_property_value_length (reply_1);
    if (reply_1->bytes_after != 0) {
        get_x11_property_part (c, window, property, first_request_size,
                               I_CEIL_DIVIDE(reply_1->bytes_after,4), &reply_2);
        *len += xcb_get_property_value_length (reply_2);
    }

    void *res = mem_pool_push_size (pool, *len);
    *type = reply_1->type;

    memcpy (res, xcb_get_property_value (reply_1), len_1);
    if (reply_1->bytes_after != 0) {
        memcpy ((char*)res+len_1, xcb_get_property_value (reply_2), *len-len_1);
        free (reply_2);
    }
    free (reply_1);

    return res;
}

void print_x11_property (xcb_connection_t *c, xcb_drawable_t window, xcb_atom_t property)
{
    mem_pool_t pool = {0};
    size_t len;
    xcb_atom_t type;

    if (property == XCB_ATOM_NONE) {
        printf ("NONE\n");
        return;
    }

    void * value = get_x11_property (c, &pool, window, property, &len, &type);
    printf ("%s (%s)", get_x11_atom_name(c, property, &pool), get_x11_atom_name(c, type, &pool));
    if (type == xcb_atoms_cache[LOC_ATOM_UTF8_STRING] ||
        type == xcb_atoms_cache[LOC_ATOM_TEXT_MIME_CHARSET] ||
        type == xcb_atoms_cache[LOC_ATOM_TEXT_MIME] ||
        type == XCB_ATOM_STRING) {

        if (type == XCB_ATOM_STRING) {
            // NOTE: This is a latin1 encoded string so some characters won't
            // print niceley, we also show the binary data so that people notice
            // this. Still it's better to show some string than just the binary
            // data.
            printf (" = %.*s (", (int)len, (char*)value);
            unsigned char *ptr = (unsigned char*)value;
            len--;
            while (len) {
                printf ("0x%X ", *ptr);
                ptr++;
                len--;
            }
            printf ("0x%X)\n", *ptr);
        } else {
            printf (" = %.*s\n", (int)len, (char*)value);
        }
    } else if (type == XCB_ATOM_ATOM) {
        xcb_atom_t *atom_list = (xcb_atom_t*)value;
        printf (" = ");
        while (len > sizeof(xcb_atom_t)) {
            printf ("%s, ", get_x11_atom_name (c, *atom_list, &pool));
            atom_list++;
            len -= sizeof(xcb_atom_t);
        }
        printf ("%s\n", get_x11_atom_name (c, *atom_list, &pool));
    } else if (type == XCB_ATOM_NONE) {
        printf ("\n");
    } else {
        unsigned char *ptr = (unsigned char*)value;
        if (len != 0) {
            printf (" = ");
            len--;
            while (len) {
                printf ("0x%X ", *ptr);
                ptr++;
                len--;
            }
            printf ("0x%X\n", *ptr);
        } else {
            printf ("\n");
        }
    }
}

void increment_sync_counter (xcb_sync_int64_t *counter)
{
    counter->lo++;
    if (counter->lo == 0) {
        counter->hi++;
    }
}

// NOTE: This seems to be the only way to get the depth from a visual_id.
#define xcb_visual_type_from_visualid(c,screen,id) \
    xcb_visual_id_lookup (c,screen,id,NULL)
xcb_visualtype_t*
xcb_visual_id_lookup (xcb_connection_t *c, xcb_screen_t *screen,
                      xcb_visualid_t id, uint8_t *depth)
{
    xcb_visualtype_t  *visual_type = NULL;

    xcb_depth_iterator_t depth_iter;
    if (screen) {
        depth_iter = xcb_screen_allowed_depths_iterator (screen);
        for (; depth_iter.rem; xcb_depth_next (&depth_iter)) {
            if (depth != NULL) {
                *depth = depth_iter.data->depth;
            }

            xcb_visualtype_iterator_t visual_iter;
            visual_iter = xcb_depth_visuals_iterator (depth_iter.data);
            for (; visual_iter.rem; xcb_visualtype_next (&visual_iter)) {
                if (id == visual_iter.data->visual_id) {
                    visual_type = visual_iter.data;
                    break;
                }
            }

            if (visual_type != NULL) {
                break;
            }
        }
    }
    return visual_type;
}

uint8_t xcb_get_visual_max_depth (xcb_connection_t *c, xcb_screen_t *screen)
{
    uint8_t depth = 0;
    xcb_depth_iterator_t depth_iter;
    if (screen) {
        depth_iter = xcb_screen_allowed_depths_iterator (screen);
        for (; depth_iter.rem; xcb_depth_next (&depth_iter)) {
            if (depth < depth_iter.data->depth) {
                depth = depth_iter.data->depth;
            }
        }
    }
    return depth;
}

xcb_visualtype_t* get_visual_of_max_depth (xcb_connection_t *c, xcb_screen_t *screen, uint8_t *found_depth)
{
    xcb_visualtype_t  *visual_type = NULL;

    *found_depth = 0;
    xcb_depth_iterator_t depth_iter;
    if (screen) {
        depth_iter = xcb_screen_allowed_depths_iterator (screen);
        for (; depth_iter.rem; xcb_depth_next (&depth_iter)) {
            xcb_visualtype_iterator_t visual_iter;
            visual_iter = xcb_depth_visuals_iterator (depth_iter.data);
            if (visual_iter.rem) {
                if (*found_depth < depth_iter.data->depth) {
                    *found_depth = depth_iter.data->depth;
                    visual_type = visual_iter.data;
                }
            }
        }
    }
    return visual_type;
}

Visual* Visual_from_visualid (Display *dpy, xcb_visualid_t visualid)
{
    XVisualInfo templ = {0};
    templ.visualid = visualid;
    int n;
    XVisualInfo *found = XGetVisualInfo (dpy, VisualIDMask, &templ, &n);
    Visual *retval = found->visual;
    XFree (found);
    return retval;
}

void x11_change_property (xcb_connection_t *c, xcb_drawable_t window,
                          xcb_atom_t property, xcb_atom_t type,
                          uint32_t len, const void *data,
                          bool checked)
{
    uint8_t format = 32;
    if (type == XCB_ATOM_STRING ||
        type == xcb_atoms_cache[LOC_ATOM_UTF8_STRING] ||
        type == xcb_atoms_cache[LOC_ATOM_TEXT_MIME] ||
        type == xcb_atoms_cache[LOC_ATOM_TEXT_MIME_CHARSET])
    {
        format = 8;
    } else if (type == XCB_ATOM_ATOM ||
               type == XCB_ATOM_CARDINAL ||
               type == XCB_ATOM_INTEGER) {
        format = 32;
    }

    if (checked) {
        xcb_void_cookie_t ck =
            xcb_change_property_checked (c,
                                         XCB_PROP_MODE_REPLACE,
                                         window,
                                         property,
                                         type,
                                         format,
                                         len,
                                         data);
        
        xcb_generic_error_t *error; 
        if ((error = xcb_request_check(c, ck))) { 
            printf("Error changing property %d\n", error->error_code); 
            free(error); 
        }
    } else {
        xcb_change_property (c,
                             XCB_PROP_MODE_REPLACE,
                             window,
                             property,
                             type,
                             format,
                             len,
                             data);
    }
}

void x11_create_window (struct x_state *x_st, const char *title, int visual_id)
{
    uint32_t event_mask = XCB_EVENT_MASK_EXPOSURE|XCB_EVENT_MASK_KEY_PRESS|
                             XCB_EVENT_MASK_STRUCTURE_NOTIFY|XCB_EVENT_MASK_BUTTON_PRESS|
                             XCB_EVENT_MASK_BUTTON_RELEASE|XCB_EVENT_MASK_POINTER_MOTION|
                             XCB_EVENT_MASK_PROPERTY_CHANGE;
    uint32_t mask = XCB_CW_EVENT_MASK;

    // NOTE: We will probably want a window that allows transparencies, this
    // means it has higher depth than the root window. Usually colormap and
    // color_pixel are inherited from root, if we don't set them here and the
    // depths are different window creation will fail.
    mask |= XCB_CW_BORDER_PIXEL|XCB_CW_COLORMAP;
    xcb_colormap_t colormap = xcb_generate_id (x_st->xcb_c);
    xcb_create_colormap (x_st->xcb_c, XCB_COLORMAP_ALLOC_NONE,
                         colormap, x_st->screen->root, visual_id); 


    uint32_t values[] = {// , // XCB_CW_BACK_PIXMAP
                         // , // XCB_CW_BACK_PIXEL
                         // , // XCB_CW_BORDER_PIXMAP
                         0  , // XCB_CW_BORDER_PIXEL
                         // , // XCB_CW_BIT_GRAVITY
                         // , // XCB_CW_WIN_GRAVITY
                         // , // XCB_CW_BACKING_STORE
                         // , // XCB_CW_BACKING_PLANES
                         // , // XCB_CW_BACKING_PIXEL     
                         // , // XCB_CW_OVERRIDE_REDIRECT
                         // , // XCB_CW_SAVE_UNDER
                         event_mask, //XCB_CW_EVENT_MASK
                         // , // XCB_CW_DONT_PROPAGATE
                         colormap, // XCB_CW_COLORMAP
                         //  // XCB_CW_CURSOR
                         };

    x_st->window = xcb_generate_id (x_st->xcb_c);
    xcb_void_cookie_t ck = xcb_create_window_checked (
                x_st->xcb_c,                   /* connection          */
                x_st->depth,                   /* depth               */
                x_st->window,                  /* window Id           */
                x_st->screen->root,            /* parent window       */
                0, 0,                          /* x, y                */
                WINDOW_WIDTH, WINDOW_HEIGHT,   /* width, height       */
                0,                             /* border_width        */
                XCB_WINDOW_CLASS_INPUT_OUTPUT, /* class               */
                visual_id,                     /* visual              */
                mask, values);                 /* masks */
    xcb_generic_error_t *err = xcb_request_check (x_st->xcb_c, ck);
    if (err != NULL) {
        printf ("Window creation failed (Error code: %d)\n", err->error_code);
        free (err);
        return;
    }

    // Set window title
    x11_change_property (x_st->xcb_c,
            x_st->window,
            XCB_ATOM_WM_NAME,
            XCB_ATOM_STRING,
            strlen (title),
            title,
            false);
}

void x11_setup_icccm_and_ewmh_protocols (struct x_state *x_st)
{
    xcb_atom_t net_wm_protocols_value[2];

    // Set up counters for extended mode
    x_st->counter_val = (xcb_sync_int64_t){0, 0}; //{HI, LO}
    x_st->counters[0] = xcb_generate_id (x_st->xcb_c);
    xcb_sync_create_counter (x_st->xcb_c, x_st->counters[0], x_st->counter_val);

    x_st->counters[1] = xcb_generate_id (x_st->xcb_c);
    xcb_sync_create_counter (x_st->xcb_c, x_st->counters[1], x_st->counter_val);

    x11_change_property (x_st->xcb_c,
                         x_st->window,
                         xcb_atoms_cache[LOC_ATOM__NET_WM_SYNC__REQUEST_COUNTER],
                         XCB_ATOM_CARDINAL,
                         ARRAY_SIZE(x_st->counters),
                         x_st->counters,
                         false);

    /////////////////////////////
    // Set WM_PROTOCOLS property
    net_wm_protocols_value[0] = xcb_atoms_cache[LOC_ATOM_WM_DELETE_WINDOW];
    net_wm_protocols_value[1] = xcb_atoms_cache[LOC_ATOM__NET_WM_SYNC_REQUEST];

    x11_change_property (x_st->xcb_c,
                         x_st->window,
                         xcb_atoms_cache[LOC_ATOM_WM_PROTOCOLS],
                         XCB_ATOM_ATOM,
                         ARRAY_SIZE(net_wm_protocols_value),
                         net_wm_protocols_value,
                         false);
}

void blocking_xcb_sync_set_counter (xcb_connection_t *c, xcb_sync_counter_t counter, xcb_sync_int64_t *val)
{
    xcb_void_cookie_t ck = xcb_sync_set_counter_checked (c, counter, *val);
    xcb_generic_error_t *error; 
    if ((error = xcb_request_check(c, ck))) { 
        printf("Error setting counter %d\n", error->error_code); 
        free(error); 
    }
}

void x11_notify_start_of_frame (struct x_state *x_st)
{
    increment_sync_counter (&x_st->counter_val);
    assert (x_st->counter_val.lo % 2 == 1);
    blocking_xcb_sync_set_counter (x_st->xcb_c, x_st->counters[1], &x_st->counter_val);
}

void x11_notify_end_of_frame (struct x_state *x_st)
{
    increment_sync_counter (&x_st->counter_val);
    assert (x_st->counter_val.lo % 2 == 0);
    blocking_xcb_sync_set_counter (x_st->xcb_c, x_st->counters[1], &x_st->counter_val);
}

void x11_print_window_name (struct x_state *x_st, xcb_drawable_t window)
{
    size_t len;
    char *prop = get_x11_text_property (x_st->xcb_c, &x_st->transient_pool, window, XCB_ATOM_WM_NAME, &len);
    printf ("id: 0x%x, \"%*s\"\n", window, (uint32_t)len, prop);
}

// NOTE: IMPORTANT!!! event MUST have 32 bytes allocated, otherwise we will read
// invalid values. Use xcb_generic_event_t and NOT xcb_<some_event>_event_t.
void x11_send_event (xcb_connection_t *c, xcb_drawable_t window, void *event)
{
    xcb_void_cookie_t ck = xcb_send_event_checked (c, 0, window, 0, (const char*)event);
    xcb_generic_error_t *error; 
    if ((error = xcb_request_check(c, ck))) { 
        printf("Error sending event. %d\n", error->error_code); 
        free(error); 
    }
}

void x11_get_screen_extents (struct x_state *x_st, float *x_dpi, float *y_dpi,
                             uint16_t *width, uint16_t *height)
{
    const xcb_query_extension_reply_t * render_info =
        xcb_get_extension_data (x_st->xcb_c, &xcb_randr_id);

    if (!render_info->present) {
        *width = x_st->screen->width_in_pixels;
        *height = x_st->screen->height_in_pixels;
        *x_dpi = x_st->screen->width_in_pixels * 25.4 / (float)x_st->screen->width_in_millimeters;
        *y_dpi = x_st->screen->height_in_pixels * 25.4 / (float)x_st->screen->height_in_millimeters;
        printf ("No RANDR extension, computing DPI from X11 screen object. It's probably wrong.\n");
        return;
    }

    xcb_randr_get_screen_resources_cookie_t ck =
        xcb_randr_get_screen_resources (x_st->xcb_c, x_st->window);
    xcb_generic_error_t *error = NULL;
    xcb_randr_get_screen_resources_reply_t *get_resources_reply =
        xcb_randr_get_screen_resources_reply (x_st->xcb_c, ck, &error);
    if (error) {
        printf("RANDR: Error getting screen resources. %d\n", error->error_code);
        free(error);
        return;
    }

    xcb_randr_output_t *outputs =
        xcb_randr_get_screen_resources_outputs (get_resources_reply);
    int num_outputs =
        xcb_randr_get_screen_resources_outputs_length  (get_resources_reply);

    // TODO: Compute in which CRTC is x_st->window. ATM we assume there is only
    // one output and use that.
    int num_active_outputs = 0;
    xcb_randr_output_t output = 0;
    int i;
    for (i=0; i<num_outputs; i++) {
        xcb_randr_get_output_info_cookie_t ck =
            xcb_randr_get_output_info (x_st->xcb_c, outputs[i], XCB_CURRENT_TIME);
        xcb_randr_get_output_info_reply_t *output_info =
            xcb_randr_get_output_info_reply (x_st->xcb_c, ck, &error);
        if (error) {
            printf("RANDR: Error getting output info. %d\n", error->error_code);
            free(error);
            return;
        }

        if (output_info->crtc) {
            num_active_outputs++;
            if (output == 0) {
                output = outputs[i];
            }
        }
    }

    if (num_active_outputs != 1) {
        printf ("There are more than 1 outputs, we may be setting the DPI of the wrong one.\n");
    }

    xcb_randr_get_output_info_reply_t *output_info;
    {
        xcb_randr_get_output_info_cookie_t ck =
            xcb_randr_get_output_info (x_st->xcb_c, output, XCB_CURRENT_TIME);
        output_info = xcb_randr_get_output_info_reply (x_st->xcb_c, ck, &error);
        if (error) {
            printf("RANDR: Error getting output info. %d\n", error->error_code);
            free(error);
            return;
        }
    }

    xcb_randr_get_crtc_info_reply_t *crtc_info;
    {
        xcb_randr_get_crtc_info_cookie_t ck =
            xcb_randr_get_crtc_info (x_st->xcb_c, output_info->crtc, XCB_CURRENT_TIME);
        crtc_info = xcb_randr_get_crtc_info_reply (x_st->xcb_c, ck, &error);
        if (error) {
            printf("RANDR: Error getting crtc info. %d\n", error->error_code);
            free(error);
            return;
        }
    }

    *width = crtc_info->width;
    *height = crtc_info->height;
    *x_dpi = crtc_info->width / (float)output_info->mm_width;
    *y_dpi = crtc_info->height / (float)output_info->mm_height;
}

typedef GLXContext (*glXCreateContextAttribsARBProc)(Display*, GLXFBConfig, GLXContext, Bool, const int*);

int main (void)
{
    // Setup clocks
    setup_clocks ();

    //////////////////
    // X11 setup
    // By default xcb is used, because it allows more granularity if we ever reach
    // performance issues, but for cases when we need Xlib functions we have an
    // Xlib Display too.
    struct x_state *x_st = &global_x11_state;
    x_st->transient_pool_flush = mem_pool_begin_temporary_memory (&x_st->transient_pool);

    x_st->xlib_dpy = XOpenDisplay (NULL);
    if (!x_st->xlib_dpy) {
        printf ("Could not open display\n");
        return -1;
    }

    x_st->xcb_c = XGetXCBConnection (x_st->xlib_dpy);
    if (!x_st->xcb_c) {
        printf ("Could not get XCB x_st->xcb_c from Xlib Display\n");
        return -1;
    }
    XSetEventQueueOwner (x_st->xlib_dpy, XCBOwnsEventQueue);

    init_x11_atoms (x_st);

    /* Get the default screen */
    // TODO: Do this using only xcb.
    // TODO: What happens if there is more than 1 screen?, probably will
    // have to iterate with xcb_setup_roots_iterator(), and xcb_screen_next ().
    int default_screen = DefaultScreen (x_st->xlib_dpy);
    xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator (xcb_get_setup (x_st->xcb_c));
    int scr_id = 0;
    for (; screen_iter.rem; scr_id++, xcb_screen_next (&screen_iter)) {
        if (scr_id == default_screen) {
            x_st->screen = screen_iter.data;
        }
    }

    /* Get a GLXFBConfig */
    // We want a GLXFBConfig with a double buffer and a X11 visual that allows
    // for alpha channel in the window (transparent windows).
    int num_GLX_confs;
    int attrib_list[] = {GLX_DOUBLEBUFFER, GL_TRUE,
                         GLX_RED_SIZE, 8,
                         GLX_GREEN_SIZE, 8,
                         GLX_BLUE_SIZE, 8,
                         GLX_ALPHA_SIZE, 8,
                         GLX_STENCIL_SIZE, 8,
                         GLX_SAMPLE_BUFFERS, 1,
                         GLX_SAMPLES, 4,
                         GL_NONE};
    GLXFBConfig* framebuffer_confs =
        glXChooseFBConfig (x_st->xlib_dpy, default_screen, attrib_list, &num_GLX_confs);
    // NOTE: Turns out that GLX_BUFFER_SIZE is independent from X11 Visual's
    // depth. We get all GLXFBConfigs that have an alpha channel and then
    // itereate over them until we find one where its visual has the highest
    // depth available.
    int visual_id;
    uint8_t x11_depth = 0;
    uint8_t max_x11_depth = xcb_get_visual_max_depth (x_st->xcb_c, x_st->screen);
    int i;
    for (i=0; i<num_GLX_confs; i++) {
        glXGetFBConfigAttrib (x_st->xlib_dpy, framebuffer_confs[i], GLX_VISUAL_ID, &visual_id);
        xcb_visual_id_lookup (x_st->xcb_c, x_st->screen, visual_id, &x11_depth);
        if (x11_depth == max_x11_depth) {
            break;
        }
    }
    GLXFBConfig framebuffer_config = framebuffer_confs[i];

    x_st->depth = x11_depth;
    x_st->visual_id = visual_id;

    if (num_GLX_confs == 0 || x11_depth != max_x11_depth) {
        printf ("Failed to get an good glXConfig.\n");
        return -1;
    }

    if (max_x11_depth != 32) {
        printf ("Can't create a window with alpha channel.\n");
    }

    x11_create_window (x_st, "Closet Maker", x_st->visual_id);

    x11_setup_icccm_and_ewmh_protocols (x_st);

    xcb_map_window (x_st->xcb_c, x_st->window);

    /* Set up the GL context */
    glXCreateContextAttribsARBProc glXCreateContextAttribsARB = 0;
    glXCreateContextAttribsARB = (glXCreateContextAttribsARBProc)
        glXGetProcAddressARB( (const GLubyte *) "glXCreateContextAttribsARB" );

    int context_attribs[] = {
        GLX_CONTEXT_MAJOR_VERSION_ARB, 3,
        GLX_CONTEXT_MINOR_VERSION_ARB, 2,
        //GLX_CONTEXT_FLAGS_ARB        , GLX_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
        0
      };

    GLXContext gl_context = glXCreateContextAttribsARB(
        x_st->xlib_dpy, framebuffer_config, 0,
        GL_TRUE, context_attribs
    );

    // TODO: If we fail to get a 3.2 context fallback to an older version, maybe
    // using the old API:
    //GLXContext gl_context =
    //    glXCreateNewContext(x_st->xlib_dpy, framebuffer_config, GLX_RGBA_TYPE, NULL, GL_TRUE);

    GLXWindow glX_window =
        glXCreateWindow(x_st->xlib_dpy, framebuffer_config, x_st->window, NULL);

    if(!glXMakeContextCurrent(x_st->xlib_dpy, glX_window, glX_window, gl_context))
    {
        xcb_destroy_window(x_st->xcb_c, x_st->window);
        glXDestroyContext(x_st->xlib_dpy, gl_context);

        printf("glXMakeContextCurrent() failed\n");
        return -1;
    }

    GLboolean has_compiler;
    glGetBooleanv (GL_SHADER_COMPILER, &has_compiler);
    assert (has_compiler == GL_TRUE);
    glEnable(GL_SCISSOR_TEST);

    glEnable (GL_DEBUG_OUTPUT);
    glDebugMessageCallback((GLDEBUGPROC)MessageCallback, 0);

    glEnable (GL_MULTISAMPLE);

    // ////////////////
    // Main event loop
    xcb_generic_event_t *event;
    app_graphics_t graphics;
    graphics.width = WINDOW_WIDTH;
    graphics.height = WINDOW_HEIGHT;
    x11_get_screen_extents (x_st, &graphics.x_dpi, &graphics.y_dpi,
                            &graphics.screen_width, &graphics.screen_height);
    bool force_blit = false;

    float frame_rate = 60;
    float target_frame_length_ms = 1000/(frame_rate);
    struct timespec start_ticks;
    struct timespec end_ticks;

    clock_gettime(CLOCK_MONOTONIC, &start_ticks);
    app_input_t app_input = {0};
    app_input.wheel = 1;

    mem_pool_t bootstrap = {0};
    struct app_state_t *st =
        (struct app_state_t*)mem_pool_push_size_full (&bootstrap, sizeof(struct app_state_t), POOL_ZERO_INIT);
    st->memory = bootstrap;

    while (!st->end_execution) {
        while ((event = xcb_poll_for_event (x_st->xcb_c))) {
            // NOTE: The most significant bit of event->response_type is set if
            // the event was generated from a SendEvent request, here we don't
            // care about the source of the event.
            switch (event->response_type & ~0x80) {
                case XCB_CONFIGURE_NOTIFY:
                    {
                        graphics.width = ((xcb_configure_notify_event_t*)event)->width;
                        graphics.height = ((xcb_configure_notify_event_t*)event)->height;
                    } break;
                case XCB_MOTION_NOTIFY:
                    {
                        x_st->last_timestamp = ((xcb_motion_notify_event_t*)event)->time;
                        app_input.ptr.x = ((xcb_motion_notify_event_t*)event)->event_x;
                        app_input.ptr.y = ((xcb_motion_notify_event_t*)event)->event_y;
                    } break;
                case XCB_KEY_PRESS:
                    {
                        x_st->last_timestamp = ((xcb_key_press_event_t*)event)->time;
                        app_input.keycode = ((xcb_key_press_event_t*)event)->detail;
                        app_input.modifiers = ((xcb_key_press_event_t*)event)->state;
                    } break;
                case XCB_EXPOSE:
                    {
                        // We should tell which areas need exposing
                        app_input.force_redraw = true;
                        force_blit = true;
                    } break;
                case XCB_BUTTON_PRESS:
                    {
                       x_st->last_timestamp = ((xcb_key_press_event_t*)event)->time;
                       char button_pressed = ((xcb_key_press_event_t*)event)->detail;
                       if (button_pressed == 4) {
                           app_input.wheel *= 1.2;
                       } else if (button_pressed == 5) {
                           app_input.wheel /= 1.2;
                       } else if (button_pressed >= 1 && button_pressed <= 3) {
                           app_input.mouse_down[button_pressed-1] = 1;
                       }
                    } break;
                case XCB_BUTTON_RELEASE:
                    {
                        x_st->last_timestamp = ((xcb_key_press_event_t*)event)->time;
                        // NOTE: This loses clicks if the button press-release
                        // sequence happens in the same batch of events, right now
                        // it does not seem to be a problem.
                        char button_pressed = ((xcb_key_press_event_t*)event)->detail;
                        if (button_pressed >= 1 && button_pressed <= 3) {
                            app_input.mouse_down[button_pressed-1] = 0;
                        }
                    } break;
                case XCB_CLIENT_MESSAGE:
                    {
                        bool handled = false;
                        xcb_client_message_event_t *client_message = ((xcb_client_message_event_t*)event);

                        // WM_DELETE_WINDOW protocol
                        if (client_message->type == xcb_atoms_cache[LOC_ATOM_WM_PROTOCOLS]) {
                            if (client_message->data.data32[0] == xcb_atoms_cache[LOC_ATOM_WM_DELETE_WINDOW]) {
                                st->end_execution = true;
                            }
                            handled = true;
                        }

                        // _NET_WM_SYNC_REQUEST protocol using the extended mode
                        if (client_message->type == xcb_atoms_cache[LOC_ATOM_WM_PROTOCOLS] &&
                            client_message->data.data32[0] == xcb_atoms_cache[LOC_ATOM__NET_WM_SYNC_REQUEST]) {

                            x_st->counter_val.lo = client_message->data.data32[2];
                            x_st->counter_val.hi = client_message->data.data32[3];
                            if (x_st->counter_val.lo % 2 != 0) {
                                increment_sync_counter (&x_st->counter_val);
                            }
                            handled = true;
                        } else if (client_message->type == xcb_atoms_cache[LOC_ATOM__NET_WM_FRAME_DRAWN]) {
                            handled = true;
                        } else if (client_message->type == xcb_atoms_cache[LOC_ATOM__NET_WM_FRAME_TIMINGS]) {
                            handled = true;
                        }

                        if (!handled) {
                            printf ("Unrecognized Client Message: %s\n",
                                    get_x11_atom_name (x_st->xcb_c,
                                                       client_message->type,
                                                       &x_st->transient_pool));
                        }
                    } break;
                case XCB_PROPERTY_NOTIFY:
                    {
                        x_st->last_timestamp = ((xcb_property_notify_event_t*)event)->time;
                    } break;
                case 0:
                    { // XCB_ERROR
                        xcb_generic_error_t *error = (xcb_generic_error_t*)event;
                        printf("Received X11 error %d\n", error->error_code);
                    } break;
                default: 
                    /* Unknown event type, ignore it */
                    break;
            }
            free (event);
        }

        x11_notify_start_of_frame (x_st);

        // TODO: How bad is this? should we actually measure it?
        app_input.time_elapsed_ms = target_frame_length_ms;

        bool blit_needed = update_and_render (st, &graphics, app_input);

        if (blit_needed || force_blit) {
            glXSwapBuffers(x_st->xlib_dpy, glX_window);
            force_blit = false;
        }

        x11_notify_end_of_frame (x_st);

        clock_gettime (CLOCK_MONOTONIC, &end_ticks);
        float time_elapsed = time_elapsed_in_ms (&start_ticks, &end_ticks);
        if (time_elapsed < target_frame_length_ms) {
            struct timespec sleep_ticks;
            sleep_ticks.tv_sec = 0;
            sleep_ticks.tv_nsec = (long)((target_frame_length_ms-time_elapsed)*1000000);
            nanosleep (&sleep_ticks, NULL);
        } else {
            printf ("Frame missed! %f ms elapsed\n", time_elapsed);
        }

        clock_gettime(CLOCK_MONOTONIC, &end_ticks);
        //printf ("FPS: %f\n", 1000/time_elapsed_in_ms (&start_ticks, &end_ticks));
        start_ticks = end_ticks;

        xcb_flush (x_st->xcb_c);
        app_input.keycode = 0;
        app_input.wheel = 1;
        app_input.force_redraw = 0;
        mem_pool_end_temporary_memory (x_st->transient_pool_flush);
    }

    glXDestroyWindow(x_st->xlib_dpy, glX_window);
    xcb_destroy_window(x_st->xcb_c, x_st->window);
    glXDestroyContext (x_st->xlib_dpy, gl_context);
    XCloseDisplay (x_st->xlib_dpy);

    // NOTE: Even though we try to clean everything up Valgrind complains a
    // lot about leaking objects. Mainly inside Pango, Glib (libgobject),
    // libontconfig. Maybe all of that is global data that does not grow over
    // time but I haven't found a way to check this is the case.

    // These are not necessary but make valgrind complain less when debugging
    // memory leaks.
    gui_destroy (&st->gui_st);
    mem_pool_destroy (&st->memory);

    return 0;
}

