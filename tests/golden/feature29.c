/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mem/mem.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t main(void) {
    /* line 31 */
    void *p = chunk(100);
    /* line 32 */
    char *s = ((char*)(p));
    /* line 33 */
    sprintf(s, "pooled-%d", 42);
    /* line 34 */
    printf("chunk(100): usable>=100 -> %d, text=%s\n", mem_usable(p) >= 100, s);
    /* line 37 */
    p = refit(p, 5000);
    /* line 38 */
    printf("refit(5000): usable>=5000 -> %d, keep=%s\n", mem_usable(p) >= 5000, ((char*)(p)));
    /* line 39 */
    recycle(p);
    /* line 42 */
    void *z = chunk0(64);
    /* line 43 */
    uint8_t *zb = ((uint8_t*)(z));
    /* line 44 */
    printf("chunk0(64): first=%d last=%d\n", zb[0], zb[63]);
    /* line 45 */
    recycle(z);
    /* line 48 */
    void *arr = chunk_array(10, 8);
    /* line 49 */
    uint8_t *ab = ((uint8_t*)(arr));
    /* line 50 */
    printf("chunk_array(10,8): usable>=80 -> %d zeroed=%d\n", mem_usable(arr) >= 80, ab[0]);
    /* line 51 */
    recycle(arr);
    /* line 52 */
    printf("chunk_array overflow -> nil=%d\n", chunk_array(0xFFFFFFFFFFFFFFFFUL, 2) == NULL);
    /* line 55 */
    void *al = chunk_aligned(200, 64);
    /* line 56 */
    uint8_t *alb = ((uint8_t*)(al));
    /* line 57 */
    alb[0] = 7;
    /* line 58 */
    alb[199] = 9;
    /* line 59 */
    printf("chunk_aligned(200,64): usable>=200 -> %d rw=%d\n", mem_usable(al) >= 200, alb[0] + alb[199]);
    /* line 61 */
    recycle(al);
    /* line 62 */
    printf("chunk_aligned bad-align -> nil=%d\n", chunk_aligned(64, 48) == NULL);
    /* line 65 */
    arena a = {0};
    /* line 66 */
    arena_init(&a, 0);
    /* line 67 */
    int32_t i = 0;
    /* line 68 */
    int32_t n = 0;
    /* line 69 */
    for (i = 0; i < 1000; i++) {
        /* line 70 */
        void *q = arena_chunk(&a, 48);
        /* line 71 */
        if (q != NULL) {
            /* line 72 */
            n++;
        }
    }
    /* line 73 */
    printf("arena: 1000x48 allocated=%d\n", n);
    /* line 74 */
    arena_reset(&a);
    /* line 75 */
    void *r = arena_chunk(&a, 16);
    /* line 76 */
    printf("arena: reset+reuse ok=%d\n", r != NULL);
    /* line 77 */
    arena_drop(&a);
    /* line 80 */
    void *p2 = chunk(1000);
    /* line 81 */
    void *p3 = chunk(2000);
    /* line 82 */
    mem_stat_t st = {0};
    /* line 83 */
    mem_stat(&(st));
    /* line 84 */
    printf("stat live: count=%llu live>=3000 -> %d peak>=live -> %d allocs>=2 -> %d\n", st.count, st.live >= 3000, st.peak_live >= st.live, st.allocs >= 2);
    /* line 86 */
    recycle(p2);
    /* line 87 */
    recycle(p3);
    /* line 88 */
    mem_stat(&(st));
    /* line 89 */
    printf("stat freed: count=%llu live=%llu peak>=3000 -> %d\n", st.count, st.live, st.peak_live >= 3000);
    /* line 93 */
    int32_t k = 0;
    /* line 94 */
    for (k = 0; k < 200; k++) {
        /* line 95 */
        void *t = chunk(64);
        /* line 96 */
        recycle(t);
    }
    /* line 97 */
    uint64_t freed = mem_trim();
    /* line 98 */
    printf("mem_trim: freed>0 -> %d\n", freed > 0);
    /* line 101 */
    char *nm = "sc_feature29_region";
    /* line 102 */
    shm_remove(nm);
    /* line 103 */
    shm sm = {0};
    /* line 104 */
    if (shm_make(&sm, nm, 256, 0)) {
        /* line 105 */
        char *sd = ((char*)(shm_data(&sm)));
        /* line 106 */
        sprintf(sd, "shm-%d", 29);
        /* line 107 */
        shm sm2 = {0};
        /* line 108 */
        shm_make(&sm2, nm, 256, 0);
        /* line 109 */
        printf("shm: size>=256 -> %d shared=%s\n", shm_size(&sm) >= 256, ((char*)(shm_data(&sm2))));
        /* line 110 */
        shm_drop(&sm2);
        /* line 111 */
        shm_drop(&sm);
    }
    /* line 112 */
    shm_remove(nm);
    /* line 115 */
    shm_remove(nm);
    /* line 116 */
    shm w = {0};
    /* line 117 */
    if (shm_make(&w, nm, 256, 0)) {
        /* line 118 */
        char *wd = ((char*)(shm_data(&w)));
        /* line 119 */
        sprintf(wd, "ro-%d", 7);
        /* line 120 */
        shm ro = {0};
        /* line 121 */
        if (shm_make(&ro, nm, 256, 1)) {
            /* line 122 */
            printf("shm rdonly: see=%s size>=256 -> %d\n", ((char*)(shm_data(&ro))), shm_size(&ro) >= 256);
            /* line 123 */
            shm_drop(&ro);
        }
        /* line 124 */
        shm ex = {0};
        /* line 125 */
        printf("shm excl on existing -> fail=%d\n", !(shm_make(&ex, nm, 256, 2)));
        /* line 126 */
        shm_drop(&w);
        shm_drop(&ex);
    }
    /* line 127 */
    shm_remove(nm);
    /* line 129 */
    mem_teardown();
    /* line 130 */
    printf("mem feature ok\n");
    /* line 131 */
    return 0;
}
