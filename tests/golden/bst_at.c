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
int32_t sc_dump_keys(sc_bst *t);
sc_fat sc_make(int32_t x);
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
    /* line 17 */
    printf("drop %d\n", _this->v);
}

bool sc_sum_each(const void *key, sc_thin value, void *ctx) {
    /* line 26 */
    sc_acc *a = ((sc_acc*)(ctx));
    /* line 27 */
    a->sum += ((sc_node*)((value).p))->v;
    /* line 28 */
    a->cnt += 1;
    /* line 29 */
    return true;
}

int32_t sc_dump_keys(sc_bst *t) {
    /* line 33 */
    int64_t c = sc_bst_first(t);
    /* line 34 */
    int32_t n = 0;
    /* line 35 */
    while (c != 0) {
        /* line 36 */
        int32_t *kp = ((int32_t*)(sc_bst_key_at(t, c)));
        /* line 37 */
        printf(" %d", kp[0]);
        /* line 38 */
        n += 1;
        /* line 39 */
        c = sc_bst_next(t, c);
    }
    /* line 40 */
    printf("\n");
    /* line 41 */
    return n;
}

sc_fat sc_make(int32_t x) {
    /* line 44 */
    sc_fat p = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 45 */
    ((sc_node *)(p).p)->v = x;
    /* line 46 */
    {
        sc_fat _ret = p;
        return _ret;
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 50 */
    sc_bst ta = {0};
    /* line 51 */
    sc_bst_init(&ta, 0, 4, NULL, NULL);
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
        sc_fat_unbind_d(&ha[i], (void (*)(void *))sc_node_drop);
        ha[i] = sc_make(keys[i] * 10);
        /* line 66 */
        sc_bst_put(&ta, ((const void*)(&(keys[i]))), sc_fat_as_thin(ha[i], (void (*)(void *))sc_node_drop));
        /* line 67 */
        i += 1;
    }
    /* line 68 */
    printf("A len=%llu\n", sc_bst_len(&ta));
    /* line 69 */
    printf("A inorder:");
    /* line 70 */
    sc_dump_keys(&(ta));
    /* line 73 */
    int32_t k40 = 40;
    /* line 74 */
    int32_t k99 = 99;
    /* line 75 */
    bool _sq0 = sc_bst_has(&ta, ((const void*)(&(k40))));
    bool _sq1 = sc_bst_has(&ta, ((const void*)(&(k99))));
    int32_t _sq2 = ((sc_node*)((sc_bst_get(&ta, ((const void*)(&(k40))))).p))->v;
    printf("A has40=%d miss99=%d get40=%d\n", _sq0, _sq1, _sq2);
    /* line 80 */
    sc_acc sa = {0};
    /* line 81 */
    sa.sum = 0;
    /* line 82 */
    sa.cnt = 0;
    /* line 83 */
    sc_bst_each(&ta, sc_sum_each, ((void*)(&(sa))));
    /* line 84 */
    printf("A each sum=%d cnt=%d\n", sa.sum, sa.cnt);
    /* line 87 */
    int32_t fs = 0;
    /* line 88 */
    int64_t c = sc_bst_first(&ta);
    /* line 89 */
    while (c != 0) {
        /* line 90 */
        fs += ((sc_node*)((sc_bst_value_at(&ta, c)).p))->v;
        /* line 91 */
        c = sc_bst_next(&ta, c);
    }
    /* line 92 */
    int32_t bs = 0;
    /* line 93 */
    c = sc_bst_last(&ta);
    /* line 94 */
    while (c != 0) {
        /* line 95 */
        bs += ((sc_node*)((sc_bst_value_at(&ta, c)).p))->v;
        /* line 96 */
        c = sc_bst_prev(&ta, c);
    }
    /* line 97 */
    printf("A fwd_sum=%d bwd_sum=%d\n", fs, bs);
    /* line 100 */
    printf("A idx40=%lld\n", sc_bst_index_of(&ta, ((const void*)(&(k40)))));
    /* line 101 */
    int64_t c0 = sc_bst_at(&ta, 0);
    /* line 102 */
    int32_t *c0k = ((int32_t*)(sc_bst_key_at(&ta, c0)));
    /* line 103 */
    printf("A at0_key=%d at0_val=%d\n", c0k[0], ((sc_node*)((sc_bst_value_at(&ta, c0)).p))->v);
    /* line 106 */
    int32_t k45 = 45;
    /* line 107 */
    int64_t cm = sc_bst_most(&ta, ((const void*)(&(k45))));
    /* line 108 */
    int64_t cl = sc_bst_least(&ta, ((const void*)(&(k45))));
    /* line 109 */
    int32_t *cmk = ((int32_t*)(sc_bst_key_at(&ta, cm)));
    /* line 110 */
    int32_t *clk = ((int32_t*)(sc_bst_key_at(&ta, cl)));
    /* line 111 */
    printf("A most45=%d least45=%d\n", cmk[0], clk[0]);
    /* line 114 */
    sc_fat_unbind_d(&ha[7], (void (*)(void *))sc_node_drop);
    ha[7] = sc_make(999);
    /* line 115 */
    sc_bst_put(&ta, ((const void*)(&(k40))), sc_fat_as_thin(ha[7], (void (*)(void *))sc_node_drop));
    /* line 116 */
    int32_t _sq3 = ((sc_node*)((sc_bst_get(&ta, ((const void*)(&(k40))))).p))->v;
    uint64_t _sq4 = sc_bst_len(&ta);
    printf("A put40b=%d len=%llu\n", _sq3, _sq4);
    /* line 119 */
    int32_t k50 = 50;
    /* line 120 */
    int32_t k20 = 20;
    /* line 121 */
    int32_t k70 = 70;
    /* line 122 */
    bool _sq5 = sc_bst_remove(&ta, ((const void*)(&(k50))));
    bool _sq6 = sc_bst_remove(&ta, ((const void*)(&(k20))));
    bool _sq7 = sc_bst_remove(&ta, ((const void*)(&(k70))));
    bool _sq8 = sc_bst_remove(&ta, ((const void*)(&(k99))));
    uint64_t _sq9 = sc_bst_len(&ta);
    printf("A rm50=%d rm20=%d rm70=%d rmmiss=%d len=%llu\n", _sq5, _sq6, _sq7, _sq8, _sq9);
    /* line 125 */
    printf("A inorder2:");
    /* line 126 */
    sc_dump_keys(&(ta));
    /* line 127 */
    sc_bst_drop(&ta);
    /* line 128 */
    printf("A after_drop len=%llu\n", sc_bst_len(&ta));
    /* line 132 */
    sc_bst tb = {0};
    /* line 133 */
    sc_bst_init(&tb, 1, 4, NULL, NULL);
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
        sc_fat_unbind_d(&hb[i], (void (*)(void *))sc_node_drop);
        hb[i] = sc_make(101 + i);
        /* line 140 */
        sc_bst_put(&tb, ((const void*)(&(bk[i]))), sc_fat_as_thin(hb[i], (void (*)(void *))sc_node_drop));
        /* line 141 */
        i += 1;
    }
    /* line 142 */
    printf("B len=%llu\n", sc_bst_len(&tb));
    /* line 143 */
    printf("B inorder:");
    /* line 144 */
    sc_dump_keys(&(tb));
    /* line 146 */
    int64_t b8 = sc_bst_at(&tb, 7);
    /* line 147 */
    int32_t *b8k = ((int32_t*)(sc_bst_key_at(&tb, b8)));
    /* line 148 */
    printf("B at7_key=%d idx_key10=%lld\n", b8k[0], sc_bst_index_of(&tb, ((const void*)(&(bk[9])))));
    /* line 151 */
    i = 1;
    /* line 152 */
    while (i < 15) {
        /* line 153 */
        sc_bst_remove(&tb, ((const void*)(&(bk[i]))));
        /* line 154 */
        i += 2;
    }
    /* line 155 */
    printf("B after_rm_even len=%llu inorder:", sc_bst_len(&tb));
    /* line 156 */
    sc_dump_keys(&(tb));
    /* line 157 */
    sc_bst_drop(&tb);
    /* line 160 */
    sc_bst tc = {0};
    /* line 161 */
    sc_bst_init(&tc, 0, 0 - 1, NULL, NULL);
    /* line 162 */
    sc_fat hc[4] = {0};
    /* line 163 */
    sc_fat_unbind_d(&hc[0], (void (*)(void *))sc_node_drop);
    hc[0] = sc_make(4);
    /* line 164 */
    sc_bst_put(&tc, "delta", sc_fat_as_thin(hc[0], (void (*)(void *))sc_node_drop));
    /* line 165 */
    sc_fat_unbind_d(&hc[1], (void (*)(void *))sc_node_drop);
    hc[1] = sc_make(1);
    /* line 166 */
    sc_bst_put(&tc, "alpha", sc_fat_as_thin(hc[1], (void (*)(void *))sc_node_drop));
    /* line 167 */
    sc_fat_unbind_d(&hc[2], (void (*)(void *))sc_node_drop);
    hc[2] = sc_make(3);
    /* line 168 */
    sc_bst_put(&tc, "charlie", sc_fat_as_thin(hc[2], (void (*)(void *))sc_node_drop));
    /* line 169 */
    sc_fat_unbind_d(&hc[3], (void (*)(void *))sc_node_drop);
    hc[3] = sc_make(2);
    /* line 170 */
    sc_bst_put(&tc, "bravo", sc_fat_as_thin(hc[3], (void (*)(void *))sc_node_drop));
    /* line 171 */
    printf("C len=%llu inorder:", sc_bst_len(&tc));
    /* line 172 */
    int64_t cc = sc_bst_first(&tc);
    /* line 173 */
    while (cc != 0) {
        /* line 174 */
        char *_sq10 = ((char*)(sc_bst_key_at(&tc, cc)));
        int32_t _sq11 = ((sc_node*)((sc_bst_value_at(&tc, cc)).p))->v;
        printf(" %s=%d", _sq10, _sq11);
        /* line 175 */
        cc = sc_bst_next(&tc, cc);
    }
    /* line 176 */
    printf("\n");
    /* line 177 */
    int32_t _sq12 = ((sc_node*)((sc_bst_get(&tc, ((const void*)("charlie")))).p))->v;
    bool _sq13 = sc_bst_has(&tc, ((const void*)("zzz")));
    printf("C get_charlie=%d has_x=%d\n", _sq12, _sq13);
    /* line 179 */
    sc_bst_remove(&tc, ((const void*)("alpha")));
    /* line 180 */
    printf("C after_rm len=%llu\n", sc_bst_len(&tc));
    /* line 181 */
    sc_bst_drop(&tc);
    /* line 184 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hc[_k0], (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (15); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (8); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
