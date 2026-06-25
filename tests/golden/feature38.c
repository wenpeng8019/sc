/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

typedef struct acc acc;

typedef struct acc {
    int32_t sum;
    int32_t cnt;
} acc;

struct add {
    acc *a;
    int32_t v;
};
static void add_rpc(struct add *_p);
static inline void add(acc *a, int32_t v) {
    struct add _p = {0};
    _p.a = a;
    _p.v = v;
    add_rpc(&_p);
}

struct tag {
    acc *a;
    char *label;
    int32_t v;
};
static void tag_rpc(struct tag *_p);
static inline void tag(acc *a, char *label, int32_t v) {
    struct tag _p = {0};
    _p.a = a;
    _p.label = label;
    _p.v = v;
    tag_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void add_rpc(struct add *_p) {
    /* line 23 */
    _p->a->sum = (_p->a->sum + _p->v);
    /* line 24 */
    _p->a->cnt = (_p->a->cnt + 1);
}

static void tag_rpc(struct tag *_p) {
    /* line 28 */
    printf("  [%s] += %d\n", _p->label, _p->v);
    /* line 29 */
    _p->a->sum = (_p->a->sum + _p->v);
    /* line 30 */
    _p->a->cnt = (_p->a->cnt + 1);
}

int32_t main(void) {
    sc_mod_mt_init();
    /* line 33 */
    acc a = {0};
    /* line 34 */
    a.sum = 0;
    /* line 35 */
    a.cnt = 0;
    /* line 38 */
    queue *q = default_queue(((struct pool *)-1));
    /* line 41 */
    {
        struct add _rp = {0};
        _rp.a = &(a);
        _rp.v = 10;
        q->post(q, (void (*)(void *))add_rpc, &_rp, sizeof(_rp));
    }
    /* line 42 */
    {
        struct add _rp = {0};
        _rp.a = &(a);
        _rp.v = 20;
        q->post(q, (void (*)(void *))add_rpc, &_rp, sizeof(_rp));
    }
    {
        struct add _rp = {0};
        _rp.a = &(a);
        _rp.v = 30;
        q->post(q, (void (*)(void *))add_rpc, &_rp, sizeof(_rp));
    }
    /* line 43 */
    {
        struct tag _rp = {0};
        _rp.a = &(a);
        _rp.label = "boost";
        _rp.v = 40;
        q->post(q, (void (*)(void *))tag_rpc, &_rp, sizeof(_rp));
    }
    /* line 46 */
    int32_t n = 0;
    /* line 47 */
    while (q->pull(q, 0) > 0) {
        /* line 48 */
        n = (n + 1);
    }
    /* line 49 */
    printf("queue drained: msgs=%d sum=%d cnt=%d\n", n, a.sum, a.cnt);
    /* line 52 */
    q->drop(q);
    /* line 55 */
    queue *q2 = default_queue(NULL);
    /* line 56 */
    {
        struct add _rp = {0};
        _rp.a = &(a);
        _rp.v = 5;
        q2->post(q2, (void (*)(void *))add_rpc, &_rp, sizeof(_rp));
    }
    /* line 57 */
    {
        struct add _rp = {0};
        _rp.a = &(a);
        _rp.v = 7;
        q2->post(q2, (void (*)(void *))add_rpc, &_rp, sizeof(_rp));
    }
    /* line 58 */
    int32_t m = 0;
    /* line 59 */
    while (q2->pull(q2, 0) > 0) {
        /* line 60 */
        m = (m + 1);
    }
    /* line 61 */
    printf("deferred queue: msgs=%d sum=%d cnt=%d\n", m, a.sum, a.cnt);
    /* line 62 */
    q2->drop(q2);
    /* line 64 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
