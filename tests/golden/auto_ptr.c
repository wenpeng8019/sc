/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
    sc_fat child;
} sc_node;

sc_fat sc_make(int32_t val);
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

static inline sc_node *sc_node__new_ref(int32_t _flags) {
    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)
        ? sc_alloc(SC_REF_HDR + sizeof(sc_node))
        : sc_chunk(SC_REF_HDR + sizeof(sc_node)));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;
    sc_node *_p = (sc_node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_node));
    return _p;
}

sc_fat sc_make(int32_t val) {
    /* line 10 */
    sc_fat n = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 11 */
    ((sc_node *)(n).p)->v = val;
    /* line 12 */
    {
        sc_fat _ret = n;
        return _ret;
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 15 */
    sc_fat root = {0};
    root = sc_make(1);
    /* line 16 */
    sc_fat_unbind(&((sc_node *)(root).p)->child);
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 17 */
    ((sc_node *)(((sc_node *)(root).p)->child).p)->v = 2;
    /* line 19 */
    sc_fat alias = {0};
    sc_fat_bind(&alias, (root).p, (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 21 */
    sc_fat sub = {0};
    sc_fat_bind(&sub, &(((sc_node *)(root).p)->child), (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 23 */
    sc_fat_unbind(&((sc_node *)(root).p)->child);
    /* line 24 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&sub);
        sc_fat_unbind(&alias);
        sc_fat_unbind(&root);
        return _ret;
    }
}
