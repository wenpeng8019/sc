/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

struct compute {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void compute_rpc(struct compute *_p);
static inline int32_t compute(int32_t a, int32_t b) {
    struct compute _p = {0};
    _p.a = a;
    _p.b = b;
    compute_rpc(&_p);
    return _p._;
}

struct consume_n {
    queue *qq;
    int32_t n;
};
static void consume_n_rpc(struct consume_n *_p);
static inline void consume_n(queue *qq, int32_t n) {
    struct consume_n _p = {0};
    _p.qq = qq;
    _p.n = n;
    consume_n_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void compute_rpc(struct compute *_p) {
    /* line 16 */
    _p->_ = _p->a + _p->b; return;
}

static void consume_n_rpc(struct consume_n *_p) {
    /* line 20 */
    int32_t i = 0;
    /* line 21 */
    for (i = 0; i < _p->n; i++) {
        /* line 22 */
        _p->qq->pull(_p->qq, -(1));
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 26 */
    pool *p = default_pool(2);
    /* line 27 */
    queue *q = default_queue(p);
    /* line 29 */
    int32_t r1 = ({ struct compute _rp = {0}; _rp.a = 3; _rp.b = 4; q->sync(q, (void (*)(void *))compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 30 */
    int32_t r2 = ({ struct compute _rp = {0}; _rp.a = 100; _rp.b = 23; q->sync(q, (void (*)(void *))compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 31 */
    printf("pool sync: r1=%d r2=%d\n", r1, r2);
    /* line 33 */
    q->drop(q);
    /* line 34 */
    p->drop(p);
    /* line 37 */
    queue *q2 = default_queue(NULL);
    /* line 38 */
    thread *ct = NULL;
    /* line 39 */
    {
        struct consume_n _rp = {0};
        _rp.qq = q2;
        _rp.n = 2;
        thread_run((void (*)(void *))consume_n_rpc, &_rp, sizeof(_rp), (thread **)(&(ct)), (uint32_t)0, (uint8_t)0);
    }
    /* line 41 */
    int32_t s1 = ({ struct compute _rp = {0}; _rp.a = 10; _rp.b = 20; q2->sync(q2, (void (*)(void *))compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 42 */
    int32_t s2 = ({ struct compute _rp = {0}; _rp.a = 5; _rp.b = 6; q2->sync(q2, (void (*)(void *))compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 43 */
    printf("thread sync: s1=%d s2=%d\n", s1, s2);
    /* line 45 */
    thread_join(ct);
    /* line 46 */
    q2->drop(q2);
    /* line 48 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
