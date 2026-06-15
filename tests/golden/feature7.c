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
    /* line 25 */
    int32_t i = 0;
    /* line 26 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 27 */
        mutex_lock(&_p->c->mu);
        /* line 28 */
        _p->c->n = (_p->c->n + 1);
        /* line 29 */
        mutex_unlock(&_p->c->mu);
    }
}

static void note_rpc(struct note *_p) {
    /* line 33 */
    printf("detached note: tag=%d\n", _p->tag);
}

static void bump_rpc(struct bump *_p) {
    /* line 39 */
    int32_t i = 0;
    /* line 40 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 41 */
        hits = (hits + 1);
    }
    /* line 42 */
    if (hits == _p->rounds) {
        /* line 43 */
        mutex_lock(&_p->c->mu);
        /* line 44 */
        _p->c->n = (_p->c->n + 1);
        /* line 45 */
        mutex_unlock(&_p->c->mu);
    }
}

static int32_t next_id(void) {
    /* line 49 */
    static TLS int32_t id = 100;
    /* line 50 */
    id++;
    /* line 51 */
    return id;
}

static void ping_rpc(struct ping *_p) {
    /* line 61 */
    mutex_lock(&_p->s->mu);
    /* line 62 */
    _p->s->ready = 1;
    /* line 63 */
    cond_one(&_p->s->cv);
    /* line 64 */
    mutex_unlock(&_p->s->mu);
}

int32_t main(void) {
    /* line 67 */
    ctx c = {0};
    /* line 68 */
    c.n = 0;
    /* line 69 */
    mutex_init(&c.mu);
    /* line 72 */
    thread *t1 = NULL;
    /* line 73 */
    thread *t2 = NULL;
    /* line 74 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t1)));
    }
    /* line 75 */
    {
        struct work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))work_rpc, &_rp, sizeof(_rp), (thread **)(&(t2)));
    }
    /* line 76 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 77 */
    thread_join(t1);
    /* line 78 */
    thread_join(t2);
    /* line 79 */
    printf("threads done: n=%d\n", c.n);
    /* line 82 */
    {
        struct note _rp = {0};
        _rp.tag = 7;
        thread_run((void (*)(void *))note_rpc, &_rp, sizeof(_rp), NULL);
    }
    /* line 83 */
    P_usleep(50000);
    /* line 86 */
    if (mutex_try_lock(&c.mu)) {
        /* line 87 */
        printf("try_lock ok\n");
        /* line 88 */
        mutex_unlock(&c.mu);
    }
    /* line 91 */
    next_id();
    /* line 92 */
    next_id();
    /* line 93 */
    printf("tls id=%d\n", next_id());
    /* line 96 */
    c.n = 0;
    /* line 97 */
    thread *b1 = NULL;
    /* line 98 */
    thread *b2 = NULL;
    /* line 99 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b1)));
    }
    /* line 100 */
    {
        struct bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        thread_run((void (*)(void *))bump_rpc, &_rp, sizeof(_rp), (thread **)(&(b2)));
    }
    /* line 101 */
    thread_join(b1);
    /* line 102 */
    thread_join(b2);
    /* line 103 */
    printf("tls threads ok: %d\n", c.n);
    /* line 105 */
    mutex_drop(&c.mu);
    /* line 108 */
    sig s = {0};
    /* line 109 */
    s.ready = 0;
    /* line 110 */
    mutex_init(&s.mu);
    /* line 111 */
    cond_init(&s.cv);
    /* line 112 */
    {
        struct ping _rp = {0};
        _rp.s = &(s);
        thread_run((void (*)(void *))ping_rpc, &_rp, sizeof(_rp), NULL);
    }
    /* line 113 */
    mutex_lock(&s.mu);
    /* line 114 */
    while (s.ready == 0) {
        /* line 115 */
        cond_wait(&(s.cv), &(s.mu), 0, 0);
    }
    /* line 116 */
    mutex_unlock(&s.mu);
    /* line 117 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 120 */
    mutex_lock(&s.mu);
    /* line 121 */
    cond_wait(&(s.cv), &(s.mu), 5000000, 0);
    /* line 122 */
    mutex_unlock(&s.mu);
    /* line 123 */
    printf("cond timeout ok\n");
    /* line 125 */
    cond_drop(&s.cv);
    /* line 126 */
    mutex_drop(&s.mu);
    /* line 129 */
    ctx c2 = {0};
    /* line 130 */
    c2.n = 0;
    /* line 131 */
    mutex_init(&c2.mu);
    /* line 132 */
    pool p = {0};
    /* line 133 */
    pool_init(&p, 4);
    /* line 134 */
    int32_t k = 0;
    /* line 135 */
    for (k = 0; k < 8; k++) {
        /* line 136 */
        {
            struct work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 137 */
    pool_join(&p);
    /* line 138 */
    printf("pool done: n=%d\n", c2.n);
    /* line 139 */
    {
        struct work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        pool_run(&(p), (void (*)(void *))work_rpc, &_rp, sizeof(_rp));
    }
    /* line 140 */
    pool_drop(&p);
    /* line 141 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 142 */
    mutex_drop(&c2.mu);
    /* line 143 */
    return 0;
}
