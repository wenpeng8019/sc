/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/mem/mem.h"

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_mem_init(void); void sc_mod_mem_drop(void);

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_mem_init();
    /* line 30 */
    void *p = chunk(100);
    /* line 31 */
    char *s = ((char*)(p));
    /* line 32 */
    sprintf(s, "pooled-%d", 42);
    /* line 33 */
    printf("chunk(100): usable>=100 -> %d, text=%s\n", mem_usable(p) >= 100, s);
    /* line 36 */
    p = refit(p, 5000);
    /* line 37 */
    printf("refit(5000): usable>=5000 -> %d, keep=%s\n", mem_usable(p) >= 5000, ((char*)(p)));
    /* line 38 */
    recycle(p);
    /* line 41 */
    void *z = chunk0(64);
    /* line 42 */
    uint8_t *zb = ((uint8_t*)(z));
    /* line 43 */
    printf("chunk0(64): first=%d last=%d\n", zb[0], zb[63]);
    /* line 44 */
    recycle(z);
    /* line 47 */
    void *arr = chunk_array(10, 8);
    /* line 48 */
    uint8_t *ab = ((uint8_t*)(arr));
    /* line 49 */
    printf("chunk_array(10,8): usable>=80 -> %d zeroed=%d\n", mem_usable(arr) >= 80, ab[0]);
    /* line 50 */
    recycle(arr);
    /* line 51 */
    printf("chunk_array overflow -> nil=%d\n", chunk_array(0xFFFFFFFFFFFFFFFFUL, 2) == NULL);
    /* line 54 */
    void *al = chunk_aligned(200, 64);
    /* line 55 */
    uint8_t *alb = ((uint8_t*)(al));
    /* line 56 */
    alb[0] = 7;
    /* line 57 */
    alb[199] = 9;
    /* line 58 */
    printf("chunk_aligned(200,64): usable>=200 -> %d rw=%d\n", mem_usable(al) >= 200, alb[0] + alb[199]);
    /* line 60 */
    recycle(al);
    /* line 61 */
    printf("chunk_aligned bad-align -> nil=%d\n", chunk_aligned(64, 48) == NULL);
    /* line 64 */
    arena a = {0};
    /* line 65 */
    arena_init(&a, 0);
    /* line 66 */
    int32_t i = 0;
    /* line 67 */
    int32_t n = 0;
    /* line 68 */
    for (i = 0; i < 1000; i++) {
        /* line 69 */
        void *q = arena_chunk(&a, 48);
        /* line 70 */
        if (q != NULL) {
            /* line 71 */
            n++;
        }
    }
    /* line 72 */
    printf("arena: 1000x48 allocated=%d\n", n);
    /* line 73 */
    arena_reset(&a);
    /* line 74 */
    void *r = arena_chunk(&a, 16);
    /* line 75 */
    printf("arena: reset+reuse ok=%d\n", r != NULL);
    /* line 76 */
    arena_drop(&a);
    /* line 79 */
    void *p2 = chunk(1000);
    /* line 80 */
    void *p3 = chunk(2000);
    /* line 81 */
    mem_stat_t st = {0};
    /* line 82 */
    mem_stat(&(st));
    /* line 83 */
    printf("stat live: count=%llu live>=3000 -> %d peak>=live -> %d allocs>=2 -> %d\n", st.count, st.live >= 3000, st.peak_live >= st.live, st.allocs >= 2);
    /* line 85 */
    recycle(p2);
    /* line 86 */
    recycle(p3);
    /* line 87 */
    mem_stat(&(st));
    /* line 88 */
    printf("stat freed: count=%llu live=%llu peak>=3000 -> %d\n", st.count, st.live, st.peak_live >= 3000);
    /* line 92 */
    int32_t k = 0;
    /* line 93 */
    for (k = 0; k < 200; k++) {
        /* line 94 */
        void *t = chunk(64);
        /* line 95 */
        recycle(t);
    }
    /* line 96 */
    uint64_t freed = mem_trim();
    /* line 97 */
    printf("mem_trim: freed>0 -> %d\n", freed > 0);
    /* line 100 */
    char *nm = "sc_feature29_region";
    /* line 101 */
    shm_remove(nm);
    /* line 102 */
    shm sm = {0};
    /* line 103 */
    if (shm_make(&sm, nm, 256, 0)) {
        /* line 104 */
        char *sd = ((char*)(shm_data(&sm)));
        /* line 105 */
        sprintf(sd, "shm-%d", 29);
        /* line 106 */
        shm sm2 = {0};
        /* line 107 */
        shm_make(&sm2, nm, 256, 0);
        /* line 108 */
        printf("shm: size>=256 -> %d shared=%s\n", shm_size(&sm) >= 256, ((char*)(shm_data(&sm2))));
        /* line 109 */
        shm_drop(&sm2);
        /* line 110 */
        shm_drop(&sm);
    }
    /* line 111 */
    shm_remove(nm);
    /* line 114 */
    shm_remove(nm);
    /* line 115 */
    shm w = {0};
    /* line 116 */
    if (shm_make(&w, nm, 256, 0)) {
        /* line 117 */
        char *wd = ((char*)(shm_data(&w)));
        /* line 118 */
        sprintf(wd, "ro-%d", 7);
        /* line 119 */
        shm ro = {0};
        /* line 120 */
        if (shm_make(&ro, nm, 256, 1)) {
            /* line 121 */
            printf("shm rdonly: see=%s size>=256 -> %d\n", ((char*)(shm_data(&ro))), shm_size(&ro) >= 256);
            /* line 122 */
            shm_drop(&ro);
        }
        /* line 123 */
        shm ex = {0};
        /* line 124 */
        printf("shm excl on existing -> fail=%d\n", !(shm_make(&ex, nm, 256, 2)));
        /* line 125 */
        shm_drop(&w);
        shm_drop(&ex);
    }
    /* line 126 */
    shm_remove(nm);
    /* line 128 */
    mem_teardown();
    /* line 129 */
    printf("mem feature ok\n");
    /* line 130 */
    {
        int32_t _ret = 0;
        sc_mod_mem_drop();
        return _ret;
    }
}
