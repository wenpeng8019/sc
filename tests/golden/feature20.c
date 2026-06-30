/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/async/async.h"

typedef struct sess sess;

typedef enum { /* base: int32_t */
    conn,
    data,
    term
} future_id;

typedef struct sess {
    char *name;
    int32_t seq;
} sess;

static int32_t async_proc(future_id id, future *f);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_async_init(void); void sc_mod_async_drop(void);

static inline future *future__new(void) {
    future *_p = (future *)sc_alloc(sizeof(future));
    if (_p) {
        memset(_p, 0, sizeof(future));
        future_init(_p);
    }
    return _p;
}

static inline future *future__new_tagged(int _id, void *_ctx) {
    future *_p = future__new();
    if (_p) { _p->id = _id; _p->ctx = _ctx; }
    return _p;
}

static int32_t async_proc(future_id id, future *f) {
    /* line 42 */
    int32_t v = ((int32_t)(future_get(f)));
    /* line 43 */
    sess *s = ((sess*)(future_ctx(f)));
    /* line 44 */
    switch (id) {
        case conn:
        {
            /* line 46 */
            printf("派发 conn[%s#%d]: v=%d\n", s->name, s->seq, v);
            break;
        }
        case data:
        {
            /* line 48 */
            printf("派发 data[%s#%d]: v=%d\n", s->name, s->seq, v);
            break;
        }
        case term:
        {
            /* line 50 */
            printf("派发 term[%s#%d]: v=%d（终止事件，停循环）\n", s->name, s->seq, v);
            /* line 51 */
            return -(1);
            break;
        }
    }
    /* line 52 */
    return 0;
}

int32_t main(void) {
    sc_mod_async_init();
    /* line 55 */
    async_init();
    /* line 58 */
    sess s1 = {"alpha", 1};
    /* line 59 */
    sess s2 = {"beta", 2};
    /* line 63 */
    future *c = future__new_tagged(conn, &(s1));
    /* line 64 */
    future_done(c, (void *)(intptr_t)(7));
    /* line 65 */
    future *d1 = future__new_tagged(data, &(s2));
    /* line 66 */
    future_done(d1, (void *)(intptr_t)(42));
    /* line 67 */
    future *d2 = future__new_tagged(data, &(s1));
    /* line 68 */
    future_done(d2, (void *)(intptr_t)(43));
    /* line 69 */
    future *x = future__new_tagged(term, &(s2));
    /* line 70 */
    future_done(x, (void *)(intptr_t)(0));
    /* line 73 */
    async_loop(async_proc);
    /* line 75 */
    async_final();
    /* line 76 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        return _ret;
    }
}
