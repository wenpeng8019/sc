/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

static int32_t int_cmp(void *a, void *b);
static void dump(array *a);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static int32_t int_cmp(void *a, void *b) {
    /* line 7 */
    return ((int32_t*)(a))[0] - ((int32_t*)(b))[0];
}

static void dump(array *a) {
    /* line 10 */
    uint64_t i = 0;
    /* line 11 */
    for (i = 0; i < array_len(a); i++) {
        /* line 12 */
        printf(" %d", ((int32_t*)(array_at(a, i)))[0]);
    }
    /* line 13 */
    printf("\n");
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 16 */
    array a = {0};
    /* line 17 */
    array_init(&a, 4);
    /* line 18 */
    int32_t v = 30;
    /* line 19 */
    array_push(&a, &(v));
    /* line 20 */
    v = 10;
    /* line 21 */
    array_push(&a, &(v));
    /* line 22 */
    v = 20;
    /* line 23 */
    array_push(&a, &(v));
    /* line 24 */
    v = 10;
    /* line 25 */
    array_push(&a, &(v));
    /* line 26 */
    printf("len=%llu\n", array_len(&a));
    /* line 27 */
    dump(&(a));
    /* line 29 */
    array_sort(&a, int_cmp);
    /* line 30 */
    printf("sorted:");
    /* line 31 */
    dump(&(a));
    /* line 33 */
    int32_t key = 20;
    /* line 34 */
    int32_t *found = array_bsearch(&a, &(key), int_cmp);
    /* line 35 */
    printf("bsearch(20)=%d\n", (found != NULL) ? found[0] : (0 - 1));
    /* line 36 */
    key = 10;
    /* line 37 */
    printf("find(10)=%lld rfind(10)=%lld\n", array_find(&a, &(key), 0, int_cmp), array_rfind(&a, &(key), int_cmp));
    /* line 39 */
    array b = {0};
    /* line 40 */
    array_clone(&a, &(b));
    /* line 41 */
    printf("clone equals=%d\n", array_equals(&a, &(b), int_cmp));
    /* line 42 */
    array_reverse(&b);
    /* line 43 */
    printf("reversed:");
    /* line 44 */
    dump(&(b));
    /* line 46 */
    array part = {0};
    /* line 47 */
    array_slice(&a, 1, 3, &(part));
    /* line 48 */
    printf("slice(1,3):");
    /* line 49 */
    dump(&(part));
    /* line 51 */
    v = 99;
    /* line 52 */
    array_set(&a, 0, &(v));
    /* line 53 */
    array_insert(&a, 0, &(key));
    /* line 54 */
    printf("after set/insert:");
    /* line 55 */
    dump(&(a));
    /* line 57 */
    array_erase(&a, 0, 2);
    /* line 58 */
    printf("after erase:");
    /* line 59 */
    dump(&(a));
    /* line 61 */
    array_drop(&a);
    /* line 62 */
    array_drop(&b);
    /* line 63 */
    array_drop(&part);
    /* line 64 */
    {
        int32_t _ret = 0;
        sc_mod_adt_drop();
        return _ret;
    }
}
