/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;

typedef struct point {
    int32_t x;
    int32_t y;
    int32_t (*op)(point *p, int32_t k);
} point;

static void point_init(point *_this);
static int32_t point_sum(point *_this, int32_t dx, int32_t dy);
static int32_t point_scale(point *p, int32_t k);
static int32_t add3(int32_t a, int32_t b, int32_t c);
static int32_t desc(char *s, point pt);
struct job {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void job_rpc(struct job *_p);
static inline int32_t job(int32_t a, int32_t b) {
    struct job _p = {0};
    _p.a = a;
    _p.b = b;
    job_rpc(&_p);
    return _p._;
}


static inline point *point__new(void) {
    point *_p = (point *)malloc(sizeof(point));
    if (_p) {
        memset(_p, 0, sizeof(point));
        point_init(_p);
    }
    return _p;
}

static void point_init(point *_this) {
    /* line 13 */
    _this->x = 1;
    /* line 14 */
    _this->y = 2;
}

static int32_t point_sum(point *_this, int32_t dx, int32_t dy) {
    /* line 16 */
    return ((_this->x + _this->y) + dx) + dy;
}

static int32_t point_scale(point *p, int32_t k) {
    /* line 22 */
    return (p->x + p->y) * k;
}

static int32_t add3(int32_t a, int32_t b, int32_t c) {
    /* line 26 */
    return (a + b) + c;
}

static int32_t desc(char *s, point pt) {
    /* line 30 */
    if (s == NULL) {
        /* line 31 */
        return pt.x + pt.y;
    }
    /* line 32 */
    return 100;
}

static void job_rpc(struct job *_p) {
    /* line 35 */
    _p->_ = _p->a + _p->b; return;
}

int32_t main(void) {
    /* line 39 */
    int64_t big = 300;
    /* line 40 */
    int32_t small = ((int32_t)(big));
    /* line 41 */
    printf("cast assign: %d\n", small);
    /* line 42 */
    char *buf = ((char*)(malloc(8)));
    /* line 43 */
    free(((void*)(buf)));
    /* line 44 */
    double f = 3.75;
    /* line 45 */
    printf("cast arg: %d\n", ((int32_t)(small + f)));
    /* line 47 */
    point pt = {0};
    point_init(&pt);
    /* line 48 */
    void *pv = &(pt);
    /* line 49 */
    printf("paren cast deref: %d\n", ((point*)(pv))->x);
    /* line 52 */
    printf("add3(7) = %d\n", add3(7, 0, 0));
    /* line 53 */
    printf("add3(1,2) = %d\n", add3(1, 2, 0));
    /* line 54 */
    printf("desc() = %d\n", desc(NULL, (point){0}));
    /* line 55 */
    printf("pt.sum(10) = %d\n", point_sum(&pt, 10, 0));
    /* line 56 */
    pt.op = point_scale;
    /* line 57 */
    printf("pt.op(&pt) = %d\n", pt.op(&(pt), 0));
    /* line 58 */
    printf("job(5) = %d\n", job(5, 0));
    /* line 61 */
    printf("init: x=%d y=%d\n", pt.x, pt.y);
    /* line 62 */
    point *pp = &(pt);
    /* line 63 */
    printf("pp->sum(3,4) = %d\n", point_sum(pp, 3, 4));
    /* line 64 */
    point *hp = point__new();
    /* line 65 */
    printf("heap: sum() = %d\n", point_sum(hp, 0, 0));
    /* line 66 */
    free(((void*)(hp)));
    /* line 67 */
    return 0;
}
