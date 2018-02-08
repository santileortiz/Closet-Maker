/*
 * Copyright (C) 2017 Santiago León O. <santileortiz@gmail.com>
 */

#if !defined(GUI_H)

#define RGBA VECT4
#define RGB(r,g,b) VECT4(r,g,b,1)
#define ARGS_RGBA(vect4) (vect4).r, (vect4).g, (vect4).b, (vect4).a
#define ARGS_RGB(vect4) (vect4.r), (vect4).g, (vect4).b
#define RGB_HEX(hex) VECT4(((double)(((hex)&0xFF0000) >> 16))/255, \
                           ((double)(((hex)&0x00FF00) >>  8))/255, \
                           ((double)((hex)&0x0000FF))/255, 1)

// TODO: Maybe in the future we want this to be defined for each different
// plattform we support. Maybe move _width_ and _height_ to app_input_t.
typedef struct {
    cairo_t *cr;
    uint16_t width;
    uint16_t height;
    float x_dpi;
    float y_dpi;
} app_graphics_t;

// TODO: Do per platform too.
typedef struct {
    uint64_t start_ticks;
    float time_elapsed_ms;

    bool force_redraw;

    xcb_keycode_t keycode;
    uint16_t modifiers;

    float wheel;
    char mouse_down[3];

    vect2_t ptr;
} app_input_t;

enum layout_content_type_t {
    LAYOUT_CONTENT_NONE,
    LAYOUT_CONTENT_C_STRING
};

typedef struct {
    enum layout_content_type_t type;
    union {
        char *str;
        // image_t *image // Not yet implemented
    };
    //vect2_t pos; // Is this useful? so far there are no usecases
    double width;
    double height;
} layout_content_t;

typedef enum {
    CSS_NONE,
    CSS_BUTTON,
    CSS_BUTTON_ACTIVE,
    CSS_BUTTON_DISABLED,
    CSS_BUTTON_SA,
    CSS_BUTTON_SA_ACTIVE,
    CSS_BACKGROUND,
    CSS_TEXT_ENTRY,
    CSS_TEXT_TEXT,
    CSS_TEXT_TEXT_SELECTED,
    CSS_TEXT_ENTRY_FOCUS,
    CSS_LABEL,
    CSS_TITLE_LABEL,

    CSS_NUM_STYLES
} css_style_t;

typedef enum {
    CSS_TEXT_ALIGN_INITIAL,
    CSS_TEXT_ALIGN_CENTER,
    CSS_TEXT_ALIGN_LEFT,
    CSS_TEXT_ALIGN_RIGHT
} css_text_align_t;

typedef enum {
    CSS_FONT_WEIGHT_NONE,
    CSS_FONT_WEIGHT_NORMAL,
    CSS_FONT_WEIGHT_BOLD
} css_font_weight_t;

// NOTE: If more than 32 properties, change this into a #define
typedef enum {
    CSS_PROP_BORDER_RADIUS      = 1<<0,
    CSS_PROP_BORDER_COLOR       = 1<<1,
    CSS_PROP_BORDER_WIDTH       = 1<<2,
    CSS_PROP_PADDING_X          = 1<<3,
    CSS_PROP_PADDING_Y          = 1<<4,
    CSS_PROP_MIN_WIDTH          = 1<<5,
    CSS_PROP_MIN_HEIGHT         = 1<<6,
    CSS_PROP_BACKGROUND_COLOR   = 1<<7,
    CSS_PROP_COLOR              = 1<<8,
    CSS_PROP_TEXT_ALIGN         = 1<<9,
    CSS_PROP_FONT_WEIGHT        = 1<<10,
    CSS_PROP_BACKGROUND_IMAGE   = 1<<11, //Only gradients supported for now
    CSS_PROP_BOX_SHADOW         = 1<<12,
    CSS_PROP_TEXT_SHADOW        = 1<<13
} css_property_t;

struct text_shadow_t {
    struct text_shadow_t *next;
    double h_offset;
    double v_offset;
    double blur_radius;
    vect4_t color;
};

// NOTE: The 'inset' field is handled by having  separate lists in css_box_t.
struct box_shadow_t {
    struct box_shadow_t *next;
    double h_offset;
    double v_offset;
    double blur_radius;
    double spread_distance;
    vect4_t color;
};

struct css_box_t {
    css_property_t property_mask;
    struct css_box_t *selector_active;
    struct css_box_t *selector_focus;
    struct css_box_t *selector_disabled;
    double border_radius;
    vect4_t border_color;
    double border_width;
    double padding_x;
    double padding_y;
    double min_width;
    double min_height;
    vect4_t background_color;
    vect4_t color;

    struct box_shadow_t *outset_shadows;
    struct box_shadow_t *inset_shadows;
    struct text_shadow_t *text_shadows;

    css_text_align_t text_align;
    char *font_family;
    int font_size;
    css_font_weight_t font_weight;

    int num_gradient_stops;
    vect4_t *gradient_stops;
};

typedef enum {
    CSS_SEL_DEFAULT  = 0,
    CSS_SEL_ACTIVE   = 1<<0,
    CSS_SEL_HOVER    = 1<<1,
    CSS_SEL_FOCUS    = 1<<2,
    CSS_SEL_DISABLED = 1<<3
} css_selector_t;

typedef enum {
    CSS_RESIZE_NONE,
    CSS_RESIZE_FIT_CONTENT
} css_resize_mode_t;

typedef struct layout_box_t layout_box_t;
#define DRAW_CALLBACK(name) void name(app_graphics_t *gr, layout_box_t *layout)
typedef DRAW_CALLBACK(draw_callback_t);
struct layout_box_t {
    box_t box;
    css_selector_t active_selectors;
    css_selector_t changed_selectors;
    css_text_align_t text_align_override;
    css_style_t base_style_id;
    struct css_box_t *style;
    draw_callback_t *draw;

    css_resize_mode_t resize_mode;
    bool content_changed;
    struct behavior_t *behavior;
    layout_content_t content;
};

typedef struct {
    uint64_t i;
    string_t str;
} uint64_string_t;

struct focus_element_t {
    layout_box_t *dest;
    struct focus_element_t *prev;
    struct focus_element_t *next;
};

// NOTE: dest == NULL means there is nothing selected.
struct selection_t {
    layout_box_t *dest;
    char *start;
    int32_t len; // -1 means select string untill the end.
    vect4_t color;
    vect4_t background_color;
};

enum behavior_type_t {
    BEHAVIOR_BUTTON,
    BEHAVIOR_TEXT_ENTRY,
};

struct behavior_t {
    enum behavior_type_t type;
    int state;
    union {
        void *ptr;
        bool *b;
        int *i32;
        float *r32;
        vect2_t *vect2;
        uint64_string_t *u64_str;
    } target;
    layout_box_t *box;
    struct behavior_t *next;
};

struct font_style_t {
    const char *family;
    int size;
    css_font_weight_t weight;
};
#define FONT_STYLE_FSW(family,size,weight) ((struct font_style_t){family,size,weight})
#define FONT_STYLE_CSS(css_box) \
    FONT_STYLE_FSW((css_box)->font_family,(css_box)->font_size,(css_box)->font_weight)

#define NUM_LAYOUT_BOXES_ALLOCATED 30

struct gui_state_t {
    mem_pool_t pool;

    app_input_t input;
    app_graphics_t gr;
    struct font_style_t default_font_style;

    // TODO: convert this into a work queue.
    pthread_t thread;
    mem_pool_t thread_pool;
    mem_pool_temp_marker_t thread_mem_flush;

    char dragging[3];
    vect2_t ptr_delta;
    vect2_t click_coord[3];
    bool mouse_clicked[3];
    bool mouse_double_clicked[3];
    float time_since_last_click[3]; // in ms
    float double_click_time; // in ms
    float min_distance_for_drag; // in pixels

    int focused_layout_box;
    int num_layout_boxes;
    struct layout_box_t *layout_boxes;

    struct selection_t selection;
    struct behavior_t *behaviors;

    struct focus_element_t *focus;
    struct focus_element_t *focus_end;
    struct focus_element_t *freed_focus_elements;

    bool clipboard_ready;
    char *clipboard_str;

    struct css_box_t css_styles[CSS_NUM_STYLES];
};

struct gui_state_t *global_gui_st;
static inline
vect4_t shade (vect4_t *in, double f);
static inline
vect4_t mix (vect4_t *c1, vect4_t *c2, double f);
static inline
vect4_t alpha (vect4_t c, double f);

// Forward declaration of CSS "stylesheet"

vect4_t BLACK_500 = RGB_HEX(0x333333);
vect4_t SILVER_100 = RGB_HEX(0xfafafa);
vect4_t SILVER_300 = RGB_HEX(0xd4d4d4);

