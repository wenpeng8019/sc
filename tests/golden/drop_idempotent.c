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

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 5 */
    sc_string *a = sc_string__new_init("a");
    /* line 6 */
    sc_ptr_drop_slot((void *)&(a), (void (*)(void *))sc_string_drop);
    /* line 7 */
    sc_ptr_drop_slot((void *)&(a), (void (*)(void *))sc_string_drop);
    /* line 9 */
    sc_string *b = sc_string__new_init("b");
    /* line 10 */
    if (false) {
        /* line 11 */
        sc_ptr_drop_slot((void *)&(b), (void (*)(void *))sc_string_drop);
    }
    /* line 12 */
    {
        int32_t _ret = 0;
        sc_ptr_drop_slot((void *)&(b), (void (*)(void *))sc_string_drop);
        sc_ptr_drop_slot((void *)&(a), (void (*)(void *))sc_string_drop);
        sc_mod_adt_drop();
        return _ret;
    }
}
