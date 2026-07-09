/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "scm_examples_feature4_lib_sc.h"

typedef struct sc_point sc_point;
typedef struct sc_obj sc_obj;

typedef struct sc_point {
    int32_t x;
    int32_t y;
    int32_t (*op)(sc_point *p, int32_t k);
} sc_point;

static void sc_point_init(sc_point *_this);
static int32_t sc_point_sum(sc_point *_this, int32_t dx, int32_t dy);
static void sc_point_drop(sc_point *_this);
typedef struct sc_obj {
    int32_t id;
} sc_obj;

extern void sc_obj_dump(sc_obj *_this);
extern int32_t sc_obj_calc(sc_obj *_this, int32_t a, int32_t b);
static sc_point sc_g_origin = {0};
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_feature4_lib_init(void); void sc_mod_feature4_lib_drop(void);

static inline sc_point *sc_point__new(void) {
    sc_point *_p = (sc_point *)sc_alloc(sizeof(sc_point));
    if (_p) {
        memset(_p, 0, sizeof(sc_point));
        sc_point_init(_p);
    }
    return _p;
}

static void sc_point_init(sc_point *_this) {
    /* line 18 */
    _this->x = 1;
    /* line 19 */
    _this->y = 2;
}

static int32_t sc_point_sum(sc_point *_this, int32_t dx, int32_t dy) {
    /* line 21 */
    return ((_this->x + _this->y) + dx) + dy;
}

static void sc_point_drop(sc_point *_this) {
    /* line 23 */
    printf("point(%d,%d) dropped\n", _this->x, _this->y);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_feature4_lib_init();
    sc_point_init(&sc_g_origin);
    /* line 42 */
    printf("global: x=%d y=%d\n", sc_g_origin.x, sc_g_origin.y);
    /* line 47 */
    sc_lib_audit();
    /* line 48 */
    sc_lib_audit();
    /* line 52 */
    sc_point pt = {0};
    sc_point_init(&pt);
    /* line 53 */
    printf("init: x=%d y=%d\n", pt.x, pt.y);
    /* line 56 */
    printf("pt.sum(3,4) = %d\n", sc_point_sum(&pt, 3, 4));
    /* line 59 */
    sc_point *pp = &(pt);
    /* line 60 */
    printf("pp->sum(3,4) = %d\n", sc_point_sum(pp, 3, 4));
    /* line 63 */
    sc_point p2 = {0};
    sc_point_init(&p2);
    /* line 64 */
    printf("op is nil (before bind): %d\n", p2.op == NULL);
    /* line 67 */
    sc_point_drop(&p2);
    /* line 72 */
    sc_point *hp = sc_point__new();
    /* line 73 */
    printf("heap: sum() = %d\n", sc_point_sum(hp, 0, 0));
    /* line 74 */
    sc_point_drop(hp);
    /* line 75 */
    free(hp);
    /* line 86 */
    {
        int32_t _ret = 0;
        sc_point_drop(&sc_g_origin);
        sc_mod_feature4_lib_drop();
        return _ret;
    }
}