vect4_t text_color;
vect4_t text_color_primary;
vect4_t text_shadow_color = RGBA (1, 1, 1, 0.4);
vect4_t text_color_primary_shadw;
vect4_t titlebar_color;
vect4_t base_color = RGB(1,1,1);
vect4_t bg_color;
vect4_t selected_bg_color = RGB(0.239216, 0.607843, 0.854902);
vect4_t selected_fg_color = RGB(1,1,1);
vect4_t insensitive_color;
vect4_t bg_highlight_color = RGB(1,1,1);
vect4_t border_color = RGBA(0,0,0,0.25);
vect4_t inset_dark_color = RGBA(0,0,0,0.06);
vect4_t color_accent = RGB_HEX(0x3d9bda);

void init_button (mem_pool_t *pool, struct css_box_t *box);
void init_button_active (mem_pool_t *pool, struct css_box_t *box);
void init_button_disabled (mem_pool_t *pool, struct css_box_t *box);
void init_suggested_action_button (mem_pool_t *pool, struct css_box_t *box);
void init_suggested_action_button_active (mem_pool_t *pool, struct css_box_t *box);
void init_background (mem_pool_t *pool, struct css_box_t *box);
void init_text_entry (mem_pool_t *pool, struct css_box_t *box);
void init_text_entry_focused (mem_pool_t *pool, struct css_box_t *box);
void init_label (mem_pool_t *pool, struct css_box_t *box);
void init_title_label (mem_pool_t *pool, struct css_box_t *box);

void default_gui_init (struct gui_state_t *gui_st)
{
    gui_st->layout_boxes =
        (layout_box_t*)mem_pool_push_array (&gui_st->pool, NUM_LAYOUT_BOXES_ALLOCATED, layout_box_t);
    gui_st->focused_layout_box = -1;

    gui_st->default_font_style.family = "Open Sans";
    gui_st->default_font_style.size = 9;
    gui_st->default_font_style.weight = CSS_FONT_WEIGHT_NORMAL;

    text_color = BLACK_500;
    bg_color = shade (&base_color, 0.96);
    insensitive_color = mix (&text_color, &bg_color, 0.31);
    titlebar_color = mix (&SILVER_300, &SILVER_100, 0.25);
    text_color_primary = shade (&titlebar_color, 0.5);
    text_color_primary_shadw = alpha (shade (&titlebar_color, 1.4), 0.4);

    gui_st->double_click_time = 200;
    gui_st->min_distance_for_drag = 3;
    gui_st->time_since_last_click[0] = gui_st->double_click_time;
    gui_st->time_since_last_click[1] = gui_st->double_click_time;
    gui_st->time_since_last_click[2] = gui_st->double_click_time;

    init_button (&gui_st->pool, &gui_st->css_styles[CSS_BUTTON]);
    init_button_active (&gui_st->pool, &gui_st->css_styles[CSS_BUTTON_ACTIVE]);
    gui_st->css_styles[CSS_BUTTON].selector_active = &gui_st->css_styles[CSS_BUTTON_ACTIVE];
    init_button_disabled (&gui_st->pool, &gui_st->css_styles[CSS_BUTTON_DISABLED]);
    gui_st->css_styles[CSS_BUTTON].selector_disabled = &gui_st->css_styles[CSS_BUTTON_DISABLED];

    init_suggested_action_button (&gui_st->pool, &gui_st->css_styles[CSS_BUTTON_SA]);
    init_suggested_action_button_active (&gui_st->pool, &gui_st->css_styles[CSS_BUTTON_SA_ACTIVE]);
    gui_st->css_styles[CSS_BUTTON_SA].selector_active = &gui_st->css_styles[CSS_BUTTON_SA_ACTIVE];

    init_background (&gui_st->pool, &gui_st->css_styles[CSS_BACKGROUND]);
    init_text_entry (&gui_st->pool, &gui_st->css_styles[CSS_TEXT_ENTRY]);
    init_text_entry_focused (&gui_st->pool, &gui_st->css_styles[CSS_TEXT_ENTRY_FOCUS]);
    gui_st->css_styles[CSS_TEXT_ENTRY].selector_focus = &gui_st->css_styles[CSS_TEXT_ENTRY_FOCUS];
    init_label (&gui_st->pool, &gui_st->css_styles[CSS_LABEL]);
    init_title_label (&gui_st->pool, &gui_st->css_styles[CSS_TITLE_LABEL]);

    gui_st->selection.color = selected_fg_color;
    gui_st->selection.background_color = selected_bg_color;
}

void gui_destroy (struct gui_state_t *gui_st)
{
    mem_pool_destroy (&gui_st->pool);
    mem_pool_destroy (&gui_st->thread_pool);
}

void cairo_clear (cairo_t *cr)
{
    cairo_save (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_restore (cr);
}

int _g_gui_num_colors = 0;
vect4_t _g_gui_colors[50];

#define int_string_inc(int_str) int_string_update(int_str,(int_str)->i+1)
#define int_string_dec(int_str) int_string_update(int_str,(int_str)->i-1)
void int_string_update (uint64_string_t *x, uint64_t i)
{
    x->i = i;

    char buff[20];
    snprintf (buff, ARRAY_SIZE(buff), "%ld", i);
    str_set (&x->str, buff);
}

void int_string_clear (uint64_string_t *x)
{
    x->i = 0;
    str_set (&x->str, "");
}

void int_string_append_digit (uint64_string_t *x, int digit)
{
    x->i = x->i*10 + digit;

    char digit_c_str[] = {(char)(0x30+digit), '\0'};
    string_t str_digit = str_new (digit_c_str);
    str_cat (&x->str, &str_digit);
    str_free (&str_digit);
}

void int_string_delete_digit (uint64_string_t *x)
{
    if (x->i != 0 && x->i/10 == 0) {
        int_string_clear (x);
    } else {
        x->i = x->i/10;

        char buff[20];
        snprintf (buff, ARRAY_SIZE(buff), "%ld", x->i);
        str_set (&x->str, buff);
    }
}

void int_string_update_s (uint64_string_t *x, char* str)
{
    x->i = strtoull (str, NULL, 10);
    str_set (&x->str, str);
}

// Calculates a ratio by which multiply box a so that it fits inside box b
double best_fit_ratio (double a_width, double a_height,
                       double b_width, double b_height)
{
    if (a_width/a_height < b_width/b_height) {
        return b_height/a_height;
    } else {
        return b_width/a_width;
    }
}

void pixel_align_as_line (vect2_t *p, int line_width)
{
    p->x = floor (p->x)+(double)(line_width%2)/2;
    p->y = floor (p->y)+(double)(line_width%2)/2;
}

struct rounded_box_t {
    double x;
    double y;
    double width;
    double height;
    // TODO: This should turn into an array of 8 doubles.
    double radius;
};

void rounded_box_path (cairo_t *cr, struct rounded_box_t *rb)
{
    double x = rb->x;
    double y = rb->y;
    double r = rb->radius;
    double w = rb->width;
    double h = rb->height;
    cairo_move_to (cr, x, y+r);
    cairo_arc (cr, x+r, y+r, r, M_PI, 3*M_PI/2);
    cairo_arc (cr, x+w - r, y+r, r, 3*M_PI/2, 0);
    cairo_arc (cr, x+w - r, y+h - r, r, 0, M_PI/2);
    cairo_arc (cr, x+r, y+h - r, r, M_PI/2, M_PI);
    cairo_close_path (cr);
}

void rounded_box_path_negative (cairo_t *cr, struct rounded_box_t *rb)
{
    double x = rb->x;
    double y = rb->y;
    double r = rb->radius;
    double w = rb->width;
    double h = rb->height;
    cairo_move_to (cr, x+r, y);
    cairo_arc_negative (cr, x+r, y+r, r, 3*M_PI/2, M_PI);
    cairo_arc_negative (cr, x+r, y+h - r, r, M_PI, M_PI/2);
    cairo_arc_negative (cr, x+w - r, y+h - r, r, M_PI/2, 0);
    cairo_arc_negative (cr, x+w - r, y+r, r, 0, 3*M_PI/2);
    cairo_close_path (cr);
}

static inline
struct rounded_box_t css_get_border_box (struct css_box_t *css, layout_box_t *layout)
{
    struct rounded_box_t res;
    res.x = 0;
    res.y = 0;
    res.width = BOX_WIDTH (layout->box);
    res.height = BOX_HEIGHT (layout->box);
    res.radius = css->border_radius;
    return res;
}

static inline
struct rounded_box_t css_get_padding_box (struct css_box_t *css, layout_box_t *layout)
{
    struct rounded_box_t res;
    res.x = css->border_width;
    res.y = css->border_width;
    res.width = BOX_WIDTH (layout->box) - 2*css->border_width;
    res.height = BOX_HEIGHT (layout->box) - 2*css->border_width;
    res.radius = LOW_CLAMP(css->border_radius - css->border_width, 0);
    return res;
}

#define is_vect2_in_box(v2,box) is_point_in_box((v2).x,(v2).y,(box).min.x,\
                                                (box).min.y,BOX_WIDTH(box),BOX_HEIGHT(box))
