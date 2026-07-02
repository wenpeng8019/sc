/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static inline sc_node *sc_node__new(void) {
    sc_node *_p = (sc_node *)sc_alloc(sizeof(sc_node));
    if (_p) {
        memset(_p, 0, sizeof(sc_node));
    }
    return _p;
}

static inline sc_node *sc_node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(sc_node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    sc_node *_p = (sc_node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_node));
    return _p;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 8 */
    int32_t i = 0;
    /* line 9 */
    loop:;
        /* line 10 */
        sc_fat n = {0};
        sc_node *_fat0 = sc_node__new_ref(0);
        sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 11 */
        ((sc_node *)(n).p)->v = i;
        /* line 12 */
        i = (i + 1);
        /* line 13 */
        if (i < 3) {
            /* line 14 */
            sc_fat_unbind(&n);
            goto loop;
        }
        sc_fat_unbind(&n);
    /* line 15 */
    return 0;
}
