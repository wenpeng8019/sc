/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
    sc_fat child;
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
    /* line 9 */
    sc_fat arr[3] = {0};
    /* line 10 */
    int32_t i = 0;
    /* line 11 */
    for (i = 0; i < 3; i++) {
        /* line 12 */
        sc_fat_unbind(&arr[i]);
        node *_fat0 = node__new_ref(0);
        sc_fat_bind(&arr[i], _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 13 */
        ((node *)(arr[i]).p)->v = (i * 10);
    }
    /* line 14 */
    sc_fat_unbind(&((node *)(arr[0]).p)->child);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(arr[0]).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(arr[0]).tar)->out);
    /* line 15 */
    ((node *)(((node *)(arr[0]).p)->child).p)->v = 99;
    /* line 17 */
    sc_fat pick = {0};
    sc_fat_bind(&pick, (arr[1]).p, (sc_ref *)(arr[1]).tar, SC_OWN_ROOT);
    /* line 18 */
    printf("%d %d %d\n", ((node *)(arr[0]).p)->v, ((node *)(pick).p)->v, ((node *)(((node *)(arr[0]).p)->child).p)->v);
    /* line 19 */
    sc_fat_unbind(&((node *)(arr[0]).p)->child);
    /* line 20 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&pick);
        for (size_t _k = (3); _k-- > 0; ) sc_fat_unbind(&arr[_k]);
        return _ret;
    }
}
