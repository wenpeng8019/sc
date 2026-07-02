/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

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

static inline sc_string *sc_string__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(sc_string));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    sc_string *_p = (sc_string *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_string));
    return _p;
}

static inline sc_string *sc_string__new_ref_init(const char *s, int32_t _atom) {
    sc_string *_p = sc_string__new_ref(_atom);
    if (_p) sc_string_init(_p, s);
    return _p;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 4 */
    sc_fat s = {0};
    sc_string *_fat0 = sc_string__new_ref(0);
    sc_fat_bind(&s, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 6 */
    int32_t n = 42;
    /* line 7 */
    char *name = "sc";
    /* line 8 */
    sc_string_printf(((string *)(s).p), "hello %s n=%d", name, (int)(n));
    /* line 9 */
    sc_string_printf(((string *)(s).p), " | second line");
    /* line 11 */
    sc_string_printf(((string *)(s).p), "; x=%d y=%d", 7, 8);
    /* line 13 */
    sc_print((uint8_t)(0), "stdout still works: %d", (int)(n));
    /* line 14 */
    printf("collected string = [%s]\n", sc_string_cstr(((sc_string *)(s).p)));
    /* line 15 */
    sc_string_drop(((sc_string *)(s).p));
    /* line 16 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&s, (void (*)(void *))sc_string_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
