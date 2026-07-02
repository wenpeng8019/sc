/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"
#include "builtins/mt/mt.h"

typedef struct sc_shared sc_shared;

typedef struct sc_shared {
    sc_ring q;
    int64_t sum;
    int32_t got;
} sc_shared;

struct sc_producer {
    sc_shared *s;
    int32_t n;
};
void sc_producer_rpc(struct sc_producer *_p);
static inline void sc_producer(sc_shared *s, int32_t n) {
    struct sc_producer _p = {0};
    _p.s = s;
    _p.n = n;
    sc_producer_rpc(&_p);
}

struct sc_consumer {
    sc_shared *s;
    int32_t n;
};
void sc_consumer_rpc(struct sc_consumer *_p);
static inline void sc_consumer(sc_shared *s, int32_t n) {
    struct sc_consumer _p = {0};
    _p.s = s;
    _p.n = n;
    sc_consumer_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);
void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

void sc_producer_rpc(struct sc_producer *_p) {
    /* line 16 */
    int32_t i = 1;
    /* line 17 */
    for (i = 1; i <= _p->n; i++) {
        /* line 18 */
        while (!(sc_ring_push(&_p->s->q, &(i)))) {
            /* line 19 */
            P_usleep(0);
        }
    }
}

void sc_consumer_rpc(struct sc_consumer *_p) {
    /* line 22 */
    int32_t v = 0;
    /* line 23 */
    while (_p->s->got < _p->n) {
        /* line 24 */
        if (sc_ring_pop(&_p->s->q, &(v))) {
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
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    sc_mod_mt_init();
    /* line 32 */
    int32_t x = 0;
    /* line 33 */
    sc_ring r = {0};
    /* line 34 */
    sc_ring_init(&r, sizeof(x), 3);
    /* line 35 */
    uint64_t _sq0 = sc_ring_cap(&r);
    bool _sq1 = sc_ring_is_empty(&r);
    bool _sq2 = sc_ring_is_full(&r);
    printf("cap=%llu empty=%d full=%d\n", _sq0, _sq1, _sq2);
    /* line 37 */
    x = 10;
    /* line 38 */
    sc_ring_push(&r, &(x));
    /* line 39 */
    x = 20;
    /* line 40 */
    sc_ring_push(&r, &(x));
    /* line 41 */
    x = 30;
    /* line 42 */
    sc_ring_push(&r, &(x));
    /* line 43 */
    x = 40;
    /* line 44 */
    sc_ring_push(&r, &(x));
    /* line 45 */
    uint64_t _sq3 = sc_ring_len(&r);
    bool _sq4 = sc_ring_is_full(&r);
    bool _sq5 = sc_ring_push(&r, &(x));
    printf("len=%llu full=%d push_when_full=%d\n", _sq3, _sq4, _sq5);
    /* line 47 */
    int32_t *pk = ((int32_t*)(sc_ring_peek(&r)));
    /* line 48 */
    printf("peek=%d len_after_peek=%llu\n", pk[0], sc_ring_len(&r));
    /* line 50 */
    int32_t v = 0;
    /* line 51 */
    while (sc_ring_pop(&r, &(v))) {
        /* line 52 */
        printf("pop=%d\n", v);
    }
    /* line 53 */
    bool _sq6 = sc_ring_is_empty(&r);
    uint64_t _sq7 = sc_ring_len(&r);
    bool _sq8 = sc_ring_pop(&r, &(v));
    printf("drained empty=%d len=%llu pop_when_empty=%d\n", _sq6, _sq7, _sq8);
    /* line 57 */
    x = 100;
    /* line 58 */
    sc_ring_push(&r, &(x));
    /* line 59 */
    x = 200;
    /* line 60 */
    sc_ring_push(&r, &(x));
    /* line 61 */
    sc_ring_pop(&r, &(v));
    /* line 62 */
    printf("wrap pop=%d len=%llu\n", v, sc_ring_len(&r));
    /* line 63 */
    sc_ring_drop(&r);
    /* line 66 */
    sc_shared s = {0};
    /* line 67 */
    s.sum = 0;
    /* line 68 */
    s.got = 0;
    /* line 69 */
    sc_ring_init(&s.q, sizeof(x), 16);
    /* line 70 */
    int32_t n = 50000;
    /* line 71 */
    sc_thread *tp = NULL;
    /* line 72 */
    sc_thread *tc = NULL;
    /* line 73 */
    {
        struct sc_producer _rp = {0};
        _rp.s = &(s);
        _rp.n = n;
        sc_thread_run((void (*)(void *))sc_producer_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(tp)), (uint32_t)0, (uint8_t)0);
    }
    /* line 74 */
    {
        struct sc_consumer _rp = {0};
        _rp.s = &(s);
        _rp.n = n;
        sc_thread_run((void (*)(void *))sc_consumer_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(tc)), (uint32_t)0, (uint8_t)0);
    }
    /* line 75 */
    sc_thread_join(tp);
    /* line 76 */
    sc_thread_join(tc);
    /* line 77 */
    int64_t expect = (((int64_t)(n)) * (n + 1)) / 2;
    /* line 78 */
    printf("concurrent got=%d sum=%lld expect=%lld ok=%d\n", s.got, s.sum, expect, s.sum == expect);
    /* line 80 */
    bool _sq9 = sc_ring_is_empty(&s.q);
    uint64_t _sq10 = sc_ring_cap(&s.q);
    printf("final empty=%d cap=%llu\n", _sq9, _sq10);
    /* line 81 */
    sc_ring_drop(&s.q);
    /* line 83 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        sc_mod_adt_drop();
        return _ret;
    }
}
