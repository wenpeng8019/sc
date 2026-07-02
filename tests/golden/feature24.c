/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_point sc_point;
typedef struct sc_node sc_node;

typedef struct sc_point {
    int32_t x;
    int32_t y;
} sc_point;

typedef struct sc_node {
    int32_t v;
    sc_point pt;
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

static inline sc_node *sc_node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(sc_node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    sc_node *_p = (sc_node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_node));
    return _p;
}

sc_fat sc_make(int32_t val) {
    /* line 24 */
    sc_fat n = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 25 */
    ((sc_node *)(n).p)->v = val;
    /* line 26 */
    {
        sc_fat _ret = n;
        return _ret;
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 29 */
    sc_fat root = {0};
    root = sc_make(1);
    /* line 30 */
    ((sc_node *)(root).p)->pt.x = 10;
    /* line 31 */
    ((sc_node *)(root).p)->pt.y = 20;
    /* line 32 */
    sc_fat_unbind(&((sc_node *)(root).p)->child);
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 33 */
    ((sc_node *)(((sc_node *)(root).p)->child).p)->v = 2;
    /* line 34 */
    printf("root.v=%d child.v=%d\n", ((sc_node *)(root).p)->v, ((sc_node *)(((sc_node *)(root).p)->child).p)->v);
    /* line 36 */
    sc_fat alias = {0};
    sc_fat_bind(&alias, (root).p, (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 37 */
    printf("alias.v=%d\n", ((sc_node *)(alias).p)->v);
    /* line 39 */
    sc_fat px = {0};
    sc_fat_bind(&px, &(((sc_node *)(root).p)->pt), (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 40 */
    printf("px.x=%d px.y=%d\n", ((sc_point *)(px).p)->x, ((sc_point *)(px).p)->y);
    /* line 42 */
    sc_fat_unbind(&((sc_node *)(root).p)->child);
    /* line 43 */
    printf("after-detach root.v=%d\n", ((sc_node *)(root).p)->v);
    /* line 44 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&px);
        sc_fat_unbind(&alias);
        sc_fat_unbind(&root);
        return _ret;
    }
}
