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
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static inline string *string__new(void) {
    string *_p = (string *)sc_alloc(sizeof(string));
    if (_p) {
        memset(_p, 0, sizeof(string));
    }
    return _p;
}

static inline string *string__new_init(const char *s) {
    string *_p = string__new();
    if (_p) string_init(_p, s);
    return _p;
}

static void counter_init(counter *_this) {
    /* line 17 */
    _this->n = 100;
}

static int32_t counter_add(counter *_this, int32_t k) {
    /* line 19 */
    _this->n = (_this->n + k);
    /* line 20 */
    return _this->n;
}

static int32_t str_cmp(void *a, void *b) {
    /* line 24 */
    return strcmp(((char*)(a)), ((char*)(b)));
}

int32_t main(void) {
    sc_mod_adt_init();
    /* line 28 */
    counter c = {0};
    counter_init(&c);
    /* line 29 */
    printf("counter: init=%d add(5)=%d\n", c.n, counter_add(&c, 5));
    /* line 32 */
    string *s = string__new();
    /* line 33 */
    string_append(s, "Hello");
    /* line 34 */
    string_append(s, ", sc!");
    /* line 35 */
    printf("s=%s len=%llu\n", string_cstr(s), string_len(s));
    /* line 36 */
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n", string_find(s, "sc", 0), string_starts_with(s, "Hello"));
    /* line 38 */
    string *part = string__new();
    /* line 39 */
    string_slice(s, -(3), -(1), part);
    /* line 40 */
    printf("slice(-3,-1)=%s\n", string_cstr(part));
    /* line 41 */
    string_upper(s);
    /* line 42 */
    printf("upper=%s\n", string_cstr(s));
    /* line 45 */
    list l = {0};
    list_init(&l);
    /* line 46 */
    list_push(&l, "banana");
    /* line 47 */
    list_push(&l, "apple");
    /* line 48 */
    list_push(&l, "cherry");
    /* line 49 */
    list_sort(&l, str_cmp);
    /* line 50 */
    uint64_t i = 0;
    /* line 51 */
    for (i = 0; i < list_len(&l); i++) {
        /* line 52 */
        printf("list[%llu]=%s\n", i, ((char*)(list_get(&l, i))));
    }
    /* line 55 */
    list *lp = &(l);
    /* line 56 */
    list_drop(lp);
    /* line 57 */
    (string_drop(part), sc_free(part));
    /* line 58 */
    (string_drop(s), sc_free(s));
    /* line 61 */
    string *hs = string__new();
    /* line 62 */
    string_append(hs, "on the heap");
    /* line 63 */
    printf("heap: %s\n", string_cstr(hs));
    /* line 64 */
    (string_drop(hs), sc_free(hs));
    /* line 65 */
    {
        int32_t _ret = 0;
        sc_mod_adt_drop();
        return _ret;
    }
}