bool is_point_in_box (double p_x, double p_y, double x, double y, double width, double height)
{
    if (p_x < x || p_x > x+width) {
        return false;
    } else if (p_y < y || p_y > y+height) {
        return false;
    } else {
        return true;
    }
}

bool is_box_visible (box_t *box, app_graphics_t *gr)
{
    return
        is_point_in_box (box->min.x, box->min.y, 0, 0, gr->width, gr->height) ||
        is_point_in_box (box->max.x, box->max.y, 0, 0, gr->width, gr->height) ||
        is_point_in_box (box->min.x, box->max.y, 0, 0, gr->width, gr->height) ||
        is_point_in_box (box->max.x, box->min.y, 0, 0, gr->width, gr->height);
}

void update_input (struct gui_state_t *gui_st, app_input_t input)
{
    // FSM that detects clicks and double clicks
    assert (input.time_elapsed_ms > 0);
    gui_st->time_since_last_click[0] += input.time_elapsed_ms;

    static int state = 0;
    switch (state) {
        case 0:
            gui_st->mouse_clicked[0] = false;
            gui_st->mouse_double_clicked[0] = false;
            if (input.mouse_down[0]) {
                gui_st->click_coord[0] = input.ptr;
                state = 1;
            }
            break;
        case 1:
            if (!input.mouse_down[0]) {
                gui_st->mouse_clicked[0] = true;
                gui_st->time_since_last_click[0] = 0;
                state = 2;
            }
            break;
        case 2:
            gui_st->mouse_clicked[0] = false;
            gui_st->mouse_double_clicked[0] = false;
            if (input.mouse_down[0]) {
                if (gui_st->time_since_last_click[0] < gui_st->double_click_time) {
                    gui_st->mouse_double_clicked[0] = true;
                }
                gui_st->click_coord[0] = input.ptr;
                state = 3;
            } else {
                if (gui_st->time_since_last_click[0] >= gui_st->double_click_time) {
                    state = 0;
                }
            }
            break;
        case 3:
            gui_st->mouse_double_clicked[0] = false;
            if (!input.mouse_down[0]) {
                gui_st->mouse_clicked[0] = true;
                gui_st->time_since_last_click[0] = 0;
                state = 0;
            }
            break;
        default:
            invalid_code_path;
    }

    // Detect dragging with minimum distance threshold
    if (input.mouse_down[0]) {
        if (vect2_distance (&input.ptr, &gui_st->click_coord[0]) > gui_st->min_distance_for_drag) {
            gui_st->dragging[0] = true;
        }
    } else {
        gui_st->dragging[0] = false;
    }

    app_input_t prev_input = gui_st->input;
    gui_st->ptr_delta.x = input.ptr.x - prev_input.ptr.x;
    gui_st->ptr_delta.y = input.ptr.y - prev_input.ptr.y;
    gui_st->input = input;
}

void layout_box_print (layout_box_t *lay_box)
{
    printf ("---[0x%lX]---\n"
            "Base Style: 0x%lX\n"
            "Style: 0x%lX\n"
            "Selectors Changed: 0x%X Active: 0x%X\n",
            (uint64_t)lay_box,
            (uint64_t)lay_box->style,
            (uint64_t)&global_gui_st->css_styles[lay_box->base_style_id],
            lay_box->changed_selectors, lay_box->active_selectors);
}

void unselect (struct gui_state_t *gui_st)
{
    gui_st->selection.dest = NULL;
    gui_st->selection.start = NULL;
    gui_st->selection.len = 0;
}

#define select_all_str(gui_st,lay_box,str) select_str(gui_st,lay_box,str,-1)
void select_str (struct gui_state_t *gui_st, layout_box_t *lay_box, char* str, int len)
{
    gui_st->selection.dest = lay_box;
    gui_st->selection.start = str;
    gui_st->selection.len = len;
}

#define SEL_RISING_EDGE(lay_box,sel) \
    (((lay_box)->changed_selectors&sel)&&((lay_box)->active_selectors&sel))

#define SEL_FALLING_EDGE(lay_box,sel) \
    (((lay_box)->changed_selectors&sel)&&!((lay_box)->active_selectors&sel))
void selector_set (struct layout_box_t *lay_box, css_selector_t sel)
{
    if (!(lay_box->active_selectors & sel)) {
        lay_box->active_selectors =  (css_selector_t)(lay_box->active_selectors | sel);
        lay_box->changed_selectors = (css_selector_t)(lay_box->changed_selectors | sel);
    }
}

void selector_unset (struct layout_box_t *lay_box, css_selector_t sel)
{
    if (lay_box->active_selectors & sel) {
        lay_box->active_selectors = (css_selector_t)(lay_box->active_selectors & ~sel);
        lay_box->changed_selectors = (css_selector_t)(lay_box->active_selectors | sel);
    }
}

void focus_chain_add (struct gui_state_t *gui_st, layout_box_t *lay_box)
{
    struct focus_element_t *new_focus;
    if (gui_st->freed_focus_elements != NULL) {
        new_focus = gui_st->freed_focus_elements;
        gui_st->freed_focus_elements = new_focus->prev;
    } else {
        new_focus =
            (struct focus_element_t*)mem_pool_push_struct (&gui_st->pool, struct focus_element_t);
    }

    new_focus->dest = lay_box;

    if (gui_st->focus == NULL) {
        new_focus->next = new_focus;
        new_focus->prev = new_focus;
        gui_st->focus = new_focus;

        // NOTE: We don't call selector_set() because we don't want to trigger
        // things as if the user had just focused the first element in the
        // chain, (we did).
        lay_box->active_selectors = (css_selector_t)(lay_box->active_selectors|CSS_SEL_FOCUS);

        struct css_box_t *focus_style =
            gui_st->css_styles[lay_box->base_style_id].selector_focus;
        if (focus_style != NULL) {
            lay_box->style = focus_style;
        }
    } else {
        new_focus->next = gui_st->focus_end->next;
        gui_st->focus_end->next = new_focus;

        new_focus->prev = new_focus->next->prev;
        new_focus->next->prev = new_focus;
    }
    gui_st->focus_end = new_focus;
}

void focus_disable (struct gui_state_t *gui_st)
{
    gui_st->focus = gui_st->focus->prev;
    selector_unset (gui_st->focus->dest, CSS_SEL_FOCUS);
}

void focus_set (struct gui_state_t *gui_st, layout_box_t *lay_box)
{
    assert (gui_st->focus != NULL && gui_st->focus_end != NULL && "No focus chain");
    assert (lay_box != NULL && "lay_box can't be NULL");

    if (lay_box != gui_st->focus->dest) {
        layout_box_t *old_dest = gui_st->focus->dest;

        struct focus_element_t *iter = gui_st->focus_end;
        do {
            if (iter->dest == lay_box) {
                gui_st->focus = iter;
                break;
            }
            iter = iter->prev;
        } while (iter != gui_st->focus_end);

        if (gui_st->focus->dest == lay_box) {
            selector_unset (old_dest, CSS_SEL_FOCUS);
            selector_set (gui_st->focus->dest, CSS_SEL_FOCUS);
        } else {
            // lay_box is not in the focus chain.
            // Do nothing.
        }
    }
}

void focus_next (struct gui_state_t *gui_st)
{
    selector_unset (gui_st->focus->dest, CSS_SEL_FOCUS);
    gui_st->focus = gui_st->focus->next;
    selector_set (gui_st->focus->dest, CSS_SEL_FOCUS);
}

void focus_prev (struct gui_state_t *gui_st)
{
    selector_unset (gui_st->focus->dest, CSS_SEL_FOCUS);
    gui_st->focus = gui_st->focus->prev;
    selector_set (gui_st->focus->dest, CSS_SEL_FOCUS);
}

