/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
static sc_fat g = {0};
static sc_fat arr[3] = {0};
static sc_fat grid[2][2] = {0};
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

void node_drop(node *_this) {
    /* line 7 */
    printf("drop v=%d\n", _this->v);
}

int32_t main(void) {
    /* line 15 */
    sc_fat_unbind_d(&g, (void (*)(void *))node_drop);
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&g, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 16 */
    ((node *)(g).p)->v = 1;
    /* line 17 */
    int32_t i = 0;
    /* line 18 */
    for (i = 0; i < 3; i++) {
        /* line 19 */
        sc_fat_unbind_d(&arr[i], (void (*)(void *))node_drop);
        node *_fat1 = node__new_ref(0);
        sc_fat_bind(&arr[i], _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 20 */
        ((node *)(arr[i]).p)->v = (i * 10);
    }
    /* line 21 */
    sc_fat_unbind_d(&grid[0][0], (void (*)(void *))node_drop);
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&grid[0][0], _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 22 */
    ((node *)(grid[0][0]).p)->v = 99;
    /* line 23 */
    printf("%d %d %d\n", ((node *)(g).p)->v, ((node *)(arr[2]).p)->v, ((node *)(grid[0][0]).p)->v);
    /* line 24 */
    return 0;
}


SC_DESTRUCTOR(__sc_gfat_fini) {
    sc_fat_unbind_d(&g, (void (*)(void *))node_drop);
    for (size_t _k0 = (3); _k0-- > 0; ) sc_fat_unbind_d(&arr[_k0], (void (*)(void *))node_drop);
    for (size_t _k0 = (2); _k0-- > 0; ) for (size_t _k1 = (2); _k1-- > 0; ) sc_fat_unbind_d(&grid[_k0][_k1], (void (*)(void *))node_drop);
}
