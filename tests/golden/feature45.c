/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mt/mt.h"

static session *g_sess = NULL;
static int32_t g_arg = 0;
struct serve {
    int32_t _;
    int32_t x;
};
static void serve_rpc(struct serve *_p);
static inline int32_t serve(int32_t x) {
    struct serve _p = {0};
    _p.x = x;
    serve_rpc(&_p);
    return _p._;
}

struct server {
    queue *qq;
};
static void server_rpc(struct server *_p);
static inline void server(queue *qq) {
    struct server _p = {0};
    _p.qq = qq;
    server_rpc(&_p);
}

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mt_init(void); void sc_mod_mt_drop(void);

static void serve_rpc(struct serve *_p) {
    /* line 15 */
    session *s = op_session_current();
    /* line 16 */
    g_sess = s;
    /* line 17 */
    g_arg = _p->x;
    /* line 18 */
    _p->_ = 0; return;
}

static void server_rpc(struct server *_p) {
    /* line 22 */
    _p->qq->pull(_p->qq, -(1));
    /* line 23 */
    { int32_t _dv = (g_arg * 10); (g_sess)->respond(g_sess, &_dv, sizeof(_dv)); }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mt_init();
    /* line 26 */
    queue *sq = default_queue(NULL);
    /* line 27 */
    thread *st = NULL;
    /* line 28 */
    {
        struct server _rp = {0};
        _rp.qq = sq;
        thread_run((void (*)(void *))server_rpc, &_rp, sizeof(_rp), (thread **)(&(st)), (uint32_t)0, (uint8_t)0);
    }
    /* line 30 */
    int32_t r = ({ struct serve _rp = {0}; _rp.x = 7; sq->sync(sq, (void (*)(void *))serve_rpc, &_rp, sizeof(_rp), (int32_t)0, (int64_t)0, (int64_t)0); _rp._; });
    /* line 31 */
    printf("delayed response: r=%d\n", r);
    /* line 33 */
    thread_join(st);
    /* line 34 */
    sq->drop(sq);
    /* line 35 */
    {
        int32_t _ret = 0;
        sc_mod_mt_drop();
        return _ret;
    }
}
