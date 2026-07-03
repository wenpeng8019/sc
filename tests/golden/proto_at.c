/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/proto/proto.h"
#include "builtins/mem/mem.h"

int32_t sc_blob_hex(uint32_t tag, void *data, uint32_t size, void *out, uint32_t cap, void *ctx);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_proto_init(void); void sc_mod_proto_drop(void);
void sc_mod_mem_init(void); void sc_mod_mem_drop(void);

int32_t sc_blob_hex(uint32_t tag, void *data, uint32_t size, void *out, uint32_t cap, void *ctx) {
    /* line 13 */
    int32_t v = 0;
    /* line 14 */
    if (size >= 4) {
        /* line 15 */
        int32_t *p = ((int32_t*)(data));
        /* line 16 */
        v = p[0];
    }
    /* line 18 */
    char tmp[32];
    /* line 19 */
    int32_t n = snprintf(tmp, 32, "#%d", v);
    /* line 20 */
    if (!(out)) {
        /* line 21 */
        return n;
    }
    /* line 22 */
    uint32_t need = ((uint32_t)(n));
    /* line 23 */
    if (need > cap) {
        /* line 24 */
        need = cap;
    }
    /* line 25 */
    memcpy(out, tmp, need);
    /* line 26 */
    return n;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_proto_init();
    sc_mod_mem_init();
    /* line 30 */
    sc_proto a = {0};
    /* line 31 */
    sc_proto_init(&a, sc_PROTO_FILO, 0, 4);
    /* line 32 */
    sc_proto_feed(&a, 1, ((const void*)("alpha")), 5);
    /* line 33 */
    sc_proto_feed(&a, 2, ((const void*)("beta")), 4);
    /* line 34 */
    sc_proto_feed(&a, 3, ((const void*)("gamma")), 5);
    /* line 35 */
    uint64_t _sq0 = sc_proto_depth(&a);
    int32_t _sq1 = ((int32_t)(sc_proto_is_empty(&a)));
    printf("A depth=%llu empty=%d\n", _sq0, _sq1);
    /* line 37 */
    uint32_t tag = 0;
    /* line 38 */
    void *data = NULL;
    /* line 39 */
    while (!(sc_proto_is_empty(&a))) {
        /* line 40 */
        int32_t len = sc_proto_drain(&a, &(tag), &(data));
        /* line 41 */
        printf("  drain tag=%u len=%d data=%.*s\n", tag, len, len, ((char*)(data)));
    }
    /* line 42 */
    sc_proto_drop(&a);
    /* line 45 */
    sc_proto b = {0};
    /* line 46 */
    sc_proto_init(&b, sc_PROTO_FIFO, 0, 4);
    /* line 47 */
    sc_proto_feed(&b, 10, ((const void*)("one")), 3);
    /* line 48 */
    sc_proto_feed(&b, 20, ((const void*)("two")), 3);
    /* line 49 */
    sc_proto_feed(&b, 30, ((const void*)("three")), 5);
    /* line 50 */
    printf("B order:");
    /* line 51 */
    while (!(sc_proto_is_empty(&b))) {
        /* line 52 */
        int32_t len2 = sc_proto_drain(&b, &(tag), &(data));
        /* line 53 */
        printf(" [%u]%.*s", tag, len2, ((char*)(data)));
    }
    /* line 54 */
    printf("\n");
    /* line 55 */
    sc_proto_drop(&b);
    /* line 58 */
    sc_proto c = {0};
    /* line 59 */
    sc_proto_init(&c, sc_PROTO_FIFO, 0, 4);
    /* line 60 */
    sc_proto_push_str(&c, "id", NULL);
    /* line 61 */
    sc_proto_push_i4(&c, 42, NULL);
    /* line 62 */
    sc_proto_push_str(&c, "ratio", NULL);
    /* line 63 */
    sc_proto_push_f8(&c, 3.14159, "%.2f");
    /* line 64 */
    sc_proto_push_str(&c, "ok", NULL);
    /* line 65 */
    sc_proto_push_b(&c, true, NULL);
    /* line 66 */
    char *s = sc_proto_build(&c, ", ");
    /* line 67 */
    printf("C build: %s\n", s);
    /* line 68 */
    sc_recycle(s);
    /* line 69 */
    sc_proto_drop(&c);
    /* line 72 */
    sc_proto d = {0};
    /* line 73 */
    sc_proto_init(&d, sc_PROTO_FIFO, 0, 4);
    /* line 74 */
    int32_t n1 = 100;
    /* line 75 */
    int32_t n2 = 255;
    /* line 76 */
    sc_proto_push_blob(&d, ((const void*)(&(n1))), 4, sc_blob_hex);
    /* line 77 */
    sc_proto_push_blob(&d, ((const void*)(&(n2))), 4, sc_blob_hex);
    /* line 78 */
    char *s2 = sc_proto_build(&d, " ");
    /* line 79 */
    printf("D build: %s\n", s2);
    /* line 80 */
    sc_recycle(s2);
    /* line 81 */
    sc_proto_drop(&d);
    /* line 84 */
    sc_proto e = {0};
    /* line 85 */
    sc_proto_init(&e, sc_PROTO_FILO, 64, 8);
    /* line 86 */
    int32_t i = 0;
    /* line 87 */
    while (i < 20) {
        /* line 88 */
        sc_proto_feed(&e, ((uint32_t)(i)), ((const void*)("payload_data")), 12);
        /* line 89 */
        i += 1;
    }
    /* line 90 */
    printf("E depth=%llu\n", sc_proto_depth(&e));
    /* line 91 */
    sc_proto_peek(&e, &(tag), &(data));
    /* line 92 */
    printf("E peek tag=%u\n", tag);
    /* line 93 */
    sc_proto_back(&e, 5);
    /* line 94 */
    printf("E after back(5) depth=%llu\n", sc_proto_depth(&e));
    /* line 95 */
    int32_t pk = sc_proto_peek(&e, &(tag), &(data));
    /* line 96 */
    printf("E peek tag=%u (still %d bytes)\n", tag, pk);
    /* line 97 */
    sc_proto_clear(&e);
    /* line 98 */
    uint64_t _sq2 = sc_proto_depth(&e);
    int32_t _sq3 = ((int32_t)(sc_proto_is_empty(&e)));
    printf("E after clear depth=%llu empty=%d\n", _sq2, _sq3);
    /* line 99 */
    sc_proto_feed(&e, 999, ((const void*)("reuse")), 5);
    /* line 100 */
    printf("E reuse depth=%llu\n", sc_proto_depth(&e));
    /* line 101 */
    sc_proto_drop(&e);
    /* line 103 */
    {
        int32_t _ret = 0;
        sc_mod_mem_drop();
        sc_mod_proto_drop();
        return _ret;
    }
}
