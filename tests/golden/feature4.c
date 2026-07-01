/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm__Users_wenpeng_dev_c_sc_examples_feature4_lib_sc.h"

typedef struct point point;
typedef struct obj obj;

typedef struct point {
    int32_t x;
    int32_t y;
    int32_t (*op)(point *p, int32_t k);
} point;

static void point_init(point *_this);
static int32_t point_sum(point *_this, int32_t dx, int32_t dy);
static void point_drop(point *_this);
typedef struct obj {
    int32_t id;
} obj;

extern void obj_dump(obj *_this);
extern int32_t obj_calc(obj *_this, int32_t a, int32_t b);
static point g_origin = {0};
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_feature4_lib_init(void); void sc_mod_feature4_lib_drop(void);

static inline point *point__new(void) {
    point *_p = (point *)sc_alloc(sizeof(point));
    if (_p) {
        memset(_p, 0, sizeof(point));
        point_init(_p);
    }
    return _p;
}

static void point_init(point *_this) {
    /* line 18 */
    _this->x = 1;
    /* line 19 */
    _this->y = 2;
}

static int32_t point_sum(point *_this, int32_t dx, int32_t dy) {
    /* line 21 */
    return ((_this->x + _this->y) + dx) + dy;
}

static void point_drop(point *_this) {
    /* line 23 */
    printf("point(%d,%d) dropped\n", _this->x, _this->y);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_feature4_lib_init();
    point_init(&g_origin);
    /* line 42 */
    printf("global: x=%d y=%d\n", g_origin.x, g_origin.y);
    /* line 47 */
    lib_audit();
    /* line 48 */
    lib_audit();
    /* line 52 */
    point pt = {0};
    point_init(&pt);
    /* line 53 */
    printf("init: x=%d y=%d\n", pt.x, pt.y);
    /* line 56 */
    printf("pt.sum(3,4) = %d\n", point_sum(&pt, 3, 4));
    /* line 59 */
    point *pp = &(pt);
    /* line 60 */
    printf("pp->sum(3,4) = %d\n", point_sum(pp, 3, 4));
    /* line 63 */
    point p2 = {0};
    point_init(&p2);
    /* line 64 */
    printf("op is nil (before bind): %d\n", p2.op == NULL);
    /* line 67 */
    point_drop(&p2);
    /* line 72 */
    point *hp = point__new();
    /* line 73 */
    printf("heap: sum() = %d\n", point_sum(hp, 0, 0));
    /* line 74 */
    point_drop(hp);
    /* line 75 */
    free(hp);
    /* line 86 */
    {
        int32_t _ret = 0;
        point_drop(&g_origin);
        sc_mod_feature4_lib_drop();
        return _ret;
    }
}
