/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
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
    /* line 14 */
    printf("drop %d\n", _this->v);
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 19 */
    dict d = {0};
    /* line 20 */
    dict_init(&d, 4);
    /* line 21 */
    sc_fat d1 = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&d1, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 22 */
    ((node *)(d1).p)->v = 10;
    /* line 23 */
    sc_fat d2 = {0};
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&d2, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 24 */
    ((node *)(d2).p)->v = 20;
    /* line 25 */
    sc_fat d3 = {0};
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&d3, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 26 */
    ((node *)(d3).p)->v = 30;
    /* line 27 */
    int32_t k1 = 1;
    /* line 28 */
    int32_t k2 = 2;
    /* line 29 */
    int32_t k3 = 3;
    /* line 30 */
    dict_put(&d, ((const void*)(&(k1))), ({ sc_fat _ec3 = d1; (sc_thin){_ec3.p, _ec3.tar, (void (*)(void *))node_drop}; }));
    /* line 31 */
    dict_put(&d, ((const void*)(&(k2))), ({ sc_fat _ec4 = d2; (sc_thin){_ec4.p, _ec4.tar, (void (*)(void *))node_drop}; }));
    /* line 32 */
    dict_put(&d, ((const void*)(&(k3))), ({ sc_fat _ec5 = d3; (sc_thin){_ec5.p, _ec5.tar, (void (*)(void *))node_drop}; }));
    /* line 33 */
    int32_t dsum = 0;
    /* line 34 */
    int32_t dksum = 0;
    /* line 35 */
    {
        dict * _fr0 = &d;
        long _fc0 = 0; (void)_fc0;
        for (int64_t _fi0 = dict_first(_fr0); ; _fi0 = dict_next(_fr0, _fi0), _fc0++) {
            sc_thin v = dict_value_at(_fr0, _fi0);
            if (v.p == (void *)0) break;
            const void * k = (const void *)dict_key_at(_fr0, _fi0);
            /* line 36 */
            dksum += ((int32_t*)(k))[0];
            /* line 37 */
            dsum += ((node*)((v).p))->v;
        }
    }
    /* line 38 */
    printf("dict ksum=%d vsum=%d\n", dksum, dsum);
    /* line 41 */
    bst t = {0};
    /* line 42 */
    bst_init(&t, 0, 4, NULL, NULL);
    /* line 43 */
    sc_fat t1 = {0};
    node *_fat6 = node__new_ref(0);
    sc_fat_bind(&t1, _fat6, (sc_ref *)((char *)_fat6 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 44 */
    ((node *)(t1).p)->v = 100;
    /* line 45 */
    sc_fat t2 = {0};
    node *_fat7 = node__new_ref(0);
    sc_fat_bind(&t2, _fat7, (sc_ref *)((char *)_fat7 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 46 */
    ((node *)(t2).p)->v = 200;
    /* line 47 */
    sc_fat t3 = {0};
    node *_fat8 = node__new_ref(0);
    sc_fat_bind(&t3, _fat8, (sc_ref *)((char *)_fat8 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 48 */
    ((node *)(t3).p)->v = 300;
    /* line 49 */
    sc_fat t4 = {0};
    node *_fat9 = node__new_ref(0);
    sc_fat_bind(&t4, _fat9, (sc_ref *)((char *)_fat9 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 50 */
    ((node *)(t4).p)->v = 400;
    /* line 51 */
    int32_t b1 = 5;
    /* line 52 */
    int32_t b2 = 15;
    /* line 53 */
    int32_t b3 = 25;
    /* line 54 */
    int32_t b4 = 35;
    /* line 55 */
    bst_put(&t, ((const void*)(&(b3))), ({ sc_fat _ec10 = t3; (sc_thin){_ec10.p, _ec10.tar, (void (*)(void *))node_drop}; }));
    /* line 56 */
    bst_put(&t, ((const void*)(&(b1))), ({ sc_fat _ec11 = t1; (sc_thin){_ec11.p, _ec11.tar, (void (*)(void *))node_drop}; }));
    /* line 57 */
    bst_put(&t, ((const void*)(&(b4))), ({ sc_fat _ec12 = t4; (sc_thin){_ec12.p, _ec12.tar, (void (*)(void *))node_drop}; }));
    /* line 58 */
    bst_put(&t, ((const void*)(&(b2))), ({ sc_fat _ec13 = t2; (sc_thin){_ec13.p, _ec13.tar, (void (*)(void *))node_drop}; }));
    /* line 59 */
    printf("bst asc:");
    /* line 60 */
    {
        bst * _fr1 = &t;
        long _fc1 = 0; (void)_fc1;
        for (int64_t _fi1 = bst_first(_fr1); ; _fi1 = bst_next(_fr1, _fi1), _fc1++) {
            sc_thin v = bst_value_at(_fr1, _fi1);
            if (v.p == (void *)0) break;
            int32_t * k = (int32_t *)bst_key_at(_fr1, _fi1);
            /* line 61 */
            printf(" %d=%d", k[0], ((node*)((v).p))->v);
        }
    }
    /* line 62 */
    printf("\n");
    /* line 63 */
    printf("bst desc:");
    /* line 64 */
    {
        bst * _fr2 = &t;
        long _fc2 = 0; (void)_fc2;
        for (int64_t _fi2 = bst_last(_fr2); ; _fi2 = bst_prev(_fr2, _fi2), _fc2++) {
            sc_thin v = bst_value_at(_fr2, _fi2);
            if (v.p == (void *)0) break;
            int32_t * k = (int32_t *)bst_key_at(_fr2, _fi2);
            /* line 65 */
            printf(" %d", k[0]);
        }
    }
    /* line 66 */
    printf("\n");
    /* line 67 */
    printf("bst first2:");
    /* line 68 */
    {
        bst * _fr3 = &t;
        long _fc3 = 0; (void)_fc3;
        for (int64_t _fi3 = bst_first(_fr3); ; _fi3 = bst_next(_fr3, _fi3), _fc3++) {
            sc_thin v = bst_value_at(_fr3, _fi3);
            if (v.p == (void *)0) break;
            if (_fc3 >= (2)) break;
            int32_t * k = (int32_t *)bst_key_at(_fr3, _fi3);
            /* line 69 */
            printf(" %d", k[0]);
        }
    }
    /* line 70 */
    printf("\n");
    /* line 73 */
    lru lc = {0};
    /* line 74 */
    lru_init(&lc, 4, 0);
    /* line 75 */
    sc_fat c1 = {0};
    node *_fat14 = node__new_ref(0);
    sc_fat_bind(&c1, _fat14, (sc_ref *)((char *)_fat14 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 76 */
    ((node *)(c1).p)->v = 11;
    /* line 77 */
    sc_fat c2 = {0};
    node *_fat15 = node__new_ref(0);
    sc_fat_bind(&c2, _fat15, (sc_ref *)((char *)_fat15 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 78 */
    ((node *)(c2).p)->v = 22;
    /* line 79 */
    sc_fat c3 = {0};
    node *_fat16 = node__new_ref(0);
    sc_fat_bind(&c3, _fat16, (sc_ref *)((char *)_fat16 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 80 */
    ((node *)(c3).p)->v = 33;
    /* line 81 */
    int32_t m1 = 7;
    /* line 82 */
    int32_t m2 = 8;
    /* line 83 */
    int32_t m3 = 9;
    /* line 84 */
    lru_put(&lc, ((const void*)(&(m1))), ({ sc_fat _ec17 = c1; (sc_thin){_ec17.p, _ec17.tar, (void (*)(void *))node_drop}; }));
    /* line 85 */
    lru_put(&lc, ((const void*)(&(m2))), ({ sc_fat _ec18 = c2; (sc_thin){_ec18.p, _ec18.tar, (void (*)(void *))node_drop}; }));
    /* line 86 */
    lru_put(&lc, ((const void*)(&(m3))), ({ sc_fat _ec19 = c3; (sc_thin){_ec19.p, _ec19.tar, (void (*)(void *))node_drop}; }));
    /* line 87 */
    printf("lru mru->lru:");
    /* line 88 */
    {
        lru * _fr4 = &lc;
        long _fc4 = 0; (void)_fc4;
        for (int64_t _fi4 = lru_first(_fr4); ; _fi4 = lru_next(_fr4, _fi4), _fc4++) {
            sc_thin v = lru_value_at(_fr4, _fi4);
            if (v.p == (void *)0) break;
            int32_t * k = (int32_t *)lru_key_at(_fr4, _fi4);
            /* line 89 */
            printf(" %d=%d", k[0], ((node*)((v).p))->v);
        }
    }
    /* line 90 */
    printf("\n");
    /* line 93 */
    list ls = {0};
    list_init(&ls);
    /* line 94 */
    list_init(&ls);
    /* line 95 */
    sc_fat e1 = {0};
    node *_fat20 = node__new_ref(0);
    sc_fat_bind(&e1, _fat20, (sc_ref *)((char *)_fat20 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 96 */
    ((node *)(e1).p)->v = 1000;
    /* line 97 */
    sc_fat e2 = {0};
    node *_fat21 = node__new_ref(0);
    sc_fat_bind(&e2, _fat21, (sc_ref *)((char *)_fat21 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 98 */
    ((node *)(e2).p)->v = 2000;
    /* line 99 */
    sc_fat e3 = {0};
    node *_fat22 = node__new_ref(0);
    sc_fat_bind(&e3, _fat22, (sc_ref *)((char *)_fat22 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 100 */
    ((node *)(e3).p)->v = 3000;
    /* line 101 */
    list_push(&ls, ({ sc_fat _ec23 = e1; (sc_thin){_ec23.p, _ec23.tar, (void (*)(void *))node_drop}; }));
    /* line 102 */
    list_push(&ls, ({ sc_fat _ec24 = e2; (sc_thin){_ec24.p, _ec24.tar, (void (*)(void *))node_drop}; }));
    /* line 103 */
    list_push(&ls, ({ sc_fat _ec25 = e3; (sc_thin){_ec25.p, _ec25.tar, (void (*)(void *))node_drop}; }));
    /* line 104 */
    printf("list fwd:");
    /* line 105 */
    {
        list * _fr5 = &ls;
        long _fe5 = (long)list_len(_fr5);
        long _fc5 = 0; (void)_fc5;
        for (long _fi5 = 0; _fi5 < _fe5; _fi5 += 1, _fc5++) {
            sc_thin v = list_get(_fr5, _fi5);
            int i = (int)(_fi5);
            /* line 106 */
            printf(" [%d]=%d", i, ((node*)((v).p))->v);
        }
    }
    /* line 107 */
    printf("\n");
    /* line 108 */
    printf("list rev:");
    /* line 109 */
    {
        list * _fr6 = &ls;
        long _fe6 = (long)list_len(_fr6);
        long _fc6 = 0; (void)_fc6;
        for (long _fi6 = _fe6 - 1 - 0; _fi6 >= 0; _fi6 -= 1, _fc6++) {
            sc_thin v = list_get(_fr6, _fi6);
            /* line 110 */
            printf(" %d", ((node*)((v).p))->v);
        }
    }
    /* line 111 */
    printf("\n");
    /* line 114 */
    array ar = {0};
    /* line 115 */
    array_init(&ar, 4);
    /* line 116 */
    int32_t x0 = 41;
    /* line 117 */
    int32_t x1 = 42;
    /* line 118 */
    int32_t x2 = 43;
    /* line 119 */
    array_push(&ar, ((void*)(&(x0))));
    /* line 120 */
    array_push(&ar, ((void*)(&(x1))));
    /* line 121 */
    array_push(&ar, ((void*)(&(x2))));
    /* line 122 */
    int32_t asum = 0;
    /* line 123 */
    {
        array * _fr7 = &ar;
        long _fe7 = (long)array_len(_fr7);
        long _fc7 = 0; (void)_fc7;
        for (long _fi7 = 0; _fi7 < _fe7; _fi7 += 1, _fc7++) {
            void * v = array_at(_fr7, _fi7);
            /* line 124 */
            asum += ((int32_t*)(v))[0];
        }
    }
    /* line 125 */
    printf("array sum=%d\n", asum);
    /* line 128 */
    dict_drop(&d);
    /* line 129 */
    bst_drop(&t);
    /* line 130 */
    lru_drop(&lc);
    /* line 131 */
    list_drop(&ls);
    /* line 132 */
    array_drop(&ar);
    /* line 133 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&e3, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&e2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&e1, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&c3, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&c2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&c1, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&t4, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&t3, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&t2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&t1, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&d3, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&d2, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&d1, (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