void focus_chain_remove (struct gui_state_t *gui_st, layout_box_t *lay_box)
{
    assert (gui_st->focus != NULL && gui_st->focus_end != NULL && "No focus chain");
    assert (lay_box != NULL && "lay_box can't be NULL");

    struct focus_element_t *found_focus = NULL;
    if (lay_box == gui_st->focus->dest) {
        found_focus = gui_st->focus;
        focus_next (gui_st);

    } else if (lay_box == gui_st->focus_end->dest) {
        found_focus = gui_st->focus_end;
        gui_st->focus_end = gui_st->focus_end->prev;

    } else {
        struct focus_element_t *iter = gui_st->focus_end;
        do {
            if (iter->dest == lay_box) {
                break;
            }
            iter = iter->prev;
        } while (iter != gui_st->focus_end);

        if (iter->dest == lay_box) {
            found_focus = iter;
        }
    }

    if (found_focus) {
        found_focus->prev->next = found_focus->next;
        found_focus->next->prev = found_focus->prev;

        // The strategy to not leak focus elements is to store allocated
        // focus_element_t objects in a SINGLY linked list (prev pointer) whose
        // head can be found in gui_st->freed_focus_elements.
        if (gui_st->freed_focus_elements) {
            found_focus->prev = gui_st->freed_focus_elements;
        } else {
            found_focus->prev = NULL;
        }
        gui_st->freed_focus_elements = found_focus;
    }
}

void update_selectors (struct gui_state_t *gui_st, layout_box_t *layout_boxes, int num_layout_boxes)
{
    int i;
    for (i=0; i<num_layout_boxes; i++) {
        layout_box_t *curr_box = layout_boxes + i;
        // TODO: is_ptr_inside should consider z-axis so that only visible boxes are
        // triggered.
        bool is_ptr_inside = is_vect2_in_box (global_gui_st->input.ptr, curr_box->box);
        bool click_started_inside = is_vect2_in_box (global_gui_st->click_coord[0], curr_box->box);

        css_selector_t old = curr_box->active_selectors;
        if (!(curr_box->active_selectors & CSS_SEL_DISABLED)) {
            if (gui_st->input.mouse_down[0] && is_ptr_inside && click_started_inside) {
                curr_box->active_selectors = (css_selector_t)(curr_box->active_selectors | CSS_SEL_ACTIVE);
            } else {
                curr_box->active_selectors = (css_selector_t)(curr_box->active_selectors & ~CSS_SEL_ACTIVE);
            }
        }

        if (is_ptr_inside) {
            curr_box->active_selectors = (css_selector_t)(curr_box->active_selectors | CSS_SEL_HOVER);
        } else {
            curr_box->active_selectors = (css_selector_t)(curr_box->active_selectors & ~CSS_SEL_HOVER);
        }

        if (gui_st->mouse_clicked[0] && is_ptr_inside) {
            focus_set (gui_st, curr_box);
        }

        curr_box->changed_selectors =
            (css_selector_t)(curr_box->changed_selectors | (old ^ curr_box->active_selectors));
    }
}

#define update_layout_box_style(gui_st,box,style_id) {(box)->style=&((gui_st)->css_styles[style_id]);}
void init_layout_box_style (struct gui_state_t *gui_st, layout_box_t *box, css_style_t style_id)
{
    if (style_id != CSS_NONE) {
        box->base_style_id = style_id;
        update_layout_box_style (gui_st, box, style_id);
    }
}

void layout_box_uninitialize (layout_box_t *lay)
{
    *lay = (layout_box_t){0};
    lay->box.min = VECT2 (NAN, NAN);
    lay->box.max = VECT2 (NAN, NAN);
}

#define is_box_initialized(lay_box) \
    ((layout)->box.min.x != NAN && (layout)->box.min.y != NAN && \
     (layout)->box.max.x != NAN && (layout)->box.max.y != NAN)

layout_box_t* next_layout_box (css_style_t style_id)
{
    assert (global_gui_st->num_layout_boxes+1 < NUM_LAYOUT_BOXES_ALLOCATED);

    struct gui_state_t *gui_st = global_gui_st;
    layout_box_t *layout_box = &gui_st->layout_boxes[gui_st->num_layout_boxes];

    layout_box_uninitialize(layout_box);

    gui_st->num_layout_boxes++;

    init_layout_box_style (gui_st, layout_box, style_id);

    return layout_box;
}

void update_layout_boxes (struct gui_state_t *gui_st, layout_box_t *layout_boxes,
                          int num_layout_boxes, bool *changed)
{
    update_selectors (gui_st, layout_boxes, num_layout_boxes);

    int i;
    for (i=0; i<num_layout_boxes; i++) {
        layout_box_t *curr_box = layout_boxes + i;

        struct css_box_t *active_style =
            gui_st->css_styles[curr_box->base_style_id].selector_active;
        if (curr_box->changed_selectors & CSS_SEL_ACTIVE && active_style != NULL) {
            if (curr_box->active_selectors & CSS_SEL_ACTIVE) {
                curr_box->style = active_style;
            } else {
                curr_box->style = &gui_st->css_styles[curr_box->base_style_id];
            }
            *changed = true;
        }

        struct css_box_t *focus_style =
            gui_st->css_styles[curr_box->base_style_id].selector_focus;
        if (curr_box->changed_selectors & CSS_SEL_FOCUS && focus_style != NULL) {
            if (curr_box->active_selectors & CSS_SEL_FOCUS) {
                curr_box->style = focus_style;
            } else {
                curr_box->style = &gui_st->css_styles[curr_box->base_style_id];
            }
            *changed = true;
        }

        struct css_box_t *disabled_style =
            gui_st->css_styles[curr_box->base_style_id].selector_disabled;
        if (curr_box->changed_selectors & CSS_SEL_DISABLED && active_style != NULL) {
            if (curr_box->active_selectors & CSS_SEL_DISABLED) {
                curr_box->style = disabled_style;
            } else {
                curr_box->style = &gui_st->css_styles[curr_box->base_style_id];
            }
            *changed = true;
        }
    }
}

void layout_boxes_end_frame (layout_box_t *layout_boxes, int len)
{
    int i;
    for (i=0; i<len; i++) {
        layout_boxes[i].changed_selectors = (css_selector_t)0;
        layout_boxes[i].content_changed = false;

#if 0
        box_t *rect = &layout_boxes[i].box;
        cairo_t *cr = global_gui_st->gr.cr;
        cairo_rectangle (cr, rect->min.x+0.5, rect->min.y+0.5, BOX_WIDTH(*rect)-1, BOX_HEIGHT(*rect)-1);
        cairo_set_source_rgba (cr, 0.5, 0.1, 0.1, 0.3);
        cairo_fill (cr);
#endif
    }
}

void add_behavior (struct gui_state_t *gui_st, layout_box_t *box, enum behavior_type_t type, void *target)
{
    struct behavior_t *new_behavior =
        (struct behavior_t*)mem_pool_push_size_full (&gui_st->pool,
                                                     sizeof(struct behavior_t),
                                                     POOL_ZERO_INIT);
    new_behavior->next = gui_st->behaviors;
    gui_st->behaviors = new_behavior;
    new_behavior->type = type;
    new_behavior->target.ptr = target;
    new_behavior->box = box;
    box->behavior = new_behavior;
}

#define new_pango_layout_from_style_fsw(cr,family,size,weight) \
    new_pango_layout_from_style(cr, FONT_STYLE_FSW(family,size,weight))
PangoLayout* new_pango_layout_from_style (cairo_t *cr, struct font_style_t font_style)
{
    const char *font_family;
    if (font_style.family == NULL) {
        font_family = global_gui_st->default_font_style.family;
    } else {
        font_family = font_style.family;
    }

    int font_size;
    if (font_style.size == 0) {
        font_size = global_gui_st->default_font_style.size;
    } else {
        font_size = font_style.size;
    }

    css_font_weight_t css_font_weight;
    if (font_style.weight == CSS_FONT_WEIGHT_NONE) {
        css_font_weight = global_gui_st->default_font_style.weight;
    } else {
        css_font_weight = font_style.weight;
    }

    PangoWeight font_weight = PANGO_WEIGHT_NORMAL;
    switch (css_font_weight) {
        case CSS_FONT_WEIGHT_BOLD:
            font_weight = PANGO_WEIGHT_BOLD;
            break;
        case CSS_FONT_WEIGHT_NORMAL:
            font_weight = PANGO_WEIGHT_NORMAL;
            break;
        default:
            invalid_code_path;
            break;
    }

    PangoFontDescription *font_desc = pango_font_description_new ();
    pango_font_description_set_family (font_desc, font_family);
    pango_font_description_set_size (font_desc, font_size*PANGO_SCALE);
    pango_font_description_set_weight (font_desc, font_weight);

    PangoLayout *text_layout = pango_cairo_create_layout (cr);
    pango_layout_set_font_description (text_layout, font_desc);
    pango_font_description_free(font_desc);
    return text_layout;
}

void layout_set_content_str (layout_box_t *lay, char *str)
{
    lay->content.type = LAYOUT_CONTENT_C_STRING;
    lay->content.str = str;
}

