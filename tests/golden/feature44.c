/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

struct add {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void add_rpc(struct add *_p);
static inline int32_t add(int32_t a, int32_t b) {
    struct add _p = {0};
    _p.a = a;
    _p.b = b;
    add_rpc(&_p);
    return _p._;
}

struct kickA {
    int32_t _;
    queue *qb;
    queue *qa;
};
static void kickA_rpc(struct kickA *_p);
static inline int32_t kickA(queue *qb, queue *qa) {
    struct kickA _p = {0};
    _p.qb = qb;
    _p.qa = qa;
    kickA_rpc(&_p);
    return _p._;
}

struct innerB {
    int32_t _;
    queue *qa;
};
static void innerB_rpc(struct innerB *_p);
static inline int32_t innerB(queue *qa) {
    struct innerB _p = {0};
    _p.qa = qa;
    innerB_rpc(&_p);
    return _p._;
}

struct selfish {
    int32_t _;
    queue *qs;
};
static void selfish_rpc(struct selfish *_p);
static inline int32_t selfish(queue *qs) {
    struct selfish _p = {0};
    _p.qs = qs;
    selfish_rpc(&_p);
    return _p._;
}

struct consume1 {
    queue *qq;
};
static void consume1_rpc(struct consume1 *_p);
static inline void consume1(queue *qq) {
    struct consume1 _p = {0};
    _p.qq = qq;
    consume1_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void add_rpc(struct add *_p) {
    /* line 13 */
    _p->_ = _p->a + _p->b; return;
}

static void kickA_rpc(struct kickA *_p) {
    /* line 17 */
    int32_t v = ({ struct innerB _rp = {0}; _rp.qa = _p->qa; _p->qb->sync(_p->qb, (void (*)(void *))innerB_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 18 */
    _p->_ = v; return;
}

static void innerB_rpc(struct innerB *_p) {
    /* line 22 */
    int32_t v = ({ struct add _rp = {0}; _rp.a = 10; _rp.b = 20; _p->qa->sync(_p->qa, (void (*)(void *))add_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 23 */
    _p->_ = v; return;
}

static void selfish_rpc(struct selfish *_p) {
    /* line 27 */
    int32_t v = ({ struct add _rp = {0}; _rp.a = 7; _rp.b = 8; _p->qs->sync(_p->qs, (void (*)(void *))add_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 28 */
    _p->_ = v; return;
}

static void consume1_rpc(struct consume1 *_p) {
    /* line 32 */
    _p->qq->pull(_p->qq, -(1));
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 36 */
    queue *qa = default_queue(NULL);
    /* line 37 */
    queue *qb = default_queue(NULL);
    /* line 38 */
    thread *ta = NULL;
    /* line 39 */
    thread *tb = NULL;
    /* line 40 */
    {
        struct consume1 _rp = {0};
        _rp.qq = qa;
        thread_run((void (*)(void *))consume1_rpc, &_rp, sizeof(_rp), (thread **)(&(ta)), (uint32_t)0, (uint8_t)0);
    }
    /* line 41 */
    {
        struct consume1 _rp = {0};
        _rp.qq = qb;
        thread_run((void (*)(void *))consume1_rpc, &_rp, sizeof(_rp), (thread **)(&(tb)), (uint32_t)0, (uint8_t)0);
    }
    /* line 43 */
    int32_t rc = ({ struct kickA _rp = {0}; _rp.qb = qb; _rp.qa = qa; qa->sync(qa, (void (*)(void *))kickA_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 44 */
    printf("circular substitution: rc=%d\n", rc);
    /* line 46 */
    thread_join(ta);
    /* line 47 */
    thread_join(tb);
    /* line 48 */
    qa->drop(qa);
    /* line 49 */
    qb->drop(qb);
    /* line 52 */
    queue *qs = default_queue(NULL);
    /* line 53 */
    thread *ts = NULL;
    /* line 54 */
    {
        struct consume1 _rp = {0};
        _rp.qq = qs;
        thread_run((void (*)(void *))consume1_rpc, &_rp, sizeof(_rp), (thread **)(&(ts)), (uint32_t)0, (uint8_t)0);
    }
    /* line 56 */
    int32_t rs = ({ struct selfish _rp = {0}; _rp.qs = qs; qs->sync(qs, (void (*)(void *))selfish_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 57 */
    printf("self substitution: rs=%d\n", rs);
    /* line 59 */
    thread_join(ts);
    /* line 60 */
    qs->drop(qs);
    /* line 62 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
