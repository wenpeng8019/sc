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

uint8_t sum_each(const void *key, sc_thin value, void *ctx);
int32_t dump_keys(bst *t);
sc_fat make(int32_t x);
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
    /* line 17 */
    printf("drop %d\n", _this->v);
}

uint8_t sum_each(const void *key, sc_thin value, void *ctx) {
    /* line 26 */
    acc *a = ((acc*)(ctx));
    /* line 27 */
    a->sum += ((node*)((value).p))->v;
    /* line 28 */
    a->cnt += 1;
    /* line 29 */
    return true;
}

int32_t dump_keys(bst *t) {
    /* line 33 */
    int64_t c = bst_first(t);
    /* line 34 */
    int32_t n = 0;
    /* line 35 */
    while (c != 0) {
        /* line 36 */
        int32_t *kp = ((int32_t*)(bst_key_at(t, c)));
        /* line 37 */
        printf(" %d", kp[0]);
        /* line 38 */
        n += 1;
        /* line 39 */
        c = bst_next(t, c);
    }
    /* line 40 */
    printf("\n");
    /* line 41 */
    return n;
}

sc_fat make(int32_t x) {
    /* line 44 */
    sc_fat p = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 45 */
    ((node *)(p).p)->v = x;
    /* line 46 */
    {
        sc_fat _ret = p;
        return _ret;
    }
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 50 */
    bst ta = {0};
    /* line 51 */
    bst_init(&ta, 0, 4, NULL, NULL);
    /* line 54 */
    sc_fat ha[8] = {0};
    /* line 55 */
    int32_t keys[7];
    /* line 56 */
    keys[0] = 50;
    /* line 57 */
    keys[1] = 30;
    /* line 58 */
    keys[2] = 70;
    /* line 59 */
    keys[3] = 20;
    /* line 60 */
    keys[4] = 40;
    /* line 61 */
    keys[5] = 60;
    /* line 62 */
    keys[6] = 80;
    /* line 63 */
    int32_t i = 0;
    /* line 64 */
    while (i < 7) {
        /* line 65 */
        sc_fat_unbind_d(&ha[i], (void (*)(void *))node_drop);
        ha[i] = make(keys[i] * 10);
        /* line 66 */
        bst_put(&ta, ((const void*)(&(keys[i]))), ({ sc_fat _ec1 = ha[i]; (sc_thin){_ec1.p, _ec1.tar, (void (*)(void *))node_drop}; }));
        /* line 67 */
        i += 1;
    }
    /* line 68 */
    printf("A len=%llu\n", bst_len(&ta));
    /* line 69 */
    printf("A inorder:");
    /* line 70 */
    dump_keys(&(ta));
    /* line 73 */
    int32_t k40 = 40;
    /* line 74 */
    int32_t k99 = 99;
    /* line 75 */
    printf("A has40=%d miss99=%d get40=%d\n", bst_has(&ta, ((const void*)(&(k40)))), bst_has(&ta, ((const void*)(&(k99)))), ((node*)((bst_get(&ta, ((const void*)(&(k40))))).p))->v);
    /* line 80 */
    acc sa = {0};
    /* line 81 */
    sa.sum = 0;
    /* line 82 */
    sa.cnt = 0;
    /* line 83 */
    bst_each(&ta, sum_each, ((void*)(&(sa))));
    /* line 84 */
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt);
    /* line 87 */
    int32_t fs = 0;
    /* line 88 */
    int64_t c = bst_first(&ta);
    /* line 89 */
    while (c != 0) {
        /* line 90 */
        fs += ((node*)((bst_value_at(&ta, c)).p))->v;
        /* line 91 */
        c = bst_next(&ta, c);
    }
    /* line 92 */
    int32_t bs = 0;
    /* line 93 */
    c = bst_last(&ta);
    /* line 94 */
    while (c != 0) {
        /* line 95 */
        bs += ((node*)((bst_value_at(&ta, c)).p))->v;
        /* line 96 */
        c = bst_prev(&ta, c);
    }
    /* line 97 */
    printf("A fwd_sum=%d bwd_sum=%d\n", fs, bs);
    /* line 100 */
    printf("A idx40=%lld\n", bst_index_of(&ta, ((const void*)(&(k40)))));
    /* line 101 */
    int64_t c0 = bst_at(&ta, 0);
    /* line 102 */
    int32_t *c0k = ((int32_t*)(bst_key_at(&ta, c0)));
    /* line 103 */
    printf("A at0_key=%d at0_val=%d\n", c0k[0], ((node*)((bst_value_at(&ta, c0)).p))->v);
    /* line 106 */
    int32_t k45 = 45;
    /* line 107 */
    int64_t cm = bst_most(&ta, ((const void*)(&(k45))));
    /* line 108 */
    int64_t cl = bst_least(&ta, ((const void*)(&(k45))));
    /* line 109 */
    int32_t *cmk = ((int32_t*)(bst_key_at(&ta, cm)));
    /* line 110 */
    int32_t *clk = ((int32_t*)(bst_key_at(&ta, cl)));
    /* line 111 */
    printf("A most45=%d least45=%d\n", cmk[0], clk[0]);
    /* line 114 */
    sc_fat_unbind_d(&ha[7], (void (*)(void *))node_drop);
    ha[7] = make(999);
    /* line 115 */
    bst_put(&ta, ((const void*)(&(k40))), ({ sc_fat _ec2 = ha[7]; (sc_thin){_ec2.p, _ec2.tar, (void (*)(void *))node_drop}; }));
    /* line 116 */
    printf("A put40b=%d len=%llu\n", ((node*)((bst_get(&ta, ((const void*)(&(k40))))).p))->v, bst_len(&ta));
    /* line 119 */
    int32_t k50 = 50;
    /* line 120 */
    int32_t k20 = 20;
    /* line 121 */
    int32_t k70 = 70;
    /* line 122 */
    printf("A rm50=%d rm20=%d rm70=%d rmmiss=%d len=%llu\n", bst_remove(&ta, ((const void*)(&(k50)))), bst_remove(&ta, ((const void*)(&(k20)))), bst_remove(&ta, ((const void*)(&(k70)))), bst_remove(&ta, ((const void*)(&(k99)))), bst_len(&ta));
    /* line 125 */
    printf("A inorder2:");
    /* line 126 */
    dump_keys(&(ta));
    /* line 127 */
    bst_drop(&ta);
    /* line 128 */
    printf("A after_drop len=%llu\n", bst_len(&ta));
    /* line 132 */
    bst tb = {0};
    /* line 133 */
    bst_init(&tb, 1, 4, NULL, NULL);
    /* line 134 */
    sc_fat hb[15] = {0};
    /* line 135 */
    int32_t bk[15];
    /* line 136 */
    i = 0;
    /* line 137 */
    while (i < 15) {
        /* line 138 */
        bk[i] = (i + 1);
        /* line 139 */
        sc_fat_unbind_d(&hb[i], (void (*)(void *))node_drop);
        hb[i] = make(101 + i);
        /* line 140 */
        bst_put(&tb, ((const void*)(&(bk[i]))), ({ sc_fat _ec3 = hb[i]; (sc_thin){_ec3.p, _ec3.tar, (void (*)(void *))node_drop}; }));
        /* line 141 */
        i += 1;
    }
    /* line 142 */
    printf("B len=%llu\n", bst_len(&tb));
    /* line 143 */
    printf("B inorder:");
    /* line 144 */
    dump_keys(&(tb));
    /* line 146 */
    int64_t b8 = bst_at(&tb, 7);
    /* line 147 */
    int32_t *b8k = ((int32_t*)(bst_key_at(&tb, b8)));
    /* line 148 */
    printf("B at7_key=%d idx_key10=%lld\n", b8k[0], bst_index_of(&tb, ((const void*)(&(bk[9])))));
    /* line 151 */
    i = 1;
    /* line 152 */
    while (i < 15) {
        /* line 153 */
        bst_remove(&tb, ((const void*)(&(bk[i]))));
        /* line 154 */
        i += 2;
    }
    /* line 155 */
    printf("B after_rm_even len=%llu inorder:", bst_len(&tb));
    /* line 156 */
    dump_keys(&(tb));
    /* line 157 */
    bst_drop(&tb);
    /* line 160 */
    bst tc = {0};
    /* line 161 */
    bst_init(&tc, 0, 0 - 1, NULL, NULL);
    /* line 162 */
    sc_fat hc[4] = {0};
    /* line 163 */
    sc_fat_unbind_d(&hc[0], (void (*)(void *))node_drop);
    hc[0] = make(4);
    /* line 164 */
    bst_put(&tc, "delta", ({ sc_fat _ec4 = hc[0]; (sc_thin){_ec4.p, _ec4.tar, (void (*)(void *))node_drop}; }));
    /* line 165 */
    sc_fat_unbind_d(&hc[1], (void (*)(void *))node_drop);
    hc[1] = make(1);
    /* line 166 */
    bst_put(&tc, "alpha", ({ sc_fat _ec5 = hc[1]; (sc_thin){_ec5.p, _ec5.tar, (void (*)(void *))node_drop}; }));
    /* line 167 */
    sc_fat_unbind_d(&hc[2], (void (*)(void *))node_drop);
    hc[2] = make(3);
    /* line 168 */
    bst_put(&tc, "charlie", ({ sc_fat _ec6 = hc[2]; (sc_thin){_ec6.p, _ec6.tar, (void (*)(void *))node_drop}; }));
    /* line 169 */
    sc_fat_unbind_d(&hc[3], (void (*)(void *))node_drop);
    hc[3] = make(2);
    /* line 170 */
    bst_put(&tc, "bravo", ({ sc_fat _ec7 = hc[3]; (sc_thin){_ec7.p, _ec7.tar, (void (*)(void *))node_drop}; }));
    /* line 171 */
    printf("C len=%llu inorder:", bst_len(&tc));
    /* line 172 */
    int64_t cc = bst_first(&tc);
    /* line 173 */
    while (cc != 0) {
        /* line 174 */
        printf(" %s=%d", ((char*)(bst_key_at(&tc, cc))), ((node*)((bst_value_at(&tc, cc)).p))->v);
        /* line 175 */
        cc = bst_next(&tc, cc);
    }
    /* line 176 */
    printf("\n");
    /* line 177 */
    printf("C get_charlie=%d has_x=%d\n", ((node*)((bst_get(&tc, ((const void*)("charlie")))).p))->v, bst_has(&tc, ((const void*)("zzz"))));
    /* line 179 */
    bst_remove(&tc, ((const void*)("alpha")));
    /* line 180 */
    printf("C after_rm len=%llu\n", bst_len(&tc));
    /* line 181 */
    bst_drop(&tc);
    /* line 184 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hc[_k0], (void (*)(void *))node_drop);
        for (size_t _k0 = (15); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))node_drop);
        for (size_t _k0 = (8); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