vect2_t compute_string_size (char *str, struct css_box_t *style)
{
    vect2_t res;
    PangoLayout *text_layout =
        new_pango_layout_from_style_fsw (global_gui_st->gr.cr,
                                         style->font_family, style->font_size, style->font_weight);

    // NOTE: I have the hunch that calls into Pango will be slow, so we ideally
    // would like to call this function just once for each string that needs to
    // be computed.
    // TODO: Time this function and check if it's really a problem.
    pango_layout_set_text (text_layout, str, -1);

    PangoRectangle logical;
    pango_layout_get_pixel_extents (text_layout, NULL, &logical);

    res.x = logical.width;
    res.y = logical.height;
    g_object_unref (text_layout);
    return res;
}

void compute_content_size (layout_box_t *lay)
{
    switch (lay->content.type) {
        case LAYOUT_CONTENT_C_STRING:
            {
                vect2_t size = compute_string_size (lay->content.str, lay->style);
                lay->content.width = size.x;
                lay->content.height = size.y;
            } break;
        case LAYOUT_CONTENT_NONE:
            break;
        default:
            invalid_code_path;
    }
}

// NOTE: len == -1 means the string is null terminated.
void render_text (cairo_t *cr, vect2_t pos, struct font_style_t *font_style,
                  char *str, size_t len, vect4_t *color, vect4_t *bg_color,
                  vect2_t *out_pos)
{
    PangoLayout *text_layout = new_pango_layout_from_style (cr, *font_style);
    pango_layout_set_text (text_layout, str, len);

    PangoRectangle logical;
    pango_layout_get_pixel_extents (text_layout, NULL, &logical);
    vect2_floor (&pos);
    if (bg_color != NULL) {
        cairo_set_source_rgba (cr, ARGS_RGBA(*bg_color));
        cairo_rectangle (cr, pos.x, pos.y, logical.width, logical.height);
        cairo_fill (cr);
    }

    if (out_pos != NULL ) {
        out_pos->x = pos.x + logical.width;
    }

    cairo_set_source_rgba (cr, ARGS_RGBA(*color));
    cairo_move_to (cr, pos.x, pos.y);
    pango_cairo_show_layout (cr, text_layout);
    g_object_unref (text_layout);
}

#define css_cont_size_to_lay_size(css_box,cont_size) \
    css_cont_size_to_lay_size_w_h(css_box, &((cont_size)->x), &((cont_size)->y))
void css_cont_size_to_lay_size_w_h (struct css_box_t *css_box,
                                      double *w, double *h)
{
    if (w != NULL) {
        *w = MAX(css_box->min_width, *w) +
             2*(css_box->padding_x + css_box->border_width);
    }

    if (h != NULL) {
        *h = MAX(css_box->min_height, *h) +
             2*(css_box->padding_y + css_box->border_width);
    }
}

void layout_size_from_css_content_size (struct css_box_t *css_box,
                                        vect2_t css_content_size, vect2_t *layout_size)
{
    *layout_size = css_content_size;
    css_cont_size_to_lay_size (css_box, layout_size);
}

static inline
uint64_t rgb_to_uint (vect4_t rgb)
{
    return ((uint32_t)(rgb.r*255) << 16) +
           ((uint32_t)(rgb.g*255) <<  8) +
           (uint32_t)(rgb.b*255);
}

void print_inset_box_shadow (struct box_shadow_t *shadow)
{
    printf ("inset %.1fpx %.1fpx %.1fpx %.1fpx rgba(%d,%d,%d,%.2f);\n",
            shadow->h_offset,
            shadow->v_offset,
            shadow->blur_radius,
            shadow->spread_distance,
            (uint8_t)(shadow->color.r*255),
            (uint8_t)(shadow->color.g*255),
            (uint8_t)(shadow->color.b*255),
            shadow->color.a);
}

void print_outset_box_shadow (struct box_shadow_t *shadow)
{
    printf ("%.1fpx %.1fpx %.1fpx %.1fpx rgba(%d,%d,%d,%.2f);\n",
            shadow->h_offset,
            shadow->v_offset,
            shadow->blur_radius,
            shadow->spread_distance,
            (uint8_t)(shadow->color.r*255),
            (uint8_t)(shadow->color.g*255),
            (uint8_t)(shadow->color.b*255),
            shadow->color.a);
}

void print_text_shadow (struct text_shadow_t *shadow)
{
    printf ("%.1fpx %.1fpx %.1fpx rgba(%d,%d,%d,%.2f);\n",
            shadow->h_offset,
            shadow->v_offset,
            shadow->blur_radius,
            (uint8_t)(shadow->color.r*255),
            (uint8_t)(shadow->color.g*255),
            (uint8_t)(shadow->color.b*255),
            shadow->color.a);
}

// TODO: How are we impacted by the slow performance of this?
// An easy way to speed up this would be to receive a box where we can skip the
// computation.
void css_gaussian_blur (cairo_surface_t *image, double r)
{
    assert (cairo_surface_get_type (image) == CAIRO_SURFACE_TYPE_IMAGE);
    assert (cairo_image_surface_get_format (image) == CAIRO_FORMAT_ARGB32);

    if (r == 0) {
        return;
    }

    cairo_surface_flush (image);
    int size = 2*r+1;
    uint8_t kernel[size];
    uint32_t i;
    uint32_t area = 0;
    double mu = ARRAY_SIZE(kernel)/2;
    for (i=0; i<ARRAY_SIZE(kernel); i++) {
        kernel[i] = (uint32_t)(255*exp(-(i-mu)*(i-mu)/(2*(r/2)*(r/2))));
        area += kernel[i];
    }

    uint32_t *src = (uint32_t*)cairo_image_surface_get_data (image);
    uint32_t src_width = cairo_image_surface_get_width (image);
    uint32_t src_height = cairo_image_surface_get_height (image);
    cairo_surface_t *tmp_surface =
        cairo_image_surface_create (CAIRO_FORMAT_ARGB32, src_width, src_height);
    uint32_t *tmp = (uint32_t*)cairo_image_surface_get_data (tmp_surface);

    for (i=0; i<src_height; i++) {
        uint32_t j;
        for (j=0; j<src_width; j++) {
            uint32_t k;
            uint32_t a = 0, r = 0, g = 0, b = 0;
            for (k=0; k<ARRAY_SIZE(kernel); k++) {
                if (j - ARRAY_SIZE(kernel)/2 + k < 0) {
                    continue;
                } else if (j - ARRAY_SIZE(kernel)/2 + k >= src_width) {
                    continue;
                }

                uint8_t *src_px = (uint8_t*)(src + j - ARRAY_SIZE(kernel)/2 + k + i*src_width);
                a += src_px[0]*kernel[k];
                r += src_px[1]*kernel[k];
                g += src_px[2]*kernel[k];
                b += src_px[3]*kernel[k];
            }
            uint8_t *dest_px = (uint8_t*)(tmp + j + i*src_width);
            dest_px[0] = a/area;
            dest_px[1] = r/area;
            dest_px[2] = g/area;
            dest_px[3] = b/area;

        }
    }

    uint32_t j;
    for (j=0; j<src_width; j++) {
        uint32_t i;
        for (i=0; i<src_height; i++) {
            uint32_t k;
            uint32_t a = 0, r = 0, g = 0, b = 0;
            for (k=0; k<ARRAY_SIZE(kernel); k++) {
                if (i - ARRAY_SIZE(kernel)/2 + k < 0) {
                    continue;
                } else if (i - ARRAY_SIZE(kernel)/2 + k >= src_height) {
                    continue;
                }

                uint8_t *src_px = (uint8_t*)(tmp + j + (i - ARRAY_SIZE(kernel)/2 + k)*src_width);
                a += src_px[0]*kernel[k];
                r += src_px[1]*kernel[k];
                g += src_px[2]*kernel[k];
                b += src_px[3]*kernel[k];
            }
            uint8_t *dest_px = (uint8_t*)(src + j + i*src_width);
            dest_px[0] = a/area;
            dest_px[1] = r/area;
            dest_px[2] = g/area;
            dest_px[3] = b/area;
        }
    }

    cairo_surface_destroy (tmp_surface);
    cairo_surface_mark_dirty (image);
}

void draw_outset_shadows (app_graphics_t *gr, struct css_box_t *css, layout_box_t *layout,
                          struct rounded_box_t *border_box)
{
    if (css->outset_shadows == NULL) {
        return;
    }

    cairo_t *cr = gr->cr;
    struct box_shadow_t *curr_shadow = css->outset_shadows;

