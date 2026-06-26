/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"
#include "builtins/mt/mt.h"

typedef struct shared shared;

typedef struct shared {
    ring q;
    int64_t sum;
    int32_t got;
} shared;

struct producer {
    shared *s;
    int32_t n;
};
void producer_rpc(struct producer *_p);
static inline void producer(shared *s, int32_t n) {
    struct producer _p = {0};
    _p.s = s;
    _p.n = n;
    producer_rpc(&_p);
}

struct consumer {
    shared *s;
    int32_t n;
};
void consumer_rpc(struct consumer *_p);
static inline void consumer(shared *s, int32_t n) {
    struct consumer _p = {0};
    _p.s = s;
    _p.n = n;
    consumer_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);
void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

void producer_rpc(struct producer *_p) {
    /* line 16 */
    int32_t i = 1;
    /* line 17 */
    for (i = 1; i <= _p->n; i++) {
        /* line 18 */
        while (!(ring_push(&_p->s->q, &(i)))) {
            /* line 19 */
            P_usleep(0);
        }
    }
}

void consumer_rpc(struct consumer *_p) {
    /* line 22 */
    int32_t v = 0;
    /* line 23 */
    while (_p->s->got < _p->n) {
        /* line 24 */
        if (ring_pop(&_p->s->q, &(v))) {
            /* line 25 */
            _p->s->sum = (_p->s->sum + v);
            /* line 26 */
            _p->s->got = (_p->s->got + 1);
        } else {
            /* line 28 */
            P_usleep(0);
        }
    }
}

int32_t main(void) {
    sc_mod_adt_init();
    sc_mod_mt_init();
    /* line 32 */
    int32_t x = 0;
    /* line 33 */
    ring r = {0};
    /* line 34 */
    ring_init(&r, sizeof(x), 3);
    /* line 35 */
    printf("cap=%llu empty=%d full=%d\n", ring_cap(&r), ring_is_empty(&r), ring_is_full(&r));
    /* line 37 */
    x = 10;
    /* line 38 */
    ring_push(&r, &(x));
    /* line 39 */
    x = 20;
    /* line 40 */
    ring_push(&r, &(x));
    /* line 41 */
    x = 30;
    /* line 42 */
    ring_push(&r, &(x));
    /* line 43 */
    x = 40;
    /* line 44 */
    ring_push(&r, &(x));
    /* line 45 */
    printf("len=%llu full=%d push_when_full=%d\n", ring_len(&r), ring_is_full(&r), ring_push(&r, &(x)));
    /* line 47 */
    int32_t *pk = ((int32_t*)(ring_peek(&r)));
    /* line 48 */
    printf("peek=%d len_after_peek=%llu\n", pk[0], ring_len(&r));
    /* line 50 */
    int32_t v = 0;
    /* line 51 */
    while (ring_pop(&r, &(v))) {
        /* line 52 */
        printf("pop=%d\n", v);
    }
    /* line 53 */
    printf("drained empty=%d len=%llu pop_when_empty=%d\n", ring_is_empty(&r), ring_len(&r), ring_pop(&r, &(v)));
    /* line 57 */
    x = 100;
    /* line 58 */
    ring_push(&r, &(x));
    /* line 59 */
    x = 200;
    /* line 60 */
    ring_push(&r, &(x));
    /* line 61 */
    ring_pop(&r, &(v));
    /* line 62 */
    printf("wrap pop=%d len=%llu\n", v, ring_len(&r));
    /* line 63 */
    ring_drop(&r);
    /* line 66 */
    shared s = {0};
    /* line 67 */
    s.sum = 0;
    /* line 68 */
    s.got = 0;
    /* line 69 */
    ring_init(&s.q, sizeof(x), 16);
    /* line 70 */
    int32_t n = 50000;
    /* line 71 */
    thread *tp = NULL;
    /* line 72 */
    thread *tc = NULL;
    /* line 73 */
    {
        struct producer _rp = {0};
        _rp.s = &(s);
        _rp.n = n;
        thread_run((void (*)(void *))producer_rpc, &_rp, sizeof(_rp), (thread **)(&(tp)), (uint32_t)0, (uint8_t)0);
    }
    /* line 74 */
    {
        struct consumer _rp = {0};
        _rp.s = &(s);
        _rp.n = n;
        thread_run((void (*)(void *))consumer_rpc, &_rp, sizeof(_rp), (thread **)(&(tc)), (uint32_t)0, (uint8_t)0);
    }
    /* line 75 */
    thread_join(tp);
    /* line 76 */
    thread_join(tc);
    /* line 77 */
    int64_t expect = (((int64_t)(n)) * (n + 1)) / 2;
    /* line 78 */
    printf("concurrent got=%d sum=%lld expect=%lld ok=%d\n", s.got, s.sum, expect, s.sum == expect);
    /* line 80 */
    printf("final empty=%d cap=%llu\n", ring_is_empty(&s.q), ring_cap(&s.q));
    /* line 81 */
    ring_drop(&s.q);
    /* line 83 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        sc_mod_adt_drop();
        return _ret;
    }
}
