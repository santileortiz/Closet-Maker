/*
 * Copyright (C) 2017 Santiago León O. <santileortiz@gmail.com>
 */

#if !defined(COMMON_H)
#include <stdio.h>
#include <unistd.h>
#include <inttypes.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <wordexp.h>
#include <math.h>
typedef enum {false, true} bool;

#define ARRAY_SIZE(arr) (sizeof(arr)/sizeof((arr)[0]))

#if !defined(MIN)
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#if !defined(MAX)
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#if !defined(CLAMP)
#define CLAMP(a,lower,upper) ((a)>(upper)?(upper):((a)<(lower)?(lower):(a)))
#endif

#define LOW_CLAMP(a,lower) ((a)<(lower)?(lower):(a))

#define I_CEIL_DIVIDE(a,b) ((a%b)?(a)/(b)+1:(a)/(b))

#define kilobyte(val) ((val)*1024LL)
#define megabyte(val) (kilobyte(val)*1024LL)
#define gigabyte(val) (megabyte(val)*1024LL)
#define terabyte(val) (gigabyte(val)*1024LL)

#define invalid_code_path assert(0)

////////////
// STRINGS
//
// There are currently two implementations of string handling, one that
// optimizes operations on small strings avoiding calls to malloc, and a
// straightforward one that mallocs everything.
//
// Functions available:
//  str_new(char *c_str)
//     strn_new(char *c_str, size_t len)
//  str_set(string_t *str, char *c_str)
//     strn_set(string_t *str, char *c_str, size_t len)
//  str_len(string_t *str)
//  str_data(string_t *str)
//  str_cpy(string_t *dest, string_t *src)
//  str_cat(string_t *dest, string_t *src)
//  str_free(string_t *str)
//
// NOTE:
//  - string_t is zero initialized, an empty string can be created as:
//      string_t str = {0};
//    Use str_new() to initialize one from a null terminated string.
//
//  - str_data() returns a NULL terminated C type string.
//
// Both implementations were tested using the following code:

/*
//gcc -g -o strings strings.c -lm

#include "common.h"
int main (int argv, char *argc[])
{
    string_t str1 = {0};
    string_t str2 = str_new ("");
    // NOTE: str_len(str3) == ARRAY_SIZE(str->str_small)-1, to test correctly
    // the string implementation that uses small string optimization.
    string_t str3 = str_new ("Hercule Poirot");
    string_t str4 = str_new (" ");
    // NOTE: str_len(str3) < str_len(str5)
    string_t str5 = str_new ("is a good detective");

    printf ("str1: '%s'\n", str_data(&str1));
    printf ("str2: '%s'\n", str_data(&str2));
    printf ("str3: '%s'\n", str_data(&str3));
    printf ("str4: '%s'\n", str_data(&str4));
    printf ("str5: '%s'\n", str_data(&str5));

    string_t str_test = {0};
    str_set (&str_test, str_data(&str1));

    printf ("\nTEST 1:\n");
    str_cpy (&str_test, &str2);
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_set (&str_test, str_data(&str3));
    printf ("str_set(str_test, '%s')\n", str_data(&str3));
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_set (&str_test, str_data(&str5));
    printf ("str_set(str_test, '%s')\n", str_data(&str5));
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_set (&str_test, str_data(&str4));
    printf ("str_set(str_test, '%s')\n", str_data(&str4));
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_debug_print (&str_test);
    str_free (&str_test);

    str_set (&str_test, "");
    printf ("\nTEST 2:\n");
    str_cpy (&str_test, &str3);
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_cat (&str_test, &str4);
    printf ("str_cat(str_test, str4)\n");
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_cat (&str_test, &str5);
    printf ("str_cat(str_test, str5)\n");
    printf ("str_test: '%s'\n", str_data(&str_test));
    str_debug_print (&str_test);
    str_free (&str_test);
    return 0;
}
*/

#if 1
// String implementation with small string optimization.

#pragma pack(push, 1)
#if defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
typedef union {
    struct {
        uint8_t len_small;
        char str_small[15];
    };

    struct {
        uint32_t capacity;
        uint32_t len;
        char *str;
    };
} string_t;
#elif defined(__BYTE_ORDER__)&&(__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
// TODO: This has NOT been tested!
typedef union {
    struct {
        char str_small[15];
        uint8_t len_small;
    };

    struct {
        char *str;
        uint32_t len;
        uint32_t capacity;
    };
} string_t;
#endif
#pragma pack(pop)

#define str_is_small(string) (!((string)->len_small&0x01))
#define str_len(string) (str_is_small(string)?(string)->len_small/2-1:(string)->len)
#define str_data(string) (str_is_small(string)?(string)->str_small:(string)->str)

static inline
char* str_small_alloc (string_t *str, size_t len)
{
    str->len_small = 2*(len+1); // Guarantee LSB == 0
    return str->str_small;
}

static inline
char* str_non_small_alloc (string_t *str, size_t len)
{
    str->capacity = (len+1) | 0xF; // Round up and guarantee LSB == 1
    str->str = malloc(str->capacity);
    return str->str;
}

