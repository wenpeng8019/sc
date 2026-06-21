/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct tnode tnode;
typedef struct slist slist;
typedef struct task task;
typedef struct cnode cnode;

typedef struct tnode {
    tnode *next;
    tnode *prev;
} tnode;

typedef struct slist {
    tnode *head;
    tnode *tail;
} slist;

static void slist_init(slist *_this);
static int32_t slist_insert(slist *_this, tnode *item, int32_t tag);
static int32_t slist_remove(slist *_this, tnode *item);
static int32_t slist_find(slist *_this, tnode **out, int32_t key);
static tnode * slist_first(slist *_this);
static tnode * slist_last(slist *_this);
static tnode * slist_next(slist *_this, tnode *item);
static tnode * slist_prev(slist *_this, tnode *item);
typedef struct task {
    tnode _adt;
    int32_t id;
} task;

typedef struct cnode {
    void *_prev;
    void *_next;
    int32_t v;
} cnode;

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void slist_init(slist *_this) {
    /* line 32 */
    _this->head = NULL;
    /* line 33 */
    _this->tail = NULL;
}

static int32_t slist_insert(slist *_this, tnode *item, int32_t tag) {
    /* line 35 */
    item->prev = _this->tail;
    /* line 36 */
    item->next = NULL;
    /* line 37 */
    if (_this->tail != NULL) {
        /* line 38 */
        _this->tail->next = item;
    } else {
        /* line 40 */
        _this->head = item;
    }
    /* line 41 */
    _this->tail = item;
    /* line 42 */
    return 0;
}

static int32_t slist_remove(slist *_this, tnode *item) {
    /* line 44 */
    return 0;
}

static int32_t slist_find(slist *_this, tnode **out, int32_t key) {
    /* line 46 */
    tnode *p = _this->head;
    /* line 47 */
    while (p != NULL) {
        /* line 48 */
        task *t = ((task*)(p));
        /* line 49 */
        if (t->id != key) {
            /* line 50 */
            p = p->next;
        } else {
            /* line 52 */
            *(out) = p;
            /* line 53 */
            return 0;
        }
    }
    /* line 54 */
    return -(1);
}

static tnode * slist_first(slist *_this) {
    /* line 56 */
    return _this->head;
}

static tnode * slist_last(slist *_this) {
    /* line 58 */
    return _this->tail;
}

static tnode * slist_next(slist *_this, tnode *item) {
    /* line 60 */
    return item->next;
}

static tnode * slist_prev(slist *_this, tnode *item) {
    /* line 62 */
    return item->prev;
}

