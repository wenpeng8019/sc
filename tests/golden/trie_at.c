/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
sc_fat make(int32_t x);
uint8_t print_key(const char *key, sc_thin value, void *ctx);
uint8_t dump_kv(const char *key, sc_thin value, void *ctx);
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

sc_fat make(int32_t x) {
    /* line 18 */
    sc_fat p = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 19 */
    ((node *)(p).p)->v = x;
    /* line 20 */
    {
        sc_fat _ret = p;
        return _ret;
    }
}

uint8_t print_key(const char *key, sc_thin value, void *ctx) {
    /* line 24 */
    printf(" %s", key);
    /* line 25 */
    return true;
}

uint8_t dump_kv(const char *key, sc_thin value, void *ctx) {
    /* line 29 */
    int64_t *cnt = ((int64_t*)(ctx));
    /* line 30 */
    cnt[0] += 1;
    /* line 31 */
    printf(" %s=%d", key, ((node*)((value).p))->v);
    /* line 32 */
    return true;
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 35 */
    trie tt = {0};
    trie_init(&tt);
    /* line 38 */
    sc_fat ha[6] = {0};
    /* line 39 */
    sc_fat_unbind_d(&ha[0], (void (*)(void *))node_drop);
    ha[0] = make(10);
    /* line 40 */
    trie_put(&tt, "cat", ({ sc_fat _ec1 = ha[0]; (sc_thin){_ec1.p, _ec1.tar, (void (*)(void *))node_drop}; }));
    /* line 41 */
    sc_fat_unbind_d(&ha[1], (void (*)(void *))node_drop);
    ha[1] = make(20);
    /* line 42 */
    trie_put(&tt, "car", ({ sc_fat _ec2 = ha[1]; (sc_thin){_ec2.p, _ec2.tar, (void (*)(void *))node_drop}; }));
    /* line 43 */
    sc_fat_unbind_d(&ha[2], (void (*)(void *))node_drop);
    ha[2] = make(30);
    /* line 44 */
    trie_put(&tt, "card", ({ sc_fat _ec3 = ha[2]; (sc_thin){_ec3.p, _ec3.tar, (void (*)(void *))node_drop}; }));
    /* line 45 */
    sc_fat_unbind_d(&ha[3], (void (*)(void *))node_drop);
    ha[3] = make(40);
    /* line 46 */
    trie_put(&tt, "dog", ({ sc_fat _ec4 = ha[3]; (sc_thin){_ec4.p, _ec4.tar, (void (*)(void *))node_drop}; }));
    /* line 47 */
    sc_fat_unbind_d(&ha[4], (void (*)(void *))node_drop);
    ha[4] = make(50);
    /* line 48 */
    trie_put(&tt, "do", ({ sc_fat _ec5 = ha[4]; (sc_thin){_ec5.p, _ec5.tar, (void (*)(void *))node_drop}; }));
    /* line 49 */
    sc_fat_unbind_d(&ha[5], (void (*)(void *))node_drop);
    ha[5] = make(60);
    /* line 50 */
    trie_put(&tt, "cart", ({ sc_fat _ec6 = ha[5]; (sc_thin){_ec6.p, _ec6.tar, (void (*)(void *))node_drop}; }));
    /* line 51 */
    printf("A len=%llu\n", trie_len(&tt));
    /* line 52 */
    printf("A has: car=%d ca=%d dog=%d xyz=%d\n", trie_has(&tt, "car"), trie_has(&tt, "ca"), trie_has(&tt, "dog"), trie_has(&tt, "xyz"));
    /* line 54 */
    printf("A get card=%d\n", ((node*)((trie_get(&tt, "card")).p))->v);
    /* line 57 */
    sc_fat hrep = {0};
    hrep = make(99);
    /* line 58 */
    trie_put(&tt, "car", ({ sc_fat _ec7 = hrep; (sc_thin){_ec7.p, _ec7.tar, (void (*)(void *))node_drop}; }));
    /* line 59 */
    printf("A replace car=%d len=%llu\n", ((node*)((trie_get(&tt, "car")).p))->v, trie_len(&tt));
    /* line 62 */
    printf("B has_prefix: ca=%d car=%d xy=%d\n", trie_has_prefix(&tt, "ca"), trie_has_prefix(&tt, "car"), trie_has_prefix(&tt, "xy"));
    /* line 64 */
    printf("B count: ca=%llu car=%llu all=%llu\n", trie_count_prefix(&tt, "ca"), trie_count_prefix(&tt, "car"), trie_count_prefix(&tt, ""));
    /* line 66 */
    printf("B each_ca:");
    /* line 67 */
    trie_each_prefix(&tt, "ca", print_key, NULL);
    /* line 68 */
    printf("\n");
    /* line 69 */
    printf("B each_all:");
    /* line 70 */
    trie_each(&tt, print_key, NULL);
    /* line 71 */
    printf("\n");
    /* line 72 */
    printf("B each_car_kv:");
    /* line 73 */
    int64_t total = 0;
    /* line 74 */
    trie_each_prefix(&tt, "car", dump_kv, ((void*)(&(total))));
    /* line 75 */
    printf(" (n=%lld)\n", total);
    /* line 76 */
    printf("B longest: cartoon=%lld dog=%lld do=%lld xyz=%lld\n", trie_longest_prefix(&tt, "cartoon"), trie_longest_prefix(&tt, "dog"), trie_longest_prefix(&tt, "do"), trie_longest_prefix(&tt, "xyz"));
    /* line 81 */
    sc_fat hempty = {0};
    hempty = make(70);
    /* line 82 */
    trie_put(&tt, "", ({ sc_fat _ec8 = hempty; (sc_thin){_ec8.p, _ec8.tar, (void (*)(void *))node_drop}; }));
    /* line 83 */
    printf("C empty: has=%d get=%d all=%llu lp_zzz=%lld\n", trie_has(&tt, ""), ((node*)((trie_get(&tt, "")).p))->v, trie_count_prefix(&tt, ""), trie_longest_prefix(&tt, "zzz"));
    /* line 87 */
    printf("C remove card=%d again=%d miss=%d\n", trie_remove(&tt, "card"), trie_remove(&tt, "card"), trie_remove(&tt, "nope"));
    /* line 89 */
    printf("C after_remove: has_card=%d has_car=%d has_cart=%d count_car=%llu len=%llu\n", trie_has(&tt, "card"), trie_has(&tt, "car"), trie_has(&tt, "cart"), trie_count_prefix(&tt, "car"), trie_len(&tt));
    /* line 92 */
    trie_clear(&tt);
    /* line 93 */
    printf("C after_clear len=%llu has_car=%d\n", trie_len(&tt), trie_has(&tt, "car"));
    /* line 94 */
    trie_drop(&tt);
    /* line 97 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&hempty, (void (*)(void *))node_drop);
        sc_fat_unbind_d(&hrep, (void (*)(void *))node_drop);
        for (size_t _k0 = (6); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
