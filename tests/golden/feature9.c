/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

typedef struct ctx ctx;
typedef struct sig sig;
typedef struct bctx bctx;

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


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void work_rpc(struct work *_p) {
    /* line 29 */
    int32_t i = 0;
    /* line 30 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 31 */
        mutex_lock(&_p->c->mu);
        /* line 32 */
        _p->c->n = (_p->c->n + 1);
        /* line 33 */
        mutex_unlock(&_p->c->mu);
    }
}

static void note_rpc(struct note *_p) {
    /* line 37 */
    printf("detached note: tag=%d\n", _p->tag);
}

static void bump_rpc(struct bump *_p) {
    /* line 43 */
    int32_t i = 0;
    /* line 44 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 45 */
        hits = (hits + 1);
    }
    /* line 46 */
    if (hits == _p->rounds) {
        /* line 47 */
        mutex_lock(&_p->c->mu);
        /* line 48 */
        _p->c->n = (_p->c->n + 1);
        /* line 49 */
        mutex_unlock(&_p->c->mu);
    }
}

static int32_t next_id(void) {
    /* line 53 */
    static TLS int32_t id = 100;
    /* line 54 */
    id++;
    /* line 55 */
    return id;
}

static void ping_rpc(struct ping *_p) {
    /* line 65 */
    mutex_lock(&_p->s->mu);
    /* line 66 */
    _p->s->ready = 1;
    /* line 67 */
    cond_one(&_p->s->cv);
    /* line 68 */
    mutex_unlock(&_p->s->mu);
}

static void bwork_rpc(struct bwork *_p) {
    /* line 79 */
    mutex_lock(&_p->b->mu);
    /* line 80 */
    _p->b->arrived = (_p->b->arrived + 1);
    /* line 81 */
    mutex_unlock(&_p->b->mu);
    /* line 82 */
    if (barrier_wait(&_p->b->bar)) {
        /* line 83 */
        mutex_lock(&_p->b->mu);
        /* line 84 */
        _p->b->serial = (_p->b->serial + 1);
        /* line 85 */
        mutex_unlock(&_p->b->mu);
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 88 */
    ctx c = {0};
    /* line 89 */
    c.n = 0;
    /* line 90 */
    mutex_init(&c.mu);
    /* line 93 */
    thread *t1 = NULL;
    /* line 94 */
    thread *t2 = NULL;
    /* line 95 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 96 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t2)), (uint32_t)(262144), (uint8_t)(5));
    }
    /* line 97 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 98 */
    thread_join(t1);
    /* line 99 */
    thread_join(t2);
    /* line 100 */
    printf("threads done: n=%d\n", c.n);
    /* line 103 */
    {
        struct note _rp = {0};
        _rp.tag = 7;
        thread_run((void (*)(void *))note_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 104 */
    P_usleep(50000);
    /* line 107 */
    if (mutex_try_lock(&c.mu)) {
        /* line 108 */
        printf("try_lock ok\n");
        /* line 109 */
        mutex_unlock(&c.mu);
    }
    /* line 112 */
    next_id();
    /* line 113 */
    next_id();
    /* line 114 */
    printf("tls id=%d\n", next_id());
    /* line 117 */
    c.n = 0;
    /* line 118 */
    thread *b1 = NULL;
    /* line 119 */
    thread *b2 = NULL;
    /* line 120 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 121 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 122 */
    thread_join(b1);
    /* line 123 */
    thread_join(b2);
    /* line 124 */
    printf("tls threads ok: %d\n", c.n);
    /* line 126 */
    mutex_drop(&c.mu);
    /* line 129 */
    sig s = {0};
    /* line 130 */
    s.ready = 0;
    /* line 131 */
    mutex_init(&s.mu);
    /* line 132 */
    cond_init(&s.cv);
    /* line 133 */
    {
        struct ping _rp = {0};
        _rp.s = &(s);
        thread_run((void (*)(void *))ping_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 134 */
    mutex_lock(&s.mu);
    /* line 135 */
    while (s.ready == 0) {
        /* line 136 */
        cond_wait(&s.cv, &(s.mu), 0, 0);
    }
    /* line 137 */
    mutex_unlock(&s.mu);
    /* line 138 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 141 */
    mutex_lock(&s.mu);
    /* line 142 */
    cond_wait(&s.cv, &(s.mu), 5000000, 0);
    /* line 143 */
    mutex_unlock(&s.mu);
    /* line 144 */
    printf("cond timeout ok\n");
    /* line 146 */
    cond_drop(&s.cv);
    /* line 147 */
    mutex_drop(&s.mu);
    /* line 150 */
    ctx c2 = {0};
    /* line 151 */
    c2.n = 0;
    /* line 152 */
    mutex_init(&c2.mu);
    /* line 153 */
    pool *p = default_pool(4);
    /* line 154 */
    int32_t k = 0;
    /* line 155 */
    for (k = 0; k < 8; k++) {
        /* line 156 */
        {
            struct work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            p->run(p, (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 157 */
    p->join(p);
    /* line 158 */
    printf("pool done: n=%d\n", c2.n);
    /* line 159 */
    {
        struct work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        p->run(p, (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
    }
    /* line 160 */
    p->drop(p);
    /* line 161 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 162 */
    mutex_drop(&c2.mu);
    /* line 165 */
    bctx bc = {0};
    /* line 166 */
    bc.arrived = 0;
    /* line 167 */
    bc.serial = 0;
    /* line 168 */
    mutex_init(&bc.mu);
    /* line 169 */
    barrier_init(&bc.bar, 3);
    /* line 170 */
    thread *bt1 = NULL;
    /* line 171 */
    thread *bt2 = NULL;
    /* line 172 */
    thread *bt3 = NULL;
    /* line 173 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 174 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 175 */
    {
        struct bwork _rp = {0};
        _rp.b = &(bc);
        thread_run((void (*)(void *))bwork_rpc, &_rp, sizeof(_rp), (thread **)(&(bt3)), (uint32_t)0, (uint8_t)0);
    }
    /* line 176 */
    thread_join(bt1);
    /* line 177 */
    thread_join(bt2);
    /* line 178 */
    thread_join(bt3);
    /* line 179 */
    printf("barrier ok: arrived=%d serial=%d\n", bc.arrived, bc.serial);
    /* line 180 */
    barrier_drop(&bc.bar);
    /* line 181 */
    mutex_drop(&bc.mu);
    /* line 182 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
