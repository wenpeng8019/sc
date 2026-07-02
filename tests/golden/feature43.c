/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

struct sc_compute {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void sc_compute_rpc(struct sc_compute *_p);
static inline int32_t sc_compute(int32_t a, int32_t b) {
    struct sc_compute _p = {0};
    _p.a = a;
    _p.b = b;
    sc_compute_rpc(&_p);
    return _p._;
}

struct sc_consume_n {
    sc_queue *qq;
    int32_t n;
};
static void sc_consume_n_rpc(struct sc_consume_n *_p);
static inline void sc_consume_n(sc_queue *qq, int32_t n) {
    struct sc_consume_n _p = {0};
    _p.qq = qq;
    _p.n = n;
    sc_consume_n_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_compute_rpc(struct sc_compute *_p) {
    /* line 24 */
    _p->_ = _p->a + _p->b; return;
}

static void sc_consume_n_rpc(struct sc_consume_n *_p) {
    /* line 28 */
    int32_t i = 0;
    /* line 29 */
    for (i = 0; i < _p->n; i++) {
        /* line 30 */
        _p->qq->pull(_p->qq, -(1));
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 34 */
    sc_queue *q = sc_default_queue(NULL);
    /* line 35 */
    sc_thread *ct = NULL;
    /* line 36 */
    {
        struct sc_consume_n _rp = {0};
        _rp.qq = q;
        _rp.n = 1;
        sc_thread_run((void (*)(void *))sc_consume_n_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(ct)), (uint32_t)0, (uint8_t)0);
    }
    /* line 38 */
    int32_t st1 = -(9);
    /* line 39 */
    int32_t r1 = ({ struct sc_compute _rp = {0}; _rp.a = 3; _rp.b = 4; st1 = q->sync(q, (void (*)(void *))sc_compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)(2000)); _rp._; });
    /* line 40 */
    printf("ok path: r=%d st=%d\n", r1, st1);
    /* line 41 */
    sc_thread_join(ct);
    /* line 44 */
    int32_t st2 = -(9);
    /* line 45 */
    int32_t r2 = ({ struct sc_compute _rp = {0}; _rp.a = 10; _rp.b = 20; st2 = q->sync(q, (void (*)(void *))sc_compute_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)(50)); _rp._; });
    /* line 46 */
    printf("timeout path: r=%d st=%d\n", r2, st2);
    /* line 48 */
    q->drop(q);
    /* line 49 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
