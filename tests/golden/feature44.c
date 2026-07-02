/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

struct sc_add {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void sc_add_rpc(struct sc_add *_p);
static inline int32_t sc_add(int32_t a, int32_t b) {
    struct sc_add _p = {0};
    _p.a = a;
    _p.b = b;
    sc_add_rpc(&_p);
    return _p._;
}

struct sc_kickA {
    int32_t _;
    sc_queue *qb;
    sc_queue *qa;
};
static void sc_kickA_rpc(struct sc_kickA *_p);
static inline int32_t sc_kickA(sc_queue *qb, sc_queue *qa) {
    struct sc_kickA _p = {0};
    _p.qb = qb;
    _p.qa = qa;
    sc_kickA_rpc(&_p);
    return _p._;
}

struct sc_innerB {
    int32_t _;
    sc_queue *qa;
};
static void sc_innerB_rpc(struct sc_innerB *_p);
static inline int32_t sc_innerB(sc_queue *qa) {
    struct sc_innerB _p = {0};
    _p.qa = qa;
    sc_innerB_rpc(&_p);
    return _p._;
}

struct sc_selfish {
    int32_t _;
    sc_queue *qs;
};
static void sc_selfish_rpc(struct sc_selfish *_p);
static inline int32_t sc_selfish(sc_queue *qs) {
    struct sc_selfish _p = {0};
    _p.qs = qs;
    sc_selfish_rpc(&_p);
    return _p._;
}

struct sc_consume1 {
    sc_queue *qq;
};
static void sc_consume1_rpc(struct sc_consume1 *_p);
static inline void sc_consume1(sc_queue *qq) {
    struct sc_consume1 _p = {0};
    _p.qq = qq;
    sc_consume1_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_add_rpc(struct sc_add *_p) {
    /* line 13 */
    _p->_ = _p->a + _p->b; return;
}

static void sc_kickA_rpc(struct sc_kickA *_p) {
    /* line 17 */
    int32_t v = ({ struct sc_innerB _rp = {0}; _rp.qa = _p->qa; _p->qb->sync(_p->qb, (void (*)(void *))sc_innerB_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 18 */
    _p->_ = v; return;
}

static void sc_innerB_rpc(struct sc_innerB *_p) {
    /* line 22 */
    int32_t v = ({ struct sc_add _rp = {0}; _rp.a = 10; _rp.b = 20; _p->qa->sync(_p->qa, (void (*)(void *))sc_add_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 23 */
    _p->_ = v; return;
}

static void sc_selfish_rpc(struct sc_selfish *_p) {
    /* line 27 */
    int32_t v = ({ struct sc_add _rp = {0}; _rp.a = 7; _rp.b = 8; _p->qs->sync(_p->qs, (void (*)(void *))sc_add_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 28 */
    _p->_ = v; return;
}

static void sc_consume1_rpc(struct sc_consume1 *_p) {
    /* line 32 */
    _p->qq->pull(_p->qq, -(1));
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 36 */
    sc_queue *qa = sc_default_queue(NULL);
    /* line 37 */
    sc_queue *qb = sc_default_queue(NULL);
    /* line 38 */
    sc_thread *ta = NULL;
    /* line 39 */
    sc_thread *tb = NULL;
    /* line 40 */
    {
        struct sc_consume1 _rp = {0};
        _rp.qq = qa;
        sc_thread_run((void (*)(void *))sc_consume1_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(ta)), (uint32_t)0, (uint8_t)0);
    }
    /* line 41 */
    {
        struct sc_consume1 _rp = {0};
        _rp.qq = qb;
        sc_thread_run((void (*)(void *))sc_consume1_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(tb)), (uint32_t)0, (uint8_t)0);
    }
    /* line 43 */
    int32_t rc = ({ struct sc_kickA _rp = {0}; _rp.qb = qb; _rp.qa = qa; qa->sync(qa, (void (*)(void *))sc_kickA_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 44 */
    printf("circular substitution: rc=%d\n", rc);
    /* line 46 */
    sc_thread_join(ta);
    /* line 47 */
    sc_thread_join(tb);
    /* line 48 */
    qa->drop(qa);
    /* line 49 */
    qb->drop(qb);
    /* line 52 */
    sc_queue *qs = sc_default_queue(NULL);
    /* line 53 */
    sc_thread *ts = NULL;
    /* line 54 */
    {
        struct sc_consume1 _rp = {0};
        _rp.qq = qs;
        sc_thread_run((void (*)(void *))sc_consume1_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(ts)), (uint32_t)0, (uint8_t)0);
    }
    /* line 56 */
    int32_t rs = ({ struct sc_selfish _rp = {0}; _rp.qs = qs; qs->sync(qs, (void (*)(void *))sc_selfish_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 57 */
    printf("self substitution: rs=%d\n", rs);
    /* line 59 */
    sc_thread_join(ts);
    /* line 60 */
    qs->drop(qs);
    /* line 62 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
