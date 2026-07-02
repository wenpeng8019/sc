/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
static sc_fat sc_g = {0};
static sc_fat sc_arr[3] = {0};
static sc_fat sc_grid[2][2] = {0};
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

#if !SC_HAVE_AUTO_HOOKS
static void __sc_gfat_fini(void);
#endif

void sc_node_drop(sc_node *_this) {
    /* line 7 */
    printf("drop v=%d\n", _this->v);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 15 */
    sc_fat_unbind_d(&g, (void (*)(void *))sc_node_drop);
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&g, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 16 */
    ((sc_node *)(sc_g).p)->v = 1;
    /* line 17 */
    int32_t i = 0;
    /* line 18 */
    for (i = 0; i < 3; i++) {
        /* line 19 */
        sc_fat_unbind_d(&sc_arr[i], (void (*)(void *))sc_node_drop);
        sc_node *_fat1 = sc_node__new_ref(0);
        sc_fat_bind(&sc_arr[i], _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
        /* line 20 */
        ((sc_node *)(sc_arr[i]).p)->v = (i * 10);
    }
    /* line 21 */
    sc_fat_unbind_d(&sc_grid[0][0], (void (*)(void *))sc_node_drop);
    sc_node *_fat2 = sc_node__new_ref(0);
    sc_fat_bind(&sc_grid[0][0], _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 22 */
    ((sc_node *)(sc_grid[0][0]).p)->v = 99;
    /* line 23 */
    printf("%d %d %d\n", ((sc_node *)(sc_g).p)->v, ((sc_node *)(sc_arr[2]).p)->v, ((sc_node *)(sc_grid[0][0]).p)->v);
    /* line 24 */
    {
        int32_t _ret = 0;
#if !SC_HAVE_AUTO_HOOKS
        __sc_gfat_fini();
#endif
        return _ret;
    }
}


SC_DESTRUCTOR(__sc_gfat_fini) {
    sc_fat_unbind_d(&sc_g, (void (*)(void *))sc_node_drop);
    for (size_t _k0 = (3); _k0-- > 0; ) sc_fat_unbind_d(&sc_arr[_k0], (void (*)(void *))sc_node_drop);
    for (size_t _k0 = (2); _k0-- > 0; ) for (size_t _k1 = (2); _k1-- > 0; ) sc_fat_unbind_d(&sc_grid[_k0][_k1], (void (*)(void *))sc_node_drop);
}
