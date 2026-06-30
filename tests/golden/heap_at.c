/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_drop(node *_this);
sc_fat make(int32_t x);
int32_t drain_keys(heap *h);
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

int32_t drain_keys(heap *h) {
    /* line 24 */
    int32_t n = 0;
    /* line 25 */
    while (!(heap_is_empty(h))) {
        /* line 26 */
        int32_t *kp = ((int32_t*)(heap_peek_key(h)));
        /* line 27 */
        printf(" %d", kp[0]);
        /* line 28 */
        heap_pop(h);
        /* line 29 */
        n += 1;
    }
    /* line 30 */
    printf("\n");
    /* line 31 */
    return n;
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 35 */
    heap ta = {0};
    /* line 36 */
    heap_init(&ta, 1, 4, NULL, NULL);
    /* line 37 */
    heap_reserve(&ta, 16);
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
        sc_fat_unbind_d(&ha[i], (void (*)(void *))node_drop);
        ha[i] = make(keys[i] * 10);
        /* line 50 */
        heap_push(&ta, ((const void*)(&(keys[i]))), sc_fat_as_thin(ha[i], (void (*)(void *))node_drop));
        /* line 51 */
        i += 1;
    }
    /* line 52 */
    uint64_t _sq0 = heap_len(&ta);
    uint8_t _sq1 = heap_is_empty(&ta);
    printf("A len=%llu empty=%d\n", _sq0, _sq1);
    /* line 55 */
    int32_t *topk = ((int32_t*)(heap_peek_key(&ta)));
    /* line 56 */
    printf("A top_key=%d top_val=%d\n", topk[0], ((node*)((heap_peek(&ta)).p))->v);
    /* line 59 */
    printf("A drain:");
    /* line 60 */
    int32_t cnt = drain_keys(&(ta));
    /* line 61 */
    printf("A drained=%d empty=%d\n", cnt, heap_is_empty(&ta));
    /* line 64 */
    heap tb = {0};
    /* line 65 */
    heap_init(&tb, 0, 4, NULL, NULL);
    /* line 66 */
    sc_fat hb[7] = {0};
    /* line 67 */
    i = 0;
    /* line 68 */
    while (i < 7) {
        /* line 69 */
        sc_fat_unbind_d(&hb[i], (void (*)(void *))node_drop);
        hb[i] = make(keys[i] * 10);
        /* line 70 */
        heap_push(&tb, ((const void*)(&(keys[i]))), sc_fat_as_thin(hb[i], (void (*)(void *))node_drop));
        /* line 71 */
        i += 1;
    }
    /* line 73 */
    int32_t *tbk = ((int32_t*)(heap_peek_key(&tb)));
    /* line 74 */
    printf("B top_key=%d len=%llu\n", tbk[0], heap_len(&tb));
    /* line 76 */
    printf("B drain:");
    /* line 77 */
    drain_keys(&(tb));
    /* line 78 */
    heap_drop(&tb);
    /* line 81 */
    heap tc = {0};
    /* line 82 */
    heap_init(&tc, 1, 0 - 1, NULL, NULL);
    /* line 83 */
    sc_fat hc[4] = {0};
    /* line 84 */
    sc_fat_unbind_d(&hc[0], (void (*)(void *))node_drop);
    hc[0] = make(4);
    /* line 85 */
    heap_push(&tc, "delta", sc_fat_as_thin(hc[0], (void (*)(void *))node_drop));
    /* line 86 */
    sc_fat_unbind_d(&hc[1], (void (*)(void *))node_drop);
    hc[1] = make(1);
    /* line 87 */
    heap_push(&tc, "alpha", sc_fat_as_thin(hc[1], (void (*)(void *))node_drop));
    /* line 88 */
    sc_fat_unbind_d(&hc[2], (void (*)(void *))node_drop);
    hc[2] = make(3);
    /* line 89 */
    heap_push(&tc, "charlie", sc_fat_as_thin(hc[2], (void (*)(void *))node_drop));
    /* line 90 */
    sc_fat_unbind_d(&hc[3], (void (*)(void *))node_drop);
    hc[3] = make(2);
    /* line 91 */
    heap_push(&tc, "bravo", sc_fat_as_thin(hc[3], (void (*)(void *))node_drop));
    /* line 92 */
    uint64_t _sq2 = heap_len(&tc);
    char *_sq3 = ((char*)(heap_peek_key(&tc)));
    printf("C len=%llu top_key=%s\n", _sq2, _sq3);
    /* line 94 */
    printf("C pop:");
    /* line 95 */
    char *topc = ((char*)(heap_peek_key(&tc)));
    /* line 96 */
    printf(" %s", topc);
    /* line 97 */
    heap_pop(&tc);
    /* line 98 */
    topc = ((char*)(heap_peek_key(&tc)));
    /* line 99 */
    printf(" %s\n", topc);
    /* line 100 */
    heap_pop(&tc);
    /* line 101 */
    printf("C after_pop len=%llu\n", heap_len(&tc));
    /* line 102 */
    heap_clear(&tc);
    /* line 103 */
    uint64_t _sq4 = heap_len(&tc);
    uint8_t _sq5 = heap_is_empty(&tc);
    printf("C after_clear len=%llu empty=%d\n", _sq4, _sq5);
    /* line 104 */
    heap_drop(&tc);
    /* line 107 */
    {
        int32_t _ret = 0;
        for (size_t _k0 = (4); _k0-- > 0; ) sc_fat_unbind_d(&hc[_k0], (void (*)(void *))node_drop);
        for (size_t _k0 = (7); _k0-- > 0; ) sc_fat_unbind_d(&hb[_k0], (void (*)(void *))node_drop);
        for (size_t _k0 = (7); _k0-- > 0; ) sc_fat_unbind_d(&ha[_k0], (void (*)(void *))node_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