static inline
void str_maybe_grow (string_t *str, size_t len, bool keep_content)
{
    if (!str_is_small(str)) {
        if (len >= str->capacity) {
            if (keep_content) {
                uint32_t tmp_len = str->len;
                char *tmp = str->str;

                str_non_small_alloc (str, len);
                memcpy (str->str, tmp, tmp_len);
                free (tmp);
            } else {
                free (str->str);
                str_non_small_alloc (str, len);
            }
        }
        str->len = len;

    } else {
        if (len >= ARRAY_SIZE(str->str_small)) {
            if (keep_content) {
                char tmp[ARRAY_SIZE(str->str_small)];
                strcpy (tmp, str->str_small);

                str_non_small_alloc (str, len);
                strcpy (str->str, tmp);
            } else {
                str_non_small_alloc (str, len);
            }
            str->len = len;
        } else {
            str_small_alloc (str, len);
        }
    }
}

static inline
char* str_init (string_t *str, size_t len)
{
    char *dest;
    if (len < ARRAY_SIZE(str->str_small)) {
        dest = str_small_alloc (str, len);
    } else {
        str->len = len;
        dest = str_non_small_alloc (str, len);
    }
    return dest;
}

void str_free (string_t *str)
{
    if (!str_is_small(str)) {
        free (str->str);
    }
    *str = (string_t){0};
}

void str_debug_print (string_t *str)
{
    if (str_is_small(str)) {
        printf ("string_t: [SMALL OPT]\n"
                "  Type: small\n"
                "  data: %s\n"
                "  len: %"PRIu32"\n"
                "  len_small: %"PRIu32"\n"
                "  capacity: %lu\n",
                str->str_small, str_len(str),
                str->len_small, ARRAY_SIZE(str->str_small));
    } else {
        printf ("string_t: [SMALL OPT]\n"
                "  Type: long\n"
                "  data: %s\n"
                "  len: %"PRIu32"\n"
                "  capacity: %"PRIu32"\n",
                str->str, str->len, str->capacity);
    }
}

#else
// Straightforward string implementation

typedef struct {
    char *str;
    uint32_t len;
    uint32_t capacity;
} string_t;

static inline
char* str_alloc (string_t *str, size_t len)
{
    str->capacity = (len+1) | 0x0F; // Round up
    str->str = malloc(str->capacity);
    return str->str;
}

#define str_len(string) ((string)->len)
char* str_data (string_t *str)
{
    if (str->str == NULL) {
        str_alloc (str, 0);
        str->len = 0;
    }
    return str->str;
}

static inline
void str_maybe_grow (string_t *str, size_t len, bool keep_content)
{
    if (len >= str->capacity) {
        if (keep_content) {
            uint32_t tmp_len = str->len;
            char *tmp = str->str;
            str_alloc (str, len);
            memcpy (str->str, tmp, tmp_len);
            free (tmp);
        } else {
            free (str->str);
            str_alloc (str, len);
        }
    }
    str->len = len;
}

static inline
char* str_init (string_t *str, size_t len)
{
    char *dest;
    str->len = len;
    dest = str_alloc (str, len);
    return dest;
}

void str_free (string_t *str)
{
    free (str->str);
    *str = (string_t){0};
}

void str_debug_print (string_t *str)
{
    printf ("string_t: [SIMPLE]\n"
            "  data: %s\n"
            "  len: %"PRIu32"\n"
            "  capacity: %"PRIu32"\n",
            str->str, str->len, str->capacity);
}

#endif

#define str_new(data) strn_new((data),((data)!=NULL?strlen(data):0))
string_t strn_new (char *c_str, size_t len)
{
    string_t retval = {0};
    char *dest = str_init (&retval, len);

    if (c_str != NULL) {
        memmove (dest, c_str, len);
    }
    dest[len] = '\0';
    return retval;
}

#define str_set(str,c_str) strn_set(str,(c_str),((c_str)!=NULL?strlen(c_str):0))
void strn_set (string_t *str, char *c_str, size_t len)
{
    str_maybe_grow (str, len, false);

    char *dest = str_data(str);
    if (c_str != NULL) {
        memmove (dest, c_str, len);
    }
    dest[len] = '\0';
}

void str_cpy (string_t *dest, string_t *src)
{
    size_t len = str_len(src);
    str_maybe_grow (dest, len, false);

    char *dest_data = str_data(dest);
    memmove (dest_data, str_data(src), len);
    dest_data[len] = '\0';
}

void str_cat (string_t *dest, string_t *src)
{
    size_t len_dest = str_len(dest);
    size_t len = len_dest + str_len(src);

    str_maybe_grow (dest, len, true);
    char *dest_data = str_data(dest);
    memmove (dest_data+len_dest, str_data(src), len);
    dest_data[len] = '\0';
}

#define VECT_X 0
#define VECT_Y 1
#define VECT_Z 2
#define VECT_W 3

#define VECT_R 0
#define VECT_G 1
#define VECT_B 2
#define VECT_A 3

typedef union {
    struct {
        double x;
        double y;
    };
    double E[2];
} vect2_t;
#define VECT2(x,y) ((vect2_t){{x,y}})

// TODO: These are 128 bit structs, they may take up a lot of space, maybe have
// another one of 32 bits for small coordinates.
// TODO: Also, how does this affect performance?
typedef union {
    struct {
        int64_t x;
        int64_t y;
    };
    int64_t E[2];
} vect2i_t;
#define VECT2i(x,y) ((vect2i_t){{x,y}})

