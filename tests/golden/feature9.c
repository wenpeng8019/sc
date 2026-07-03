/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"
#include "builtins/sys/sys.h"

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
void sc_mod_sys_init(void); void sc_mod_sys_drop(void);

static void sc_work_rpc(struct sc_work *_p) {
    /* line 30 */
    int32_t i = 0;
    /* line 31 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 32 */
        sc_mutex_lock(&_p->c->mu);
        /* line 33 */
        _p->c->n = (_p->c->n + 1);
        /* line 34 */
        sc_mutex_unlock(&_p->c->mu);
    }
}

static void sc_note_rpc(struct sc_note *_p) {
    /* line 38 */
    printf("detached note: tag=%d\n", _p->tag);
}

static void sc_bump_rpc(struct sc_bump *_p) {
    /* line 44 */
    int32_t i = 0;
    /* line 45 */
    for (i = 0; i < _p->rounds; i++) {
        /* line 46 */
        sc_hits = (sc_hits + 1);
    }
    /* line 47 */
    if (sc_hits == _p->rounds) {
        /* line 48 */
        sc_mutex_lock(&_p->c->mu);
        /* line 49 */
        _p->c->n = (_p->c->n + 1);
        /* line 50 */
        sc_mutex_unlock(&_p->c->mu);
    }
}

static int32_t sc_next_id(void) {
    /* line 54 */
    static TLS int32_t id = 100;
    /* line 55 */
    id++;
    /* line 56 */
    return id;
}

static void sc_ping_rpc(struct sc_ping *_p) {
    /* line 66 */
    sc_mutex_lock(&_p->s->mu);
    /* line 67 */
    _p->s->ready = 1;
    /* line 68 */
    sc_cond_one(&_p->s->cv);
    /* line 69 */
    sc_mutex_unlock(&_p->s->mu);
}