    // Draw shadows into a pattern
    cairo_push_group (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint (cr);
    cairo_set_operator (cr, CAIRO_OPERATOR_OVER);

    while (curr_shadow != NULL) {
        struct rounded_box_t shadow_box = *border_box;
        shadow_box.width = LOW_CLAMP (shadow_box.width + 2*curr_shadow->spread_distance, 0);
        shadow_box.height = LOW_CLAMP (shadow_box.height + 2*curr_shadow->spread_distance, 0);
        shadow_box.radius = LOW_CLAMP(css->border_radius + curr_shadow->spread_distance,0);

        // TODO: How different is the performance of these paths?
        if (curr_shadow->blur_radius == 0) {
            shadow_box.x = curr_shadow->h_offset - curr_shadow->spread_distance;
            shadow_box.y = curr_shadow->v_offset - curr_shadow->spread_distance;
            rounded_box_path (cr, &shadow_box);
            cairo_set_source_rgba (cr, ARGS_RGBA(curr_shadow->color));
            cairo_fill (cr);
        } else {
            shadow_box.x = curr_shadow->blur_radius;
            shadow_box.y = curr_shadow->blur_radius;

            cairo_surface_t *single_shadow =
                cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            shadow_box.width + 2*curr_shadow->blur_radius,
                                            shadow_box.height + 2*curr_shadow->blur_radius);
            cairo_t *shadow_cr = cairo_create (single_shadow);
            rounded_box_path (shadow_cr, &shadow_box);
            cairo_set_source_rgba (shadow_cr, ARGS_RGBA(curr_shadow->color));
            cairo_fill (shadow_cr);
            css_gaussian_blur (single_shadow, curr_shadow->blur_radius);

            double xpos = curr_shadow->h_offset - curr_shadow->spread_distance - curr_shadow->blur_radius;
            double ypos = curr_shadow->v_offset - curr_shadow->spread_distance - curr_shadow->blur_radius;
            cairo_set_source_surface (cr, single_shadow, xpos, ypos);
            cairo_paint (cr);
            cairo_surface_destroy (single_shadow);
            cairo_destroy (shadow_cr);
        }

        curr_shadow = curr_shadow->next;
    }
    cairo_pop_group_to_source (cr);

    // Apply shadows masking out the border box
    cairo_push_group (cr);
    cairo_set_source_rgba (cr,0,0,0,1);
    cairo_paint (cr);
    rounded_box_path (cr, border_box);
    cairo_set_operator (cr, CAIRO_OPERATOR_CLEAR);
    cairo_fill (cr);
    cairo_pattern_t *mask = cairo_pop_group (cr);

    cairo_mask (cr, mask);
    cairo_pattern_destroy (mask);
}

void draw_inset_shadows (app_graphics_t *gr, struct css_box_t *css, layout_box_t *layout,
                         struct rounded_box_t *padding_box)
{
    if (css->inset_shadows == NULL) {
        return;
    }

    cairo_t *cr = gr->cr;
    struct box_shadow_t *curr_shadow = css->inset_shadows;

    while (curr_shadow != NULL) {
        struct rounded_box_t shadow_box = *padding_box;
        shadow_box.x += curr_shadow->h_offset + curr_shadow->spread_distance;
        shadow_box.y += curr_shadow->v_offset + curr_shadow->spread_distance;
        shadow_box.width = LOW_CLAMP (shadow_box.width - 2*curr_shadow->spread_distance, 0);
        shadow_box.height = LOW_CLAMP (shadow_box.height - 2*curr_shadow->spread_distance, 0);
        shadow_box.radius = LOW_CLAMP (shadow_box.radius - curr_shadow->spread_distance, 0);

        // TODO: How different is the performance of these paths?
        if (curr_shadow->blur_radius == 0) {
            rounded_box_path (cr, padding_box);
            rounded_box_path_negative (cr, &shadow_box);
            cairo_set_source_rgba (cr, ARGS_RGBA(curr_shadow->color));
            cairo_fill (cr);
        } else {
            shadow_box.x += curr_shadow->blur_radius;
            shadow_box.y += curr_shadow->blur_radius;

            cairo_surface_t *single_shadow =
                cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            padding_box->width + 2*curr_shadow->blur_radius,
                                            padding_box->height + 2*curr_shadow->blur_radius);
            cairo_t *shadow_cr = cairo_create (single_shadow);
            cairo_set_source_rgba (shadow_cr, ARGS_RGBA(curr_shadow->color));
            cairo_paint (shadow_cr);
            rounded_box_path (shadow_cr, &shadow_box);
            cairo_set_operator (shadow_cr, CAIRO_OPERATOR_CLEAR);
            cairo_fill (shadow_cr);

            css_gaussian_blur (single_shadow, curr_shadow->blur_radius);

            cairo_set_source_surface (cr, single_shadow, -curr_shadow->blur_radius, -curr_shadow->blur_radius);
            cairo_paint (cr);

            cairo_surface_destroy (single_shadow);
            cairo_destroy (shadow_cr);

        }
        curr_shadow = curr_shadow->next;
    }
}

void draw_text_shadows (app_graphics_t *gr, struct css_box_t *css,
                        vect2_t pos, char *str, int len)
{
    if (css->text_shadows == NULL) {
        return;
    }

    cairo_t *cr = gr->cr;
    struct text_shadow_t *curr_shadow = css->text_shadows;

    struct font_style_t font_style = FONT_STYLE_CSS(css);
    while (curr_shadow != NULL) {
        vect2_t shadow_pos = pos;
        if (curr_shadow->blur_radius == 0) {
            shadow_pos.x += curr_shadow->h_offset;
            shadow_pos.y += curr_shadow->v_offset;
            render_text (cr, shadow_pos, &font_style, str, len, &curr_shadow->color, NULL, NULL);
        } else {
            PangoLayout *text_layout = new_pango_layout_from_style (cr, font_style);
            PangoRectangle logical;
            pango_layout_set_text (text_layout, str, len);
            pango_layout_get_pixel_extents (text_layout, NULL, &logical);

            cairo_surface_t *single_shadow = 
                cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                            logical.width + 2*curr_shadow->blur_radius,
                                            logical.height + 2*curr_shadow->blur_radius);
            cairo_t *shadow_cr = cairo_create (single_shadow);
            vect2_t tmp_pos = VECT2(curr_shadow->blur_radius, curr_shadow->blur_radius);
            render_text (shadow_cr, tmp_pos, &font_style, str, len, &curr_shadow->color, NULL, NULL);
            css_gaussian_blur (single_shadow, curr_shadow->blur_radius);

            shadow_pos.x += curr_shadow->h_offset - curr_shadow->blur_radius;
            shadow_pos.y += curr_shadow->v_offset - curr_shadow->blur_radius;
            cairo_set_source_surface (cr, single_shadow, shadow_pos.x, shadow_pos.y);
            cairo_paint (cr);

            cairo_surface_destroy (single_shadow);
            cairo_destroy (shadow_cr);
            g_object_unref (text_layout);
        }
        curr_shadow = curr_shadow->next;
    }
}

