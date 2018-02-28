/*
 * Copiright (C) 2018 Santiago León O.
 */

struct camera_t {
    float width_m;
    float height_m;
    float near_plane;
    float far_plane;
    float pitch;
    float yaw;
    float distance;
};

dvec3 camera_compute_pos (struct camera_t *camera)
{
    camera->pitch = CLAMP (camera->pitch, -M_PI/2 + 0.0001, M_PI/2 - 0.0001);
    camera->yaw = WRAP (camera->yaw, -M_PI, M_PI);
    camera->distance = LOW_CLAMP (camera->distance, camera->near_plane);

    return DVEC3 (cos(camera->pitch)*sin(camera->yaw)*camera->distance,
                  sin(camera->pitch)*camera->distance,
                  cos(camera->pitch)*cos(camera->yaw)*camera->distance);
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
    fvec3 v[8];
};

#define UNIT_CUBE (struct cuboid_t){{\
                    FVEC3(-1,-1,-1), \
                    FVEC3(-1,-1, 1), \
                    FVEC3(-1, 1,-1), \
                    FVEC3(-1, 1, 1), \
                    FVEC3( 1,-1,-1), \
                    FVEC3( 1,-1, 1), \
                    FVEC3( 1, 1,-1), \
                    FVEC3( 1, 1, 1), \
                   }}

#define CUBOID_SIZE_X(c) ((c).v[4].x - (c).v[0].x)
#define CUBOID_SIZE_Y(c) ((c).v[2].y - (c).v[0].y)
#define CUBOID_SIZE_Z(c) ((c).v[1].z - (c).v[0].z)

void cuboid_init (fvec3 dim, struct cuboid_t *res)
{
    *res = UNIT_CUBE;

    int i;
    for (i=0; i<8; i++) {
        res->v[i].x = res->v[i].x * dim.x/2;
        res->v[i].y = res->v[i].y * dim.y/2;
        res->v[i].z = res->v[i].z * dim.z/2;
    }
}

void cuboid_init_anchored (fvec3 dim,
                           enum cube_vertices_t anchor_id, fvec3 anchor_pos,
                           struct cuboid_t *res)
{
    *res = UNIT_CUBE;

    fvec3 new_anchor = res->v[anchor_id];
    new_anchor.x *= dim.x/2;
    new_anchor.y *= dim.y/2;
    new_anchor.z *= dim.z/2;

    fvec3 disp = fvec3_subs (anchor_pos, new_anchor);

    int i;
    for (i=0; i<8; i++) {
        res->v[i].x = res->v[i].x * dim.x/2 + disp.x;
        res->v[i].y = res->v[i].y * dim.y/2 + disp.y;
        res->v[i].z = res->v[i].z * dim.z/2 + disp.z;
    }
}

void cuboid_print (struct cuboid_t *cb)
{
    int i;
    for (i=0; i<8; i++) {
        fvec3_print (cb->v[i]);
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
fvec3 hole_dim_direct_to_fvec3 (struct hole_dimensions_t *dim)
{
    return FVEC3 (dim->x.val, dim->y.val, dim->z.val);
}

struct separator_part_t {
    struct cuboid_t c;
    fvec3 color;
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
    fvec3 ldb = cuboid->v[0];
    fvec3 ldf = cuboid->v[1];
    fvec3 lub = cuboid->v[2];
    fvec3 luf = cuboid->v[3];
    fvec3 rdb = cuboid->v[4];
    fvec3 rdf = cuboid->v[5];
    fvec3 rub = cuboid->v[6];
    fvec3 ruf = cuboid->v[7];

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

struct separator_part_t* next_sep_part (struct closet_t *cl)
{
    assert (cl->num_sep_parts < cl->size_sep_parts - 1);
    struct separator_part_t *res = &cl->sep_parts[cl->num_sep_parts++];
    res->color = FVEC3 (1, 1, 0);
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
void color_separator (struct separator_t *sep, fvec3 color)
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

    fvec3 hole_size = hole_dim_direct_to_fvec3 (dim);
    struct hole_t *new_hole = next_hole (&res);
    cuboid_init (hole_size, &new_hole->h);

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
    fvec3 anchor_pos;
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
    fvec3 dim_vec = FVEC3(0,0,0);
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
    cuboid_init_anchored (dim_vec, anchor_id, anchor_pos, &new_hole->h);

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

    dvec3 camera_pos = camera_compute_pos (camera);
    mat4f view = look_at (camera_pos,
                          DVEC3(0,0,0),
                          DVEC3(0,1,0));
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
        fvec3 c = cl->sep_parts[i].color;
        glUniform4f (closet_scene->color_loc, c.r, c.g, c.b, 0.8);
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
    cuboid_init (FVEC3 (1,1,1), &cube);
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

    dvec3 camera_pos = camera_compute_pos (camera);
    mat4f view = look_at (camera_pos,
                          DVEC3(0,0,0),
                          DVEC3(0,1,0));
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

void depth_peel_set_shader_slots (GLuint program_id,
                                  GLuint color_texture, GLuint depth_texture,
                                  GLuint peel_depth_map, GLuint opaque_depth_map)
{
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE, color_texture, 0
    );
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
    );

    glActiveTexture (GL_TEXTURE0);
    glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map);
    glUniform1i (glGetUniformLocation (program_id, "peel_depth_map"), 0);

    glActiveTexture (GL_TEXTURE1);
    glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, opaque_depth_map);
    glUniform1i (glGetUniformLocation (program_id, "opaque_depth_map"), 1);
}

