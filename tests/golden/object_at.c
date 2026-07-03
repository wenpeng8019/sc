/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_Dog sc_Dog;
typedef struct sc_Node sc_Node;

/* 类机制运行时（cls / dim / object） */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef int8_t tril;
#define SC_TRIL_NEG ((tril)-1)
#define SC_TRIL_UNK ((tril)0)
#define SC_TRIL_POS ((tril)1)
typedef tril (*sc_hyper)(void *, uint32_t, ...);
typedef sc_hyper *object;
enum { SC_CLS_NONE = 0, SC_CLS_Dog = 1, SC_CLS_Node = 2 };
enum { SC_DIM_CLS_ID = 0, SC_DIM_REF = 1, SC_DIM_DROP = 2, SC_DIM_OBJ_KEY = 3, SC_DIM_OBJ_NAME = 4, SC_DIM_RLT_KEY = 5, SC_DIM_RLT_NAME = 6, SC_DIM_LEGS = 7 };

static void sc_obj_drop(void *);
tril sc_Dog_hyper_impl(void *, uint32_t, ...);
tril sc_Node_hyper_impl(void *, uint32_t, ...);

typedef struct sc_Dog {
    sc_hyper _class;
    int32_t age;
} sc_Dog;

static void sc_Dog_init(sc_Dog *_this);
static void sc_Dog_drop(sc_Dog *_this);
typedef struct sc_Node {
    void *_prev;
    void *_next;
    sc_hyper _class;
    int32_t age;
} sc_Node;

static void sc_Node_init(sc_Node *_this);
static void sc_Node_drop(sc_Node *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static inline sc_Dog *sc_Dog__new(void) {
    sc_Dog *_p = (sc_Dog *)sc_alloc(sizeof(sc_Dog));
    if (_p) {
        memset(_p, 0, sizeof(sc_Dog));
        _p->_class = sc_Dog_hyper_impl;
        sc_Dog_init(_p);
    }
    return _p;
}

static inline sc_Dog *sc_Dog__new_ref(int32_t _flags) {
    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)
        ? sc_alloc(SC_REF_HDR + sizeof(sc_Dog))
        : sc_chunk(SC_REF_HDR + sizeof(sc_Dog)));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;
    sc_Dog *_p = (sc_Dog *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_Dog));
    _p->_class = sc_Dog_hyper_impl;
    sc_Dog_init(_p);
    return _p;
}

static inline sc_Node *sc_Node__new(void) {
    sc_Node *_p = (sc_Node *)sc_alloc(sizeof(sc_Node));
    if (_p) {
        memset(_p, 0, sizeof(sc_Node));
        _p->_class = sc_Node_hyper_impl;
        sc_Node_init(_p);
    }
    return _p;
}

static inline sc_Node *sc_Node__new_ref(int32_t _flags) {
    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)
        ? sc_alloc(SC_REF_HDR + sizeof(sc_Node))
        : sc_chunk(SC_REF_HDR + sizeof(sc_Node)));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;
    sc_Node *_p = (sc_Node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_Node));
    _p->_class = sc_Node_hyper_impl;
    sc_Node_init(_p);
    return _p;
}

static void sc_Dog_init(sc_Dog *_this) {
    /* line 9 */
    _this->age = 5;
}

static void sc_Dog_drop(sc_Dog *_this) {
    /* line 11 */
    printf("Dog drop\n");
}

static void sc_Node_init(sc_Node *_this) {
    /* line 21 */
    _this->age = 7;
}

