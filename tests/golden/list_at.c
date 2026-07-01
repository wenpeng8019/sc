/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
int32_t node_cmp(void *a, void *b);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static inline node *node__new(void) {
    node *_p = (node *)sc_alloc(sizeof(node));
    if (_p) {
        memset(_p, 0, sizeof(node));
    }
    return _p;
}

static inline node *node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    node *_p = (node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(node));
    return _p;
}

void node_drop(node *_this) {
    /* line 13 */
    printf("drop %d\n", _this->v);
}

int32_t node_cmp(void *a, void *b) {
    /* line 17 */
    return ((node*)(a))->v - ((node*)(b))->v;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 20 */
    list l = {0};
    list_init(&l);
    /* line 22 */
    sc_fat a = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&a, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 23 */
    ((node *)(a).p)->v = 10;
    /* line 24 */
    sc_fat b = {0};
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&b, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 25 */
    ((node *)(b).p)->v = 30;
    /* line 26 */
    sc_fat c = {0};
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&c, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((node *)(c).p)->v = 20;
    /* line 29 */
    list_push(&l, sc_fat_as_thin(a, (void (*)(void *))node_drop));
    /* line 30 */
    list_push(&l, sc_fat_as_thin(b, (void (*)(void *))node_drop));
    /* line 31 */
    list_push(&l, sc_fat_as_thin(c, (void (*)(void *))node_drop));
    /* line 32 */
    printf("len=%llu\n", list_len(&l));
    /* line 35 */
    sc_thin g = {0};
    sc_thin_bind(&g, (sc_thin_as_fat(list_get(&l, 1))).p, (sc_ref *)(sc_thin_as_fat(list_get(&l, 1))).tar, (void (*)(void *))node_drop);
    /* line 36 */
    printf("get1=%d\n", ((node *)(g).p)->v);
    /* line 37 */
    printf("get2_raw=%d\n", ((node*)((list_get(&l, 2)).p))->v);
    /* line 40 */
    int64_t _sq0 = list_index_of(&l, sc_fat_as_thin(b, (void (*)(void *))node_drop));
    int64_t _sq1 = list_index_of(&l, sc_fat_as_thin(a, (void (*)(void *))node_drop));
    printf("idx_b=%lld idx_a=%lld\n", _sq0, _sq1);
    /* line 43 */
    list_sort(&l, node_cmp);
    /* line 44 */
    uint64_t i = 0;
    /* line 45 */
    for (i = 0; i < list_len(&l); i++) {
        /* line 46 */
        printf("sorted[%llu]=%d\n", i, ((node*)((list_get(&l, i)).p))->v);
    }
    /* line 49 */
    list_reverse(&l);
    /* line 50 */
    int32_t _sq2 = ((node*)((list_get(&l, 0)).p))->v;
    int32_t _sq3 = ((node*)((list_get(&l, 2)).p))->v;
    printf("rev0=%d rev2=%d\n", _sq2, _sq3);
    /* line 53 */
    sc_fat d = {0};
    node *_fat3 = node__new_ref(0);
    sc_fat_bind(&d, _fat3, (sc_ref *)((char *)_fat3 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 54 */
    ((node *)(d).p)->v = 99;
    /* line 55 */
    list_insert(&l, 0, sc_fat_as_thin(d, (void (*)(void *))node_drop));
    /* line 56 */
    uint64_t _sq4 = list_len(&l);
    int32_t _sq5 = ((node*)((list_get(&l, 0)).p))->v;
    printf("after_insert len=%llu head=%d\n", _sq4, _sq5);
    /* line 57 */
    list_remove_at(&l, list_len(&l) - 1);
    /* line 58 */
    printf("after_remove len=%llu\n", list_len(&l));
    /* line 61 */
    list_set(&l, 1, sc_fat_as_thin(a, (void (*)(void *))node_drop));
    /* line 62 */
    printf("set1=%d\n", ((node*)((list_get(&l, 1)).p))->v);
    /* line 65 */
    list_pop(&l);
    /* line 66 */
    printf("after_pop len=%llu\n", list_len(&l));
    /* line 69 */
    list_clear(&l);
    /* line 70 */
    printf("after_clear len=%llu\n", list_len(&l));
    /* line 72 */
    {
        int32_t _ret = 0;
        list_drop(&l);
        sc_fat_unbind_d(&d, (void (*)(void *))node_drop);
        sc_thin_unbind(&g);
        sc_fat_unbind_d(&c, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&b, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&a, (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
