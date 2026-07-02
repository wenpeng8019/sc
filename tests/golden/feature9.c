/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

typedef struct sc_ctx sc_ctx;
typedef struct sc_sig sc_sig;
typedef struct sc_bctx sc_bctx;

typedef struct sc_ctx {
    sc_mutex mu;
    int32_t n;
} sc_ctx;

struct sc_work {
    sc_ctx *c;
    int32_t rounds;
};
static void sc_work_rpc(struct sc_work *_p);
static inline void sc_work(sc_ctx *c, int32_t rounds) {
    struct sc_work _p = {0};
    _p.c = c;
    _p.rounds = rounds;
    sc_work_rpc(&_p);
}

struct sc_note {
    int32_t tag;
};
static void sc_note_rpc(struct sc_note *_p);
static inline void sc_note(int32_t tag) {
    struct sc_note _p = {0};
    _p.tag = tag;
    sc_note_rpc(&_p);
}

static TLS int32_t sc_hits = 0;
struct sc_bump {
    sc_ctx *c;
    int32_t rounds;
};
static void sc_bump_rpc(struct sc_bump *_p);
static inline void sc_bump(sc_ctx *c, int32_t rounds) {
    struct sc_bump _p = {0};
    _p.c = c;
    _p.rounds = rounds;
    sc_bump_rpc(&_p);
}

static int32_t sc_next_id(void);
typedef struct sc_sig {
    sc_mutex mu;
    sc_cond cv;
    int32_t ready;
} sc_sig;

struct sc_ping {
    sc_sig *s;
};
static void sc_ping_rpc(struct sc_ping *_p);
static inline void sc_ping(sc_sig *s) {
    struct sc_ping _p = {0};
    _p.s = s;
    sc_ping_rpc(&_p);
}

typedef struct sc_bctx {
    sc_barrier bar;
    sc_mutex mu;
    int32_t arrived;
    int32_t serial;
} sc_bctx;

struct sc_bwork {
    sc_bctx *b;
};
static void sc_bwork_rpc(struct sc_bwork *_p);
static inline void sc_bwork(sc_bctx *b) {
    struct sc_bwork _p = {0};
    _p.b = b;
    sc_bwork_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_work_rpc(struct sc_work *_p) {
    /* line 29 */
    int32_t i = 0;
    /* line 30 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 31 */
        sc_mutex_lock(&_p->c->mu);
        /* line 32 */
        _p->c->n = (_p->c->n + 1);
        /* line 33 */
        sc_mutex_unlock(&_p->c->mu);
    }
}

static void sc_note_rpc(struct sc_note *_p) {
    /* line 37 */
    printf("detached note: tag=%d\n", _p->tag);
}

static void sc_bump_rpc(struct sc_bump *_p) {
    /* line 43 */
    int32_t i = 0;
    /* line 44 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 45 */
        sc_hits = (sc_hits + 1);
    }
    /* line 46 */
    if (sc_hits == _p->rounds) {
        /* line 47 */
        sc_mutex_lock(&_p->c->mu);
        /* line 48 */
        _p->c->n = (_p->c->n + 1);
        /* line 49 */
        sc_mutex_unlock(&_p->c->mu);
    }
}

static int32_t sc_next_id(void) {
    /* line 53 */
    static TLS int32_t id = 100;
    /* line 54 */
    id++;
    /* line 55 */
    return id;
}

static void sc_ping_rpc(struct sc_ping *_p) {
    /* line 65 */
    sc_mutex_lock(&_p->s->mu);
    /* line 66 */
    _p->s->ready = 1;
    /* line 67 */
    sc_cond_one(&_p->s->cv);
    /* line 68 */
    sc_mutex_unlock(&_p->s->mu);
}

