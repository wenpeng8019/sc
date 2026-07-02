/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_drop(sc_node *_this);
sc_fat sc_make(int32_t x);
int32_t sc_drain_keys(sc_heap *h);
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

int32_t sc_drain_keys(sc_heap *h) {
    /* line 24 */
    int32_t n = 0;
    /* line 25 */
    while (!(sc_heap_is_empty(h))) {
        /* line 26 */
        int32_t *kp = ((int32_t*)(sc_heap_peek_key(h)));
        /* line 27 */
        printf(" %d", kp[0]);
        /* line 28 */
        sc_heap_pop(h);
        /* line 29 */
        n += 1;
    }
    /* line 30 */
    printf("\n");
    /* line 31 */
    return n;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 35 */
    sc_heap ta = {0};
    /* line 36 */
    sc_heap_init(&ta, 1, 4, NULL, NULL);
    /* line 37 */
    sc_heap_reserve(&ta, 16);
    /* line 38 */
    sc_fat ha[7] = {0};
    /* line 39 */
    int32_t keys[7];
    /* line 40 */
    keys[0] = 50;
    /* line 41 */
    keys[1] = 30;
    /* line 42 */
    keys[2] = 70;
    /* line 43 */
    keys[3] = 20;
    /* line 44 */
    keys[4] = 40;
    /* line 45 */
    keys[5] = 60;
    /* line 46 */
    keys[6] = 80;
    /* line 47 */
    int32_t i = 0;
    /* line 48 */
    while (i < 7) {
        /* line 49 */
        sc_fat_unbind_d(&ha[i], (void (*)(void *))sc_node_drop);
        ha[i] = sc_make(keys[i] * 10);
        /* line 50 */
        sc_heap_push(&ta, ((const void*)(&(keys[i]))), sc_fat_as_thin(ha[i], (void (*)(void *))sc_node_drop));
        /* line 51 */
        i += 1;
    }
    /* line 52 */
    uint64_t _sq0 = sc_heap_len(&ta);
    bool _sq1 = sc_heap_is_empty(&ta);
    printf("A len=%llu empty=%d\n", _sq0, _sq1);
    /* line 55 */
    int32_t *topk = ((int32_t*)(sc_heap_peek_key(&ta)));
    /* line 56 */
    printf("A top_key=%d top_val=%d\n", topk[0], ((sc_node*)((sc_heap_peek(&ta)).p))->v);
    /* line 59 */
    printf("A drain:");
    /* line 60 */
    int32_t cnt = sc_drain_keys(&(ta));
    /* line 61 */
    printf("A drained=%d empty=%d\n", cnt, sc_heap_is_empty(&ta));
    /* line 64 */
    sc_heap tb = {0};
    /* line 65 */
    sc_heap_init(&tb, 0, 4, NULL, NULL);
    /* line 66 */
    sc_fat hb[7] = {0};
    /* line 67 */
    i = 0;
    /* line 68 */
    while (i < 7) {
        /* line 69 */
        sc_fat_unbind_d(&hb[i], (void (*)(void *))sc_node_drop);
        hb[i] = sc_make(keys[i] * 10);
        /* line 70 */
        sc_heap_push(&tb, ((const void*)(&(keys[i]))), sc_fat_as_thin(hb[i], (void (*)(void *))sc_node_drop));
        /* line 71 */
        i += 1;
    }
    /* line 73 */
    int32_t *tbk = ((int32_t*)(sc_heap_peek_key(&tb)));
    /* line 74 */
    printf("B top_key=%d len=%llu\n", tbk[0], sc_heap_len(&tb));
    /* line 76 */
    printf("B drain:");
    /* line 77 */
    sc_drain_keys(&(tb));
    /* line 78 */
    sc_heap_drop(&tb);
    /* line 81 */
    sc_heap tc = {0};
    /* line 82 */
    sc_heap_init(&tc, 1, 0 - 1, NULL, NULL);
    /* line 83 */
    sc_fat hc[4] = {0};
    /* line 84 */
    sc_fat_unbind_d(&hc[0], (void (*)(void *))sc_node_drop);
    hc[0] = sc_make(4);
    /* line 85 */
    sc_heap_push(&tc, "delta", sc_fat_as_thin(hc[0], (void (*)(void *))sc_node_drop));
    /* line 86 */
    sc_fat_unbind_d(&hc[1], (void (*)(void *))sc_node_drop);
    hc[1] = sc_make(1);
    /* line 87 */
    sc_heap_push(&tc, "alpha", sc_fat_as_thin(hc[1], (void (*)(void *))sc_node_drop));
    /* line 88 */
    sc_fat_unbind_d(&hc[2], (void (*)(void *))sc_node_drop);
    hc[2] = sc_make(3);
    /* line 89 */
    sc_heap_push(&tc, "charlie", sc_fat_as_thin(hc[2], (void (*)(void *))sc_node_drop));
    /* line 90 */
    sc_fat_unbind_d(&hc[3], (void (*)(void *))sc_node_drop);
    hc[3] = sc_make(2);
    /* line 91 */
    sc_heap_push(&tc, "bravo", sc_fat_as_thin(hc[3], (void (*)(void *))sc_node_drop));
    /* line 92 */
    uint64_t _sq2 = sc_heap_len(&tc);
    char *_sq3 = ((char*)(sc_heap_peek_key(&tc)));
    printf("C len=%llu top_key=%s\n", _sq2, _sq3);
    /* line 94 */
    printf("C pop:");
    /* line 95 */
    char *topc = ((char*)(sc_heap_peek_key(&tc)));
    /* line 96 */
    printf(" %s", topc);
    /* line 97 */
    sc_heap_pop(&tc);
    /* line 98 */
    topc = ((char*)(sc_heap_peek_key(&tc)));
    /* line 99 */
    printf(" %s\n", topc);
    /* line 100 */
    sc_heap_pop(&tc);
    /* line 101 */
    printf("C after_pop len=%llu\n", sc_heap_len(&tc));
    /* line 102 */
    sc_heap_clear(&tc);
    /* line 103 */
    uint64_t _sq4 = sc_heap_len(&tc);
    bool _sq5 = sc_heap_is_empty(&tc);
    printf("C after_clear len=%llu empty=%d\n", _sq4, _sq5);
    /* line 104 */
    sc_heap_drop(&tc);
    /* line 107 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hc[_k0], (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (7); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))sc_node_drop);
        for (size_t _k0 = (7); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))sc_node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
