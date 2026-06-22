/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "platform.h"
#include "builtins/m/m.h"

typedef struct ctx ctx;
typedef struct sig sig;
typedef struct bctx bctx;

typedef struct pool pool;
extern uint8_t pool_run(pool *, void (*)(void *), const void *, size_t);

typedef struct ctx {
    mutex mu;
    int32_t n;
} ctx;

struct work {
    ctx *c;
    int32_t rounds;
};
static void work_rpc(struct work *_p);
static inline void work(ctx *c, int32_t rounds) {
    struct work _p = {0};
    _p.c = c;
    _p.rounds = rounds;
    work_rpc(&_p);
}

struct note {
    int32_t tag;
};
static void note_rpc(struct note *_p);
static inline void note(int32_t tag) {
    struct note _p = {0};
    _p.tag = tag;
    note_rpc(&_p);
}

static TLS int32_t hits = 0;
struct bump {
    ctx *c;
    int32_t rounds;
};
static void bump_rpc(struct bump *_p);
static inline void bump(ctx *c, int32_t rounds) {
    struct bump _p = {0};
    _p.c = c;
    _p.rounds = rounds;
    bump_rpc(&_p);
}

static int32_t next_id(void);
typedef struct sig {
    mutex mu;
    cond cv;
    int32_t ready;
} sig;

struct ping {
    sig *s;
};
static void ping_rpc(struct ping *_p);
static inline void ping(sig *s) {
    struct ping _p = {0};
    _p.s = s;
    ping_rpc(&_p);
}

typedef struct bctx {
    barrier bar;
    mutex mu;
    int32_t arrived;
    int32_t serial;
} bctx;

struct bwork {
    bctx *b;
};
static void bwork_rpc(struct bwork *_p);
static inline void bwork(bctx *b) {
    struct bwork _p = {0};
    _p.b = b;
    bwork_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_m_init(void); void sc_mod_m_drop(void);

static void work_rpc(struct work *_p) {
    /* line 31 */
    int32_t i = 0;
    /* line 32 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 33 */
        mutex_lock(&_p->c->mu);
        /* line 34 */
        _p->c->n = (_p->c->n + 1);
        /* line 35 */
        mutex_unlock(&_p->c->mu);
    }
}

static void note_rpc(struct note *_p) {
    /* line 39 */
    printf("detached note: tag=%d\n", _p->tag);
}

static void bump_rpc(struct bump *_p) {
    /* line 45 */
    int32_t i = 0;
    /* line 46 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 47 */
        hits = (hits + 1);
    }
    /* line 48 */
    if (hits == _p->rounds) {
        /* line 49 */
        mutex_lock(&_p->c->mu);
        /* line 50 */
        _p->c->n = (_p->c->n + 1);
        /* line 51 */
        mutex_unlock(&_p->c->mu);
    }
}

static int32_t next_id(void) {
    /* line 55 */
    static TLS int32_t id = 100;
    /* line 56 */
    id++;
    /* line 57 */
    return id;
}

static void ping_rpc(struct ping *_p) {
    /* line 67 */
    mutex_lock(&_p->s->mu);
    /* line 68 */
    _p->s->ready = 1;
    /* line 69 */
    cond_one(&_p->s->cv);
    /* line 70 */
    mutex_unlock(&_p->s->mu);
}

static void bwork_rpc(struct bwork *_p) {
    /* line 81 */
    mutex_lock(&_p->b->mu);
    /* line 82 */
    _p->b->arrived = (_p->b->arrived + 1);
    /* line 83 */
    mutex_unlock(&_p->b->mu);
    /* line 84 */
    if (barrier_wait(&_p->b->bar)) {
        /* line 85 */
        mutex_lock(&_p->b->mu);
        /* line 86 */
        _p->b->serial = (_p->b->serial + 1);
        /* line 87 */
        mutex_unlock(&_p->b->mu);
    }
}

