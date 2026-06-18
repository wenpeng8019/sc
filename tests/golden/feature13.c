/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/m/m.h"
#include "builtins/async/async.h"

typedef struct thread thread;
extern uint8_t thread_run(void (*)(void *), const void *, size_t, thread **, uint32_t, uint8_t);
typedef struct pool pool;
extern uint8_t pool_run(pool *, void (*)(void *), const void *, size_t);

struct greet {
    char * _;
    future *_ret;
    int _state;
    future *_fut;
    char *name;
    uint32_t ms;
};
static void greet_rpc(struct greet *_p);
static future *greet__async(char *name, uint32_t ms);
struct both {
    int32_t _;
    future *_ret;
    int _state;
    future *_fut;
    char *a;
    char *b;
    char *x;
    char *y;
};
static void both_rpc(struct both *_p);
static future *both__async(char *a, char *b);
struct square_worker {
    future *f;
    int32_t n;
};
static void square_worker_rpc(struct square_worker *_p);
static inline void square_worker(future *f, int32_t n) {
    struct square_worker _p = {0};
    _p.f = f;
    _p.n = n;
    square_worker_rpc(&_p);
}

static future * bg_square(int32_t n);
struct compute {
    int32_t _;
    future *_ret;
    int _state;
    future *_fut;
    int32_t n;
    int32_t a;
    int32_t b;
};
static void compute_rpc(struct compute *_p);
static future *compute__async(int32_t n);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static inline future *future__new(void) {
    future *_p = (future *)malloc(sizeof(future));
    if (_p) {
        memset(_p, 0, sizeof(future));
        future_init(_p);
    }
    return _p;
}

static future *greet__async(char *name, uint32_t ms) {
    struct greet *_p = (struct greet *)calloc(1, sizeof(struct greet));
    _p->_state = 0;
    _p->_ret = future_new();
    _p->name = name;
    _p->ms = ms;
    greet_rpc(_p);
    return _p->_ret;
}

static void greet_rpc(struct greet *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
    }
    _s0: ;
    printf("  [%s] 睡 %u ms...\n", _p->name, _p->ms);
    _p->_fut = delay(_p->ms);
    if (future_await(_p->_fut, _p, (void (*)(void *))greet_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    printf("  [%s] 醒来\n", _p->name);
    _p->_ = _p->name;
    { future *_r = _p->_ret; void *_res = (void *)(_p->_); free(_p); future_done(_r, _res); return; }
}

static future *both__async(char *a, char *b) {
    struct both *_p = (struct both *)calloc(1, sizeof(struct both));
    _p->_state = 0;
    _p->_ret = future_new();
    _p->a = a;
    _p->b = b;
    both_rpc(_p);
    return _p->_ret;
}

static void both_rpc(struct both *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
    }
    _s0: ;
    _p->_fut = greet__async(_p->a, 60);
    if (future_await(_p->_fut, _p, (void (*)(void *))both_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->x = (char *)future_get(_p->_fut);
    _p->_fut = greet__async(_p->b, 30);
    if (future_await(_p->_fut, _p, (void (*)(void *))both_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    _p->y = (char *)future_get(_p->_fut);
    printf("  both 收集: %s + %s\n", _p->x, _p->y);
    _p->_ = 0;
    { future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); future_done(_r, _res); return; }
}

static void square_worker_rpc(struct square_worker *_p) {
    /* line 167 */
    future_done(_p->f, (void *)(intptr_t)(_p->n * _p->n));
}

static future * bg_square(int32_t n) {
    /* line 171 */
    future *f = future__new();
    /* line 172 */
    {
        struct square_worker _rp = {0};
        _rp.f = f;
        _rp.n = n;
        thread_run((void (*)(void *))square_worker_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0u, (uint8_t)0u);
    }
    /* line 173 */
    return f;
}

static future *compute__async(int32_t n) {
    struct compute *_p = (struct compute *)calloc(1, sizeof(struct compute));
    _p->_state = 0;
    _p->_ret = future_new();
    _p->n = n;
    compute_rpc(_p);
    return _p->_ret;
}

static void compute_rpc(struct compute *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
    }
    _s0: ;
    _p->_fut = bg_square(_p->n);
    if (future_await(_p->_fut, _p, (void (*)(void *))compute_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->a = (int32_t)(intptr_t)future_get(_p->_fut);
    _p->_fut = bg_square(_p->a);
    if (future_await(_p->_fut, _p, (void (*)(void *))compute_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    _p->b = (int32_t)(intptr_t)future_get(_p->_fut);
    printf("  compute: %d -> %d\n", _p->n, _p->b);
    _p->_ = _p->b;
    { future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); future_done(_r, _res); return; }
}

int32_t main(void) {
    /* line 184 */
    async_init();
    /* line 188 */
    future *fa = greet__async("A", 80);
    /* line 189 */
    future *fb = greet__async("B", 30);
    /* line 192 */
    async_loop();
    /* line 195 */
    printf("fa = %s\n", ((char*)(future_get(fa))));
    /* line 196 */
    printf("fb = %s\n", ((char*)(future_get(fb))));
    /* line 199 */
    future *fc = both__async("X", "Y");
    /* line 200 */
    async_loop();
    /* line 201 */
    printf("both ret = %d\n", ((int32_t)(future_get(fc))));
    /* line 204 */
    future *fd = compute__async(3);
    /* line 205 */
    async_loop();
    /* line 206 */
    printf("compute ret = %d\n", ((int32_t)(future_get(fd))));
    /* line 208 */
    async_final();
    /* line 209 */
    return 0;
}
