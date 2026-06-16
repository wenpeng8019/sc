/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "platform.h"
#include "builtins/m/m.h"

typedef struct ctx ctx;
typedef struct sig sig;

typedef struct thread thread;
extern uint8_t thread_run(void (*)(void *), const void *, size_t, thread **);
typedef struct pool pool;
extern uint8_t pool_run(pool *, void (*)(void *), const void *, size_t);

typedef struct cond cond;
typedef struct mutex mutex;
extern int32_t cond_wait(cond *, mutex *, uint64_t, uint64_t);

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

int32_t main(void) {
    /* line 73 */
    ctx c = {0};
    /* line 74 */
    c.n = 0;
    /* line 75 */
    mutex_init(&c.mu);
    /* line 78 */
    thread *t1 = NULL;
    /* line 79 */
    thread *t2 = NULL;
    /* line 80 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t1)));
    }
    /* line 81 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t2)));
    }
    /* line 82 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 83 */
    thread_join(t1);
    /* line 84 */
    thread_join(t2);
    /* line 85 */
    printf("threads done: n=%d\n", c.n);
    /* line 88 */
    {
        struct note _rp = {0};
        _rp.tag = 7;
        thread_run((void (*)(void *))note_rpc, &_rp, sizeof(_rp), NULL);
    }
    /* line 89 */
    P_usleep(50000);
    /* line 92 */
    if (mutex_try_lock(&c.mu)) {
        /* line 93 */
        printf("try_lock ok\n");
        /* line 94 */
        mutex_unlock(&c.mu);
    }
    /* line 97 */
    next_id();
    /* line 98 */
    next_id();
    /* line 99 */
    printf("tls id=%d\n", next_id());
    /* line 102 */
    c.n = 0;
    /* line 103 */
    thread *b1 = NULL;
    /* line 104 */
    thread *b2 = NULL;
    /* line 105 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b1)));
    }
    /* line 106 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b2)));
    }
    /* line 107 */
    thread_join(b1);
    /* line 108 */
    thread_join(b2);
    /* line 109 */
    printf("tls threads ok: %d\n", c.n);
    /* line 111 */
    mutex_drop(&c.mu);
    /* line 114 */
    sig s = {0};
    /* line 115 */
    s.ready = 0;
    /* line 116 */
    mutex_init(&s.mu);
    /* line 117 */
    cond_init(&s.cv);
    /* line 118 */
    {
        struct ping _rp = {0};
        _rp.s = &(s);
        thread_run((void (*)(void *))ping_rpc, &_rp, sizeof(_rp), NULL);
    }
    /* line 119 */
    mutex_lock(&s.mu);
    /* line 120 */
    while (s.ready == 0) {
        /* line 121 */
        cond_wait(&(s.cv), &(s.mu), 0, 0);
    }
    /* line 122 */
    mutex_unlock(&s.mu);
    /* line 123 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 126 */
    mutex_lock(&s.mu);
    /* line 127 */
    cond_wait(&(s.cv), &(s.mu), 5000000, 0);
    /* line 128 */
    mutex_unlock(&s.mu);
    /* line 129 */
    printf("cond timeout ok\n");
    /* line 131 */
    cond_drop(&s.cv);
    /* line 132 */
    mutex_drop(&s.mu);
    /* line 135 */
    ctx c2 = {0};
    /* line 136 */
    c2.n = 0;
    /* line 137 */
    mutex_init(&c2.mu);
    /* line 138 */
    pool p = {0};
    /* line 139 */
    pool_init(&p, 4);
    /* line 140 */
    int32_t k = 0;
    /* line 141 */
    for (k = 0; k < 8; k++) {
        /* line 142 */
        {
            struct work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 143 */
    pool_join(&p);
    /* line 144 */
    printf("pool done: n=%d\n", c2.n);
    /* line 145 */
    {
        struct work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
    }
    /* line 146 */
    pool_drop(&p);
    /* line 147 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 148 */
    mutex_drop(&c2.mu);
    /* line 149 */
    return 0;
}
