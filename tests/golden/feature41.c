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
    /* line 19 */
    _p->_ = _p->a + _p->b; return;
}

static void consume_n_rpc(struct consume_n *_p) {
    /* line 23 */
    int32_t i = 0;
    /* line 24 */
    for (i = 0; i < _p->n; i++) {
        /* line 25 */
        _p->qq->pull(_p->qq, -(1));
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 29 */
    pool *p = default_pool(2);
    /* line 30 */
    queue *q = default_queue(p);
    /* line 33 */
    promise *f1 = q->async(q, (void (*)(void *))compute_rpc, &(struct compute){.a = 3, .b = 4}, sizeof(struct compute), (int32_t)0, (int64_t)0);
    /* line 34 */
    promise *f2 = q->async(q, (void (*)(void *))compute_rpc, &(struct compute){.a = 100, .b = 23}, sizeof(struct compute), (int32_t)0, (int64_t)0);
    /* line 35 */
    int32_t r1 = ((int32_t)(f1->wait(f1)));
    /* line 36 */
    int32_t r2 = ((int32_t)(f2->wait(f2)));
    /* line 37 */
    printf("pool async: r1=%d r2=%d\n", r1, r2);
    /* line 38 */
    f1->drop(f1);
    /* line 39 */
    f2->drop(f2);
    /* line 41 */
    q->drop(q);
    /* line 42 */
    p->drop(p);
    /* line 45 */
    queue *q2 = default_queue(NULL);
    /* line 46 */
    thread *ct = NULL;
    /* line 47 */
    {
        struct consume_n _rp = {0};
        _rp.qq = q2;
        _rp.n = 2;
        thread_run((void (*)(void *))consume_n_rpc, &_rp, sizeof(_rp), (thread **)(&(ct)), (uint32_t)0, (uint8_t)0);
    }
    /* line 49 */
    promise *g1 = q2->async(q2, (void (*)(void *))compute_rpc, &(struct compute){.a = 10, .b = 20}, sizeof(struct compute), (int32_t)0, (int64_t)0);
    /* line 50 */
    promise *g2 = q2->async(q2, (void (*)(void *))compute_rpc, &(struct compute){.a = 5, .b = 6}, sizeof(struct compute), (int32_t)0, (int64_t)0);
    /* line 51 */
    int32_t s1 = ((int32_t)(g1->wait(g1)));
    /* line 52 */
    int32_t s2 = ((int32_t)(g2->wait(g2)));
    /* line 53 */
    printf("thread async: s1=%d s2=%d\n", s1, s2);
    /* line 54 */
    g1->drop(g1);
    /* line 55 */
    g2->drop(g2);
    /* line 57 */
    thread_join(ct);
    /* line 58 */
    q2->drop(q2);
    /* line 60 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