int32_t main(void) {
    /* line 76 */
    printf("闭区间[1,5]:");
    /* line 77 */
    {
        int _flo0 = 1;
        int _fhi0 = 5;
        long _fc0 = 0; (void)_fc0;
        for (int _fi0 = _flo0 + 0; _fi0 <= _fhi0; _fi0 += 1, _fc0++) {
            int i = _fi0;
            /* line 78 */
            printf(" %d", i);
        }
    }
    /* line 79 */
    printf("\n");
    /* line 81 */
    printf("半开[1,5):");
    /* line 82 */
    {
        int _flo1 = 1;
        int _fhi1 = 5;
        long _fc1 = 0; (void)_fc1;
        for (int _fi1 = _flo1 + 0; _fi1 < _fhi1; _fi1 += 1, _fc1++) {
            int i = _fi1;
            /* line 83 */
            printf(" %d", i);
        }
    }
    /* line 84 */
    printf("\n");
    /* line 86 */
    printf("整数 4:");
    /* line 87 */
    {
        int _flo2 = 0;
        int _fhi2 = (4);
        long _fc2 = 0; (void)_fc2;
        for (int _fi2 = _flo2 + 0; _fi2 < _fhi2; _fi2 += 1, _fc2++) {
            int i = _fi2;
            /* line 88 */
            printf(" %d", i);
        }
    }
    /* line 89 */
    printf("\n");
    /* line 91 */
    printf("范围逆序:");
    /* line 92 */
    {
        int _flo3 = 1;
        int _fhi3 = 5;
        long _fc3 = 0; (void)_fc3;
        for (int _fi3 = _fhi3 - 0; _fi3 >= _flo3; _fi3 -= 1, _fc3++) {
            int i = _fi3;
            /* line 93 */
            printf(" %d", i);
        }
    }
    /* line 94 */
    printf("\n");
    /* line 96 */
    printf("范围步进2:");
    /* line 97 */
    {
        int _flo4 = 0;
        int _fhi4 = 10;
        long _fc4 = 0; (void)_fc4;
        for (int _fi4 = _flo4 + 0; _fi4 <= _fhi4; _fi4 += (2), _fc4++) {
            int i = _fi4;
            /* line 98 */
            printf(" %d", i);
        }
    }
    /* line 99 */
    printf("\n");
    /* line 102 */
    int32_t a[5];
    /* line 103 */
    int32_t k;
    /* line 104 */
    for (k = 0; k < 5; k++) {
        /* line 105 */
        a[k] = ((k + 1) * 11);
    }
    /* line 106 */
    printf("数组:");
    /* line 107 */
    {
        int32_t * _fb5 = a;
        long _fe5 = 5;
        long _fc5 = 0; (void)_fc5;
        for (long _fi5 = 0; _fi5 < _fe5; _fi5 += 1, _fc5++) {
            int32_t x = _fb5[_fi5];
            /* line 108 */
            printf(" %d", x);
        }
    }
    /* line 109 */
    printf("\n");
    /* line 111 */
    printf("数组逆序跳1:");
    /* line 112 */
    {
        int32_t * _fb6 = a;
        long _fe6 = 5;
        long _fc6 = 0; (void)_fc6;
        for (long _fi6 = _fe6 - 1 - (1); _fi6 >= 0; _fi6 -= 1, _fc6++) {
            int32_t x = _fb6[_fi6];
            /* line 113 */
            printf(" %d", x);
        }
    }
    /* line 114 */
    printf("\n");
    /* line 116 */
    char *s = "hello";
    /* line 117 */
    printf("字符串:");
    /* line 118 */
    {
        char * _fb7 = s;
        long _fe7 = 0;
        while (_fb7[_fe7] != '\0') _fe7++;
        long _fc7 = 0; (void)_fc7;
        for (long _fi7 = 0; _fi7 < _fe7; _fi7 += 1, _fc7++) {
            char c = _fb7[_fi7];
            /* line 119 */
            printf(" %c", c);
        }
    }
    /* line 120 */
    printf("\n");
    /* line 122 */
    printf("字符串前3:");
    /* line 123 */
    {
        char * _fb8 = s;
        long _fe8 = 0;
        while (_fb8[_fe8] != '\0') _fe8++;
        long _fc8 = 0; (void)_fc8;
        for (long _fi8 = 0; _fi8 < _fe8 && _fc8 < (3); _fi8 += 1, _fc8++) {
            char c = _fb8[_fi8];
            /* line 124 */
            printf(" %c", c);
        }
    }
    /* line 125 */
    printf("\n");
    /* line 128 */
    chain l = {0};
    /* line 129 */
    cnode cn[4] = {0};
    /* line 130 */
    for (k = 0; k < 4; k++) {
        /* line 131 */
        cn[k].v = ((k + 1) * 100);
        /* line 132 */
        chain_append(&l, &(cn[k]));
    }
    /* line 133 */
    printf("链:");
    /* line 134 */
    {
        chain * _fr9 = &l;
        void *_fi9 = (void *)chain_first(_fr9);
        long _fc9 = 0; (void)_fc9;
        for (; _fi9 != (void *)0; _fi9 = ((void *)*(void **)((char *)_fi9 + sizeof(void *))), _fc9++) {
            cnode * it = (cnode *)_fi9;
            /* line 135 */
            printf(" %d", it->v);
        }
    }
    /* line 136 */
    printf("\n");
    /* line 138 */
    printf("链逆序:");
    /* line 139 */
    {
        chain * _fr10 = &l;
        void *_fi10 = (void *)chain_last(_fr10);
        long _fc10 = 0; (void)_fc10;
        for (; _fi10 != (void *)0; _fi10 = ((void *)chain_prev(_fi10)), _fc10++) {
            cnode * it = (cnode *)_fi10;
            /* line 140 */
            printf(" %d", it->v);
        }
    }
    /* line 141 */
    printf("\n");
    /* line 143 */
    slist lst = {0};
    slist_init(&lst);
    /* line 144 */
    task t[4] = {0};
    /* line 145 */
    for (k = 0; k < 4; k++) {
        /* line 146 */
        t[k].id = ((k + 1) * 10);
        /* line 147 */
        slist_insert(&lst, (tnode *)(&(t[k])), 0);
    }
    /* line 148 */
    printf("容器:");
    /* line 149 */
    {
        slist * _fr11 = &lst;
        void *_fi11 = (void *)slist_first(_fr11);
        long _fc11 = 0; (void)_fc11;
        for (; _fi11 != (void *)0; _fi11 = ((void *)slist_next(_fr11, _fi11)), _fc11++) {
            task * it = (task *)_fi11;
            /* line 150 */
            printf(" %d", it->id);
        }
    }
    /* line 151 */
    printf("\n");
    /* line 153 */
    printf("容器逆序步2:");
    /* line 154 */
    {
        slist * _fr12 = &lst;
        void *_fi12 = (void *)slist_last(_fr12);
        long _fc12 = 0; (void)_fc12;
        for (; _fi12 != (void *)0; ({ long _fk12 = (2); while (_fk12-- > 0 && _fi12 != (void *)0) _fi12 = ((void *)slist_prev(_fr12, _fi12)); }), _fc12++) {
            task * it = (task *)_fi12;
            /* line 155 */
            printf(" %d", it->id);
        }
    }
    /* line 156 */
    printf("\n");
    /* line 162 */
    printf("数组带下标:");
    /* line 163 */
    {
        int32_t * _fb13 = a;
        long _fe13 = 5;
        long _fc13 = 0; (void)_fc13;
        for (long _fi13 = 0; _fi13 < _fe13; _fi13 += 1, _fc13++) {
            int32_t x = _fb13[_fi13];
            int i = (int)(_fi13);
            /* line 164 */
            printf(" a[%d]=%d", i, x);
        }
    }
    /* line 165 */
    printf("\n");
    /* line 167 */
    printf("数组逆序带下标:");
    /* line 168 */
    {
        int32_t * _fb14 = a;
        long _fe14 = 5;
        long _fc14 = 0; (void)_fc14;
        for (long _fi14 = _fe14 - 1 - 0; _fi14 >= 0; _fi14 -= 1, _fc14++) {
            int32_t x = _fb14[_fi14];
            int i = (int)(_fi14);
            /* line 169 */
            printf(" a[%d]=%d", i, x);
        }
    }
    /* line 170 */
    printf("\n");
    /* line 172 */
    printf("链带计数:");
    /* line 173 */
    {
        chain * _fr15 = &l;
        void *_fi15 = (void *)chain_first(_fr15);
        long _fc15 = 0; (void)_fc15;
        for (; _fi15 != (void *)0; _fi15 = ((void *)*(void **)((char *)_fi15 + sizeof(void *))), _fc15++) {
            cnode * it = (cnode *)_fi15;
            int i = (int)(_fc15);
            /* line 174 */
            printf(" #%d=%d", i, it->v);
        }
    }
    /* line 175 */
    printf("\n");
    /* line 178 */
    int32_t m[2][3];
    /* line 179 */
    int32_t r;
    /* line 180 */
    int32_t c;
    /* line 181 */
    for (r = 0; r < 2; r++) {
        /* line 182 */
        for (c = 0; c < 3; c++) {
            /* line 183 */
            m[r][c] = ((r * 10) + c);
        }
    }
    /* line 184 */
    printf("二维:");
    /* line 185 */
    {
        for (long _fi16_0 = 0; _fi16_0 < (2); _fi16_0++) {
            for (long _fi16_1 = 0; _fi16_1 < (3); _fi16_1++) {
                int i = (int)_fi16_0;
                int j = (int)_fi16_1;
                int32_t v = m[_fi16_0][_fi16_1];
                /* line 186 */
                printf(" m[%d][%d]=%d", i, j, v);
            }
        }
    }
    /* line 187 */
    printf("\n");
    /* line 189 */
    printf("二维逆序:");
    /* line 190 */
    {
        for (long _fi17_0 = (2) - 1; _fi17_0 >= 0; _fi17_0--) {
            for (long _fi17_1 = (3) - 1; _fi17_1 >= 0; _fi17_1--) {
                int i = (int)_fi17_0;
                int j = (int)_fi17_1;
                int32_t v = m[_fi17_0][_fi17_1];
                /* line 191 */
                printf(" m[%d][%d]=%d", i, j, v);
            }
        }
    }
    /* line 192 */
    printf("\n");
    /* line 194 */
    return 0;
}
