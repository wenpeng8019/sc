/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;
typedef struct acc acc;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
typedef struct acc {
    int32_t sum;
    int32_t cnt;
} acc;

uint8_t sum_each(const void *key, sc_afat value, void *ctx);
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

uint8_t sum_each(const void *key, sc_afat value, void *ctx) {
    /* line 22 */
    acc *a = ((acc*)(ctx));
    /* line 23 */
    a->sum += ((node*)((value).p))->v;
    /* line 24 */
    a->cnt += 1;
    /* line 25 */
    return true;
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 29 */
    dict da = {0};
    /* line 30 */
    dict_init(&da, 4);
    /* line 32 */
    sc_fat a1 = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&a1, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 33 */
    ((node *)(a1).p)->v = 100;
    /* line 34 */
    sc_fat a2 = {0};
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&a2, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 35 */
    ((node *)(a2).p)->v = 200;
    /* line 36 */
    sc_fat a3 = {0};
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&a3, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 37 */
    ((node *)(a3).p)->v = 300;
    /* line 39 */
    int32_t k1 = 11;
    /* line 40 */
    int32_t k2 = 22;
    /* line 41 */
    int32_t k3 = 33;
    /* line 42 */
    dict_put(&da, ((const void*)(&(k1))), ({ sc_fat _ec3 = a1; (sc_afat){_ec3.p, _ec3.tar, _ec3.own, (void (*)(void *))node_drop}; }));
    /* line 43 */
    dict_put(&da, ((const void*)(&(k2))), ({ sc_fat _ec4 = a2; (sc_afat){_ec4.p, _ec4.tar, _ec4.own, (void (*)(void *))node_drop}; }));
    /* line 44 */
    dict_put(&da, ((const void*)(&(k3))), ({ sc_fat _ec5 = a3; (sc_afat){_ec5.p, _ec5.tar, _ec5.own, (void (*)(void *))node_drop}; }));
    /* line 45 */
    printf("A len=%llu\n", dict_len(&da));
    /* line 46 */
    printf("A has22=%d has99=%d\n", dict_has(&da, ((const void*)(&(k2)))), dict_has(&da, ((const void*)(&(k3)))));
    /* line 47 */
    int32_t kx = 99;
    /* line 48 */
    printf("A miss=%d\n", dict_has(&da, ((const void*)(&(kx)))));
    /* line 49 */
    printf("A get33=%d\n", ((node*)((dict_get(&da, ((const void*)(&(k3))))).p))->v);
    /* line 52 */
    sc_fat a2b = {0};
    node *_fat6 = node__new_ref(0);
    sc_fat_bind(&a2b, _fat6, (sc_ref *)((char *)_fat6 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 53 */
    ((node *)(a2b).p)->v = 250;
    /* line 54 */
    dict_put(&da, ((const void*)(&(k2))), ({ sc_fat _ec7 = a2b; (sc_afat){_ec7.p, _ec7.tar, _ec7.own, (void (*)(void *))node_drop}; }));
    /* line 55 */
    printf("A put22b=%d len=%llu\n", ((node*)((dict_get(&da, ((const void*)(&(k2))))).p))->v, dict_len(&da));
    /* line 58 */
    acc sa = {0};
    /* line 59 */
    sa.sum = 0;
    /* line 60 */
    sa.cnt = 0;
    /* line 61 */
    dict_each(&da, sum_each, ((void*)(&(sa))));
    /* line 62 */
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt);
    /* line 65 */
    int32_t fs = 0;
    /* line 66 */
    int32_t fc = 0;
    /* line 67 */
    int64_t i = dict_first(&da);
    /* line 68 */
    while (i >= 0) {
        /* line 69 */
        fs += ((node*)((dict_value_at(&da, i)).p))->v;
        /* line 70 */
        fc += 1;
        /* line 71 */
        i = dict_next(&da, i);
    }
    /* line 72 */
    int32_t bs = 0;
    /* line 73 */
    int32_t bc = 0;
    /* line 74 */
    i = dict_last(&da);
    /* line 75 */
    while (i >= 0) {
        /* line 76 */
        bs += ((node*)((dict_value_at(&da, i)).p))->v;
        /* line 77 */
        bc += 1;
        /* line 78 */
        i = dict_prev(&da, i);
    }
    /* line 79 */
    printf("A fwd sum=%d cnt=%d  bwd sum=%d cnt=%d\n", fs, fc, bs, bc);
    /* line 82 */
    printf("A rm11=%d rm99=%d len=%llu\n", dict_remove(&da, ((const void*)(&(k1)))), dict_remove(&da, ((const void*)(&(kx)))), dict_len(&da));
    /* line 85 */
    dict_drop(&da);
    /* line 86 */
    printf("A after_drop len=%llu\n", dict_len(&da));
    /* line 89 */
    dict db = {0};
    /* line 90 */
    dict_init(&db, 0);
    /* line 91 */
    sc_fat b1 = {0};
    node *_fat8 = node__new_ref(0);
    sc_fat_bind(&b1, _fat8, (sc_ref *)((char *)_fat8 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 92 */
    ((node *)(b1).p)->v = 10;
    /* line 93 */
    sc_fat b2 = {0};
    node *_fat9 = node__new_ref(0);
    sc_fat_bind(&b2, _fat9, (sc_ref *)((char *)_fat9 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 94 */
    ((node *)(b2).p)->v = 20;
    /* line 95 */
    dict_put(&db, "alpha", ({ sc_fat _ec10 = b1; (sc_afat){_ec10.p, _ec10.tar, _ec10.own, (void (*)(void *))node_drop}; }));
    /* line 96 */
    dict_put(&db, "beta", ({ sc_fat _ec11 = b2; (sc_afat){_ec11.p, _ec11.tar, _ec11.own, (void (*)(void *))node_drop}; }));
    /* line 97 */
    printf("B len=%llu get_beta=%d has_alpha=%d has_x=%d\n", dict_len(&db), ((node*)((dict_get(&db, "beta")).p))->v, dict_has(&db, "alpha"), dict_has(&db, "zzz"));
    /* line 99 */
    dict_remove(&db, "alpha");
    /* line 100 */
    printf("B after_rm len=%llu\n", dict_len(&db));
    /* line 101 */
    dict_drop(&db);
    /* line 104 */
    dict dc = {0};
    /* line 105 */
    dict_init(&dc, 0 - 1);
    /* line 106 */
    sc_fat c1 = {0};
    node *_fat12 = node__new_ref(0);
    sc_fat_bind(&c1, _fat12, (sc_ref *)((char *)_fat12 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 107 */
    ((node *)(c1).p)->v = 70;
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
    dict_put(&dc, ((const void*)(&(buf[0]))), ({ sc_fat _ec13 = c1; (sc_afat){_ec13.p, _ec13.tar, _ec13.own, (void (*)(void *))node_drop}; }));
    /* line 114 */
    buf[0] = 'X';
    /* line 115 */
    printf("C has_key=%d get_key=%d\n", dict_has(&dc, ((const void*)(&(buf[0])))), dict_has(&dc, ((const void*)("key"))));
    /* line 117 */
    printf("C lookup=%d\n", ((node*)((dict_get(&dc, ((const void*)("key")))).p))->v);
    /* line 118 */
    dict_drop(&dc);
    /* line 120 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&c1, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&b2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&b1, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&a2b, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&a3, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&a2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&a1, (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
