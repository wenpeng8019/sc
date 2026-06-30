/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
sc_fat make(int32_t x);
uint8_t dump_str(const void *key, sc_thin value, void *ctx);
uint8_t dump_int(const void *key, sc_thin value, void *ctx);
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

uint8_t dump_str(const void *key, sc_thin value, void *ctx) {
    /* line 24 */
    printf(" %s=%d", ((char*)(key)), ((node*)((value).p))->v);
    /* line 25 */
    return true;
}

uint8_t dump_int(const void *key, sc_thin value, void *ctx) {
    /* line 29 */
    int32_t *kp = ((int32_t*)(key));
    /* line 30 */
    printf(" %d=%d", kp[0], ((node*)((value).p))->v);
    /* line 31 */
    return true;
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 35 */
    lru ca = {0};
    /* line 36 */
    lru_init(&ca, 0 - 1, 3);
    /* line 37 */
    sc_fat ha[5] = {0};
    /* line 38 */
    sc_fat_unbind_d(&ha[0], (void (*)(void *))node_drop);
    ha[0] = make(1);
    /* line 39 */
    lru_put(&ca, "a", ({ sc_fat _ec1 = ha[0]; (sc_thin){_ec1.p, _ec1.tar, (void (*)(void *))node_drop}; }));
    /* line 40 */
    sc_fat_unbind_d(&ha[1], (void (*)(void *))node_drop);
    ha[1] = make(2);
    /* line 41 */
    lru_put(&ca, "b", ({ sc_fat _ec2 = ha[1]; (sc_thin){_ec2.p, _ec2.tar, (void (*)(void *))node_drop}; }));
    /* line 42 */
    sc_fat_unbind_d(&ha[2], (void (*)(void *))node_drop);
    ha[2] = make(3);
    /* line 43 */
    lru_put(&ca, "c", ({ sc_fat _ec3 = ha[2]; (sc_thin){_ec3.p, _ec3.tar, (void (*)(void *))node_drop}; }));
    /* line 44 */
    printf("A len=%llu cap=%llu mru=%s lru=%s\n", lru_len(&ca), lru_cap(&ca), ((char*)(lru_mru_key(&ca))), ((char*)(lru_lru_key(&ca))));
    /* line 48 */
    printf("A peek_a=%d lru_still=%s\n", ((node*)((lru_peek(&ca, "a")).p))->v, ((char*)(lru_lru_key(&ca))));
    /* line 50 */
    printf("A get_a=%d mru=%s lru=%s\n", ((node*)((lru_get(&ca, "a")).p))->v, ((char*)(lru_mru_key(&ca))), ((char*)(lru_lru_key(&ca))));
    /* line 54 */
    sc_fat_unbind_d(&ha[3], (void (*)(void *))node_drop);
    ha[3] = make(4);
    /* line 55 */
    lru_put(&ca, "d", ({ sc_fat _ec4 = ha[3]; (sc_thin){_ec4.p, _ec4.tar, (void (*)(void *))node_drop}; }));
    /* line 56 */
    printf("A after_put_d: has_b=%d has_d=%d len=%llu\n", lru_has(&ca, "b"), lru_has(&ca, "d"), lru_len(&ca));
    /* line 58 */
    printf("A each:");
    /* line 59 */
    lru_each(&ca, dump_str, NULL);
    /* line 60 */
    printf("\n");
    /* line 63 */
    sc_fat_unbind_d(&ha[4], (void (*)(void *))node_drop);
    ha[4] = make(99);
    /* line 64 */
    lru_put(&ca, "a", ({ sc_fat _ec5 = ha[4]; (sc_thin){_ec5.p, _ec5.tar, (void (*)(void *))node_drop}; }));
    /* line 65 */
    printf("A replace_a=%d len=%llu\n", ((node*)((lru_get(&ca, "a")).p))->v, lru_len(&ca));
    /* line 66 */
    printf("A each2:");
    /* line 67 */
    lru_each(&ca, dump_str, NULL);
    /* line 68 */
    printf("\n");
    /* line 69 */
    lru_drop(&ca);
    /* line 72 */
    lru cb = {0};
    /* line 73 */
    lru_init(&cb, 4, 0);
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
        sc_fat_unbind_d(&hb[i], (void (*)(void *))node_drop);
        hb[i] = make((keys[i] + 1) * 10);
        /* line 83 */
        lru_put(&cb, ((const void*)(&(keys[i]))), ({ sc_fat _ec6 = hb[i]; (sc_thin){_ec6.p, _ec6.tar, (void (*)(void *))node_drop}; }));
        /* line 84 */
        i += 1;
    }
    /* line 86 */
    int32_t *mk = ((int32_t*)(lru_mru_key(&cb)));
    /* line 87 */
    int32_t *lk = ((int32_t*)(lru_lru_key(&cb)));
    /* line 88 */
    printf("B len=%llu cap=%llu mru=%d lru=%d empty=%d\n", lru_len(&cb), lru_cap(&cb), mk[0], lk[0], lru_is_empty(&cb));
    /* line 92 */
    printf("B remove20=%d miss=%d has20=%d len=%llu\n", lru_remove(&cb, ((const void*)(&(keys[1])))), lru_remove(&cb, ((const void*)(&(keys[1])))), lru_has(&cb, ((const void*)(&(keys[1])))), lru_len(&cb));
    /* line 97 */
    lru_set_cap(&cb, 2);
    /* line 98 */
    int32_t *lk2 = ((int32_t*)(lru_lru_key(&cb)));
    /* line 99 */
    printf("B after_set_cap2: cap=%llu len=%llu has10=%d lru=%d\n", lru_cap(&cb), lru_len(&cb), lru_has(&cb, ((const void*)(&(keys[0])))), lk2[0]);
    /* line 101 */
    printf("B each:");
    /* line 102 */
    lru_each(&cb, dump_int, NULL);
    /* line 103 */
    printf("\n");
    /* line 104 */
    lru_drop(&cb);
    /* line 107 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))node_drop);
        for (size_t _k0 = (5); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