#define vect_equal(v1,v2) ((v1)->x == (v2)->x && (v1)->y == (v2)->y)

typedef struct {
    vect2_t min;
    vect2_t max;
} box_t;

void box_print (box_t *b)
{
    printf ("min: (%f, %f), max: (%f, %f)\n", b->min.x, b->min.y, b->max.x, b->max.y);
}

void vect2_floor (vect2_t *p)
{
    p->x = floor (p->x);
    p->y = floor (p->y);
}

void vect2_round (vect2_t *p)
{
    p->x = round (p->x);
    p->y = round (p->y);
}

static inline
vect2_t vect2_add (vect2_t v1, vect2_t v2)
{
    vect2_t res;
    res.x = v1.x+v2.x;
    res.y = v1.y+v2.y;
    return res;
}

static inline
void vect2_add_to (vect2_t *v1, vect2_t v2)
{
    v1->x += v2.x;
    v1->y += v2.y;
}

static inline
vect2_t vect2_subs (vect2_t v1, vect2_t v2)
{
    vect2_t res;
    res.x = v1.x-v2.x;
    res.y = v1.y-v2.y;
    return res;
}

static inline
void vect2_subs_to (vect2_t *v1, vect2_t v2)
{
    v1->x -= v2.x;
    v1->y -= v2.y;
}

static inline
vect2_t vect2_mult (vect2_t v, double k)
{
    vect2_t res;
    res.x = v.x*k;
    res.y = v.y*k;
    return res;
}

static inline
void vect2_mult_to (vect2_t *v, double k)
{
    v->x *= k;
    v->y *= k;
}

static inline
double vect2_dot (vect2_t v1, vect2_t v2)
{
    return v1.x*v2.x + v1.y*v2.y;
}

static inline
double vect2_norm (vect2_t v)
{
    return sqrt ((v.x)*(v.x) + (v.y)*(v.y));
}

double area_2 (vect2_t a, vect2_t b, vect2_t c)
{
    return (b.x-a.x)*(c.y-a.y) - (c.x-a.x)*(b.y-a.y);
}

// true if point c is to the left of a-->b
bool left (vect2_t a, vect2_t b, vect2_t c)
{
    return area_2 (a, b, c) > 0;
}

// true if vector p points to the left of vector a
#define left_vect(a,p) left(VECT2(0,0),a,p)

// true if point c is to the left or in a-->b
bool left_on (vect2_t a, vect2_t b, vect2_t c)
{
    return area_2 (a, b, c) > -0.01;
}

// Angle from v1 to v2 clockwise.
//
// NOTE: The return value is in the range [0, 2*M_PI).
static inline
double vect2_clockwise_angle_between (vect2_t v1, vect2_t v2)
{
    if (vect_equal (&v1, &v2)) {
        return 0;
    } else if (left_vect (v2, v1))
        return acos (vect2_dot (v1, v2)/(vect2_norm (v1)*vect2_norm(v2)));
    else
        return 2*M_PI - acos (vect2_dot (v1, v2)/(vect2_norm (v1)*vect2_norm(v2)));
}

// Smallest angle between v1 to v2.
//
// NOTE: The return value is in the range [0, M_PI].
static inline
double vect2_angle_between (vect2_t v1, vect2_t v2)
{
    if (vect_equal (&v1, &v2)) {
        return 0;
    } else
        return acos (vect2_dot (v1, v2)/(vect2_norm (v1)*vect2_norm(v2)));
}

static inline
void vect2_normalize (vect2_t *v)
{
    double norm = vect2_norm (*v);
    assert (norm != 0);
    v->x /= norm;
    v->y /= norm;
}

static inline
void vect2_normalize_or_0 (vect2_t *v)
{
    double norm = vect2_norm (*v);
    if (norm == 0) {
        v->x = 0;
        v->y = 0;
    } else {
        v->x /= norm;
        v->y /= norm;
    }
}

vect2_t vect2_clockwise_rotate (vect2_t v, double rad)
{
    vect2_t res;
    res.x =  v.x*cos(rad) + v.y*sin(rad);
    res.y = -v.x*sin(rad) + v.y*cos(rad);
    return res;
}

void vect2_clockwise_rotate_on (vect2_t *v, double rad)
{
    vect2_t tmp = *v;
    v->x =  tmp.x*cos(rad) + tmp.y*sin(rad);
    v->y = -tmp.x*sin(rad) + tmp.y*cos(rad);
}

static inline
double vect2_distance (vect2_t *v1, vect2_t *v2)
{
    if (vect_equal(v1, v2)) {
        return 0;
    } else {
        return sqrt ((v1->x-v2->x)*(v1->x-v2->x) + (v1->y-v2->y)*(v1->y-v2->y));
    }
}

void vect2_print (vect2_t *v)
{
    printf ("(%f, %f) [%f]\n", v->x, v->y, vect2_norm(*v));
}

// NOTE: W anf H MUST be positive.
#define BOX_X_Y_W_H(box,n_x,n_y,n_w,n_h) {     \
    (box).min.x=(n_x);(box).max.x=(n_x)+(n_w); \
    (box).min.y=(n_y);(box).max.y=(n_y)+(n_h); \
}

