/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/async/async.h"

typedef struct sc_sess sc_sess;

typedef enum { /* base: int32_t */
    sc_conn,
    sc_data,
    sc_term
} sc_future_id;

typedef struct sc_sess {
    char *name;
    int32_t seq;
} sc_sess;

static int32_t sc_async_proc(sc_future_id id, sc_future *f);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_async_init(void); void sc_mod_async_drop(void);

static inline sc_future *sc_future__new(void) {
    sc_future *_p = (sc_future *)sc_alloc(sizeof(sc_future));
    if (_p) {
        memset(_p, 0, sizeof(sc_future));
        sc_future_init(_p);
    }
    return _p;
}

static inline sc_future *sc_future__new_tagged(int _id, void *_ctx) {
    sc_future *_p = sc_future__new();
    if (_p) { _p->id = _id; _p->ctx = _ctx; }
    return _p;
}

static int32_t sc_async_proc(sc_future_id id, sc_future *f) {
    /* line 42 */
    int32_t v = (int32_t)(intptr_t)(sc_future_get(f));
    /* line 43 */
    sc_sess *s = ((sc_sess*)(sc_future_ctx(f)));
    /* line 44 */
    switch (id) {
        case sc_conn:
        {
            /* line 46 */
            printf("派发 conn[%s#%d]: v=%d\n", s->name, s->seq, v);
            break;
        }
        case sc_data:
        {
            /* line 48 */
            printf("派发 data[%s#%d]: v=%d\n", s->name, s->seq, v);
            break;
        }
        case sc_term:
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
    SC_CONSOLE_UTF8();
    sc_mod_async_init();
    /* line 55 */
    sc_async_init();
    /* line 58 */
    sc_sess s1 = {"alpha", 1};
    /* line 59 */
    sc_sess s2 = {"beta", 2};
    /* line 63 */
    sc_future *c = sc_future__new_tagged(sc_conn, &(s1));
    /* line 64 */
    sc_future_done(c, (void *)(intptr_t)(7));
    /* line 65 */
    sc_future *d1 = sc_future__new_tagged(sc_data, &(s2));
    /* line 66 */
    sc_future_done(d1, (void *)(intptr_t)(42));
    /* line 67 */
    sc_future *d2 = sc_future__new_tagged(sc_data, &(s1));
    /* line 68 */
    sc_future_done(d2, (void *)(intptr_t)(43));
    /* line 69 */
    sc_future *x = sc_future__new_tagged(sc_term, &(s2));
    /* line 70 */
    sc_future_done(x, (void *)(intptr_t)(0));
    /* line 73 */
    sc_async_loop(sc_async_proc);
    /* line 75 */
    sc_async_final();
    /* line 76 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        return _ret;
    }
}
