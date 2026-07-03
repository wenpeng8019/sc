/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
sc_fat sc_make(int32_t x);
bool sc_dump_str(const void *key, sc_thin value, void *ctx);
bool sc_dump_int(const void *key, sc_thin value, void *ctx);
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
    /* line 14 */
    printf("drop %d\n", _this->v);
}

sc_fat sc_make(int32_t x) {
    /* line 18 */
    sc_fat p = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 19 */
    ((sc_node *)(p).p)->v = x;
    /* line 20 */
    {
        sc_fat _ret = p;
        return _ret;
    }
}

bool sc_dump_str(const void *key, sc_thin value, void *ctx) {
    /* line 24 */
    printf(" %s=%d", ((char*)(key)), ((sc_node*)((value).p))->v);
    /* line 25 */
    return true;
}

bool sc_dump_int(const void *key, sc_thin value, void *ctx) {
    /* line 29 */
    int32_t *kp = ((int32_t*)(key));
    /* line 30 */
    printf(" %d=%d", kp[0], ((sc_node*)((value).p))->v);
    /* line 31 */
    return true;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 35 */
    sc_lru ca = {0};
    /* line 36 */
    sc_lru_init(&ca, 0 - 1, 3);
    /* line 37 */
    sc_fat ha[5] = {0};
    /* line 38 */
    sc_fat_unbind_d(&ha[0], (void (*)(void *))sc_node_drop);
    ha[0] = sc_make(1);
    /* line 39 */
    sc_lru_put(&ca, "a", sc_fat_as_thin(ha[0], (void (*)(void *))sc_node_drop));
    /* line 40 */
    sc_fat_unbind_d(&ha[1], (void (*)(void *))sc_node_drop);
    ha[1] = sc_make(2);
    /* line 41 */
    sc_lru_put(&ca, "b", sc_fat_as_thin(ha[1], (void (*)(void *))sc_node_drop));
    /* line 42 */
    sc_fat_unbind_d(&ha[2], (void (*)(void *))sc_node_drop);
    ha[2] = sc_make(3);
    /* line 43 */
    sc_lru_put(&ca, "c", sc_fat_as_thin(ha[2], (void (*)(void *))sc_node_drop));
    /* line 44 */
    uint64_t _sq0 = sc_lru_len(&ca);
    uint64_t _sq1 = sc_lru_cap(&ca);
    char *_sq2 = ((char*)(sc_lru_mru_key(&ca)));
    char *_sq3 = ((char*)(sc_lru_lru_key(&ca)));
    printf("A len=%llu cap=%llu mru=%s lru=%s\n", _sq0, _sq1, _sq2, _sq3);
    /* line 48 */
    int32_t _sq4 = ((sc_node*)((sc_lru_peek(&ca, "a")).p))->v;
    char *_sq5 = ((char*)(sc_lru_lru_key(&ca)));
    printf("A peek_a=%d lru_still=%s\n", _sq4, _sq5);
    /* line 50 */
    int32_t _sq6 = ((sc_node*)((sc_lru_get(&ca, "a")).p))->v;
    char *_sq7 = ((char*)(sc_lru_mru_key(&ca)));
    char *_sq8 = ((char*)(sc_lru_lru_key(&ca)));
    printf("A get_a=%d mru=%s lru=%s\n", _sq6, _sq7, _sq8);
    /* line 54 */
    sc_fat_unbind_d(&ha[3], (void (*)(void *))sc_node_drop);
    ha[3] = sc_make(4);
    /* line 55 */
    sc_lru_put(&ca, "d", sc_fat_as_thin(ha[3], (void (*)(void *))sc_node_drop));
    /* line 56 */
    bool _sq9 = sc_lru_has(&ca, "b");
    bool _sq10 = sc_lru_has(&ca, "d");
    uint64_t _sq11 = sc_lru_len(&ca);
    printf("A after_put_d: has_b=%d has_d=%d len=%llu\n", _sq9, _sq10, _sq11);
    /* line 58 */
    printf("A each:");
    /* line 59 */
    sc_lru_each(&ca, sc_dump_str, NULL);
    /* line 60 */
    printf("\n");
    /* line 63 */
    sc_fat_unbind_d(&ha[4], (void (*)(void *))sc_node_drop);
    ha[4] = sc_make(99);
    /* line 64 */
    sc_lru_put(&ca, "a", sc_fat_as_thin(ha[4], (void (*)(void *))sc_node_drop));
    /* line 65 */
    int32_t _sq12 = ((sc_node*)((sc_lru_get(&ca, "a")).p))->v;
    uint64_t _sq13 = sc_lru_len(&ca);
    printf("A replace_a=%d len=%llu\n", _sq12, _sq13);
    /* line 66 */
    printf("A each2:");
    /* line 67 */
    sc_lru_each(&ca, sc_dump_str, NULL);
    /* line 68 */
    printf("\n");
    /* line 69 */
    sc_lru_drop(&ca);
    /* line 72 */
    sc_lru cb = {0};
    /* line 73 */
    sc_lru_init(&cb, 4, 0);
    /* line 74 */
    int32_t keys[4];
    /* line 75 */
    keys[0] = 10;
    /* line 76 */
    keys[1] = 20;
    /* line 77 */
    keys[2] = 30;
    /* line 78 */
    keys[3] = 40;
    /* line 79 */
    sc_fat hb[4] = {0};
    /* line 80 */
    int32_t i = 0;
    /* line 81 */
    while (i < 4) {
        /* line 82 */
        sc_fat_unbind_d(&hb[i], (void (*)(void *))sc_node_drop);
        hb[i] = sc_make((keys[i] + 1) * 10);
        /* line 83 */
        sc_lru_put(&cb, ((const void*)(&(keys[i]))), sc_fat_as_thin(hb[i], (void (*)(void *))sc_node_drop));
        /* line 84 */
        i += 1;
    }
    /* line 86 */
    int32_t *mk = ((int32_t*)(sc_lru_mru_key(&cb)));
    /* line 87 */
    int32_t *lk = ((int32_t*)(sc_lru_lru_key(&cb)));
    /* line 88 */
    uint64_t _sq14 = sc_lru_len(&cb);
    uint64_t _sq15 = sc_lru_cap(&cb);
    bool _sq16 = sc_lru_is_empty(&cb);
    printf("B len=%llu cap=%llu mru=%d lru=%d empty=%d\n", _sq14, _sq15, mk[0], lk[0], _sq16);
    /* line 92 */
    bool _sq17 = sc_lru_remove(&cb, ((const void*)(&(keys[1]))));
    bool _sq18 = sc_lru_remove(&cb, ((const void*)(&(keys[1]))));
    bool _sq19 = sc_lru_has(&cb, ((const void*)(&(keys[1]))));
    uint64_t _sq20 = sc_lru_len(&cb);
    printf("B remove20=%d miss=%d has20=%d len=%llu\n", _sq17, _sq18, _sq19, _sq20);
    /* line 97 */
    sc_lru_set_cap(&cb, 2);
    /* line 98 */
    int32_t *lk2 = ((int32_t*)(sc_lru_lru_key(&cb)));
    /* line 99 */
    uint64_t _sq21 = sc_lru_cap(&cb);
    uint64_t _sq22 = sc_lru_len(&cb);
    bool _sq23 = sc_lru_has(&cb, ((const void*)(&(keys[0]))));
    printf("B after_set_cap2: cap=%llu len=%llu has10=%d lru=%d\n", _sq21, _sq22, _sq23, lk2[0]);
    /* line 101 */
    printf("B each:");
    /* line 102 */
    sc_lru_each(&cb, sc_dump_int, NULL);
    /* line 103 */
    printf("\n");
    /* line 104 */
    sc_lru_drop(&cb);
    /* line 107 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (5); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
