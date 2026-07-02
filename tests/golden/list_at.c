/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
int32_t sc_node_cmp(void *a, void *b);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

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

void sc_node_drop(sc_node *_this) {
    /* line 13 */
    printf("drop %d\n", _this->v);
}

int32_t sc_node_cmp(void *a, void *b) {
    /* line 17 */
    return ((sc_node*)(a))->v - ((sc_node*)(b))->v;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 20 */
    sc_list l = {0};
    sc_list_init(&l);
    /* line 22 */
    sc_fat a = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&a, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 23 */
    ((sc_node *)(a).p)->v = 10;
    /* line 24 */
    sc_fat b = {0};
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&b, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 25 */
    ((sc_node *)(b).p)->v = 30;
    /* line 26 */
    sc_fat c = {0};
    sc_node *_fat2 = sc_node__new_ref(0);
    sc_fat_bind(&c, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((sc_node *)(c).p)->v = 20;
    /* line 29 */
    sc_list_push(&l, sc_fat_as_thin(a, (void (*)(void *))sc_node_drop));
    /* line 30 */
    sc_list_push(&l, sc_fat_as_thin(b, (void (*)(void *))sc_node_drop));
    /* line 31 */
    sc_list_push(&l, sc_fat_as_thin(c, (void (*)(void *))sc_node_drop));
    /* line 32 */
    printf("len=%llu\n", sc_list_len(&l));
    /* line 35 */
    sc_thin g = {0};
    sc_thin_bind(&g, (sc_thin_as_fat(sc_list_get(&l, 1))).p, (sc_ref *)(sc_thin_as_fat(sc_list_get(&l, 1))).tar, (void (*)(void *))sc_node_drop);
    /* line 36 */
    printf("get1=%d\n", ((sc_node *)(g).p)->v);
    /* line 37 */
    printf("get2_raw=%d\n", ((sc_node*)((sc_list_get(&l, 2)).p))->v);
    /* line 40 */
    int64_t _sq0 = sc_list_index_of(&l, sc_fat_as_thin(b, (void (*)(void *))sc_node_drop));
    int64_t _sq1 = sc_list_index_of(&l, sc_fat_as_thin(a, (void (*)(void *))sc_node_drop));
    printf("idx_b=%lld idx_a=%lld\n", _sq0, _sq1);
    /* line 43 */
    sc_list_sort(&l, sc_node_cmp);
    /* line 44 */
    uint64_t i = 0;
    /* line 45 */
    for (i = 0; i < sc_list_len(&l); i++) {
        /* line 46 */
        printf("sorted[%llu]=%d\n", i, ((sc_node*)((sc_list_get(&l, i)).p))->v);
    }
    /* line 49 */
    sc_list_reverse(&l);
    /* line 50 */
    int32_t _sq2 = ((sc_node*)((sc_list_get(&l, 0)).p))->v;
    int32_t _sq3 = ((sc_node*)((sc_list_get(&l, 2)).p))->v;
    printf("rev0=%d rev2=%d\n", _sq2, _sq3);
    /* line 53 */
    sc_fat d = {0};
    sc_node *_fat3 = sc_node__new_ref(0);
    sc_fat_bind(&d, _fat3, (sc_ref *)((char *)_fat3 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 54 */
    ((sc_node *)(d).p)->v = 99;
    /* line 55 */
    sc_list_insert(&l, 0, sc_fat_as_thin(d, (void (*)(void *))sc_node_drop));
    /* line 56 */
    uint64_t _sq4 = sc_list_len(&l);
    int32_t _sq5 = ((sc_node*)((sc_list_get(&l, 0)).p))->v;
    printf("after_insert len=%llu head=%d\n", _sq4, _sq5);
    /* line 57 */
    sc_list_remove_at(&l, sc_list_len(&l) - 1);
    /* line 58 */
    printf("after_remove len=%llu\n", sc_list_len(&l));
    /* line 61 */
    sc_list_set(&l, 1, sc_fat_as_thin(a, (void (*)(void *))sc_node_drop));
    /* line 62 */
    printf("set1=%d\n", ((sc_node*)((sc_list_get(&l, 1)).p))->v);
    /* line 65 */
    sc_list_pop(&l);
    /* line 66 */
    printf("after_pop len=%llu\n", sc_list_len(&l));
    /* line 69 */
    sc_list_clear(&l);
    /* line 70 */
    printf("after_clear len=%llu\n", sc_list_len(&l));
    /* line 72 */
    {
        int32_t _ret = 0;
        sc_list_drop(&l);
        sc_fat_unbind_d(&d, (void (*)(void *))sc_node_drop);
        sc_thin_unbind(&g);
        sc_fat_unbind_d(&c, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&b, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&a, (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
