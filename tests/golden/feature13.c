/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"
#include "builtins/async/async.h"

struct sc_greet {
    char * _;
    sc_future *_ret;
    int _state;
    sc_future *_fut;
    char *name;
    uint32_t ms;
};
static void sc_greet_rpc(struct sc_greet *_p);
static sc_future *sc_greet__async(char *name, uint32_t ms);
struct sc_both {
    int32_t _;
    sc_future *_ret;
    int _state;
    sc_future *_fut;
    char *a;
    char *b;
    char *x;
    char *y;
};
static void sc_both_rpc(struct sc_both *_p);
static sc_future *sc_both__async(char *a, char *b);
struct sc_square_worker {
    sc_future *f;
    int32_t n;
};
static void sc_square_worker_rpc(struct sc_square_worker *_p);
static inline void sc_square_worker(sc_future *f, int32_t n) {
    struct sc_square_worker _p = {0};
    _p.f = f;
    _p.n = n;
    sc_square_worker_rpc(&_p);
}

static sc_future * sc_bg_square(int32_t n);
struct sc_compute {
    int32_t _;
    sc_future *_ret;
    int _state;
    sc_future *_fut;
    int32_t n;
    int32_t a;
    int32_t b;
};
static void sc_compute_rpc(struct sc_compute *_p);
static sc_future *sc_compute__async(int32_t n);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);
void sc_mod_async_init(void); void sc_mod_async_drop(void);

static inline sc_future *sc_future__new(void) {
    sc_future *_p = (sc_future *)sc_alloc(sizeof(sc_future));
    if (_p) {
        memset(_p, 0, sizeof(sc_future));
        sc_future_init(_p);
    }
    return _p;
}

static sc_future *sc_greet__async(char *name, uint32_t ms) {
    struct sc_greet *_p = (struct sc_greet *)calloc(1, sizeof(struct sc_greet));
    _p->_state = 0;
    _p->_ret = sc_future_new();
    _p->name = name;
    _p->ms = ms;
    sc_greet_rpc(_p);
    return _p->_ret;
}

static void sc_greet_rpc(struct sc_greet *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
    }
    _s0: ;
    printf("  [%s] 睡 %u ms...\n", _p->name, _p->ms);
    _p->_fut = sc_delay(_p->ms);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_greet_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    printf("  [%s] 醒来\n", _p->name);
    _p->_ = _p->name;
    { sc_future *_r = _p->_ret; void *_res = (void *)(_p->_); free(_p); sc_future_done(_r, _res); return; }
}

static sc_future *sc_both__async(char *a, char *b) {
    struct sc_both *_p = (struct sc_both *)calloc(1, sizeof(struct sc_both));
    _p->_state = 0;
    _p->_ret = sc_future_new();
    _p->a = a;
    _p->b = b;
    sc_both_rpc(_p);
    return _p->_ret;
}

static void sc_both_rpc(struct sc_both *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
    }
    _s0: ;
    _p->_fut = sc_greet__async(_p->a, 60);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_both_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->x = (char *)sc_future_get(_p->_fut);
    _p->_fut = sc_greet__async(_p->b, 30);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_both_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    _p->y = (char *)sc_future_get(_p->_fut);
    printf("  both 收集: %s + %s\n", _p->x, _p->y);
    _p->_ = 0;
    { sc_future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); sc_future_done(_r, _res); return; }
}

static void sc_square_worker_rpc(struct sc_square_worker *_p) {
    /* line 166 */
    sc_future_done(_p->f, (void *)(intptr_t)(_p->n * _p->n));
}

static sc_future * sc_bg_square(int32_t n) {
    /* line 170 */
    sc_future *f = sc_future__new();
    /* line 171 */
    {
        struct sc_square_worker _rp = {0};
        _rp.f = f;
        _rp.n = n;
        sc_thread_run((void (*)(void *))sc_square_worker_rpc, &_rp, sizeof(_rp), NULL, (uint32_t)0, (uint8_t)0);
    }
    /* line 172 */
    return f;
}

static sc_future *sc_compute__async(int32_t n) {
    struct sc_compute *_p = (struct sc_compute *)calloc(1, sizeof(struct sc_compute));
    _p->_state = 0;
    _p->_ret = sc_future_new();
    _p->n = n;
    sc_compute_rpc(_p);
    return _p->_ret;
}

static void sc_compute_rpc(struct sc_compute *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
    }
    _s0: ;
    _p->_fut = sc_bg_square(_p->n);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_compute_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->a = (int32_t)(intptr_t)sc_future_get(_p->_fut);
    _p->_fut = sc_bg_square(_p->a);
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_compute_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    _p->b = (int32_t)(intptr_t)sc_future_get(_p->_fut);
    printf("  compute: %d -> %d\n", _p->n, _p->b);
    _p->_ = _p->b;
    { sc_future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); sc_future_done(_r, _res); return; }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    sc_mod_async_init();
    /* line 183 */
    sc_async_init();
    /* line 187 */
    sc_future *fa = sc_greet__async("A", 80);
    /* line 188 */
    sc_future *fb = sc_greet__async("B", 30);
    /* line 191 */
    sc_async_loop(NULL);
    /* line 194 */
    printf("fa = %s\n", ((char*)(sc_future_get(fa))));
    /* line 195 */
    printf("fb = %s\n", ((char*)(sc_future_get(fb))));
    /* line 198 */
    sc_future *fc = sc_both__async("X", "Y");
    /* line 199 */
    sc_async_loop(NULL);
    /* line 200 */
    printf("both ret = %d\n", ((int32_t)(sc_future_get(fc))));
    /* line 203 */
    sc_future *fd = sc_compute__async(3);
    /* line 204 */
    sc_async_loop(NULL);
    /* line 205 */
    printf("compute ret = %d\n", ((int32_t)(sc_future_get(fd))));
    /* line 207 */
    sc_async_final();
    /* line 208 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        sc_mod_mt_drop();
        return _ret;
    }
}
