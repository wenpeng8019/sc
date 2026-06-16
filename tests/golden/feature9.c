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
    /* line 18 */
    _this->n = 100;
}

static int32_t counter_add(counter *_this, int32_t k) {
    /* line 20 */
    _this->n = (_this->n + k);
    /* line 21 */
    return _this->n;
}

static int32_t str_cmp(void *a, void *b) {
    /* line 25 */
    return strcmp(((char*)(a)), ((char*)(b)));
}

int32_t main(void) {
    /* line 29 */
    counter c = {0};
    counter_init(&c);
    /* line 30 */
    printf("counter: init=%d add(5)=%d\n", c.n, counter_add(&c, 5));
    /* line 33 */
    string s = {0};
    string_init(&s);
    /* line 34 */
    string_append(&s, "Hello");
    /* line 35 */
    string_append(&s, ", sc!");
    /* line 36 */
    printf("s=%s len=%llu\n", string_cstr(&s), string_len(&s));
    /* line 37 */
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n", string_find(&s, "sc", 0), string_starts_with(&s, "Hello"));
    /* line 39 */
    string part = {0};
    string_init(&part);
    /* line 40 */
    string_slice(&s, -(3), -(1), &(part));
    /* line 41 */
    printf("slice(-3,-1)=%s\n", string_cstr(&part));
    /* line 42 */
    string_upper(&s);
    /* line 43 */
    printf("upper=%s\n", string_cstr(&s));
    /* line 46 */
    list l = {0};
    list_init(&l);
    /* line 47 */
    list_push(&l, "banana");
    /* line 48 */
    list_push(&l, "apple");
    /* line 49 */
    list_push(&l, "cherry");
    /* line 50 */
    list_sort(&l, str_cmp);
    /* line 51 */
    uint64_t i = 0;
    /* line 52 */
    for (i = 0; i < list_len(&l); i++) {
        /* line 53 */
        printf("list[%llu]=%s\n", i, ((char*)(list_get(&l, i))));
    }
    /* line 56 */
    list *lp = &(l);
    /* line 57 */
    list_drop(lp);
    /* line 58 */
    string_drop(&part);
    /* line 59 */
    string_drop(&s);
    /* line 62 */
    string *hs = string__new();
    /* line 63 */
    string_append(hs, "on the heap");
    /* line 64 */
    printf("heap: %s\n", string_cstr(hs));
    /* line 65 */
    string_drop(hs);
    /* line 66 */
    free(hs);
    /* line 67 */
    return 0;
}