#define BOX_CENTER_X_Y_W_H(box,n_x,n_y,n_w,n_h) {(box).min.x=(n_x)-(n_w)/2;(box).max.x=(n_x)+(n_w)/2; \
                                                 (box).min.y=(n_y)-(n_h)/2;(box).max.y=(n_y)+(n_h)/2;}
#define BOX_POS_SIZE(box,pos,size) BOX_X_Y_W_H(box,(pos).x,(pos).y,(size).x,(size).y)

#define BOX_WIDTH(box) ((box).max.x-(box).min.x)
#define BOX_HEIGHT(box) ((box).max.y-(box).min.y)
#define BOX_AR(box) (BOX_WIDTH(box)/BOX_HEIGHT(box))

typedef union {
    struct {
        double x;
        double y;
        double z;
    };

    struct {
        double r;
        double g;
        double b;
    };
    double E[3];
} vect3_t;
#define VECT3(x,y,z) ((vect3_t){{x,y,z}})

static inline
vect3_t vect3_cross (vect3_t v1, vect3_t v2)
{
    vect3_t res;
    res.x = v1.y*v2.z - v1.z*v2.y;
    res.y = v1.z*v2.x - v1.x*v2.z;
    res.z = v1.x*v2.y - v1.y*v2.x;
    return res;
}

typedef union {
    struct {
        double x;
        double y;
        double z;
        double w;
    };

    struct {
        double r;
        double g;
        double b;
        double a;
    };

    struct {
        double h;
        double s;
        double l;
    };
    double E[4];
} vect4_t;
#define VECT4(x,y,z,w) ((vect4_t){{x,y,z,w}})

void vect4_print (vect4_t *v)
{
    printf ("(%f, %f, %f, %f)", v->x, v->y, v->z, v->w);
}

void swap (int*a, int*b)
{
    int temp = *a;
    *a = *b;
    *b = temp;
}

// Merge sort implementation
void int_sort (int *arr, int n)
{
    if (n==1) {
        return;
    } else if (n == 2) {
        if (arr[1] < arr[0]) {
            swap (&arr[0], &arr[1]);
        }
    } else if (n==3) {
        if (arr[0] > arr[1]) swap(&arr[0],&arr[1]);
        if (arr[1] > arr[2]) swap(&arr[1],&arr[2]);
        if (arr[0] > arr[1]) swap(&arr[0],&arr[1]);
    } else {
        int res[n];
        int_sort (arr, n/2);
        int_sort (&arr[n/2], n-n/2);

        int i;
        int a=0;
        int b=n/2;
        for (i=0; i<n; i++) {
            if (b==n || (a<n/2 && arr[a] < arr[b])) {
                res[i] = arr[a];
                a++;
            } else {
                res[i] = arr[b];
                b++;
            }
        }
        for (i=0; i<n; i++) {
            arr[i] = res[i];
        }
    }
}

void swap_n_bytes (void *a, void*b, int n)
{
    while (sizeof(int)<=n) {
        n -= sizeof(int);
        int *a_c = (int*)((char*)a+n);
        int *b_c = (int*)((char*)b+n);
        *a_c = *a_c^*b_c;
        *b_c = *a_c^*b_c;
        *a_c = *a_c^*b_c;
    }

    if (n<0) {
        n += sizeof(int);
        while (n<0) {
            n--;
            char *a_c = (char*)a+n;
            char *b_c = (char*)b+n;
            *a_c = *a_c^*b_c;
            *b_c = *a_c^*b_c;
            *a_c = *a_c^*b_c;
        }
    }
}

// Templetized merge sort
// IS_A_LT_B is an expression where a and b are pointers
// to _arr_ true when *a<*b.
#define templ_sort(FUNCNAME,TYPE,IS_A_LT_B)                     \
void FUNCNAME ## _user_data (TYPE *arr, int n, void *user_data) \
{                                                               \
    if (n==1) {                                                 \
        return;                                                 \
    } else if (n == 2) {                                        \
        TYPE *a = &arr[1];                                      \
        TYPE *b = &arr[0];                                      \
        int c = IS_A_LT_B;                                      \
        if (c) {                                                \
            swap_n_bytes (&arr[0], &arr[1], sizeof(TYPE));      \
        }                                                       \
    } else {                                                    \
        TYPE res[n];                                            \
        FUNCNAME ## _user_data (arr, n/2, user_data);           \
        FUNCNAME ## _user_data (&arr[n/2], n-n/2, user_data);   \
                                                                \
        int i;                                                  \
        int h=0;                                                \
        int k=n/2;                                              \
        for (i=0; i<n; i++) {                                   \
            TYPE *a = &arr[h];                                  \
            TYPE *b = &arr[k];                                  \
            if (k==n || (h<n/2 && (IS_A_LT_B))) {               \
                res[i] = arr[h];                                \
                h++;                                            \
            } else {                                            \
                res[i] = arr[k];                                \
                k++;                                            \
            }                                                   \
        }                                                       \
        for (i=0; i<n; i++) {                                   \
            arr[i] = res[i];                                    \
        }                                                       \
    }                                                           \
}                                                               \
\
void FUNCNAME(TYPE *arr, int n) {                               \
    FUNCNAME ## _user_data (arr,n,NULL);                        \
}

typedef struct {
    int origin;
    int key;
} int_key_t;

