/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

typedef struct acc acc;

typedef struct acc {
    mutex mu;
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

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void add_rpc(struct add *_p) {
    /* line 23 */
    mutex_lock(&_p->a->mu);
    /* line 24 */
    _p->a->sum = (_p->a->sum + _p->v);
    /* line 25 */
    _p->a->cnt = (_p->a->cnt + 1);
    /* line 26 */
    mutex_unlock(&_p->a->mu);
}

int32_t main(void) {
    sc_mod_mt_init();
    /* line 29 */
    acc a = {0};
    /* line 30 */
    a.sum = 0;
    /* line 31 */
    a.cnt = 0;
    /* line 32 */
    mutex_init(&a.mu);
    /* line 35 */
    pool *p = default_pool(4);
    /* line 36 */
    queue *q = default_queue(p);
    /* line 39 */
    int32_t i = 0;
    /* line 40 */
    for (i = 1; i <= 100; i++) {
        /* line 41 */
        {
            struct add _rp = {0};
            _rp.a = &(a);
            _rp.v = i;
            q->post(q, (void (*)(void *))add_rpc, &_rp, sizeof(_rp), 0, 0);
        }
    }
    /* line 43 */
    p->join(p);
    /* line 44 */
    printf("pool queue: sum=%d cnt=%d\n", a.sum, a.cnt);
    /* line 46 */
    q->drop(q);
    /* line 47 */
    p->drop(p);
    /* line 49 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
