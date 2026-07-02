/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

static sc_session *sc_g_sess = NULL;
static int32_t sc_g_arg = 0;
struct sc_serve {
    int32_t _;
    int32_t x;
};
static void sc_serve_rpc(struct sc_serve *_p);
static inline int32_t sc_serve(int32_t x) {
    struct sc_serve _p = {0};
    _p.x = x;
    sc_serve_rpc(&_p);
    return _p._;
}

struct sc_server {
    sc_queue *qq;
};
static void sc_server_rpc(struct sc_server *_p);
static inline void sc_server(sc_queue *qq) {
    struct sc_server _p = {0};
    _p.qq = qq;
    sc_server_rpc(&_p);
}

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void sc_serve_rpc(struct sc_serve *_p) {
    /* line 15 */
    sc_session *s = sc_op_session_current();
    /* line 16 */
    sc_g_sess = s;
    /* line 17 */
    sc_g_arg = _p->x;
    /* line 18 */
    _p->_ = 0; return;
}

static void sc_server_rpc(struct sc_server *_p) {
    /* line 22 */
    _p->qq->pull(_p->qq, -(1));
    /* line 23 */
    { int32_t _dv = (sc_g_arg * 10); (sc_g_sess)->respond(sc_g_sess, &_dv, sizeof(_dv)); }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 26 */
    sc_queue *sq = sc_default_queue(NULL);
    /* line 27 */
    sc_thread *st = NULL;
    /* line 28 */
    {
        struct sc_server _rp = {0};
        _rp.qq = sq;
        sc_thread_run((void (*)(void *))sc_server_rpc, &_rp, sizeof(_rp), (sc_thread **)(&(st)), (uint32_t)0, (uint8_t)0);
    }
    /* line 30 */
    int32_t r = ({ struct sc_serve _rp = {0}; _rp.x = 7; sq->sync(sq, (void (*)(void *))sc_serve_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 31 */
    printf("delayed response: r=%d\n", r);
    /* line 33 */
    sc_thread_join(st);
    /* line 34 */
    sq->drop(sq);
    /* line 35 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
