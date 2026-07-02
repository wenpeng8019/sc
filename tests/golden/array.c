/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

static int32_t sc_int_cmp(void *a, void *b);
static void sc_dump(sc_array *a);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static int32_t sc_int_cmp(void *a, void *b) {
    /* line 7 */
    return ((int32_t*)(a))[0] - ((int32_t*)(b))[0];
}

static void sc_dump(sc_array *a) {
    /* line 10 */
    uint64_t i = 0;
    /* line 11 */
    for (i = 0; i < sc_array_len(a); i++) {
        /* line 12 */
        printf(" %d", ((int32_t*)(sc_array_at(a, i)))[0]);
    }
    /* line 13 */
    printf("\n");
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 16 */
    sc_array a = {0};
    /* line 17 */
    sc_array_init(&a, 4);
    /* line 18 */
    int32_t v = 30;
    /* line 19 */
    sc_array_push(&a, &(v));
    /* line 20 */
    v = 10;
    /* line 21 */
    sc_array_push(&a, &(v));
    /* line 22 */
    v = 20;
    /* line 23 */
    sc_array_push(&a, &(v));
    /* line 24 */
    v = 10;
    /* line 25 */
    sc_array_push(&a, &(v));
    /* line 26 */
    printf("len=%llu\n", sc_array_len(&a));
    /* line 27 */
    sc_dump(&(a));
    /* line 29 */
    sc_array_sort(&a, sc_int_cmp);
    /* line 30 */
    printf("sorted:");
    /* line 31 */
    sc_dump(&(a));
    /* line 33 */
    int32_t key = 20;
    /* line 34 */
    int32_t *found = sc_array_bsearch(&a, &(key), sc_int_cmp);
    /* line 35 */
    printf("bsearch(20)=%d\n", (found != NULL) ? found[0] : (0 - 1));
    /* line 36 */
    key = 10;
    /* line 37 */
    int64_t _sq0 = sc_array_find(&a, &(key), 0, sc_int_cmp);
    int64_t _sq1 = sc_array_rfind(&a, &(key), sc_int_cmp);
    printf("find(10)=%lld rfind(10)=%lld\n", _sq0, _sq1);
    /* line 39 */
    sc_array b = {0};
    /* line 40 */
    sc_array_clone(&a, &(b));
    /* line 41 */
    printf("clone equals=%d\n", sc_array_equals(&a, &(b), sc_int_cmp));
    /* line 42 */
    sc_array_reverse(&b);
    /* line 43 */
    printf("reversed:");
    /* line 44 */
    sc_dump(&(b));
    /* line 46 */
    sc_array part = {0};
    /* line 47 */
    sc_array_slice(&a, 1, 3, &(part));
    /* line 48 */
    printf("slice(1,3):");
    /* line 49 */
    sc_dump(&(part));
    /* line 51 */
    v = 99;
    /* line 52 */
    sc_array_set(&a, 0, &(v));
    /* line 53 */
    sc_array_insert(&a, 0, &(key));
    /* line 54 */
    printf("after set/insert:");
    /* line 55 */
    sc_dump(&(a));
    /* line 57 */
    sc_array_erase(&a, 0, 2);
    /* line 58 */
    printf("after erase:");
    /* line 59 */
    sc_dump(&(a));
    /* line 61 */
    sc_array_drop(&a);
    /* line 62 */
    sc_array_drop(&b);
    /* line 63 */
    sc_array_drop(&part);
    /* line 64 */
    {
        int32_t _ret = 0;
        sc_mod_adt_drop();
        return _ret;
    }
}