void int_key_print (int_key_t k)
{
    printf ("origin: %d, key: %d\n", k.origin, k.key);
}

templ_sort (sort_int_keys, int_key_t, a->key < b->key)

bool in_array (int i, int* arr, int size)
{
    while (size) {
        size--;
        if (arr[size] == i) {
            return true;
        }
    }
    return false;
}

void array_clear (int *arr, int n)
{
    while (n) {
        n--;
        arr[n] = 0;
    }
}

void array_print_full (int *arr, int n, char *sep, char *start, char *end)
{
    int i;
    if (start != NULL) {
        printf ("%s", start);
    }

    if (sep != NULL) {
        for (i=0; i<n-1; i++) {
            printf ("%d%s", arr[i], sep);
        }
    } else {
        for (i=0; i<n-1; i++) {
            printf ("%d", arr[i]);
        }
    }

    if (end != NULL) {
        printf ("%d%s", arr[i], end);
    } else {
        printf ("%d", arr[i]);
    }
}

#define INT_ARR_PRINT_CALLBACK(name) void name(int *arr, int n)
typedef INT_ARR_PRINT_CALLBACK(int_arr_print_callback_t);

// NOTE: As long as we use array_print() as an INT_ARR_PRINT_CALLBACK, this needs
// to be a function and not a macro.
void array_print(int *arr, int n) {
    array_print_full(arr,n," ",NULL,"\n");
}

void sorted_array_print (int *arr, int n)
{
    int sorted[n];
    memcpy (sorted, arr, n*sizeof(int));
    int_sort (sorted, n);
    array_print (sorted, n);
}

void print_u64_array (uint64_t *arr, int n)
{
    printf ("[");
    int i;
    for (i=0; i<n-1; i++) {
        printf ("%"PRIu64", ", arr[i]);
    }
    printf ("%"PRIu64"]\n", arr[i]);
}

void print_line (char *sep, int len)
{
    int w = strlen(sep);
    char str[w*len+1];
    int i;
    for (i=0; i<len; i++) {
        memcpy (str+i*w, sep, w);
    }
    str[w*len] = '\0';
    printf ("%s", str);
}

struct ascii_tbl_t {
    char* vert_sep;
    char* hor_sep;
    char* cross;
    int curr_col;
    int num_cols;
};

void print_table_bar (char *hor_sep, char* cross, int *lens, int num_cols)
{
    int i;
    for (i=0; i<num_cols-1; i++) {
        print_line (hor_sep, lens[i]);
        printf ("%s", cross);
    }
    print_line (hor_sep, lens[i]);
    printf ("\n");
}

void ascii_tbl_header (struct ascii_tbl_t *tbl, char **titles, int *widths, int num_cols)
{
    if (tbl->vert_sep == NULL) {
        tbl->vert_sep = " | ";
    }

    if (tbl->hor_sep == NULL) {
        tbl->hor_sep = "-";
    }

    if (tbl->cross == NULL) {
        tbl->cross = "-|-";
    }

    tbl->num_cols = num_cols;
    int i;
    for (i=0; i<num_cols-1; i++) {
        widths[i] = MAX(widths[i], (int)strlen(titles[i]));
        printf ("%*s%s", widths[i], titles[i], tbl->vert_sep);
    }
    widths[i] = MAX(widths[i], (int)strlen(titles[i]));
    printf ("%*s\n", widths[i], titles[i]);
    print_table_bar (tbl->hor_sep, tbl->cross, widths, num_cols);
}

void ascii_tbl_sep (struct ascii_tbl_t *tbl)
{
    if (tbl->curr_col < tbl->num_cols-1) {
        tbl->curr_col++;
        printf ("%s", tbl->vert_sep);
    } else {
        tbl->curr_col = 0;
        printf ("\n");
    }
}

// Function that returns an integer in [min,max] with a uniform distribution.
// NOTE: Remember to call srand() ONCE before using this.
#define rand_int_max(max) rand_int_range(0,max)
uint32_t rand_int_range (uint32_t min, uint32_t max)
{
    assert (max < RAND_MAX);
    assert (min < max);
    uint32_t res;
    uint32_t valid_range = RAND_MAX-(RAND_MAX%(max-min+1));
    do {
        res = rand ();
    } while (res >= valid_range);
    return min + res/(valid_range/(max-min+1));
}

// NOTE: Remember to call srand() ONCE before using this.
void fisher_yates_shuffle (int *arr, int n)
{
    int i;
    for (i=n-1; i>0; i--) {
        int j = rand_int_max (i);
        swap (arr+i, arr+j);
    }
}

// NOTE: Remember to call srand() ONCE before using this.
void init_random_array (int *arr, int size)
{
    int i;
    for (i=0; i<size; i++) {
        arr[i] = i;
    }
    fisher_yates_shuffle (arr, size);
}

// TODO: Make this zero initialized in all cases
typedef struct {
    uint32_t size;
    uint32_t len;
    int *data;
} int_dyn_arr_t;

void int_dyn_arr_init (int_dyn_arr_t *arr, uint32_t size)
{
    arr->data = malloc (size * sizeof (*arr->data));
    if (!arr->data) {
        printf ("Malloc failed.\n");
    }
    arr->len = 0;
    arr->size = size;
}

