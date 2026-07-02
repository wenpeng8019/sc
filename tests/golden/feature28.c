/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_Rect sc_Rect;
typedef struct sc_Shape sc_Shape;
typedef struct sc_Result sc_Result;

typedef struct sc_Rect {
    float w;
    float h;
} sc_Rect;

typedef struct sc_Shape {
    enum { sc_Shape__Empty, sc_Shape__Circle, sc_Shape__Rect } tag;
    union {
        float Circle;
        sc_Rect Rect;
    } u;
} sc_Shape;

static float sc_area(sc_Shape s);
typedef struct sc_Result {
    enum { sc_Result__Ok, sc_Result__Err } tag;
    union {
        int32_t Ok;
        int32_t Err;
    } u;
} sc_Result;

static sc_Result sc_safe_div(int32_t a, int32_t b);
static int32_t sc_unwrap_or(sc_Result r, int32_t fallback);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static float sc_area(sc_Shape s) {
    /* line 29 */
    sc_Shape _case0 = s;
    switch (_case0.tag) {
        case sc_Shape__Empty:
        {
            /* line 31 */
            return 0.0;
            break;
        }
        case sc_Shape__Circle:
        {
            float r = _case0.u.Circle;
            /* line 33 */
            return (3.14159 * r) * r;
            break;
        }
        case sc_Shape__Rect:
        {
            sc_Rect box = _case0.u.Rect;
            /* line 35 */
            return box.w * box.h;
            break;
        }
    }
}

static sc_Result sc_safe_div(int32_t a, int32_t b) {
    /* line 41 */
    if (b == 0) {
        /* line 42 */
        return ((sc_Result){ .tag = sc_Result__Err, .u.Err = -(1) });
    }
    /* line 43 */
    return ((sc_Result){ .tag = sc_Result__Ok, .u.Ok = a / b });
}

static int32_t sc_unwrap_or(sc_Result r, int32_t fallback) {
    /* line 47 */
    sc_Result _case1 = r;
    switch (_case1.tag) {
        case sc_Result__Ok:
        {
            int32_t v = _case1.u.Ok;
            /* line 49 */
            return v;
            break;
        }
        default:
        {
            /* line 51 */
            return fallback;
            break;
        }
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 54 */
    sc_Shape a = ((sc_Shape){ .tag = sc_Shape__Circle, .u.Circle = 2.0 });
    /* line 55 */
    sc_Shape b = ((sc_Shape){ .tag = sc_Shape__Empty });
    /* line 56 */
    sc_Rect rc = {.w = 3.0, .h = 4.0};
    /* line 57 */
    sc_Shape c = ((sc_Shape){ .tag = sc_Shape__Rect, .u.Rect = rc });
    /* line 58 */
    printf("circle area = %.2f\n", sc_area(a));
    /* line 59 */
    printf("empty area  = %.2f\n", sc_area(b));
    /* line 60 */
    printf("rect area   = %.2f\n", sc_area(c));
    /* line 62 */
    sc_Result r1 = sc_safe_div(10, 2);
    /* line 63 */
    sc_Result r2 = sc_safe_div(10, 0);
    /* line 64 */
    printf("10/2 = %d\n", sc_unwrap_or(r1, -(999)));
    /* line 65 */
    printf("10/0 = %d (fallback)\n", sc_unwrap_or(r2, -(999)));
    /* line 66 */
    return 0;
}
