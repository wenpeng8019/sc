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
    s.append("Hello");
    /* line 35 */
    s.append(", sc!");
    /* line 36 */
    printf("s=%s len=%llu\n", s.cstr(), s.len());
    /* line 37 */
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n", s.find("sc", 0), s.starts_with("Hello"));
    /* line 39 */
    string part = {0};
    string_init(&part);
    /* line 40 */
    s.slice(-(3), -(1), &(part));
    /* line 41 */
    printf("slice(-3,-1)=%s\n", part.cstr());
    /* line 42 */
    s.upper();
    /* line 43 */
    printf("upper=%s\n", s.cstr());
    /* line 46 */
    list l = {0};
    list_init(&l);
    /* line 47 */
    l.push("banana");
    /* line 48 */
    l.push("apple");
    /* line 49 */
    l.push("cherry");
    /* line 50 */
    l.sort(str_cmp);
    /* line 51 */
    uint64_t i = 0;
    /* line 52 */
    for (i = 0; i < l.len(); i++) {
        /* line 53 */
        printf("list[%llu]=%s\n", i, ((char*)(l.get(i))));
    }
    /* line 56 */
    list *lp = &(l);
    /* line 57 */
    lp->drop();
    /* line 58 */
    part.drop();
    /* line 59 */
    s.drop();
    /* line 62 */
    string *hs = string__new();
    /* line 63 */
    hs->append("on the heap");
    /* line 64 */
    printf("heap: %s\n", hs->cstr());
    /* line 65 */
    hs->drop();
    /* line 66 */
    free(hs);
    /* line 67 */
    return 0;
}
