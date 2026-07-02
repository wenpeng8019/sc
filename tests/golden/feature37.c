/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_Cat sc_Cat;
typedef struct sc_Dog sc_Dog;
typedef struct sc_Fish sc_Fish;
typedef struct sc_Item sc_Item;

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
enum { SC_CLS_NONE = 0, SC_CLS_Cat = 1, SC_CLS_Dog = 2, SC_CLS_Fish = 3, SC_CLS_Item = 4 };
enum { SC_DIM_CLS_ID = 0, SC_DIM_REF = 1, SC_DIM_DROP = 2, SC_DIM_OBJ_KEY = 3, SC_DIM_OBJ_NAME = 4, SC_DIM_RLT_KEY = 5, SC_DIM_RLT_NAME = 6, SC_DIM_SPEAK = 7, SC_DIM_LEGS = 8 };

static void sc_obj_drop(void *);
tril sc_Cat_hyper_impl(void *, uint32_t, ...);
tril sc_Dog_hyper_impl(void *, uint32_t, ...);
tril sc_Fish_hyper_impl(void *, uint32_t, ...);
tril sc_Item_hyper_impl(void *, uint32_t, ...);

typedef struct sc_Cat {
    sc_hyper _class;
    int32_t age;
} sc_Cat;

static void sc_Cat_init(sc_Cat *_this);
typedef struct sc_Dog {
    sc_hyper _class;
    int32_t age;
} sc_Dog;

static void sc_Dog_init(sc_Dog *_this);
typedef struct sc_Fish {
    sc_hyper _class;
    int32_t age;
} sc_Fish;

static void sc_Fish_init(sc_Fish *_this);
typedef struct sc_Item {
    sc_hyper _class;
    int32_t obj_key;
    char obj_name[16];
} sc_Item;

static void sc_Item_init(sc_Item *_this);
static sc_Dog sc_gDog = {0};
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static void sc_Cat_init(sc_Cat *_this) {
    /* line 15 */
    _this->age = 3;
}

static void sc_Dog_init(sc_Dog *_this) {
    /* line 28 */
    _this->age = 5;
}

static void sc_Fish_init(sc_Fish *_this) {
    /* line 41 */
    _this->age = 1;
}

