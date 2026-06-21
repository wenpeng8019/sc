/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static inline node *node__new(void) {
    node *_p = (node *)malloc(sizeof(node));
    if (_p) {
        memset(_p, 0, sizeof(node));
    }
    return _p;
}

static inline node *node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)malloc(SC_REF_HDR + sizeof(node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    node *_p = (node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(node));
    return _p;
}

int32_t main(void) {
    /* line 8 */
    int32_t i = 0;
    /* line 9 */
    loop:;
        /* line 10 */
        sc_fat n = {0};
        node *_fat0 = node__new_ref(0);
        sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 11 */
        ((node *)(n).p)->v = i;
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
