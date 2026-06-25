/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct counter counter;

typedef struct counter {
    int32_t n;
} counter;

static void counter_init(counter *_this);
static int32_t counter_add(counter *_this, int32_t k);
static int32_t cnt_cmp(void *a, void *b);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static inline counter *counter__new(void) {
    counter *_p = (counter *)sc_alloc(sizeof(counter));
    if (_p) {
        memset(_p, 0, sizeof(counter));
        counter_init(_p);
    }
    return _p;
}

static inline counter *counter__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(counter));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    counter *_p = (counter *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(counter));
    counter_init(_p);
    return _p;
}

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

static int32_t cnt_cmp(void *a, void *b) {
    /* line 24 */
    return ((counter*)(a))->n - ((counter*)(b))->n;
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
    sc_fat c1 = {0};
    counter *_fat0 = counter__new_ref(0);
    sc_fat_bind(&c1, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 47 */
    counter_add(((counter *)(c1).p), 30);
    /* line 48 */
    sc_fat c2 = {0};
    counter *_fat1 = counter__new_ref(0);
    sc_fat_bind(&c2, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 49 */
    counter_add(((counter *)(c2).p), 10);
    /* line 50 */
    sc_fat c3 = {0};
    counter *_fat2 = counter__new_ref(0);
    sc_fat_bind(&c3, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 51 */
    counter_add(((counter *)(c3).p), 20);
    /* line 52 */
    list_push(&l, ({ sc_fat _ec3 = c1; (sc_afat){_ec3.p, _ec3.tar, _ec3.own, (void (*)(void *))0}; }));
    /* line 53 */
    list_push(&l, ({ sc_fat _ec4 = c2; (sc_afat){_ec4.p, _ec4.tar, _ec4.own, (void (*)(void *))0}; }));
    /* line 54 */
    list_push(&l, ({ sc_fat _ec5 = c3; (sc_afat){_ec5.p, _ec5.tar, _ec5.own, (void (*)(void *))0}; }));
    /* line 55 */
    list_sort(&l, cnt_cmp);
    /* line 56 */
    uint64_t i = 0;
    /* line 57 */
    for (i = 0; i < list_len(&l); i++) {
        /* line 58 */
        printf("list[%llu]=%d\n", i, ((counter*)((list_get(&l, i)).p))->n);
    }
    /* line 61 */
    list *lp = &(l);
    /* line 62 */
    list_drop(lp);
    /* line 63 */
    (string_drop(part), sc_free(part));
    /* line 64 */
    (string_drop(s), sc_free(s));
    /* line 67 */
    string *hs = string__new();
    /* line 68 */
    string_append(hs, "on the heap");
    /* line 69 */
    printf("heap: %s\n", string_cstr(hs));
    /* line 70 */
    (string_drop(hs), sc_free(hs));
    /* line 71 */
    {
        int32_t _ret = 0;
        sc_fat_unbind(&c3);
        sc_fat_unbind(&c2);
        sc_fat_unbind(&c1);
        sc_mod_adt_drop();
        return _ret;
    }
}
