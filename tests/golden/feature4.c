/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

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
    void (*dump)();
    int32_t (*calc)(int32_t a, int32_t b);
} obj;

extern void obj_dump(obj *_this);
extern int32_t obj_calc(obj *_this, int32_t a, int32_t b);

static inline point *point__new(void) {
    point *_p = (point *)malloc(sizeof(point));
    if (_p) {
        memset(_p, 0, sizeof(point));
        point_init(_p);
    }
    return _p;
}

static void point_init(point *_this) {
    /* line 20 */
    _this->x = 1;
    /* line 21 */
    _this->y = 2;
}

static int32_t point_sum(point *_this, int32_t dx, int32_t dy) {
    /* line 23 */
    return ((_this->x + _this->y) + dx) + dy;
}

static void point_drop(point *_this) {
    /* line 25 */
    printf("point(%d,%d) dropped\n", _this->x, _this->y);
}

int32_t main(void) {
    /* line 40 */
    point pt = {0};
    point_init(&pt);
    /* line 41 */
    printf("init: x=%d y=%d\n", pt.x, pt.y);
    /* line 44 */
    printf("pt.sum(3,4) = %d\n", point_sum(&pt, 3, 4));
    /* line 47 */
    point *pp = &(pt);
    /* line 48 */
    printf("pp->sum(3,4) = %d\n", point_sum(pp, 3, 4));
    /* line 51 */
    point p2 = {0};
    point_init(&p2);
    /* line 52 */
    printf("op is nil (before bind): %d\n", p2.op == NULL);
    /* line 55 */
    point_drop(&p2);
    /* line 60 */
    point *hp = point__new();
    /* line 61 */
    printf("heap: sum() = %d\n", point_sum(hp, 0, 0));
    /* line 62 */
    point_drop(hp);
    /* line 63 */
    free(hp);
    /* line 74 */
    return 0;
}