void int_dyn_arr_destroy (int_dyn_arr_t *arr)
{
    free (arr->data);
    *arr = (int_dyn_arr_t){0};
}

void int_dyn_arr_grow (int_dyn_arr_t *arr, uint32_t new_size)
{
    assert (new_size < UINT32_MAX);
    int *new_data;
    if ((new_data = realloc (arr->data, new_size * sizeof(*arr->data)))) {
        arr->data = new_data;
        arr->size = new_size;
    } else {
        printf ("Error: Realloc failed.\n");
        return;
    }
}

void int_dyn_arr_append (int_dyn_arr_t *arr, int element)
{
    if (arr->size == 0) {
        int_dyn_arr_init (arr, 100);
    }

    if (arr->len == arr->size) {
        int_dyn_arr_grow (arr, 2*arr->size);
    }
    arr->data[arr->len++] = element;
}

void int_dyn_arr_insert_and_swap (int_dyn_arr_t *arr, uint32_t pos, int element)
{
    int_dyn_arr_append (arr, element);
    swap (&arr->data[pos], &arr->data[arr->len-1]);
}

void int_dyn_arr_insert_and_shift (int_dyn_arr_t *arr, uint32_t pos, int element)
{
    assert (pos < arr->len);
    if (arr->len == arr->size) {
        int_dyn_arr_grow (arr, 2*arr->size);
    }
    
    int i;
    for (i=arr->len; i>pos; i--) {
        arr->data[i] = arr->data[i-1];
    }
    arr->data[pos] = element;
    arr->len++;
}

void int_dyn_arr_insert_multiple_and_shift (int_dyn_arr_t *arr, uint32_t pos, int *elements, int len)
{
    assert (pos < arr->len);
    if (arr->len + len > arr->size) {
        int new_size = arr->size;
        while (new_size < arr->len + len) {
            new_size *= 2;
        }
        int_dyn_arr_grow (arr, new_size);
    }

    int i;
    for (i=arr->len+len-1; i>pos+len-1; i--) {
        arr->data[i] = arr->data[i-len];
    }

    for (i=0; i<len; i++) {
        arr->data[pos] = elements[i];
        pos++;
    }
    arr->len += len;
}

void int_dyn_arr_print (int_dyn_arr_t *arr)
{
    array_print (arr->data, arr->len);
}

// This is a simple contiguous buffer, arbitrary sized objects are pushed and
// it grows automatically doubling it's previous size.
//
// NOTE: We use zero is initialization unless min_size is used
// WARNING: Don't create pointers into this structure because they won't work
// after a realloc happens.
#define CONT_BUFF_MIN_SIZE 1024
typedef struct {
    uint32_t min_size;
    uint32_t size;
    uint32_t used;
    void *data;
} cont_buff_t;

void* cont_buff_push (cont_buff_t *buff, int size)
{
    if (buff->used + size >= buff->size) {
        void *new_data;
        if (buff->size == 0) {
            int new_size = MAX (CONT_BUFF_MIN_SIZE, buff->min_size);
            if ((buff->data = malloc (new_size))) {
                buff->size = new_size;
            } else {
                printf ("Malloc failed.\n");
            }
        } else if ((new_data = realloc (buff->data, 2*buff->size))) {
            buff->data = new_data;
            buff->size = 2*buff->size;
        } else {
            printf ("Error: Realloc failed.\n");
            return NULL;
        }
    }

    void *ret = (uint8_t*)buff->data + buff->used;
    buff->used += size;
    return ret;
}

// NOTE: The same cont_buff_t can be used again after this.
void cont_buff_destroy (cont_buff_t *buff)
{
    free (buff->data);
    buff->data = NULL;
    buff->size = 0;
    buff->used = 0;
}

// Memory pool that grows as needed, and can be freed easily.
#define MEM_POOL_MIN_BIN_SIZE 1024
typedef struct {
    uint32_t min_bin_size;
    uint32_t size;
    uint32_t used;
    void *base;

    uint32_t total_used;
    uint32_t num_bins;
} mem_pool_t;

struct _bin_info_t {
    void *base;
    uint32_t size;
    struct _bin_info_t *prev_bin_info;
};

typedef struct _bin_info_t bin_info_t;

enum alloc_opts {
    POOL_UNINITIALIZED,
    POOL_ZERO_INIT
};

// pom == pool or malloc
#define pom_push_struct(pool, type) pom_push_size(pool, sizeof(type))
#define pom_push_array(pool, n, type) pom_push_size(pool, (n)*sizeof(type))
#define pom_push_size(pool, size) (pool==NULL? malloc(size) : mem_pool_push_size(pool,size))

