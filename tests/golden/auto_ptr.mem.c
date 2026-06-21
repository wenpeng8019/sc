/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
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
    char *_b = (char *)malloc(SC_CANARY + SC_REF_HDR + sizeof(node) + SC_CANARY);
    if (!_b) return 0;
    sc_ref *_h = (sc_ref *)(_b + SC_CANARY);
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom | SC_REF_CANARY;
    uintptr_t _m = sc_canary_magic(_b);
    ((uintptr_t *)_b)[0] = _m; ((uintptr_t *)_b)[1] = sizeof(node);
    *(uintptr_t *)(_b + SC_CANARY + SC_REF_HDR + sizeof(node)) = _m;
    node *_p = (node *)(_b + SC_CANARY + SC_REF_HDR);
    memset(_p, 0, sizeof(node));
    return _p;
}

sc_fat make(int32_t val) {
    /* line 10 */
    sc_fat n = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&n, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 11 */
    ((node *)(n).p)->v = val;
    /* line 12 */
    {
        sc_fat _ret = n;
        return _ret;
    }
}

int32_t main(void) {
    /* line 15 */
    sc_fat root = {0};
    root = make(1);
    /* line 16 */
    sc_fat_unbind(&((node *)(root).p)->child);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 17 */
    ((node *)(((node *)(root).p)->child).p)->v = 2;
    /* line 19 */
    sc_fat alias = {0};
    sc_fat_bind(&alias, (root).p, (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 21 */
    sc_fat sub = {0};
    sc_fat_bind(&sub, &(((node *)(root).p)->child), (sc_ref *)(root).tar, SC_OWN_ROOT);
    /* line 23 */
    sc_fat_unbind(&((node *)(root).p)->child);
    /* line 24 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&sub);
        sc_fat_unbind(&alias);
        sc_fat_unbind(&root);
        return _ret;
    }
}