static void sc_Node_drop(sc_Node *_this) {
    /* line 23 */
    printf("Node drop\n");
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 30 */
    sc_fat d = {0};
    sc_Dog *_fat0 = sc_Dog__new_ref(0);
    sc_fat_bind(&d, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 31 */
    sc_fat od = {0};
    sc_fat_bind(&od, (void *)&((sc_Dog *)(d).p)->_class, (sc_ref *)(d).tar, SC_OWN_ROOT);
    /* line 32 */
    int32_t nd;
    /* line 33 */
    (*(object)(od).p)((object)(od).p, SC_DIM_LEGS, &(nd));
    /* line 34 */
    printf("dog legs=%d\n", nd);
    /* line 36 */
    sc_fat k = {0};
    sc_Node *_fat1 = sc_Node__new_ref(0);
    sc_fat_bind(&k, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 37 */
    sc_fat ok = {0};
    sc_fat_bind(&ok, (void *)&((sc_Node *)(k).p)->_class, (sc_ref *)(k).tar, SC_OWN_ROOT);
    /* line 38 */
    int32_t nk;
    /* line 39 */
    (*(object)(ok).p)((object)(ok).p, SC_DIM_LEGS, &(nk));
    /* line 40 */
    printf("node legs=%d\n", nk);
    /* line 44 */
    sc_fat bk = {0};
    sc_fat_bind(&bk, (sc_fat_suboff(ok, offsetof(sc_Node, _class))).p, (sc_ref *)(sc_fat_suboff(ok, offsetof(sc_Node, _class))).tar, SC_OWN_ROOT);
    /* line 45 */
    printf("back age=%d\n", ((sc_Node *)(bk).p)->age);
    /* line 50 */
    sc_afat be = {0};
    sc_afat_bind(&be, (ok).p, (sc_ref *)(ok).tar, SC_OWN_ROOT, (void (*)(void *))sc_obj_drop);
    /* line 51 */
    sc_fat back2 = {0};
    sc_fat_bind(&back2, (sc_afat_as_fat_suboff(be, offsetof(sc_Node, _class), (void (*)(void *))sc_obj_drop)).p, (sc_ref *)(sc_afat_as_fat_suboff(be, offsetof(sc_Node, _class), (void (*)(void *))sc_obj_drop)).tar, SC_OWN_ROOT);
    /* line 52 */
    printf("back2 age=%d\n", ((sc_Node *)(back2).p)->age);
    /* line 56 */
    sc_fat d2 = {0};
    sc_Dog *_fat2 = sc_Dog__new_ref(0);
    sc_fat_bind(&d2, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 57 */
    sc_afat bd = {0};
    sc_afat_bind(&bd, (void *)&((sc_Dog *)(d2).p)->_class, (sc_ref *)(d2).tar, SC_OWN_ROOT, (void (*)(void *))sc_obj_drop);
    /* line 58 */
    sc_fat od2 = {0};
    sc_fat_bind(&od2, (sc_afat_as_fat(bd)).p, (sc_ref *)(sc_afat_as_fat(bd)).tar, SC_OWN_ROOT);
    /* line 59 */
    int32_t nd2;
    /* line 60 */
    (*(object)(od2).p)((object)(od2).p, SC_DIM_LEGS, &(nd2));
    /* line 61 */
    printf("od2 legs=%d\n", nd2);
    /* line 62 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&od2, (void (*)(void *))sc_obj_drop);
        sc_afat_unbind(&bd);
        sc_fat_unbind_d(&d2, (void (*)(void *))sc_Dog_drop);
        sc_fat_unbind_d(&back2, (void (*)(void *))sc_Node_drop);
        sc_afat_unbind(&be);
        sc_fat_unbind_d(&bk, (void (*)(void *))sc_Node_drop);
        sc_fat_unbind_d(&ok, (void (*)(void *))sc_obj_drop);
        sc_fat_unbind_d(&k, (void (*)(void *))sc_Node_drop);
        sc_fat_unbind_d(&od, (void (*)(void *))sc_obj_drop);
        sc_fat_unbind_d(&d, (void (*)(void *))sc_Dog_drop);
        return _ret;
    }
}

static void sc_obj_drop(void *_p) {
    if (!_p) return;
    sc_hyper _h = *(sc_hyper *)_p;
    if (_h) _h(_p, SC_DIM_DROP);
}

tril sc_Dog_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Dog *_this = (sc_Dog *)((char *)_slot - offsetof(sc_Dog, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Dog; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_DROP: { va_end(_va); sc_Dog_drop(_this); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Dog@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Dog_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Dog_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_LEGS: {
        int32_t *out = va_arg(_va, int32_t *);
        va_end(_va);
        /* line 13 */
        *(out) = 4;
        /* line 14 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}

tril sc_Node_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Node *_this = (sc_Node *)((char *)_slot - offsetof(sc_Node, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Node; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_DROP: { va_end(_va); sc_Node_drop(_this); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Node@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Node_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Node_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_LEGS: {
        int32_t *out = va_arg(_va, int32_t *);
        va_end(_va);
        /* line 25 */
        *(out) = _this->age;
        /* line 26 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}
