/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Rect Rect;
typedef struct Shape Shape;
typedef struct Result Result;

typedef struct Rect {
    float w;
    float h;
} Rect;

typedef struct Shape {
    enum { Shape__Empty, Shape__Circle, Shape__Rect } tag;
    union {
        float Circle;
        Rect Rect;
    } u;
} Shape;

static float area(Shape s);
typedef struct Result {
    enum { Result__Ok, Result__Err } tag;
    union {
        int32_t Ok;
        int32_t Err;
    } u;
} Result;

static Result safe_div(int32_t a, int32_t b);
static int32_t unwrap_or(Result r, int32_t fallback);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static float area(Shape s) {
    /* line 31 */
    Shape _case0 = s;
    switch (_case0.tag) {
        case Shape__Empty:
        {
            /* line 33 */
            return 0.0;
            break;
        }
        case Shape__Circle:
        {
            float r = _case0.u.Circle;
            /* line 35 */
            return (3.14159 * r) * r;
            break;
        }
        case Shape__Rect:
        {
            Rect box = _case0.u.Rect;
            /* line 37 */
            return box.w * box.h;
            break;
        }
    }
}

static Result safe_div(int32_t a, int32_t b) {
    /* line 43 */
    if (b == 0) {
        /* line 44 */
        return ((Result){ .tag = Result__Err, .u.Err = -(1) });
    }
    /* line 45 */
    return ((Result){ .tag = Result__Ok, .u.Ok = a / b });
}

static int32_t unwrap_or(Result r, int32_t fallback) {
    /* line 49 */
    Result _case1 = r;
    switch (_case1.tag) {
        case Result__Ok:
        {
            int32_t v = _case1.u.Ok;
            /* line 51 */
            return v;
            break;
        }
        default:
        {
            /* line 53 */
            return fallback;
            break;
        }
    }
}

int32_t main(void) {
    /* line 56 */
    Shape a = ((Shape){ .tag = Shape__Circle, .u.Circle = 2.0 });
    /* line 57 */
    Shape b = ((Shape){ .tag = Shape__Empty });
    /* line 58 */
    Rect rc = {.w = 3.0, .h = 4.0};
    /* line 59 */
    Shape c = ((Shape){ .tag = Shape__Rect, .u.Rect = rc });
    /* line 60 */
    printf("circle area = %.2f\n", area(a));
    /* line 61 */
    printf("empty area  = %.2f\n", area(b));
    /* line 62 */
    printf("rect area   = %.2f\n", area(c));
    /* line 64 */
    Result r1 = safe_div(10, 2);
    /* line 65 */
    Result r2 = safe_div(10, 0);
    /* line 66 */
    printf("10/2 = %d\n", unwrap_or(r1, -(999)));
    /* line 67 */
    printf("10/0 = %d (fallback)\n", unwrap_or(r2, -(999)));
    /* line 68 */
    return 0;
}
