/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_counter sc_counter;

typedef struct sc_counter {
    int32_t n;
} sc_counter;

static void sc_counter_init(sc_counter *_this);
static int32_t sc_counter_add(sc_counter *_this, int32_t k);
static int32_t sc_cnt_cmp(void *a, void *b);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static inline sc_counter *sc_counter__new(void) {
    sc_counter *_p = (sc_counter *)sc_alloc(sizeof(sc_counter));
    if (_p) {
        memset(_p, 0, sizeof(sc_counter));
        sc_counter_init(_p);
    }
    return _p;
}

static inline sc_counter *sc_counter__new_ref(int32_t _flags) {
    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)
        ? sc_alloc(SC_REF_HDR + sizeof(sc_counter))
        : sc_chunk(SC_REF_HDR + sizeof(sc_counter)));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;
    sc_counter *_p = (sc_counter *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_counter));
    sc_counter_init(_p);
    return _p;
}

static inline sc_string *sc_string__new(void) {
    sc_string *_p = (sc_string *)sc_alloc(sizeof(sc_string));
    if (_p) {
        memset(_p, 0, sizeof(sc_string));
    }
    return _p;
}

static inline sc_string *sc_string__new_init(const char *s) {
    sc_string *_p = sc_string__new();
    if (_p) sc_string_init(_p, s);
    return _p;
}

static void sc_counter_init(sc_counter *_this) {
    /* line 17 */
    _this->n = 100;
}

static int32_t sc_counter_add(sc_counter *_this, int32_t k) {
    /* line 19 */
    _this->n = (_this->n + k);
    /* line 20 */
    return _this->n;
}

static int32_t sc_cnt_cmp(void *a, void *b) {
    /* line 24 */
    return ((sc_counter*)(a))->n - ((sc_counter*)(b))->n;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 28 */
    sc_counter c = {0};
    sc_counter_init(&c);
    /* line 29 */
    printf("counter: init=%d add(5)=%d\n", c.n, sc_counter_add(&c, 5));
    /* line 32 */
    sc_string *s = sc_string__new();
    /* line 33 */
    sc_string_append(s, "Hello");
    /* line 34 */
    sc_string_append(s, ", sc!");
    /* line 35 */
    char *_sq0 = sc_string_cstr(s);
    uint64_t _sq1 = sc_string_len(s);
    printf("s=%s len=%llu\n", _sq0, _sq1);
    /* line 36 */
    int64_t _sq2 = sc_string_find(s, "sc", 0);
    bool _sq3 = sc_string_starts_with(s, "Hello");
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n", _sq2, _sq3);
    /* line 38 */
    sc_string *part = sc_string__new();
    /* line 39 */
    sc_string_slice(s, -(3), -(1), part);
    /* line 40 */
    printf("slice(-3,-1)=%s\n", sc_string_cstr(part));
    /* line 41 */
    sc_string_upper(s);
    /* line 42 */
    printf("upper=%s\n", sc_string_cstr(s));
    /* line 45 */
    sc_list l = {0};
    sc_list_init(&l);
    /* line 46 */
    sc_fat c1 = {0};
    sc_counter *_fat0 = sc_counter__new_ref(0);
    sc_fat_bind(&c1, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 47 */
    sc_counter_add(((sc_counter *)(c1).p), 30);
    /* line 48 */
    sc_fat c2 = {0};
    sc_counter *_fat1 = sc_counter__new_ref(0);
    sc_fat_bind(&c2, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 49 */
    sc_counter_add(((sc_counter *)(c2).p), 10);
    /* line 50 */
    sc_fat c3 = {0};
    sc_counter *_fat2 = sc_counter__new_ref(0);
    sc_fat_bind(&c3, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 51 */
    sc_counter_add(((sc_counter *)(c3).p), 20);
    /* line 52 */
    sc_list_push(&l, sc_fat_as_thin(c1, (void (*)(void *))0));
    /* line 53 */
    sc_list_push(&l, sc_fat_as_thin(c2, (void (*)(void *))0));
    /* line 54 */
    sc_list_push(&l, sc_fat_as_thin(c3, (void (*)(void *))0));
    /* line 55 */
    sc_list_sort(&l, sc_cnt_cmp);
    /* line 56 */
    uint64_t i = 0;
    /* line 57 */
    for (i = 0; i < sc_list_len(&l); i++) {
        /* line 58 */
        printf("list[%llu]=%d\n", i, ((sc_counter*)((sc_list_get(&l, i)).p))->n);
    }
    /* line 61 */
    sc_list *lp = &(l);
    /* line 62 */
    sc_list_drop(lp);
    /* line 63 */
    (sc_string_drop(part), sc_free(part));
    /* line 64 */
    (sc_string_drop(s), sc_free(s));
    /* line 67 */
    sc_string *hs = sc_string__new();
    /* line 68 */
    sc_string_append(hs, "on the heap");
    /* line 69 */
    printf("heap: %s\n", sc_string_cstr(hs));
    /* line 70 */
    (sc_string_drop(hs), sc_free(hs));
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