#define mem_pool_push_struct(pool, type) mem_pool_push_size(pool, sizeof(type))
#define mem_pool_push_array(pool, n, type) mem_pool_push_size(pool, (n)*sizeof(type))
#define mem_pool_push_size(pool, size) mem_pool_push_size_full(pool, size, POOL_UNINITIALIZED)
void* mem_pool_push_size_full (mem_pool_t *pool, int size, enum alloc_opts opts)
{
    if (pool->used + size >= pool->size) {
        pool->num_bins++;
        int new_bin_size = MAX (MAX (MEM_POOL_MIN_BIN_SIZE, pool->min_bin_size), size);
        void *new_bin;
        bin_info_t *new_info;
        if ((new_bin = malloc (new_bin_size + sizeof(bin_info_t)))) {
            new_info = (bin_info_t*)((uint8_t*)new_bin + new_bin_size);
        } else {
            printf ("Malloc failed.\n");
            return NULL;
        }

        new_info->base = new_bin;
        new_info->size = new_bin_size;

        if (pool->base == NULL) {
            new_info->prev_bin_info = NULL;
        } else {
            bin_info_t *prev_info = (bin_info_t*)((uint8_t*)pool->base + pool->size);
            new_info->prev_bin_info = prev_info;
        }

        pool->used = 0;
        pool->size = new_bin_size;
        pool->base = new_bin;
    }

    void *ret = (uint8_t*)pool->base + pool->used;
    pool->used += size;
    pool->total_used += size;

    if (opts == POOL_ZERO_INIT) {
        memset (ret, 0, size);
    }
    return ret;
}

// NOTE: Do NOT use _pool_ again after calling this. We don't reset pool because
// it could have been bootstrapped into itself. Reusing is better hendled by
// mem_pool_end_temporary_memory().
void mem_pool_destroy (mem_pool_t *pool)
{
    if (pool->base != NULL) {
        bin_info_t *curr_info = (bin_info_t*)((uint8_t*)pool->base + pool->size);

        void *to_free = curr_info->base;
        curr_info = curr_info->prev_bin_info;
        while (curr_info != NULL) {
            free (to_free);
            to_free = curr_info->base;
            curr_info = curr_info->prev_bin_info;
        }
        free (to_free);
    }
}

uint32_t mem_pool_allocated (mem_pool_t *pool)
{
    uint64_t allocated = 0;
    if (pool->base != NULL) {
        bin_info_t *curr_info = (bin_info_t*)((uint8_t*)pool->base + pool->size);
        while (curr_info != NULL) {
            allocated += curr_info->size + sizeof(bin_info_t);
            curr_info = curr_info->prev_bin_info;
        }
    }
    return allocated;
}

void mem_pool_print (mem_pool_t *pool)
{
    uint32_t allocated = mem_pool_allocated(pool);
    printf ("Allocated: %u bytes\n", allocated);
    printf ("Available: %u bytes\n", pool->size-pool->used);
    printf ("Used: %u bytes (%.2f%%)\n", pool->total_used, ((double)pool->total_used*100)/allocated);
    uint64_t info_size = pool->num_bins*sizeof(bin_info_t);
    printf ("Info: %lu bytes (%.2f%%)\n", info_size, ((double)info_size*100)/allocated);

    // NOTE: This is the amount of space left empty in previous bins
    uint64_t left_empty;
    if (pool->num_bins>0)
        left_empty = (allocated - pool->size - sizeof(bin_info_t))- /*allocated except last bin*/
                     (pool->total_used - pool->used)-               /*total_used except last bin*/
                     (pool->num_bins-1)*sizeof(bin_info_t);         /*size used in bin_info_t*/
    else {
        left_empty = 0;
    }
    printf ("Left empty: %lu bytes (%.2f%%)\n", left_empty, ((double)left_empty*100)/allocated);
    printf ("Bins: %u\n", pool->num_bins);
}

typedef struct {
    mem_pool_t *pool;
    void* base;
    uint32_t used;
    uint32_t total_used;
} mem_pool_temp_marker_t;

mem_pool_temp_marker_t mem_pool_begin_temporary_memory (mem_pool_t *pool)
{
    mem_pool_temp_marker_t res;
    res.total_used = pool->total_used;
    res.used = pool->used;
    res.base = pool->base;
    res.pool = pool;
    return res;
}

void mem_pool_end_temporary_memory (mem_pool_temp_marker_t mrkr)
{
    if (mrkr.base != NULL) {
        bin_info_t *curr_info = (bin_info_t*)((uint8_t*)mrkr.pool->base + mrkr.pool->size);
        while (curr_info->base != mrkr.base) {
            void *to_free = curr_info->base;
            curr_info = curr_info->prev_bin_info;
            free (to_free);
            mrkr.pool->num_bins--;
        }
        mrkr.pool->size = curr_info->size;
        mrkr.pool->base = mrkr.base;
        mrkr.pool->used = mrkr.used;
        mrkr.pool->total_used = mrkr.total_used;
    } else {
        // NOTE: Here mrkr was created before the pool was initialized, so we
        // destroy everything.
        mem_pool_destroy (mrkr.pool);

        // This assumes pool wasn't bootstrapped after taking mrkr
        mrkr.pool->size = 0;
        mrkr.pool->base = NULL;
        mrkr.pool->used = 0;
        mrkr.pool->total_used = 0;
    }
}

// Flatten an array of null terminated strings into a single string allocated
// into _pool_ or heap.
char* collapse_str_arr (char **arr, int n, mem_pool_t *pool)
{
    int len = 0;
    int i;
    for (i=0; i<n; i++) {
        len += strlen (arr[i]) + 1;
    }
    char *res = pom_push_size (pool, len);

    char *ptr = res;
    for (i=0; i<n; i++) {
        ptr = stpcpy (ptr, arr[i]);
        *ptr = ' ';
        ptr++;
    }
    ptr--;
    *ptr = '\0';
    return res;
}

