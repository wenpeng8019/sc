/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

struct sc_serve {
    int32_t _;
    int32_t tag;
    int32_t info;
};
static void sc_serve_rpc(struct sc_serve *_p);
static inline int32_t sc_serve(int32_t tag, int32_t info) {
    struct sc_serve _p = {0};
    _p.tag = tag;
    _p.info = info;
    sc_serve_rpc(&_p);
    return _p._;
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_serve_rpc(struct sc_serve *_p) {
    /* line 23 */
    printf("  served tag=%d (%d)\n", _p->tag, _p->info);
    /* line 24 */
    _p->_ = _p->tag; return;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 29 */
    sc_queue *q = sc_default_queue(NULL);
    /* line 30 */
    sc_promise *a1 = q->async(q, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 1, .info = 1}, sizeof(struct sc_serve), (int32_t)(1), (int64_t)0);
    /* line 31 */
    sc_promise *a2 = q->async(q, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 2, .info = 5}, sizeof(struct sc_serve), (int32_t)(5), (int64_t)0);
    /* line 32 */
    sc_promise *a3 = q->async(q, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 3, .info = 3}, sizeof(struct sc_serve), (int32_t)(3), (int64_t)0);
    /* line 33 */
    printf("priority order (expect tag 2,3,1):\n");
    /* line 34 */
    q->pull(q, -(1));
    /* line 35 */
    q->pull(q, -(1));
    /* line 36 */
    q->pull(q, -(1));
    /* line 37 */
    a1->wait(a1);
    /* line 38 */
    a2->wait(a2);
    /* line 39 */
    a3->wait(a3);
    /* line 40 */
    a1->drop(a1);
    /* line 41 */
    a2->drop(a2);
    /* line 42 */
    a3->drop(a3);
    /* line 43 */
    q->drop(q);
    /* line 46 */
    sc_queue *q2 = sc_default_queue(NULL);
    /* line 47 */
    sc_promise *d1 = q2->async(q2, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 100, .info = 60}, sizeof(struct sc_serve), (int32_t)0, (int64_t)(60));
    /* line 48 */
    sc_promise *d2 = q2->async(q2, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 200, .info = 20}, sizeof(struct sc_serve), (int32_t)0, (int64_t)(20));
    /* line 49 */
    sc_promise *d3 = q2->async(q2, (void (*)(void *))sc_serve_rpc, &(struct sc_serve){.tag = 300, .info = 40}, sizeof(struct sc_serve), (int32_t)0, (int64_t)(40));
    /* line 50 */
    printf("delay order (expect tag 200,300,100):\n");
    /* line 51 */
    q2->pull(q2, -(1));
    /* line 52 */
    q2->pull(q2, -(1));
    /* line 53 */
    q2->pull(q2, -(1));
    /* line 54 */
    d1->wait(d1);
    /* line 55 */
    d2->wait(d2);
    /* line 56 */
    d3->wait(d3);
    /* line 57 */
    d1->drop(d1);
    /* line 58 */
    d2->drop(d2);
    /* line 59 */
    d3->drop(d3);
    /* line 60 */
    q2->drop(q2);
    /* line 62 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
