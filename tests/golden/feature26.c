/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct sc_tnode sc_tnode;
typedef struct sc_slist sc_slist;
typedef struct sc_task sc_task;
typedef struct sc_cnode sc_cnode;

typedef struct sc_tnode {
    sc_tnode *next;
    sc_tnode *prev;
} sc_tnode;

typedef struct sc_slist {
    sc_tnode *head;
    sc_tnode *tail;
} sc_slist;

static void sc_slist_init(sc_slist *_this);
static int32_t sc_slist_insert(sc_slist *_this, sc_tnode *item, int32_t tag);
static int32_t sc_slist_remove(sc_slist *_this, sc_tnode *item);
static int32_t sc_slist_find(sc_slist *_this, sc_tnode **out, int32_t key);
static sc_tnode * sc_slist_first(sc_slist *_this);
static sc_tnode * sc_slist_last(sc_slist *_this);
static sc_tnode * sc_slist_next(sc_slist *_this, sc_tnode *item);
static sc_tnode * sc_slist_prev(sc_slist *_this, sc_tnode *item);
typedef struct sc_task {
    sc_tnode _adt;
    int32_t id;
} sc_task;

typedef struct sc_cnode {
    void *_prev;
    void *_next;
    int32_t v;
} sc_cnode;

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);

static void sc_slist_init(sc_slist *_this) {
    /* line 31 */
    _this->head = NULL;
    /* line 32 */
    _this->tail = NULL;
}

static int32_t sc_slist_insert(sc_slist *_this, sc_tnode *item, int32_t tag) {
    /* line 34 */
    item->prev = _this->tail;
    /* line 35 */
    item->next = NULL;
    /* line 36 */
    if (_this->tail != NULL) {
        /* line 37 */
        _this->tail->next = item;
    } else {
        /* line 39 */
        _this->head = item;
    }
    /* line 40 */
    _this->tail = item;
    /* line 41 */
    return 0;
}

static int32_t sc_slist_remove(sc_slist *_this, sc_tnode *item) {
    /* line 43 */
    return 0;
}

static int32_t sc_slist_find(sc_slist *_this, sc_tnode **out, int32_t key) {
    /* line 45 */
    sc_tnode *p = _this->head;
    /* line 46 */
    while (p != NULL) {
        /* line 47 */
        sc_task *t = ((sc_task*)(p));
        /* line 48 */
        if (t->id != key) {
            /* line 49 */
            p = p->next;
        } else {
            /* line 51 */
            *(out) = p;
            /* line 52 */
            return 0;
        }
    }
    /* line 53 */
    return -(1);
}

static sc_tnode * sc_slist_first(sc_slist *_this) {
    /* line 55 */
    return _this->head;
}

static sc_tnode * sc_slist_last(sc_slist *_this) {
    /* line 57 */
    return _this->tail;
}

static sc_tnode * sc_slist_next(sc_slist *_this, sc_tnode *item) {
    /* line 59 */
    return item->next;
}

