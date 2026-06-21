/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct point point;
typedef struct node node;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct node {
    int32_t v;
    point pt;
    sc_fat child;
} node;

sc_fat make(int32_t val);
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

sc_fat make(int32_t val) {
    /* line 26 */
    sc_fat n = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((node *)(n).p)->v = val;
    /* line 28 */
    {
        sc_fat _ret = n;
        return _ret;
    }
}

int32_t main(void) {
    /* line 31 */
    sc_fat root = {0};
    root = make(1);
    /* line 32 */
    ((node *)(root).p)->pt.x = 10;
    /* line 33 */
    ((node *)(root).p)->pt.y = 20;
    /* line 34 */
    sc_fat_unbind(&((node *)(root).p)->child);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 35 */
    ((node *)(((node *)(root).p)->child).p)->v = 2;
    /* line 36 */
    printf("root.v=%d child.v=%d\n", ((node *)(root).p)->v, ((node *)(((node *)(root).p)->child).p)->v);
    /* line 38 */
    sc_fat alias = {0};
    sc_fat_bind(&alias, (root).p, (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 39 */
    printf("alias.v=%d\n", ((node *)(alias).p)->v);
    /* line 41 */
    sc_fat px = {0};
    sc_fat_bind(&px, &(((node *)(root).p)->pt), (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 42 */
    printf("px.x=%d px.y=%d\n", ((point *)(px).p)->x, ((point *)(px).p)->y);
    /* line 44 */
    sc_fat_unbind(&((node *)(root).p)->child);
    /* line 45 */
    printf("after-detach root.v=%d\n", ((node *)(root).p)->v);
    /* line 46 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&px);
        sc_fat_unbind(&alias);
        sc_fat_unbind(&root);
        return _ret;
    }
}
