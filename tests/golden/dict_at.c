/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_node sc_node;
typedef struct sc_acc sc_acc;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
typedef struct sc_acc {
    int32_t sum;
    int32_t cnt;
} sc_acc;

bool sc_sum_each(const void *key, sc_thin value, void *ctx);
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

void sc_node_drop(sc_node *_this) {
    /* line 13 */
    printf("drop %d\n", _this->v);
}

bool sc_sum_each(const void *key, sc_thin value, void *ctx) {
    /* line 22 */
    sc_acc *a = ((sc_acc*)(ctx));
    /* line 23 */
    a->sum += ((sc_node*)((value).p))->v;
    /* line 24 */
    a->cnt += 1;
    /* line 25 */
    return true;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 29 */
    sc_dict da = {0};
    /* line 30 */
    sc_dict_init(&da, 4);
    /* line 32 */
    sc_fat a1 = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&a1, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 33 */
    ((sc_node *)(a1).p)->v = 100;
    /* line 34 */
    sc_fat a2 = {0};
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&a2, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 35 */
    ((sc_node *)(a2).p)->v = 200;
    /* line 36 */
    sc_fat a3 = {0};
    sc_node *_fat2 = sc_node__new_ref(0);
    sc_fat_bind(&a3, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 37 */
    ((sc_node *)(a3).p)->v = 300;
    /* line 39 */
    int32_t k1 = 11;
    /* line 40 */
    int32_t k2 = 22;
    /* line 41 */
    int32_t k3 = 33;
    /* line 42 */
    sc_dict_put(&da, ((const void*)(&(k1))), sc_fat_as_thin(a1, (void (*)(void *))sc_node_drop));
    /* line 43 */
    sc_dict_put(&da, ((const void*)(&(k2))), sc_fat_as_thin(a2, (void (*)(void *))sc_node_drop));
    /* line 44 */
    sc_dict_put(&da, ((const void*)(&(k3))), sc_fat_as_thin(a3, (void (*)(void *))sc_node_drop));
    /* line 45 */
    printf("A len=%llu\n", sc_dict_len(&da));
    /* line 46 */
    bool _sq0 = sc_dict_has(&da, ((const void*)(&(k2))));
    bool _sq1 = sc_dict_has(&da, ((const void*)(&(k3))));
    printf("A has22=%d has99=%d\n", _sq0, _sq1);
    /* line 47 */
    int32_t kx = 99;
    /* line 48 */
    printf("A miss=%d\n", sc_dict_has(&da, ((const void*)(&(kx)))));
    /* line 49 */
    printf("A get33=%d\n", ((sc_node*)((sc_dict_get(&da, ((const void*)(&(k3))))).p))->v);
    /* line 52 */
    sc_fat a2b = {0};
    sc_node *_fat3 = sc_node__new_ref(0);
    sc_fat_bind(&a2b, _fat3, (sc_ref *)((char *)_fat3 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 53 */
    ((sc_node *)(a2b).p)->v = 250;
    /* line 54 */
    sc_dict_put(&da, ((const void*)(&(k2))), sc_fat_as_thin(a2b, (void (*)(void *))sc_node_drop));
    /* line 55 */
    int32_t _sq2 = ((sc_node*)((sc_dict_get(&da, ((const void*)(&(k2))))).p))->v;
    uint64_t _sq3 = sc_dict_len(&da);
    printf("A put22b=%d len=%llu\n", _sq2, _sq3);
    /* line 58 */
    sc_acc sa = {0};
    /* line 59 */
    sa.sum = 0;
    /* line 60 */
    sa.cnt = 0;
    /* line 61 */
    sc_dict_each(&da, sc_sum_each, ((void*)(&(sa))));
    /* line 62 */
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt);
    /* line 65 */
    int32_t fs = 0;
    /* line 66 */
    int32_t fc = 0;
    /* line 67 */
    int64_t i = sc_dict_first(&da);
    /* line 68 */
    while (i >= 0) {
        /* line 69 */
        fs += ((sc_node*)((sc_dict_value_at(&da, i)).p))->v;
        /* line 70 */
        fc += 1;
        /* line 71 */
        i = sc_dict_next(&da, i);
    }
    /* line 72 */
    int32_t bs = 0;
    /* line 73 */
    int32_t bc = 0;
    /* line 74 */
    i = sc_dict_last(&da);
    /* line 75 */
    while (i >= 0) {
        /* line 76 */
        bs += ((sc_node*)((sc_dict_value_at(&da, i)).p))->v;
        /* line 77 */
        bc += 1;
        /* line 78 */
        i = sc_dict_prev(&da, i);
    }
    /* line 79 */
    printf("A fwd sum=%d cnt=%d  bwd sum=%d cnt=%d\n", fs, fc, bs, bc);
    /* line 82 */
    bool _sq4 = sc_dict_remove(&da, ((const void*)(&(k1))));
    bool _sq5 = sc_dict_remove(&da, ((const void*)(&(kx))));
    uint64_t _sq6 = sc_dict_len(&da);
    printf("A rm11=%d rm99=%d len=%llu\n", _sq4, _sq5, _sq6);
    /* line 85 */
    sc_dict_drop(&da);
    /* line 86 */
    printf("A after_drop len=%llu\n", sc_dict_len(&da));
    /* line 89 */
    sc_dict db = {0};
    /* line 90 */
    sc_dict_init(&db, 0);
    /* line 91 */
    sc_fat b1 = {0};
    sc_node *_fat4 = sc_node__new_ref(0);
    sc_fat_bind(&b1, _fat4, (sc_ref *)((char *)_fat4 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 92 */
    ((sc_node *)(b1).p)->v = 10;
    /* line 93 */
    sc_fat b2 = {0};
    sc_node *_fat5 = sc_node__new_ref(0);
    sc_fat_bind(&b2, _fat5, (sc_ref *)((char *)_fat5 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 94 */
    ((sc_node *)(b2).p)->v = 20;
    /* line 95 */
    sc_dict_put(&db, "alpha", sc_fat_as_thin(b1, (void (*)(void *))sc_node_drop));
    /* line 96 */
    sc_dict_put(&db, "beta", sc_fat_as_thin(b2, (void (*)(void *))sc_node_drop));
    /* line 97 */
    uint64_t _sq7 = sc_dict_len(&db);
    int32_t _sq8 = ((sc_node*)((sc_dict_get(&db, "beta")).p))->v;
    bool _sq9 = sc_dict_has(&db, "alpha");
    bool _sq10 = sc_dict_has(&db, "zzz");
    printf("B len=%llu get_beta=%d has_alpha=%d has_x=%d\n", _sq7, _sq8, _sq9, _sq10);
    /* line 99 */
    sc_dict_remove(&db, "alpha");
    /* line 100 */
    printf("B after_rm len=%llu\n", sc_dict_len(&db));
    /* line 101 */
    sc_dict_drop(&db);
    /* line 104 */
    sc_dict dc = {0};
    /* line 105 */
    sc_dict_init(&dc, 0 - 1);
    /* line 106 */
    sc_fat c1 = {0};
    sc_node *_fat6 = sc_node__new_ref(0);
    sc_fat_bind(&c1, _fat6, (sc_ref *)((char *)_fat6 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 107 */
    ((sc_node *)(c1).p)->v = 70;
    /* line 108 */
    char buf[8];
    /* line 109 */
    buf[0] = 'k';
    /* line 110 */
    buf[1] = 'e';
    /* line 111 */
    buf[2] = 'y';
    /* line 112 */
    buf[3] = 0;
    /* line 113 */
    sc_dict_put(&dc, ((const void*)(&(buf[0]))), sc_fat_as_thin(c1, (void (*)(void *))sc_node_drop));
    /* line 114 */
    buf[0] = 'X';
    /* line 115 */
    bool _sq11 = sc_dict_has(&dc, ((const void*)(&(buf[0]))));
    bool _sq12 = sc_dict_has(&dc, ((const void*)("key")));
    printf("C has_key=%d get_key=%d\n", _sq11, _sq12);
    /* line 117 */
    printf("C lookup=%d\n", ((sc_node*)((sc_dict_get(&dc, ((const void*)("key")))).p))->v);
    /* line 118 */
    sc_dict_drop(&dc);
    /* line 120 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&c1, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&b2, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&b1, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&a2b, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&a3, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&a2, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&a1, (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
