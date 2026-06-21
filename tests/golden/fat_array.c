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
    /* line 10 */
    sc_fat arr[3] = {0};
    /* line 11 */
    int32_t i = 0;
    /* line 12 */
    for (i = 0; i < 3; i++) {
        /* line 13 */
        sc_fat_unbind(&arr[i]);
        node *_fat0 = node__new_ref(0);
        sc_fat_bind(&arr[i], _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 14 */
        ((node *)(arr[i]).p)->v = (i * 10);
    }
    /* line 15 */
    sc_fat_unbind(&((node *)(arr[0]).p)->child);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(arr[0]).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(arr[0]).tar)->out);
    /* line 16 */
    ((node *)(((node *)(arr[0]).p)->child).p)->v = 99;
    /* line 18 */
    sc_fat pick = {0};
    sc_fat_bind(&pick, (arr[1]).p, (sc_ref *)(arr[1]).tar, SC_OWN_ROOT);
    /* line 19 */
    printf("%d %d %d\n", ((node *)(arr[0]).p)->v, ((node *)(pick).p)->v, ((node *)(((node *)(arr[0]).p)->child).p)->v);
    /* line 20 */
    sc_fat_unbind(&((node *)(arr[0]).p)->child);
    /* line 22 */
    sc_fat grid[2][2] = {0};
    /* line 23 */
    int32_t r = 0;
    /* line 24 */
    for (r = 0; r < 2; r++) {
        /* line 25 */
        int32_t c = 0;
        /* line 26 */
        for (c = 0; c < 2; c++) {
            /* line 27 */
            sc_fat_unbind(&grid[r][c]);
            node *_fat2 = node__new_ref(0);
            sc_fat_bind(&grid[r][c], _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
            /* line 28 */
            ((node *)(grid[r][c]).p)->v = ((r * 10) + c);
        }
    }
    /* line 29 */
    printf("%d %d %d %d\n", ((node *)(grid[0][0]).p)->v, ((node *)(grid[0][1]).p)->v, ((node *)(grid[1][0]).p)->v, ((node *)(grid[1][1]).p)->v);
    /* line 30 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&pick);
        for (size_t _k0 = (2); _k0-- > 0; ) for (size_t _k1 = (2); _k1-- > 0; ) sc_fat_unbind(&grid[_k0][_k1]);
        for (size_t _k0 = (3); _k0-- > 0; ) sc_fat_unbind(&arr[_k0]);
        return _ret;
    }
}