static void sc_bwork_rpc(struct sc_bwork *_p) {
    /* line 79 */
    sc_mutex_lock(&_p->b->mu);
    /* line 80 */
    _p->b->arrived = (_p->b->arrived + 1);
    /* line 81 */
    sc_mutex_unlock(&_p->b->mu);
    /* line 82 */
    if (sc_barrier_wait(&_p->b->bar)) {
        /* line 83 */
        sc_mutex_lock(&_p->b->mu);
        /* line 84 */
        _p->b->serial = (_p->b->serial + 1);
        /* line 85 */
        sc_mutex_unlock(&_p->b->mu);
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 88 */
    sc_ctx c = {0};
    /* line 89 */
    c.n = 0;
    /* line 90 */
    sc_mutex_init(&c.mu);
    /* line 93 */
    sc_thread *t1 = NULL;
    /* line 94 */
    sc_thread *t2 = NULL;
    /* line 95 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(t1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 96 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(t2)), (uint32_t)(262144), (uint8_t)(5));
    }
    /* line 97 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 98 */
    sc_thread_join(t1);
    /* line 99 */
    sc_thread_join(t2);
    /* line 100 */
    printf("threads done: n=%d\n", c.n);
    /* line 103 */
    {
        struct sc_note _rp = {0};
        _rp.tag = 7;
        sc_thread_run((void (*)(void *))sc_note_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 104 */
    P_usleep(50000);
    /* line 107 */
    if (sc_mutex_try_lock(&c.mu)) {
        /* line 108 */
        printf("try_lock ok\n");
        /* line 109 */
        sc_mutex_unlock(&c.mu);
    }
    /* line 112 */
    sc_next_id();
    /* line 113 */
    sc_next_id();
    /* line 114 */
    printf("tls id=%d\n", sc_next_id());
    /* line 117 */
    c.n = 0;
    /* line 118 */
    sc_thread *b1 = NULL;
    /* line 119 */
    sc_thread *b2 = NULL;
    /* line 120 */
    {
        struct sc_bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_bump_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(b1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 121 */
    {
        struct sc_bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        sc_thread_run((void (*)(void *))sc_bump_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(b2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 122 */
    sc_thread_join(b1);
    /* line 123 */
    sc_thread_join(b2);
    /* line 124 */
    printf("tls threads ok: %d\n", c.n);
    /* line 126 */
    sc_mutex_drop(&c.mu);
    /* line 129 */
    sc_sig s = {0};
    /* line 130 */
    s.ready = 0;
    /* line 131 */
    sc_mutex_init(&s.mu);
    /* line 132 */
    sc_cond_init(&s.cv);
    /* line 133 */
    {
        struct sc_ping _rp = {0};
        _rp.s = &(s);
        sc_thread_run((void (*)(void *))sc_ping_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 134 */
    sc_mutex_lock(&s.mu);
    /* line 135 */
    while (s.ready == 0) {
        /* line 136 */
        sc_cond_wait(&s.cv, &(s.mu), 0, 0);
    }
    /* line 137 */
    sc_mutex_unlock(&s.mu);
    /* line 138 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 141 */
    sc_mutex_lock(&s.mu);
    /* line 142 */
    sc_cond_wait(&s.cv, &(s.mu), 5000000, 0);
    /* line 143 */
    sc_mutex_unlock(&s.mu);
    /* line 144 */
    printf("cond timeout ok\n");
    /* line 146 */
    sc_cond_drop(&s.cv);
    /* line 147 */
    sc_mutex_drop(&s.mu);
    /* line 150 */
    sc_ctx c2 = {0};
    /* line 151 */
    c2.n = 0;
    /* line 152 */
    sc_mutex_init(&c2.mu);
    /* line 153 */
    sc_pool *p = sc_default_pool(4);
    /* line 154 */
    int32_t k = 0;
    /* line 155 */
    for (k = 0; k < 8; k++) {
        /* line 156 */
        {
            struct sc_work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            p->run(p, (void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 157 */
    p->join(p);
    /* line 158 */
    printf("pool done: n=%d\n", c2.n);
    /* line 159 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        p->run(p, (void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp));
    }
    /* line 160 */
    p->drop(p);
    /* line 161 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 162 */
    sc_mutex_drop(&c2.mu);
    /* line 165 */
    sc_bctx bc = {0};
    /* line 166 */
    bc.arrived = 0;
    /* line 167 */
    bc.serial = 0;
    /* line 168 */
    sc_mutex_init(&bc.mu);
    /* line 169 */
    sc_barrier_init(&bc.bar, 3);
    /* line 170 */
    sc_thread *bt1 = NULL;
    /* line 171 */
    sc_thread *bt2 = NULL;
    /* line 172 */
    sc_thread *bt3 = NULL;
    /* line 173 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 174 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 175 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt3)), (uint32_t)0, (uint8_t)0);
    }
    /* line 176 */
    sc_thread_join(bt1);
    /* line 177 */
    sc_thread_join(bt2);
    /* line 178 */
    sc_thread_join(bt3);
    /* line 179 */
    printf("barrier ok: arrived=%d serial=%d\n", bc.arrived, bc.serial);
    /* line 180 */
    sc_barrier_drop(&bc.bar);
    /* line 181 */
    sc_mutex_drop(&bc.mu);
    /* line 182 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