// NOTE: We draw assuming the option box-sizing: border-box. Which means
// BOX_WIDTH(layout->box) includes the content width, x_padding and border_width.
void css_box_draw (app_graphics_t *gr, struct css_box_t *box, layout_box_t *layout)
{
    assert (is_box_initialized(layout) && "Can't draw an uninitialized layout_box_t");

    cairo_t *cr = gr->cr;
    cairo_save (cr);
    cairo_translate (cr, layout->box.min.x, layout->box.min.y);

    // NOTE: This is the css content+padding.
    double content_width = BOX_WIDTH(layout->box) - 2*(box->border_width);
    double content_height = BOX_HEIGHT(layout->box) - 2*(box->border_width);

    struct rounded_box_t border_box = css_get_border_box (box, layout);
    draw_outset_shadows (gr, box, layout, &border_box);

    rounded_box_path (cr, &border_box);
    cairo_set_source_rgba (cr, ARGS_RGBA(box->background_color));
    cairo_fill (cr);

    struct rounded_box_t padding_box = css_get_padding_box (box, layout);

    // Draw border
    if (box->border_width != 0) {
        rounded_box_path (cr, &border_box);
        rounded_box_path_negative (cr, &padding_box);
        cairo_set_source_rgba (cr, ARGS_RGBA(box->border_color));
        cairo_fill (cr);
    }

    // FIXME: There seems to be a bug in Cairo where setting a clip with arcs
    // disables RGB antialiasing of text drawn with Pango. For now we can set
    // the clip region to be a rectangle or have no antialiasing on boxes with
    // border_radius != 0.
#if 0
    rounded_box_path (cr, &padding_box);
#else
    cairo_rectangle (cr, padding_box.x, padding_box.y, padding_box.width, padding_box.height);
#endif
    cairo_clip (cr);

    if (box->num_gradient_stops > 0) {
        cairo_pattern_t *patt =
            cairo_pattern_create_linear (padding_box.x, padding_box.y,
                                         padding_box.x, padding_box.y + padding_box.height);
        double position;
        double step = 1.0/((double)box->num_gradient_stops-1);
        int stop_idx = 0;
        for (position = 0.0; position<1; position+=step) {
            cairo_pattern_add_color_stop_rgba (patt, position, ARGS_RGBA(box->gradient_stops[stop_idx]));
            stop_idx++;
        }
        cairo_pattern_add_color_stop_rgba (patt, position, ARGS_RGBA(box->gradient_stops[stop_idx]));
        cairo_set_source (cr, patt);
        cairo_paint (cr);
        cairo_pattern_destroy (patt);
    }

    draw_inset_shadows (gr, box, layout, &padding_box);

    if (layout->content.type != LAYOUT_CONTENT_NONE) {

        compute_content_size (layout);

        css_text_align_t effective_text_align;
        if (layout->text_align_override != CSS_TEXT_ALIGN_INITIAL) {
            effective_text_align = layout->text_align_override;
        } else {
            effective_text_align = box->text_align;
        }

        double text_pos_x = box->border_width, text_pos_y = box->border_width;
        switch (effective_text_align) {
            case CSS_TEXT_ALIGN_LEFT:
                break;
            case CSS_TEXT_ALIGN_RIGHT:
                text_pos_x += content_width - layout->content.width;
                break;
            case CSS_TEXT_ALIGN_INITIAL:
            case CSS_TEXT_ALIGN_CENTER:
                text_pos_x += (content_width - layout->content.width)/2;
                break;
            default:
                invalid_code_path;
        }
        text_pos_y += (content_height - layout->content.height)/2;

        struct selection_t *selection = &global_gui_st->selection;
        vect2_t pos = VECT2(text_pos_x, text_pos_y);
        draw_text_shadows (gr, box, pos, layout->content.str, -1);

        struct font_style_t font_style = FONT_STYLE_CSS(box);
        if (selection->dest == layout) {
            assert (layout->content.str >= selection->start &&
                    selection->start <= layout->content.str + strlen (layout->content.str) &&
                    "selection->start must point into selection->dest->content.str");

            if (selection->start != layout->content.str) {
                render_text (cr, pos, &font_style, layout->content.str, selection->start - layout->content.str,
                             &box->color, NULL, &pos);
            }

            render_text (cr, pos, &font_style, selection->start, selection->len,
                         &selection->color, &selection->background_color, &pos);

            if (selection->len != -1) {
                render_text (cr, pos, &font_style, selection->start+selection->len, -1,
                             &box->color, NULL, NULL);
            }
        } else {
          render_text (cr, pos, &font_style, layout->content.str, -1, &box->color, NULL, NULL);
        }

#if 0
        cairo_set_source_rgba (cr, 1.0, 0.0, 0.0, 0.3);
        cairo_rectangle (cr, box->border_width, box->border_width, content_width, content_height);
        cairo_fill (cr);

        cairo_set_source_rgba (cr, 0.0, 1.0, 0.0, 0.3);
        cairo_rectangle (cr, text_pos_x, text_pos_y,
                         layout->str.width, layout->str.height);
        cairo_fill (cr);
#endif
    }

    cairo_restore (cr);
}

void css_add_text_shadow (mem_pool_t *pool, struct css_box_t *css,
                          double h_offset, double v_offset,
                          double blur_radius, vect4_t color)
{
    struct text_shadow_t *new_text_shadow =
        (struct text_shadow_t*)mem_pool_push_size (pool, sizeof (struct text_shadow_t));
    new_text_shadow->h_offset = h_offset;
    new_text_shadow->v_offset = v_offset;
    new_text_shadow->blur_radius = blur_radius;
    new_text_shadow->color = color;

    new_text_shadow->next = css->text_shadows;
    css->text_shadows = new_text_shadow;
}

void css_add_box_shadow (mem_pool_t *pool, struct css_box_t *css,
                         bool inset, double h_offset, double v_offset,
                         double blur_radius, double spread_distance,
                         vect4_t color)
{
    struct box_shadow_t *new_box_shadow =
        (struct box_shadow_t*)mem_pool_push_size (pool, sizeof (struct box_shadow_t));
    new_box_shadow->h_offset = h_offset;
    new_box_shadow->v_offset = v_offset;
    new_box_shadow->blur_radius = blur_radius;
    new_box_shadow->spread_distance = spread_distance;
    new_box_shadow->color = color;

    struct box_shadow_t **box_shadow_list;
    if (inset) {
        box_shadow_list = &css->inset_shadows;
    } else {
        box_shadow_list = &css->outset_shadows;
    }

    new_box_shadow->next = *box_shadow_list;
    *box_shadow_list = new_box_shadow;
}

void css_box_add_gradient_stops (struct css_box_t *box, int num_stops, vect4_t *stops)
{
    assert (_g_gui_num_colors+num_stops < ARRAY_SIZE(_g_gui_colors));
    box->num_gradient_stops = num_stops;
    box->gradient_stops = &_g_gui_colors[_g_gui_num_colors];

    int i = 0;
    while (i < num_stops) {
        _g_gui_colors[_g_gui_num_colors] = stops[i];
        _g_gui_num_colors++;
        i++;
    }
}

void rgba_to_hsla (vect4_t *rgba, vect4_t *hsla)
{
    double max = MAX(MAX(rgba->r,rgba->g),rgba->b);
    double min = MIN(MIN(rgba->r,rgba->g),rgba->b);

    // NOTE: This is the same for HSV
    if (max == min) {
        // NOTE: we define this, in reality H is undefined
        // in this case.
        hsla->h = INFINITY;
    } else if (max == rgba->r) {
        hsla->h = (60*(rgba->r-rgba->b)/(max-min) + 360);
        if (hsla->h > 360) {
            hsla->h -= 360;
        }
    } else if (max == rgba->g) {
        hsla->h = (60*(rgba->b-rgba->r)/(max-min) + 120);
    } else { // if (max == rgba->b)
        hsla->h = (60*(rgba->r-rgba->g)/(max-min) + 240);
    }

    hsla->h /= 360; // h\in[0,1]

    hsla->l = (max+min)/2;

    if (max == min) {
        hsla->s = 0;
    } else if (hsla->l <= 0.5) {
        hsla->s = (max-min)/(2*hsla->l);
    } else {
        hsla->s = (max-min)/(2-2*hsla->l);
    }

    hsla->a = rgba->a;
}

void hsla_to_rgba (vect4_t *hsla, vect4_t *rgba)
{
    double h_pr = hsla->h*6;
    double chroma = (1-fabs(2*hsla->l-1)) * hsla->s;
    // Alternative version can be:
    //
    //double chroma;
    //if (hsla->l <= 0.5) {
    //    chroma = (2*hsla->l)*hsla->s;
    //} else {
    //    chroma = (2-2*hsla->l)*hsla->s;
    //}

    double h_pr_mod_2 = ((int)h_pr)%2+(h_pr-(int)h_pr);
    double x = chroma*(1-fabs(h_pr_mod_2-1));
    double m = hsla->l - chroma/2;

    *rgba = VECT4(0,0,0,0);
    if (h_pr == INFINITY) {
        rgba->r = 0;
        rgba->g = 0;
        rgba->b = 0;
    } else if (h_pr < 1) {
        rgba->r = chroma;
        rgba->g = x;
        rgba->b = 0;
    } else if (h_pr < 2) {
        rgba->r = x;
        rgba->g = chroma;
        rgba->b = 0;
    } else if (h_pr < 3) {
        rgba->r = 0;
        rgba->g = chroma;
        rgba->b = x;
    } else if (h_pr < 4) {
        rgba->r = 0;
        rgba->g = x;
        rgba->b = chroma;
    } else if (h_pr < 5) {
        rgba->r = x;
        rgba->g = 0;
        rgba->b = chroma;
    } else if (h_pr < 6) {
        rgba->r = chroma;
        rgba->g = 0;
        rgba->b = x;
    }
    rgba->r += m;
    rgba->g += m;
    rgba->b += m;
    rgba->a = hsla->a;
}

static inline
vect4_t shade (vect4_t *in, double f)
{
    vect4_t temp;
    rgba_to_hsla (in, &temp);

    temp.l *= f;
    temp.l = temp.l>1? 1 : temp.l;
    temp.s *= f;
    temp.s = temp.s>1? 1 : temp.s;

    vect4_t ret;
    hsla_to_rgba (&temp, &ret);
    return ret;
}

static inline
vect4_t alpha (vect4_t c, double f)
{
    vect4_t ret = c;
    ret.a *= f;
    return ret;
}

static inline
vect4_t mix (vect4_t *c1, vect4_t *c2, double f)
{
    vect4_t ret;
    int i;
    for (i=0; i<4; i++) {
        ret.E[i] = CLAMP (c1->E[i] + (c2->E[i]-c1->E[i])*f, 0, 1);
    }
    return ret;
}