// Expand _str_ as bash would, allocate it in _pool_ or heap. 
// NOTE: $(<cmd>) and `<cmd>` work but don't get too crazy, this spawns /bin/sh
// and a subprocess. Using env vars like $HOME, or ~/ doesn't.
char* sh_expand (char *str, mem_pool_t *pool)
{
    wordexp_t out;
    wordexp (str, &out, 0);
    char *res = collapse_str_arr (out.we_wordv, out.we_wordc, pool);
    wordfree (&out);
    return res;
}

void file_write (int file, void *pos,  size_t size)
{
    if (write (file, pos, size) < size) {
        printf ("Write interrupted\n");
    }
}

void file_read (int file, void *pos,  size_t size)
{
    int bytes_read = read (file, pos, size);
    if (bytes_read < size) {
        printf ("Did not read full file\n"
                "asked for: %ld\n"
                "received: %d\n", size, bytes_read);
    }
}

// NOTE: Returns false if there was an error creating the directory.
bool ensure_dir_exists (char *path)
{
    bool retval = true;
    char *dir_path = sh_expand (path, NULL);

    struct stat st;
    int success = 0;
    if (stat(dir_path, &st) == -1 && errno == ENOENT) {
        success = mkdir (dir_path, 0777);
    }

    if (success == -1) {
        char *expl = strerror (errno);
        printf ("Could not create %s: %s\n", dir_path, expl);
        free (expl);
        retval = false;
    }

    free (dir_path);
    return retval;
}

//////////////////////////////
//
// PATH/FILENAME MANIPULATIONS
//
char* change_extension (mem_pool_t *pool, char *path, char *new_ext)
{
    size_t path_len = strlen(path);
    int i=path_len;
    while (i>0 && path[i-1] != '.') {
        i--;
    }
    char *res = mem_pool_push_size (pool, path_len+strlen(new_ext)+1);
    strcpy (res, path);
    strcpy (&res[i], new_ext);
    return res;
}

char* add_extension (mem_pool_t *pool, char *path, char *new_ext)
{
    size_t path_len = strlen(path);
    char *res = mem_pool_push_size (pool, path_len+strlen(new_ext)+2);
    strcpy (res, path);
    res[path_len++] = '.';
    strcpy (&res[path_len], new_ext);
    return res;
}

// NOTE: Returns a pointer INTO _path_ after the last '.' or NULL if it does not
// have an extension. Hidden files are NOT extensions (/home/.bashrc returns NULL).
char* get_extension (char *path)
{
    size_t path_len = strlen(path);
    int i=path_len-1;
    while (i>=0 && path[i] != '.' && path[i] != '/') {
        i--;
    }

    if (i == -1 || path[i] == '/' || (path[i] == '.' && (i==0 || path[i-1] == '/'))) {
        return NULL;
    }

    return &path[i+1];
}

#ifdef __CURL_CURL_H
#define DOWNLOAD_PROGRESS_CALLBACK(name) \
    int name(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
typedef DOWNLOAD_PROGRESS_CALLBACK(download_progress_callback_t);

// NOTE: This function is blocking.
#define download_file(url,dest) download_file_full(url,dest,NULL,NULL,true)
#define download_file_cbk(url,dest,cbk,data) download_file_full(url,dest,cbk,data,false)
bool download_file_full (char *url, char *dest,
                         download_progress_callback_t callback, void *clientp,
                         bool progress_meter)
{
    CURL *h = curl_easy_init ();
    if (!h) {
        printf ("Error downloading: could not create curl handle.\n");
        return false;
    }

    char *temp_dest = malloc (strlen(dest) + 5 + 1);
    sprintf (temp_dest, "%s%s", dest, ".part");

    //curl_easy_setopt (h, CURLOPT_VERBOSE, 1);
    curl_easy_setopt (h, CURLOPT_WRITEFUNCTION, NULL); // Required on Windows

    if (callback != NULL) {
        curl_easy_setopt (h, CURLOPT_NOPROGRESS, 0);
        curl_easy_setopt (h, CURLOPT_XFERINFOFUNCTION, callback);
        curl_easy_setopt (h, CURLOPT_XFERINFODATA, clientp);
    } else {
        if (progress_meter) {
            printf ("Downloading: %s\n", url);
            curl_easy_setopt (h, CURLOPT_NOPROGRESS, 0);
        }
    }

    FILE *f = fopen (temp_dest, "w");
    curl_easy_setopt (h, CURLOPT_WRITEDATA, f);
    curl_easy_setopt (h, CURLOPT_URL, url);
    CURLcode rc = curl_easy_perform (h);
    fclose (f);
    curl_easy_cleanup (h);

    bool retval = true;
    if (rc != CURLE_OK) {
        printf ("Error downloading: %s\n", curl_easy_strerror (rc));
        unlink (temp_dest);
        retval = false;
    } else {
        rename (temp_dest, dest);
    }

    free (temp_dest);
    return retval;
}
#endif /*__CURL_CURL_H*/

///////////////
//
//  THREADING

//  Handmade busywait mutex for GCC
void start_mutex (volatile int *lock) {
    while (__sync_val_compare_and_swap (lock, 0, 1) == 1) {
        // Busy wait
    }
}

void end_mutex (volatile int *lock) {
    *lock = 0;
}


#define COMMON_H
#endif
