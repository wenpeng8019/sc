/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

typedef struct sc_acc sc_acc;

typedef struct sc_acc {
    sc_mutex mu;
    int32_t sum;
    int32_t cnt;
} sc_acc;

struct sc_add {
    sc_acc *a;
    int32_t v;
};
static void sc_add_rpc(struct sc_add *_p);
static inline void sc_add(sc_acc *a, int32_t v) {
    struct sc_add _p = {0};
    _p.a = a;
    _p.v = v;
    sc_add_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_add_rpc(struct sc_add *_p) {
    /* line 23 */
    sc_mutex_lock(&_p->a->mu);
    /* line 24 */
    _p->a->sum = (_p->a->sum + _p->v);
    /* line 25 */
    _p->a->cnt = (_p->a->cnt + 1);
    /* line 26 */
    sc_mutex_unlock(&_p->a->mu);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 29 */
    sc_acc a = {0};
    /* line 30 */
    a.sum = 0;
    /* line 31 */
    a.cnt = 0;
    /* line 32 */
    sc_mutex_init(&a.mu);
    /* line 35 */
    sc_pool *p = sc_default_pool(4);
    /* line 36 */
    sc_queue *q = sc_default_queue(p);
    /* line 39 */
    int32_t i = 0;
    /* line 40 */
    for (i = 1; i <= 100; i++) {
        /* line 41 */
        {
            struct sc_add _rp = {0};
            _rp.a = &(a);
            _rp.v = i;
            q->post(q, (void (*)(void *))sc_add_rpc, &_rp, sizeof(_rp), 0, 0);
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
