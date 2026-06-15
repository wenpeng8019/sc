/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct counter counter;

typedef struct counter {
    int32_t n;
} counter;

static void counter_init(counter *_this);
static int32_t counter_add(counter *_this, int32_t k);
static int32_t str_cmp(void *a, void *b);

static inline string *string__new(void) {
    string *_p = (string *)malloc(sizeof(string));
    if (_p) {
        memset(_p, 0, sizeof(string));
        string_init(_p);
    }
    return _p;
}

static void counter_init(counter *_this) {
    /* line 16 */
    _this->n = 100;
}

static int32_t counter_add(counter *_this, int32_t k) {
    /* line 18 */
    _this->n = (_this->n + k);
    /* line 19 */
    return _this->n;
}

static int32_t str_cmp(void *a, void *b) {
    /* line 23 */
    return strcmp(((char*)(a)), ((char*)(b)));
}

int32_t main(void) {
    /* line 27 */
    counter c = {0};
    counter_init(&c);
    /* line 28 */
    printf("counter: init=%d add(5)=%d\n", c.n, counter_add(&c, 5));
    /* line 31 */
    string s = {0};
    string_init(&s);
    /* line 32 */
    string_append(&s, "Hello");
    /* line 33 */
    string_append(&s, ", sc!");
    /* line 34 */
    printf("s=%s len=%llu\n", string_cstr(&s), string_len(&s));
    /* line 35 */
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n", string_find(&s, "sc", 0), string_starts_with(&s, "Hello"));
    /* line 37 */
    string part = {0};
    string_init(&part);
    /* line 38 */
    string_slice(&s, -(3), -(1), &(part));
    /* line 39 */
    printf("slice(-3,-1)=%s\n", string_cstr(&part));
    /* line 40 */
    string_upper(&s);
    /* line 41 */
    printf("upper=%s\n", string_cstr(&s));
    /* line 44 */
    list l = {0};
    list_init(&l);
    /* line 45 */
    list_push(&l, "banana");
    /* line 46 */
    list_push(&l, "apple");
    /* line 47 */
    list_push(&l, "cherry");
    /* line 48 */
    list_sort(&l, str_cmp);
    /* line 49 */
    uint64_t i = 0;
    /* line 50 */
    for (i = 0; i < list_len(&l); i++) {
        /* line 51 */
        printf("list[%llu]=%s\n", i, ((char*)(list_get(&l, i))));
    }
    /* line 54 */
    list *lp = &(l);
    /* line 55 */
    list_drop(lp);
    /* line 56 */
    string_drop(&part);
    /* line 57 */
    string_drop(&s);
    /* line 60 */
    string *hs = string__new();
    /* line 61 */
    string_append(hs, "on the heap");
    /* line 62 */
    printf("heap: %s\n", string_cstr(hs));
    /* line 63 */
    string_drop(hs);
    /* line 64 */
    free(hs);
    /* line 65 */
    return 0;
}