static void sc_Item_init(sc_Item *_this) {
    /* line 54 */
    _this->obj_key = 0;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_gDog._class = sc_Dog_hyper_impl;
    sc_Dog_init(&sc_gDog);
    /* line 62 */
    sc_Cat c = {0};
    c._class = sc_Cat_hyper_impl;
    sc_Cat_init(&c);
    /* line 63 */
    sc_Dog d = {0};
    d._class = sc_Dog_hyper_impl;
    sc_Dog_init(&d);
    /* line 64 */
    sc_Fish f = {0};
    f._class = sc_Fish_hyper_impl;
    sc_Fish_init(&f);
    /* line 67 */
    object pets[3];
    /* line 68 */
    pets[0] = (&(c)._class);
    /* line 69 */
    pets[1] = (&(d)._class);
    /* line 70 */
    pets[2] = (&(f)._class);
    /* line 72 */
    int32_t i = 0;
    /* line 73 */
    while (i < 3) {
        /* line 74 */
        char sound[16];
        /* line 75 */
        int8_t r = (*(pets[i]))(pets[i], SC_DIM_SPEAK, &(sound[0]), 16);
        /* line 76 */
        int32_t legs = 0;
        /* line 77 */
        (*(pets[i]))(pets[i], SC_DIM_LEGS, &(legs));
        /* line 78 */
        if (r == SC_TRIL_POS) {
            /* line 79 */
            printf("第%d只：%s（%d条腿）\n", i, &(sound[0]), legs);
        } else {
            /* line 81 */
            printf("第%d只：……不出声（%d条腿）\n", i, legs);
        }
        /* line 82 */
        i = (i + 1);
    }
    /* line 85 */
    if (*(pets[0]) == sc_Cat_hyper_impl) {
        /* line 86 */
        printf("pets[0] 是 Cat\n");
    }
    /* line 87 */
    if (!(*(pets[0]) == sc_Dog_hyper_impl)) {
        /* line 88 */
        printf("pets[0] 不是 Dog\n");
    }
    /* line 92 */
    char nm[32];
    /* line 93 */
    sc_Cat_hyper_impl(&(c)._class, SC_DIM_OBJ_NAME, &(nm[0]), 32);
    /* line 94 */
    printf("c 默认名前缀：%c\n", nm[0]);
    /* line 98 */
    sc_Item x = {0};
    x._class = sc_Item_hyper_impl;
    sc_Item_init(&x);
    /* line 99 */
    sc_Item y = {0};
    y._class = sc_Item_hyper_impl;
    sc_Item_init(&y);
    /* line 100 */
    x.obj_key = 10;
    /* line 101 */
    snprintf(&(x.obj_name[0]), 16, "apple");
    /* line 102 */
    y.obj_key = 20;
    /* line 103 */
    snprintf(&(y.obj_name[0]), 16, "banana");
    /* line 104 */
    int8_t rk = sc_Item_hyper_impl(&(x)._class, SC_DIM_RLT_KEY, (&(y)._class));
    /* line 105 */
    int8_t rn = sc_Item_hyper_impl(&(x)._class, SC_DIM_RLT_NAME, (&(y)._class));
    /* line 106 */
    int8_t rk2 = sc_Item_hyper_impl(&(y)._class, SC_DIM_RLT_KEY, (&(x)._class));
    /* line 107 */
    if (rk == SC_TRIL_NEG) {
        /* line 108 */
        printf("x.key < y.key（按 obj_key 字段比对）\n");
    }
    /* line 109 */
    if (rn == SC_TRIL_NEG) {
        /* line 110 */
        printf("x.name < y.name（按 obj_name 字段比对）\n");
    }
    /* line 111 */
    if (rk2 == SC_TRIL_POS) {
        /* line 112 */
        printf("y.key > x.key\n");
    }
    /* line 115 */
    object go = (&(sc_gDog)._class);
    /* line 116 */
    char gsound[16];
    /* line 117 */
    (*(go))(go, SC_DIM_SPEAK, &(gsound[0]), 16);
    /* line 118 */
    printf("全局 gDog 叫：%s\n", &(gsound[0]));
    /* line 119 */
    if (*(go) == sc_Dog_hyper_impl) {
        /* line 120 */
        printf("全局 gDog 是 Dog\n");
    }
    /* line 121 */
    return 0;
}

static void sc_obj_drop(void *_p) {
    if (!_p) return;
    sc_hyper _h = *(sc_hyper *)_p;
    if (_h) _h(_p, SC_DIM_DROP);
}

tril sc_Cat_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Cat *_this = (sc_Cat *)((char *)_slot - offsetof(sc_Cat, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Cat; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Cat@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Cat_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Cat_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_SPEAK: {
        char *buf = va_arg(_va, char *);
        int32_t cap = va_arg(_va, int32_t);
        va_end(_va);
        /* line 18 */
        snprintf(buf, cap, "喵");
        /* line 19 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    case SC_DIM_LEGS: {
        int32_t *out = va_arg(_va, int32_t *);
        va_end(_va);
        /* line 21 */
        *(out) = 4;
        /* line 22 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}

tril sc_Dog_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Dog *_this = (sc_Dog *)((char *)_slot - offsetof(sc_Dog, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Dog; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Dog@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Dog_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Dog_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_SPEAK: {
        char *buf = va_arg(_va, char *);
        int32_t cap = va_arg(_va, int32_t);
        va_end(_va);
        /* line 31 */
        snprintf(buf, cap, "汪");
        /* line 32 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    case SC_DIM_LEGS: {
        int32_t *out = va_arg(_va, int32_t *);
        va_end(_va);
        /* line 34 */
        *(out) = 4;
        /* line 35 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}

tril sc_Fish_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Fish *_this = (sc_Fish *)((char *)_slot - offsetof(sc_Fish, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Fish; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Fish@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Fish_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Fish_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_LEGS: {
        int32_t *out = va_arg(_va, int32_t *);
        va_end(_va);
        /* line 44 */
        *(out) = 0;
        /* line 45 */
        return SC_TRIL_POS;
        return SC_TRIL_UNK;
    }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}

tril sc_Item_hyper_impl(void *_slot, uint32_t _dim, ...) {
    sc_Item *_this = (sc_Item *)((char *)_slot - offsetof(sc_Item, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Item; return SC_TRIL_POS; }
    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)(uintptr_t)_this->obj_key; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "%s", _this->obj_name); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; sc_Item_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; sc_Item_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}
