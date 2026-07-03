/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/async/async.h"

static int32_t sc_dev_read(sc_com *_this, void *data, uint32_t *size);
static int32_t sc_dev_write(sc_com *_this, void *buf, uint32_t *size);
struct sc_handler {
    int32_t _;
    sc_future *_ret;
    int _state;
    sc_future *_fut;
    sc_com *c;
    char buf[4];
    char msg[3];
};
static void sc_handler_rpc(struct sc_handler *_p);
static sc_future *sc_handler__async(sc_com *c);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_async_init(void); void sc_mod_async_drop(void);

static int32_t sc_dev_read(sc_com *_this, void *data, uint32_t *size) {
    /* line 19 */
    char *p = ((char*)(data));
    /* line 20 */
    uint32_t i = 0;
    /* line 21 */
    while (i < *(size)) {
        /* line 22 */
        p[i] = (((char)(i)) + 'a');
        /* line 23 */
        i = (i + 1);
    }
    /* line 24 */
    return ((int32_t)(*(size)));
}

static int32_t sc_dev_write(sc_com *_this, void *buf, uint32_t *size) {
    /* line 28 */
    char *p = ((char*)(buf));
    /* line 29 */
    printf("  写出: ");
    /* line 30 */
    uint32_t i = 0;
    /* line 31 */
    while (i < *(size)) {
        /* line 32 */
        printf("%c", p[i]);
        /* line 33 */
        i = (i + 1);
    }
    /* line 34 */
    printf("\n");
    /* line 35 */
    return ((int32_t)(*(size)));
}

static sc_future *sc_handler__async(sc_com *c) {
    struct sc_handler *_p = (struct sc_handler *)sc_chunk0(sizeof(struct sc_handler));
    _p->_state = 0;
    _p->_ret = sc_future_new();
    _p->c = c;
    sc_handler_rpc(_p);
    return _p->_ret;
}

static void sc_handler_rpc(struct sc_handler *_p) {
    switch (_p->_state) {
        case 0: goto _s0;
        case 1: goto _s1;
        case 2: goto _s2;
    }
    _s0: ;
    _p->_fut = sc_com_read_async(_p->c, (void *)&(_p->buf), sizeof(_p->buf));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_handler_rpc)) goto _s1;
    _p->_state = 1; return;
    _s1: ;
    _p->buf[3] = 0;
    printf("  读入: %s\n", ((char*)(_p->buf)));
    _p->msg[0] = 'O';
    _p->msg[1] = 'K';
    _p->msg[2] = 0;
    _p->_fut = sc_com_write_async(_p->c, (void *)&(_p->msg), sizeof(_p->msg));
    if (sc_future_await(_p->_fut, _p, (void (*)(void *))sc_handler_rpc)) goto _s2;
    _p->_state = 2; return;
    _s2: ;
    _p->_ = 0;
    { sc_future *_r = _p->_ret; void *_res = (void *)(intptr_t)(_p->_); sc_recycle(_p); sc_future_done(_r, _res); return; }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_async_init();
    /* line 52 */
    sc_async_init();
    /* line 54 */
    sc_com c = {0};
    /* line 55 */
    c.read = sc_dev_read;
    /* line 56 */
    c.write = sc_dev_write;
    /* line 58 */
    sc_future *f = sc_handler__async(&(c));
    /* line 59 */
    sc_async_loop(NULL);
    /* line 61 */
    printf("done\n");
    /* line 62 */
    sc_async_final();
    /* line 63 */
    {
        int32_t _ret = 0;
        sc_mod_async_drop();
        return _ret;
    }
}