fvec3 undefined_color = FVEC3 (1,1,0);
fvec3 selected_color = FVEC3(0.93,0.5,0.1);

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
    static GLuint opaque_color_texture;
    static GLuint depth_texture;
    static GLuint peel_depth_map;
    static GLuint opaque_depth_map;

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
        create_color_texture (&opaque_color_texture, width, height, 4);

        create_depth_texture (&peel_depth_map, width, height, 4);
        create_depth_texture (&opaque_depth_map, width, height, 4);
        create_depth_texture (&depth_texture, width, height, 4);

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
        dvec2 change = st->gui_st.ptr_delta;
        main_camera.pitch += 0.01 * change.y;
        main_camera.yaw -= 0.01 * change.x;
    }

    if (input.wheel != 1) {
        main_camera.distance -= (input.wheel - 1)*main_camera.distance*0.7;
    }

    main_camera.width_m = px_to_m_x (graphics, graphics->width);
    main_camera.height_m = px_to_m_y (graphics, graphics->height);

    closet_scene_set_camera (&closet_scene, &main_camera);

    glEnable (GL_DEPTH_TEST);
    glEnable (GL_SAMPLE_SHADING);
    glMinSampleShading (1.0);

    glBindFramebuffer (GL_FRAMEBUFFER, fb);
    glViewport (0, 0, graphics->width, graphics->height);
    glScissor (0, 0, graphics->width, graphics->height);

    // Initial texture contents
    //
    // color_texture -> (0,0,0,0)
    // depth_texture -> 1
    // opaque_color_texture -> (0,0,0,0)
    // opaque_depth_map -> 1
    // peel_depth_map -> 0

    // Init color_texture and depth_texture
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE, color_texture, 0
    );
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
    );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Init opaque_color_texture and opaque_depth_texture
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D_MULTISAMPLE, opaque_color_texture, 0
    );
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, opaque_depth_map, 0
    );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Init peel_depth_map
    glFramebufferTexture2D (
        GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
        GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map, 0
    );
    glClearDepth (0);
    glClear (GL_DEPTH_BUFFER_BIT);
    glClearDepth (1);

    // Opaque pass fragment shader slot content:
    //
    // COLOR BUFFER: opaque_color_texture
    // DEPTH BUFFER: opaque_depth_map
    // uniform peel_depth_map: peel_depth_map (0's)
    // uniform opaque_depth_map: depth_texture (1's)
    depth_peel_set_shader_slots (closet_scene.program_id,
                                 opaque_color_texture, opaque_depth_map,
                                 peel_depth_map, depth_texture);
    glDisable (GL_BLEND);
    render_closet_opaque (&closet_scene);

    int num_pass = 8;

    // Transparent passes fragment shader slot content:
    //
    // COLOR BUFFER: color_texture
    // DEPTH BUFFER: depth_texture
    // uniform peel_depth_map: peel_depth_map
    // uniform opaque_depth_map: opaque_depth_map
    depth_peel_set_shader_slots (closet_scene.program_id,
                                 color_texture, depth_texture,
                                 peel_depth_map, opaque_depth_map);

    glDisable (GL_BLEND);
    render_closet_transparent (&closet_scene, &cl);

    glEnable (GL_BLEND);
    int i;
    for (i = 0; i < num_pass-1; i++) {
        // Swap the depth buffer with peel_depth_map shader slot
        GLuint tmp = peel_depth_map;
        peel_depth_map = depth_texture;
        depth_texture = tmp;

        glFramebufferTexture2D (
            GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
            GL_TEXTURE_2D_MULTISAMPLE, depth_texture, 0
        );

        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, peel_depth_map);
        glUniform1i (glGetUniformLocation (closet_scene.program_id, "peel_depth_map"), 0);

        glClear(GL_DEPTH_BUFFER_BIT);

        // Render scene using UNDER blending operator
        glBlendFunc (GL_ONE_MINUS_SRC_ALPHA, GL_ONE);
        render_closet_transparent (&closet_scene, &cl);
    }

    // Blend resulting color buffers into the window using the OVER operator
    glBlendFunc (GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    draw_into_window (graphics);
    glClearColor(0.164f, 0.203f, 0.223f, 1.0f);
    glClear (GL_COLOR_BUFFER_BIT);
    set_texture_clip (&quad_renderer, graphics->screen_width, graphics->screen_height,
                      0, 0, graphics->width, graphics->height);
    blend_premul_quad (&quad_renderer, opaque_color_texture, true, graphics,
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
        dvec2 change = st->gui_st.ptr_delta;
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

