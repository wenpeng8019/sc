/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/io/io.h"
#include "builtins/async/async.h"

struct on_pair {
    int32_t _;
    int32_t a;
    int32_t b;
};
static void on_pair_rpc(struct on_pair *_p);
static inline int32_t on_pair(int32_t a, int32_t b) {
    struct on_pair _p = {0};
    _p.a = a;
    _p.b = b;
    on_pair_rpc(&_p);
    return _p._;
}

static int32_t sync_roundtrip(char *path);
struct async_read {
    int32_t _;
    future *_ret;
    int _state;
    future *_fut;
    com *rc;
    int32_t n;
};
static void async_read_rpc(struct async_read *_p);
static future *async_read__async(com *rc);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_io_init(void); void sc_mod_io_drop(void);
void sc_mod_async_init(void); void sc_mod_async_drop(void);

static void on_pair_rpc(struct on_pair *_p) {
    /* line 14 */
    printf("  rpc 反序列化: a=%d b=%d\n", _p->a, _p->b);
    /* line 15 */
    _p->_ = 0; return;
}

static int32_t sync_roundtrip(char *path) {
    /* line 19 */
    com *wc = file(path, false, 0, 1);
    /* line 20 */
    if (wc == NULL) {
        /* line 21 */
        printf("E: 打开写端口失败\n");
        /* line 22 */
        return 1;
    }
    /* line 23 */
    {
        {
            struct on_pair _rp = {0};
            _rp.a = 7;
            _rp.b = 9;
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); wc->write(wc, (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); wc->write(wc, (void *)&(_rp.b), &_scsz);
        }
    }
    /* line 24 */
    int32_t tag = 42;
    /* line 25 */
    {
        uint32_t _scsz;
        _scsz = sizeof(tag); wc->write(wc, (void *)&(tag), &_scsz);
    }
    /* line 26 */
    char msg[8] = "hi-file";
    /* line 27 */
    {
        uint32_t _scsz;
        _scsz = sizeof(msg); wc->write(wc, (void *)&(msg), &_scsz);
    }
    /* line 29 */
    com *rc = file(path, false, 1, 0);
    /* line 30 */
    if (rc == NULL) {
        /* line 31 */
        printf("E: 打开读端口失败\n");
        /* line 32 */
        return 1;
    }
    /* line 33 */
    {
        {
            struct on_pair _rp = {0};
            uint32_t _scsz;
            _scsz = sizeof(_rp.a); rc->read(rc, (void *)&(_rp.a), &_scsz);
            _scsz = sizeof(_rp.b); rc->read(rc, (void *)&(_rp.b), &_scsz);
            on_pair_rpc(&_rp);
        }
    }
    /* line 34 */
    int32_t t = 0;
    /* line 35 */
    {
        uint32_t _scsz;
        _scsz = sizeof(t); rc->read(rc, (void *)&(t), &_scsz);
    }
    /* line 36 */
    char buf[8];
    /* line 37 */
    {
        uint32_t _scsz;
        _scsz = sizeof(buf); rc->read(rc, (void *)&(buf), &_scsz);
    }
    /* line 38 */
    printf("  同步读回: tag=%d msg=%s\n", t, &(buf[0]));
    /* line 39 */
    wc->close(wc);
    /* line 40 */
    rc->close(rc);
    /* line 41 */
    return 0;
}

static future *async_read__async(com *rc) {
    struct async_read *_p = (struct async_read *)calloc(1, sizeof(struct async_read));
    _p->_state = 0;
    _p->_ret = future_new();
    _p->rc = rc;
    async_read_rpc(_p);
    return _p->_ret;
}

static void async_read_rpc(struct async_read *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
    }
    _s0: ;
    _p->n = 0;
    _p->_fut = com_read_async(_p->rc, (void *)&(_p->n), sizeof(_p->n));
    if (future_await(_p->_fut, _p, (void (*)(void *))async_read_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    printf("  异步读回: n=%d\n", _p->n);
    _p->_ = 0;
    { future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); free(_p); future_done(_r, _res); return; }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_io_init();
    sc_mod_async_init();
    /* line 51 */
    char *path = "/tmp/sc_feature22.bin";
    /* line 52 */
    printf("== 同步往返 ==\n");
    /* line 53 */
    if (sync_roundtrip(path) != 0) {
        /* line 54 */
        {
            int32_t _ret = 1;
            sc_mod_async_drop();
            sc_mod_io_drop();
            return _ret;
        }
    }
    /* line 56 */
    printf("== 异步读 ==\n");
    /* line 57 */
    com *awc = file(path, false, 0, 1);
    /* line 58 */
    int32_t v = 2026;
    /* line 59 */
    {
        uint32_t _scsz;
        _scsz = sizeof(v); awc->write(awc, (void *)&(v), &_scsz);
    }
    /* line 60 */
    com *arc = file(path, false, 2, 0);
    /* line 61 */
    async_init();
    /* line 62 */
    async_read__async(arc);
    /* line 63 */
    async_loop(NULL);
    /* line 64 */
    async_final();
    /* line 65 */
    awc->close(awc);
    /* line 66 */
    arc->close(arc);
    /* line 67 */
    printf("done\n");
    /* line 68 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        sc_mod_io_drop();
        return _ret;
    }
}
