/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
sc_fat sc_make(int32_t x);
bool sc_print_key(const char *key, sc_thin value, void *ctx);
bool sc_dump_kv(const char *key, sc_thin value, void *ctx);
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

bool sc_print_key(const char *key, sc_thin value, void *ctx) {
    /* line 24 */
    printf(" %s", key);
    /* line 25 */
    return true;
}

bool sc_dump_kv(const char *key, sc_thin value, void *ctx) {
    /* line 29 */
    int64_t *cnt = ((int64_t*)(ctx));
    /* line 30 */
    cnt[0] += 1;
    /* line 31 */
    printf(" %s=%d", key, ((sc_node*)((value).p))->v);
    /* line 32 */
    return true;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 35 */
    sc_trie tt = {0};
    sc_trie_init(&tt);
    /* line 38 */
    sc_fat ha[6] = {0};
    /* line 39 */
    sc_fat_unbind_d(&ha[0], (void (*)(void *))sc_node_drop);
    ha[0] = sc_make(10);
    /* line 40 */
    sc_trie_put(&tt, "cat", sc_fat_as_thin(ha[0], (void (*)(void *))sc_node_drop));
    /* line 41 */
    sc_fat_unbind_d(&ha[1], (void (*)(void *))sc_node_drop);
    ha[1] = sc_make(20);
    /* line 42 */
    sc_trie_put(&tt, "car", sc_fat_as_thin(ha[1], (void (*)(void *))sc_node_drop));
    /* line 43 */
    sc_fat_unbind_d(&ha[2], (void (*)(void *))sc_node_drop);
    ha[2] = sc_make(30);
    /* line 44 */
    sc_trie_put(&tt, "card", sc_fat_as_thin(ha[2], (void (*)(void *))sc_node_drop));
    /* line 45 */
    sc_fat_unbind_d(&ha[3], (void (*)(void *))sc_node_drop);
    ha[3] = sc_make(40);
    /* line 46 */
    sc_trie_put(&tt, "dog", sc_fat_as_thin(ha[3], (void (*)(void *))sc_node_drop));
    /* line 47 */
    sc_fat_unbind_d(&ha[4], (void (*)(void *))sc_node_drop);
    ha[4] = sc_make(50);
    /* line 48 */
    sc_trie_put(&tt, "do", sc_fat_as_thin(ha[4], (void (*)(void *))sc_node_drop));
    /* line 49 */
    sc_fat_unbind_d(&ha[5], (void (*)(void *))sc_node_drop);
    ha[5] = sc_make(60);
    /* line 50 */
    sc_trie_put(&tt, "cart", sc_fat_as_thin(ha[5], (void (*)(void *))sc_node_drop));
    /* line 51 */
    printf("A len=%llu\n", sc_trie_len(&tt));
    /* line 52 */
    bool _sq0 = sc_trie_has(&tt, "car");
    bool _sq1 = sc_trie_has(&tt, "ca");
    bool _sq2 = sc_trie_has(&tt, "dog");
    bool _sq3 = sc_trie_has(&tt, "xyz");
    printf("A has: car=%d ca=%d dog=%d xyz=%d\n", _sq0, _sq1, _sq2, _sq3);
    /* line 54 */
    printf("A get card=%d\n", ((sc_node*)((sc_trie_get(&tt, "card")).p))->v);
    /* line 57 */
    sc_fat hrep = {0};
    hrep = sc_make(99);
    /* line 58 */
    sc_trie_put(&tt, "car", sc_fat_as_thin(hrep, (void (*)(void *))sc_node_drop));
    /* line 59 */
    int32_t _sq4 = ((sc_node*)((sc_trie_get(&tt, "car")).p))->v;
    uint64_t _sq5 = sc_trie_len(&tt);
    printf("A replace car=%d len=%llu\n", _sq4, _sq5);
    /* line 62 */
    bool _sq6 = sc_trie_has_prefix(&tt, "ca");
    bool _sq7 = sc_trie_has_prefix(&tt, "car");
    bool _sq8 = sc_trie_has_prefix(&tt, "xy");
    printf("B has_prefix: ca=%d car=%d xy=%d\n", _sq6, _sq7, _sq8);
    /* line 64 */
    uint64_t _sq9 = sc_trie_count_prefix(&tt, "ca");
    uint64_t _sq10 = sc_trie_count_prefix(&tt, "car");
    uint64_t _sq11 = sc_trie_count_prefix(&tt, "");
    printf("B count: ca=%llu car=%llu all=%llu\n", _sq9, _sq10, _sq11);
    /* line 66 */
    printf("B each_ca:");
    /* line 67 */
    sc_trie_each_prefix(&tt, "ca", sc_print_key, NULL);
    /* line 68 */
    printf("\n");
    /* line 69 */
    printf("B each_all:");
    /* line 70 */
    sc_trie_each(&tt, sc_print_key, NULL);
    /* line 71 */
    printf("\n");
    /* line 72 */
    printf("B each_car_kv:");
    /* line 73 */
    int64_t total = 0;
    /* line 74 */
    sc_trie_each_prefix(&tt, "car", sc_dump_kv, ((void*)(&(total))));
    /* line 75 */
    printf(" (n=%lld)\n", total);
    /* line 76 */
    int64_t _sq12 = sc_trie_longest_prefix(&tt, "cartoon");
    int64_t _sq13 = sc_trie_longest_prefix(&tt, "dog");
    int64_t _sq14 = sc_trie_longest_prefix(&tt, "do");
    int64_t _sq15 = sc_trie_longest_prefix(&tt, "xyz");
    printf("B longest: cartoon=%lld dog=%lld do=%lld xyz=%lld\n", _sq12, _sq13, _sq14, _sq15);
    /* line 81 */
    sc_fat hempty = {0};
    hempty = sc_make(70);
    /* line 82 */
    sc_trie_put(&tt, "", sc_fat_as_thin(hempty, (void (*)(void *))sc_node_drop));
    /* line 83 */
    bool _sq16 = sc_trie_has(&tt, "");
    int32_t _sq17 = ((sc_node*)((sc_trie_get(&tt, "")).p))->v;
    uint64_t _sq18 = sc_trie_count_prefix(&tt, "");
    int64_t _sq19 = sc_trie_longest_prefix(&tt, "zzz");
    printf("C empty: has=%d get=%d all=%llu lp_zzz=%lld\n", _sq16, _sq17, _sq18, _sq19);
    /* line 87 */
    bool _sq20 = sc_trie_remove(&tt, "card");
    bool _sq21 = sc_trie_remove(&tt, "card");
    bool _sq22 = sc_trie_remove(&tt, "nope");
    printf("C remove card=%d again=%d miss=%d\n", _sq20, _sq21, _sq22);
    /* line 89 */
    bool _sq23 = sc_trie_has(&tt, "card");
    bool _sq24 = sc_trie_has(&tt, "car");
    bool _sq25 = sc_trie_has(&tt, "cart");
    uint64_t _sq26 = sc_trie_count_prefix(&tt, "car");
    uint64_t _sq27 = sc_trie_len(&tt);
    printf("C after_remove: has_card=%d has_car=%d has_cart=%d count_car=%llu len=%llu\n", _sq23, _sq24, _sq25, _sq26, _sq27);
    /* line 92 */
    sc_trie_clear(&tt);
    /* line 93 */
    uint64_t _sq28 = sc_trie_len(&tt);
    bool _sq29 = sc_trie_has(&tt, "car");
    printf("C after_clear len=%llu has_car=%d\n", _sq28, _sq29);
    /* line 94 */
    sc_trie_drop(&tt);
    /* line 97 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&hempty, (void (*)(void *))sc_node_drop);
        sc_fat_unbind_d(&hrep, (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (6); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
