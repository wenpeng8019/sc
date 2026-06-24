/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct Cat Cat;
typedef struct Dog Dog;
typedef struct Fish Fish;
typedef struct Item Item;

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
enum { SC_DIM_CLS_ID = 0, SC_DIM_OBJ_KEY = 1, SC_DIM_OBJ_NAME = 2, SC_DIM_RLT_KEY = 3, SC_DIM_RLT_NAME = 4, SC_DIM_SPEAK = 5, SC_DIM_LEGS = 6 };

tril Cat_hyper_impl(void *, uint32_t, ...);
tril Dog_hyper_impl(void *, uint32_t, ...);
tril Fish_hyper_impl(void *, uint32_t, ...);
tril Item_hyper_impl(void *, uint32_t, ...);

typedef struct Cat {
    sc_hyper _class;
    int32_t age;
} Cat;

static void Cat_init(Cat *_this);
typedef struct Dog {
    sc_hyper _class;
    int32_t age;
} Dog;

static void Dog_init(Dog *_this);
typedef struct Fish {
    sc_hyper _class;
    int32_t age;
} Fish;

static void Fish_init(Fish *_this);
typedef struct Item {
    sc_hyper _class;
    int32_t obj_key;
    char obj_name[16];
} Item;

static void Item_init(Item *_this);
static Dog gDog = {0};
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void Cat_init(Cat *_this) {
    /* line 15 */
    _this->age = 3;
}

static void Dog_init(Dog *_this) {
    /* line 28 */
    _this->age = 5;
}

static void Fish_init(Fish *_this) {
    /* line 41 */
    _this->age = 1;
}

static void Item_init(Item *_this) {
    /* line 54 */
    _this->obj_key = 0;
}

int32_t main(void) {
    gDog._class = Dog_hyper_impl;
    Dog_init(&gDog);
    /* line 62 */
    Cat c = {0};
    c._class = Cat_hyper_impl;
    Cat_init(&c);
    /* line 63 */
    Dog d = {0};
    d._class = Dog_hyper_impl;
    Dog_init(&d);
    /* line 64 */
    Fish f = {0};
    f._class = Fish_hyper_impl;
    Fish_init(&f);
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
    if (*(pets[0]) == Cat_hyper_impl) {
        /* line 86 */
        printf("pets[0] 是 Cat\n");
    }
    /* line 87 */
    if (!(*(pets[0]) == Dog_hyper_impl)) {
        /* line 88 */
        printf("pets[0] 不是 Dog\n");
    }
    /* line 92 */
    char nm[32];
    /* line 93 */
    Cat_hyper_impl(&(c)._class, SC_DIM_OBJ_NAME, &(nm[0]), 32);
    /* line 94 */
    printf("c 默认名前缀：%c\n", nm[0]);
    /* line 98 */
    Item x = {0};
    x._class = Item_hyper_impl;
    Item_init(&x);
    /* line 99 */
    Item y = {0};
    y._class = Item_hyper_impl;
    Item_init(&y);
    /* line 100 */
    x.obj_key = 10;
    /* line 101 */
    snprintf(&(x.obj_name[0]), 16, "apple");
    /* line 102 */
    y.obj_key = 20;
    /* line 103 */
    snprintf(&(y.obj_name[0]), 16, "banana");
    /* line 104 */
    int8_t rk = Item_hyper_impl(&(x)._class, SC_DIM_RLT_KEY, (&(y)._class));
    /* line 105 */
    int8_t rn = Item_hyper_impl(&(x)._class, SC_DIM_RLT_NAME, (&(y)._class));
    /* line 106 */
    int8_t rk2 = Item_hyper_impl(&(y)._class, SC_DIM_RLT_KEY, (&(x)._class));
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
    object go = (&(gDog)._class);
    /* line 116 */
    char gsound[16];
    /* line 117 */
    (*(go))(go, SC_DIM_SPEAK, &(gsound[0]), 16);
    /* line 118 */
    printf("全局 gDog 叫：%s\n", &(gsound[0]));
    /* line 119 */
    if (*(go) == Dog_hyper_impl) {
        /* line 120 */
        printf("全局 gDog 是 Dog\n");
    }
    /* line 121 */
    return 0;
}

tril Cat_hyper_impl(void *_slot, uint32_t _dim, ...) {
    Cat *_this = (Cat *)((char *)_slot - offsetof(Cat, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Cat; return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Cat@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; Cat_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; Cat_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
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

tril Dog_hyper_impl(void *_slot, uint32_t _dim, ...) {
    Dog *_this = (Dog *)((char *)_slot - offsetof(Dog, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Dog; return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Dog@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; Dog_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; Dog_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
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

tril Fish_hyper_impl(void *_slot, uint32_t _dim, ...) {
    Fish *_this = (Fish *)((char *)_slot - offsetof(Fish, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Fish; return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)_this; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "Fish@%p", (void *)_this); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; Fish_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; Fish_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
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

tril Item_hyper_impl(void *_slot, uint32_t _dim, ...) {
    Item *_this = (Item *)((char *)_slot - offsetof(Item, _class));
    (void)_this;
    va_list _va; va_start(_va, _dim);
    switch (_dim) {
    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); va_end(_va); *_id = SC_CLS_Item; return SC_TRIL_POS; }
    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = (void *)(uintptr_t)_this->obj_key; return SC_TRIL_POS; }
    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); int32_t _cap = va_arg(_va, int); va_end(_va); snprintf(_b, (size_t)_cap, "%s", _this->obj_name); return SC_TRIL_POS; }
    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); void *_ka = (void *)0, *_kb = (void *)0; Item_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; return SC_TRIL_UNK; }
    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; Item_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); int _r = strcmp(_na, _nb); if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; return SC_TRIL_UNK; }
    default: va_end(_va); return SC_TRIL_UNK;
    }
}