static sc_tnode * sc_slist_prev(sc_slist *_this, sc_tnode *item) {
    /* line 61 */
    return item->prev;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    /* line 75 */
    printf("闭区间[1,5]:");
    /* line 76 */
    {
        int _flo0 = 1;
        int _fhi0 = 5;
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 <= _fhi0; _fi0 += 1, _fc0++) {
            int i = _fi0;
            /* line 77 */
            printf(" %d", i);
        }
    }
    /* line 78 */
    printf("\n");
    /* line 80 */
    printf("半开[1,5):");
    /* line 81 */
    {
        int _flo1 = 1;
        int _fhi1 = 5;
        long _fc1 = 0; (void)_fc1;
        for (int _fi1 = _flo1 + 0; _fi1 < _fhi1; _fi1 += 1, _fc1++) {
            int i = _fi1;
            /* line 82 */
            printf(" %d", i);
        }
    }
    /* line 83 */
    printf("\n");
    /* line 85 */
    printf("整数 4:");
    /* line 86 */
    {
        int _flo2 = 0;
        int _fhi2 = (4);
        long _fc2 = 0; (void)_fc2;
        for (int _fi2 = _flo2 + 0; _fi2 < _fhi2; _fi2 += 1, _fc2++) {
            int i = _fi2;
            /* line 87 */
            printf(" %d", i);
        }
    }
    /* line 88 */
    printf("\n");
    /* line 90 */
    printf("范围逆序:");
    /* line 91 */
    {
        int _flo3 = 1;
        int _fhi3 = 5;
        long _fc3 = 0; (void)_fc3;
        for (int _fi3 = _fhi3 - 0; _fi3 >= _flo3; _fi3 -= 1, _fc3++) {
            int i = _fi3;
            /* line 92 */
            printf(" %d", i);
        }
    }
    /* line 93 */
    printf("\n");
    /* line 95 */
    printf("范围步进2:");
    /* line 96 */
    {
        int _flo4 = 0;
        int _fhi4 = 10;
        long _fc4 = 0; (void)_fc4;
        for (int _fi4 = _flo4 + 0; _fi4 <= _fhi4; _fi4 += (2), _fc4++) {
            int i = _fi4;
            /* line 97 */
            printf(" %d", i);
        }
    }
    /* line 98 */
    printf("\n");
    /* line 101 */
    int32_t a[5];
    /* line 102 */
    int32_t k;
    /* line 103 */
    for (k = 0; k < 5; k++) {
        /* line 104 */
        a[k] = ((k + 1) * 11);
    }
    /* line 105 */
    printf("数组:");
    /* line 106 */
    {
        int32_t * _fb5 = a;
        long _fe5 = 5;
        long _fc5 = 0; (void)_fc5;
        for (long _fi5 = 0; _fi5 < _fe5; _fi5 += 1, _fc5++) {
            int32_t x = _fb5[_fi5];
            /* line 107 */
            printf(" %d", x);
        }
    }
    /* line 108 */
    printf("\n");
    /* line 110 */
    printf("数组逆序跳1:");
    /* line 111 */
    {
        int32_t * _fb6 = a;
        long _fe6 = 5;
        long _fc6 = 0; (void)_fc6;
        for (long _fi6 = _fe6 - 1 - (1); _fi6 >= 0; _fi6 -= 1, _fc6++) {
            int32_t x = _fb6[_fi6];
            /* line 112 */
            printf(" %d", x);
        }
    }
    /* line 113 */
    printf("\n");
    /* line 115 */
    char *s = "hello";
    /* line 116 */
    printf("字符串:");
    /* line 117 */
    {
        char * _fb7 = s;
        long _fe7 = 0;
        while (_fb7[_fe7] != '\0') _fe7++;
        long _fc7 = 0; (void)_fc7;
        for (long _fi7 = 0; _fi7 < _fe7; _fi7 += 1, _fc7++) {
            char c = _fb7[_fi7];
            /* line 118 */
            printf(" %c", c);
        }
    }
    /* line 119 */
    printf("\n");
    /* line 121 */
    printf("字符串前3:");
    /* line 122 */
    {
        char * _fb8 = s;
        long _fe8 = 0;
        while (_fb8[_fe8] != '\0') _fe8++;
        long _fc8 = 0; (void)_fc8;
        for (long _fi8 = 0; _fi8 < _fe8 && _fc8 < (3); _fi8 += 1, _fc8++) {
            char c = _fb8[_fi8];
            /* line 123 */
            printf(" %c", c);
        }
    }
    /* line 124 */
    printf("\n");
    /* line 127 */
    sc_chain l = {0};
    /* line 128 */
    sc_cnode cn[4] = {0};
    /* line 129 */
    for (k = 0; k < 4; k++) {
        /* line 130 */
        cn[k].v = ((k + 1) * 100);
        /* line 131 */
        sc_chain_append(&l, &(cn[k]));
    }
    /* line 132 */
    printf("链:");
    /* line 133 */
    {
        sc_chain * _fr9 = &l;
        void *_fi9 = (void *)sc_chain_first(_fr9);
        long _fc9 = 0; (void)_fc9;
        for (; _fi9 != (void *)0; _fi9 = ((void *)*(void **)((char *)_fi9 + sizeof(void *))), _fc9++) {
            sc_cnode * it = (sc_cnode *)_fi9;
            /* line 134 */
            printf(" %d", it->v);
        }
    }
    /* line 135 */
    printf("\n");
    /* line 137 */
    printf("链逆序:");
    /* line 138 */
    {
        sc_chain * _fr10 = &l;
        void *_fi10 = (void *)sc_chain_last(_fr10);
        long _fc10 = 0; (void)_fc10;
        for (; _fi10 != (void *)0; _fi10 = ((void *)sc_chain_prev(_fi10)), _fc10++) {
            sc_cnode * it = (sc_cnode *)_fi10;
            /* line 139 */
            printf(" %d", it->v);
        }
    }
    /* line 140 */
    printf("\n");
    /* line 142 */
    sc_slist lst = {0};
    sc_slist_init(&lst);
    /* line 143 */
    sc_task t[4] = {0};
    /* line 144 */
    for (k = 0; k < 4; k++) {
        /* line 145 */
        t[k].id = ((k + 1) * 10);
        /* line 146 */
        sc_slist_insert(&lst, (sc_tnode *)(&(t[k])), 0);
    }
    /* line 147 */
    printf("容器:");
    /* line 148 */
    {
        sc_slist * _fr11 = &lst;
        void *_fi11 = (void *)sc_slist_first(_fr11);
        long _fc11 = 0; (void)_fc11;
        for (; _fi11 != (void *)0; _fi11 = ((void *)sc_slist_next(_fr11, _fi11)), _fc11++) {
            sc_task * it = (sc_task *)_fi11;
            /* line 149 */
            printf(" %d", it->id);
        }
    }
    /* line 150 */
    printf("\n");
    /* line 152 */
    printf("容器逆序步2:");
    /* line 153 */
    {
        sc_slist * _fr12 = &lst;
        void *_fi12 = (void *)sc_slist_last(_fr12);
        long _fc12 = 0; (void)_fc12;
        int _ff12 = 1;
        while (1) {
            if (!_ff12) { long _fk12 = (2); while (_fk12-- > 0 && _fi12 != (void *)0) _fi12 = ((void *)sc_slist_prev(_fr12, _fi12)); _fc12++; }
            _ff12 = 0;
            if (!(_fi12 != (void *)0)) break;
            sc_task * it = (sc_task *)_fi12;
            /* line 154 */
            printf(" %d", it->id);
        }
    }
    /* line 155 */
    printf("\n");
    /* line 161 */
    printf("数组带下标:");
    /* line 162 */
    {
        int32_t * _fb13 = a;
        long _fe13 = 5;
        long _fc13 = 0; (void)_fc13;
        for (long _fi13 = 0; _fi13 < _fe13; _fi13 += 1, _fc13++) {
            int32_t x = _fb13[_fi13];
            int i = (int)(_fi13);
            /* line 163 */
            printf(" a[%d]=%d", i, x);
        }
    }
    /* line 164 */
    printf("\n");
    /* line 166 */
    printf("数组逆序带下标:");
    /* line 167 */
    {
        int32_t * _fb14 = a;
        long _fe14 = 5;
        long _fc14 = 0; (void)_fc14;
        for (long _fi14 = _fe14 - 1 - 0; _fi14 >= 0; _fi14 -= 1, _fc14++) {
            int32_t x = _fb14[_fi14];
            int i = (int)(_fi14);
            /* line 168 */
            printf(" a[%d]=%d", i, x);
        }
    }
    /* line 169 */
    printf("\n");
    /* line 171 */
    printf("链带计数:");
    /* line 172 */
    {
        sc_chain * _fr15 = &l;
        void *_fi15 = (void *)sc_chain_first(_fr15);
        long _fc15 = 0; (void)_fc15;
        for (; _fi15 != (void *)0; _fi15 = ((void *)*(void **)((char *)_fi15 + sizeof(void *))), _fc15++) {
            sc_cnode * it = (sc_cnode *)_fi15;
            int i = (int)(_fc15);
            /* line 173 */
            printf(" #%d=%d", i, it->v);
        }
    }
    /* line 174 */
    printf("\n");
    /* line 177 */
    int32_t m[2][3];
    /* line 178 */
    int32_t r;
    /* line 179 */
    int32_t c;
    /* line 180 */
    for (r = 0; r < 2; r++) {
        /* line 181 */
        for (c = 0; c < 3; c++) {
            /* line 182 */
            m[r][c] = ((r * 10) + c);
        }
    }
    /* line 183 */
    printf("二维:");
    /* line 184 */
    {
        for (long _fi16_0 = 0; _fi16_0 < (2); _fi16_0++) {
            for (long _fi16_1 = 0; _fi16_1 < (3); _fi16_1++) {
                int i = (int)_fi16_0;
                int j = (int)_fi16_1;
                int32_t v = m[_fi16_0][_fi16_1];
                /* line 185 */
                printf(" m[%d][%d]=%d", i, j, v);
            }
        }
    }
    /* line 186 */
    printf("\n");
    /* line 188 */
    printf("二维逆序:");
    /* line 189 */
    {
        for (long _fi17_0 = (2) - 1; _fi17_0 >= 0; _fi17_0--) {
            for (long _fi17_1 = (3) - 1; _fi17_1 >= 0; _fi17_1--) {
                int i = (int)_fi17_0;
                int j = (int)_fi17_1;
                int32_t v = m[_fi17_0][_fi17_1];
                /* line 190 */
                printf(" m[%d][%d]=%d", i, j, v);
            }
        }
    }
    /* line 191 */
    printf("\n");
    /* line 193 */
    {
        int32_t _ret = 0;
        sc_mod_adt_drop();
        return _ret;
    }
}