static void sc_bwork_rpc(struct sc_bwork *_p) {
    /* line 80 */
    sc_mutex_lock(&_p->b->mu);
    /* line 81 */
    _p->b->arrived = (_p->b->arrived + 1);
    /* line 82 */
    sc_mutex_unlock(&_p->b->mu);
    /* line 83 */
    if (sc_barrier_wait(&_p->b->bar)) {
        /* line 84 */
        sc_mutex_lock(&_p->b->mu);
        /* line 85 */
        _p->b->serial = (_p->b->serial + 1);
        /* line 86 */
        sc_mutex_unlock(&_p->b->mu);
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    sc_mod_sys_init();
    /* line 89 */
    sc_ctx c = {0};
    /* line 90 */
    c.n = 0;
    /* line 91 */
    sc_mutex_init(&c.mu);
    /* line 94 */
    sc_thread *t1 = NULL;
    /* line 95 */
    sc_thread *t2 = NULL;
    /* line 96 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(t1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 97 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(t2)), (uint32_t)(262144), (uint8_t)(5));
    }
    /* line 98 */
    printf("t1 id set: %d\n", t1 != NULL);
    /* line 99 */
    sc_thread_join(t1);
    /* line 100 */
    sc_thread_join(t2);
    /* line 101 */
    printf("threads done: n=%d\n", c.n);
    /* line 104 */
    {
        struct sc_note _rp = {0};
        _rp.tag = 7;
        sc_thread_run((void (*)(void *))sc_note_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 105 */
    sc_usleep(50000);
    /* line 108 */
    if (sc_mutex_try_lock(&c.mu)) {
        /* line 109 */
        printf("try_lock ok\n");
        /* line 110 */
        sc_mutex_unlock(&c.mu);
    }
    /* line 113 */
    sc_next_id();
    /* line 114 */
    sc_next_id();
    /* line 115 */
    printf("tls id=%d\n", sc_next_id());
    /* line 118 */
    c.n = 0;
    /* line 119 */
    sc_thread *b1 = NULL;
    /* line 120 */
    sc_thread *b2 = NULL;
    /* line 121 */
    {
        struct sc_bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 10000;
        sc_thread_run((void (*)(void *))sc_bump_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(b1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 122 */
    {
        struct sc_bump _rp = {0};
        _rp.c = &(c);
        _rp.rounds = 20000;
        sc_thread_run((void (*)(void *))sc_bump_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(b2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 123 */
    sc_thread_join(b1);
    /* line 124 */
    sc_thread_join(b2);
    /* line 125 */
    printf("tls threads ok: %d\n", c.n);
    /* line 127 */
    sc_mutex_drop(&c.mu);
    /* line 130 */
    sc_sig s = {0};
    /* line 131 */
    s.ready = 0;
    /* line 132 */
    sc_mutex_init(&s.mu);
    /* line 133 */
    sc_cond_init(&s.cv);
    /* line 134 */
    {
        struct sc_ping _rp = {0};
        _rp.s = &(s);
        sc_thread_run((void (*)(void *))sc_ping_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 135 */
    sc_mutex_lock(&s.mu);
    /* line 136 */
    while (s.ready == 0) {
        /* line 137 */
        sc_cond_wait(&s.cv, &(s.mu), 0, 0);
    }
    /* line 138 */
    sc_mutex_unlock(&s.mu);
    /* line 139 */
    printf("cond wait ok: ready=%d\n", s.ready);
    /* line 142 */
    sc_mutex_lock(&s.mu);
    /* line 143 */
    sc_cond_wait(&s.cv, &(s.mu), 5000000, 0);
    /* line 144 */
    sc_mutex_unlock(&s.mu);
    /* line 145 */
    printf("cond timeout ok\n");
    /* line 147 */
    sc_cond_drop(&s.cv);
    /* line 148 */
    sc_mutex_drop(&s.mu);
    /* line 151 */
    sc_ctx c2 = {0};
    /* line 152 */
    c2.n = 0;
    /* line 153 */
    sc_mutex_init(&c2.mu);
    /* line 154 */
    sc_pool *p = sc_default_pool(4);
    /* line 155 */
    int32_t k = 0;
    /* line 156 */
    for (k = 0; k < 8; k++) {
        /* line 157 */
        {
            struct sc_work _rp = {0};
            _rp.c = &(c2);
            _rp.rounds = 1000;
            p->run(p, (void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp));
        }
    }
    /* line 158 */
    p->join(p);
    /* line 159 */
    printf("pool done: n=%d\n", c2.n);
    /* line 160 */
    {
        struct sc_work _rp = {0};
        _rp.c = &(c2);
        _rp.rounds = 1000;
        p->run(p, (void (*)(void *))sc_work_rpc, &_rp, sizeof(_rp));
    }
    /* line 161 */
    p->drop(p);
    /* line 162 */
    printf("pool drop: n=%d\n", c2.n);
    /* line 163 */
    sc_mutex_drop(&c2.mu);
    /* line 166 */
    sc_bctx bc = {0};
    /* line 167 */
    bc.arrived = 0;
    /* line 168 */
    bc.serial = 0;
    /* line 169 */
    sc_mutex_init(&bc.mu);
    /* line 170 */
    sc_barrier_init(&bc.bar, 3);
    /* line 171 */
    sc_thread *bt1 = NULL;
    /* line 172 */
    sc_thread *bt2 = NULL;
    /* line 173 */
    sc_thread *bt3 = NULL;
    /* line 174 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt1)), (uint32_t)0, (uint8_t)0);
    }
    /* line 175 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt2)), (uint32_t)0, (uint8_t)0);
    }
    /* line 176 */
    {
        struct sc_bwork _rp = {0};
        _rp.b = &(bc);
        sc_thread_run((void (*)(void *))sc_bwork_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(bt3)), (uint32_t)0, (uint8_t)0);
    }
    /* line 177 */
    sc_thread_join(bt1);
    /* line 178 */
    sc_thread_join(bt2);
    /* line 179 */
    sc_thread_join(bt3);
    /* line 180 */
    printf("barrier ok: arrived=%d serial=%d\n", bc.arrived, bc.serial);
    /* line 181 */
    sc_barrier_drop(&bc.bar);
    /* line 182 */
    sc_mutex_drop(&bc.mu);
    /* line 183 */
    {
        int32_t _ret = 0;
        sc_mod_sys_drop();
        sc_mod_mt_drop();
        return _ret;
    }
}
