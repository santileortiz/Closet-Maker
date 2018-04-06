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

// NOTE: This is a unity build
#include "opengl_util.h"
#include "app_api.h"
#include "closet_maker.c"

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
    glDebugMessageCallback((GLDEBUGPROC)debug_message_callback, 0);

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