int32_t main(void) {
    sc_mod_m_init();
    /* line 90 */
    ctx c = {0};
    /* line 91 */
    c.n = 0;
    /* line 92 */
    mutex_init(&c.mu);
    /* line 95 */
    thread *t1 = NULL;
    /* line 96 */
    thread *t2 = NULL;
    /* line 97 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t1)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 98 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t2)), (uint32_t)262144u, (uint8_t)5u);
    }
    /* line 99 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 100 */
    thread_join(t1);
    /* line 101 */
    thread_join(t2);
    /* line 102 */
    printf("threads done: n=%d\n", c.n);
    /* line 105 */
    {
        struct note _rp = {0};
        _rp.tag = 7;
        thread_run((void (*)(void *))note_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0u, (uint8_t)0u);
    }
    /* line 106 */
    P_usleep(50000);
    /* line 109 */
    if (mutex_try_lock(&c.mu)) {
        /* line 110 */
        printf("try_lock ok\n");
        /* line 111 */
        mutex_unlock(&c.mu);
    }
    /* line 114 */
    next_id();
    /* line 115 */
    next_id();
    /* line 116 */
    printf("tls id=%d\n", next_id());
    /* line 119 */
    c.n = 0;
    /* line 120 */
    thread *b1 = NULL;
    /* line 121 */
    thread *b2 = NULL;
    /* line 122 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b1)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 123 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b2)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 124 */
    thread_join(b1);
    /* line 125 */
    thread_join(b2);
    /* line 126 */
    printf("tls threads ok: %d\n", c.n);
    /* line 128 */
    mutex_drop(&c.mu);
    /* line 131 */
    sig s = {0};
    /* line 132 */
    s.ready = 0;
    /* line 133 */
    mutex_init(&s.mu);
    /* line 134 */
    cond_init(&s.cv);
    /* line 135 */
    {
        struct ping _rp = {0};
        _rp.s = &(s);
        thread_run((void (*)(void *))ping_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0u, (uint8_t)0u);
    }
    /* line 136 */
    mutex_lock(&s.mu);
    /* line 137 */
    while (s.ready == 0) {
        /* line 138 */
        cond_wait(&s.cv, &(s.mu), 0, 0);
    }
    /* line 139 */
    mutex_unlock(&s.mu);
    /* line 140 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 143 */
    mutex_lock(&s.mu);
    /* line 144 */
    cond_wait(&s.cv, &(s.mu), 5000000, 0);
    /* line 145 */
    mutex_unlock(&s.mu);
    /* line 146 */
    printf("cond timeout ok\n");
    /* line 148 */
    cond_drop(&s.cv);
    /* line 149 */
    mutex_drop(&s.mu);
    /* line 152 */
    ctx c2 = {0};
    /* line 153 */
    c2.n = 0;
    /* line 154 */
    mutex_init(&c2.mu);
    /* line 155 */
    pool p = {0};
    /* line 156 */
    pool_init(&p, 4);
    /* line 157 */
    int32_t k = 0;
    /* line 158 */
    for (k = 0; k < 8; k++) {
        /* line 159 */
        {
            struct work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 160 */
    pool_join(&p);
    /* line 161 */
    printf("pool done: n=%d\n", c2.n);
    /* line 162 */
    {
        struct work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
    }
    /* line 163 */
    pool_drop(&p);
    /* line 164 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 165 */
    mutex_drop(&c2.mu);
    /* line 168 */
    bctx bc = {0};
    /* line 169 */
    bc.arrived = 0;
    /* line 170 */
    bc.serial = 0;
    /* line 171 */
    mutex_init(&bc.mu);
    /* line 172 */
    barrier_init(&bc.bar, 3);
    /* line 173 */
    thread *bt1 = NULL;
    /* line 174 */
    thread *bt2 = NULL;
    /* line 175 */
    thread *bt3 = NULL;
    /* line 176 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt1)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 177 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt2)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 178 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt3)), (uint32_t)0u, (uint8_t)0u);
    }
    /* line 179 */
    thread_join(bt1);
    /* line 180 */
    thread_join(bt2);
    /* line 181 */
    thread_join(bt3);
    /* line 182 */
    printf("barrier ok: arrived=%d serial=%d\n", bc.arrived, bc.serial);
    /* line 183 */
    barrier_drop(&bc.bar);
    /* line 184 */
    mutex_drop(&bc.mu);
    /* line 185 */
    {
        int32_t _ret = 0;
        sc_mod_m_drop();
        return _ret;
    }
}