void hsv_to_rgb (vect3_t hsv, vect3_t *rgb)
{
    double h = hsv.E[0];
    double s = hsv.E[1];
    double v = hsv.E[2];
    assert (h <= 1 && s <= 1 && v <= 1);

    int h_i = (int)(h*6);
    double frac = h*6 - h_i;

    double m = v * (1 - s);
    double desc = v * (1 - frac*s);
    double asc = v * (1 - (1 - frac) * s);

    switch (h_i) {
        case 0:
            rgb->r = v;
            rgb->g = asc;
            rgb->b = m;
            break;
        case 1:
            rgb->r = desc;
            rgb->g = v;
            rgb->b = m;
            break;
        case 2:
            rgb->r = m;
            rgb->g = v;
            rgb->b = asc;
            break;
        case 3:
            rgb->r = m;
            rgb->g = desc;
            rgb->b = v;
            break;
        case 4:
            rgb->r = asc;
            rgb->g = m;
            rgb->b = v;
            break;
        case 5:
            rgb->r = v;
            rgb->g = m;
            rgb->b = desc;
            break;
    }
}

void rgba_print (vect4_t rgba)
{
    printf ("rgba");
    vect4_print(&rgba);
    printf ("\n");
}

// Automatic color palette:
//
// There are several way of doing this, using HSV with fixed SV and random Hue
// incremented every PHI_INV seems good enough. If it ever stops being good,
// research about perception based formats like CIELAB (LAB), and color
// difference functions like CIE76, CIE94, CIEDE2000.

#if 1
vect3_t color_palette[13] = {
    {{0.254902, 0.517647, 0.952941}}, //Google Blue
    {{0.858824, 0.266667, 0.215686}}, //Google Red
    {{0.956863, 0.705882, 0.000000}}, //Google Yellow
    {{0.058824, 0.615686, 0.345098}}, //Google Green
    {{0.666667, 0.274510, 0.733333}}, //Purple
    {{1.000000, 0.435294, 0.258824}}, //Deep Orange
    {{0.615686, 0.611765, 0.137255}}, //Lime
    {{0.937255, 0.380392, 0.568627}}, //Pink
    {{0.356863, 0.415686, 0.749020}}, //Indigo
    {{0.000000, 0.670588, 0.752941}}, //Teal
    {{0.756863, 0.090196, 0.352941}}, //Deep Pink
    {{0.619608, 0.619608, 0.619608}}, //Gray
    {{0.000000, 0.470588, 0.415686}}};//Deep Teal
#else

vect3_t color_palette[9] = {
    {{0.945098, 0.345098, 0.329412}}, // red
    {{0.364706, 0.647059, 0.854902}}, // blue
    {{0.980392, 0.643137, 0.227451}}, // orange
    {{0.376471, 0.741176, 0.407843}}, // green
    {{0.945098, 0.486275, 0.690196}}, // pink
    {{0.698039, 0.568627, 0.184314}}, // brown
    {{0.698039, 0.462745, 0.698039}}, // purple
    {{0.870588, 0.811765, 0.247059}}, // yellow
    {{0.301961, 0.301961, 0.301961}}};// gray
#endif

#define PHI_INV 0.618033988749895
#define COLOR_OFFSET 0.4

void get_next_color (vect3_t *color)
{
    static double h = COLOR_OFFSET;
    static uint32_t palette_idx = 0;

    // Reset color palette
    if (color == NULL) {
        palette_idx = 0;
        h = COLOR_OFFSET;
        return;
    }

    if (palette_idx < ARRAY_SIZE(color_palette)) {
        *color = color_palette[palette_idx];
        palette_idx++;
    } else {
        h += PHI_INV;
        if (h>1) {
            h -= 1;
        }
        hsv_to_rgb (VECT3(h, 0.8, 0.7), color);
    }
}

void init_button (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 3;
    box->border_color = border_color;
    box->border_radius = 2.5;
    box->color = text_color;

    vect4_t stops[3] = {RGBA(0,0,0,0),
                        RGBA(0,0,0,0),
                        RGBA(0,0,0,0.04)};
    css_box_add_gradient_stops (box, ARRAY_SIZE(stops), stops);

    css_add_text_shadow (pool, box, 0, 1, 0, text_shadow_color);

    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, alpha (bg_highlight_color, 0.05));
    css_add_box_shadow (pool, box, true, 0, 1, 0, 0, alpha (bg_highlight_color, 0.45));
    css_add_box_shadow (pool, box, true, 0,-1, 0, 0, alpha (bg_highlight_color, 0.15));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.15));
}

void init_button_active (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 3;
    box->border_color = RGBA(0, 0, 0, 0.27);
    box->color = text_color;

    box->background_color = RGBA (0, 0, 0, 0.05);

    css_add_text_shadow (pool, box, 0, 1, 0, text_shadow_color);
    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, RGBA (0, 0, 0, 0.05));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.3));
}

void init_button_disabled (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 3;
    box->border_color = RGBA(0, 0, 0, 0.2);
    box->color = insensitive_color;

    box->background_color = RGBA(0, 0, 0, 0.0);
    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, alpha (bg_highlight_color, 0.05));
    css_add_box_shadow (pool, box, true, 0, 1, 0, 0, alpha (bg_highlight_color, 0.45));
    css_add_box_shadow (pool, box, true, 0,-1, 0, 0, alpha (bg_highlight_color, 0.15));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.15));
}

void init_suggested_action_button (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    // Inherited
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 3;

    // Actual style
    box->border_color = shade (&selected_bg_color, 0.8);
    box->color = selected_fg_color;

    vect4_t stops[2] = {shade(&selected_bg_color,1.1),
                        shade(&selected_bg_color,0.9)};
    css_box_add_gradient_stops (box, ARRAY_SIZE(stops), stops);

    css_add_text_shadow (pool, box, 0, 1, 0, RGBA (0, 0, 0, 0.3));

    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, alpha (bg_highlight_color, 0.05));
    css_add_box_shadow (pool, box, true, 0, 1, 0, 0, alpha (bg_highlight_color, 0.45));
    css_add_box_shadow (pool, box, true, 0,-1, 0, 0, alpha (bg_highlight_color, 0.15));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.15));
}

void init_suggested_action_button_active (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    // Inherited
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 3;

    css_add_text_shadow (pool, box, 0, 1, 0, RGBA (0, 0, 0, 0.3));

    // Actual style
    box->border_color = shade (&selected_bg_color, 0.8);
    box->color = selected_fg_color;

    vect4_t stops[2] = {shade(&selected_bg_color,1.05),
                        shade(&selected_bg_color,0.95)};
    css_box_add_gradient_stops (box, ARRAY_SIZE(stops), stops);
}

void init_background (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 12;
    box->padding_y = 12;
    box->background_color = bg_color;
    box->border_color = RGBA(0, 0, 0, 0.27);
}

void init_text_entry (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 3;
    box->padding_y = 3;
    box->color = text_color;
    vect4_t stops[2] = {shade (&base_color, 0.93),
                        shade (&base_color, 0.97)};
    css_box_add_gradient_stops (box, ARRAY_SIZE(stops), stops);

    box->border_color = border_color;

    css_add_text_shadow (pool, box, 0, 1, 0, text_shadow_color);

    css_add_box_shadow (pool, box, true, 0, 1, 0, 0, alpha (inset_dark_color, 0.7));
    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, alpha (inset_dark_color, 0.3));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.3));
}

void init_text_entry_focused (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    // Inherited
    box->border_radius = 2.5;
    box->border_width = 1;
    box->padding_x = 3;
    box->padding_y = 3;
    box->color = text_color;
    vect4_t stops[2] = {shade (&base_color, 0.93),
                        shade (&base_color, 0.97)};
    css_box_add_gradient_stops (box, ARRAY_SIZE(stops), stops);

    css_add_text_shadow (pool, box, 0, 1, 0, text_shadow_color);

    // Actual style
    box->border_color = alpha (color_accent, 0.8);

    css_add_box_shadow (pool, box, true, 0, 0, 0, 1, alpha (color_accent, 0.23));
    css_add_box_shadow (pool, box, false, 0, 1, 0, 0, alpha (bg_highlight_color, 0.3));
}

void init_label (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->background_color = RGBA(0, 0, 0, 0);
    box->color = text_color;
}

void init_title_label (mem_pool_t *pool, struct css_box_t *box)
{
    *box = ZERO_INIT(struct css_box_t);
    box->background_color = RGBA(0, 0, 0, 0);
    box->color = text_color_primary;
    box->font_weight = CSS_FONT_WEIGHT_BOLD;

    css_add_text_shadow (pool, box, 0, 1, 0, text_color_primary_shadw);
}

#define GUI_H
#endif
