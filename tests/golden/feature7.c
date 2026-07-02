/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_tnode sc_tnode;
typedef struct sc_slist sc_slist;
typedef struct sc_task sc_task;

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

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static inline sc_tnode *sc_slist_find__idx(sc_slist *_self, int32_t key) {
    sc_tnode *_o = (void *)0;
    return (sc_slist_find(_self, &_o, key) == 0) ? _o : (void *)0;
}

static void sc_slist_init(sc_slist *_this) {
    /* line 33 */
    _this->head = NULL;
    /* line 34 */
    _this->tail = NULL;
}

static int32_t sc_slist_insert(sc_slist *_this, sc_tnode *item, int32_t tag) {
    /* line 37 */
    if (tag != 0) {
        /* line 38 */
        item->prev = NULL;
        /* line 39 */
        item->next = _this->head;
        /* line 40 */
        if (_this->head != NULL) {
            /* line 41 */
            _this->head->prev = item;
        } else {
            /* line 43 */
            _this->tail = item;
        }
        /* line 44 */
        _this->head = item;
    } else {
        /* line 46 */
        item->prev = _this->tail;
        /* line 47 */
        item->next = NULL;
        /* line 48 */
        if (_this->tail != NULL) {
            /* line 49 */
            _this->tail->next = item;
        } else {
            /* line 51 */
            _this->head = item;
        }
        /* line 52 */
        _this->tail = item;
    }
    /* line 53 */
    return 0;
}

static int32_t sc_slist_remove(sc_slist *_this, sc_tnode *item) {
    /* line 56 */
    if (item->prev != NULL) {
        /* line 57 */
        item->prev->next = item->next;
    } else {
        /* line 59 */
        _this->head = item->next;
    }
    /* line 60 */
    if (item->next != NULL) {
        /* line 61 */
        item->next->prev = item->prev;
    } else {
        /* line 63 */
        _this->tail = item->prev;
    }
    /* line 64 */
    return 0;
}

static int32_t sc_slist_find(sc_slist *_this, sc_tnode **out, int32_t key) {
    /* line 67 */
    sc_tnode *p = _this->head;
    /* line 68 */
    while (p != NULL) {
        /* line 69 */
        sc_task *t = ((sc_task*)(p));
        /* line 70 */
        if (t->id != key) {
            /* line 71 */
            p = p->next;
        } else {
            /* line 73 */
            *(out) = p;
            /* line 74 */
            return 0;
        }
    }
    /* line 75 */
    return -(1);
}

static sc_tnode * sc_slist_first(sc_slist *_this) {
    /* line 78 */
    return _this->head;
}

static sc_tnode * sc_slist_last(sc_slist *_this) {
    /* line 81 */
    return _this->tail;
}

static sc_tnode * sc_slist_next(sc_slist *_this, sc_tnode *item) {
    /* line 84 */
    return item->next;
}

static sc_tnode * sc_slist_prev(sc_slist *_this, sc_tnode *item) {
    /* line 87 */
    return item->prev;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 97 */
    sc_slist lst = {0};
    sc_slist_init(&lst);
    /* line 98 */
    sc_task a[4] = {0};
    /* line 99 */
    int32_t i;
    /* line 100 */
    for (i = 0; i < 4; i++) {
        /* line 101 */
        a[i].id = ((i + 1) * 10);
    }
    /* line 105 */
    sc_slist_insert(&lst, (sc_tnode *)(&(a[0])), 0);
    /* line 106 */
    sc_slist_insert(&lst, (sc_tnode *)(&(a[1])), 0);
    /* line 107 */
    sc_slist_insert(&lst, (sc_tnode *)(&(a[2])), 0);
    /* line 108 */
    sc_slist_insert(&lst, (sc_tnode *)(&(a[3])), 1);
    /* line 111 */
    sc_task *it = ((sc_task*)(sc_slist_first(&lst)));
    /* line 112 */
    printf("正向:");
    /* line 113 */
    while (it != NULL) {
        /* line 114 */
        printf(" %d", it->id);
        /* line 115 */
        it = ((sc_task*)(sc_slist_next(&lst, (sc_tnode *)(it))));
    }
    /* line 116 */
    printf("\n");
    /* line 119 */
    sc_task *rit = ((sc_task*)(sc_slist_last(&lst)));
    /* line 120 */
    printf("反向:");
    /* line 121 */
    while (rit != NULL) {
        /* line 122 */
        printf(" %d", rit->id);
        /* line 123 */
        rit = ((sc_task*)(sc_slist_prev(&lst, (sc_tnode *)(rit))));
    }
    /* line 124 */
    printf("\n");
    /* line 127 */
    sc_task *found = {0};
    /* line 128 */
    if (sc_slist_find(&lst, (sc_tnode * *)(&(found)), 20) == 0) {
        /* line 129 */
        printf("find 20 -> id=%d\n", found->id);
    }
    /* line 132 */
    sc_task *hit = ((sc_task*)(sc_slist_find__idx(&lst, 30)));
    /* line 133 */
    if (hit != NULL) {
        /* line 134 */
        printf("lst[30] -> id=%d\n", hit->id);
    }
    /* line 135 */
    sc_task *miss = ((sc_task*)(sc_slist_find__idx(&lst, 99)));
    /* line 136 */
    if (miss == NULL) {
        /* line 137 */
        printf("lst[99] -> nil\n");
    }
    /* line 140 */
    sc_slist_remove(&lst, (sc_tnode *)(&(a[1])));
    /* line 141 */
    sc_task *it2 = ((sc_task*)(sc_slist_first(&lst)));
    /* line 142 */
    printf("remove 20 后:");
    /* line 143 */
    while (it2 != NULL) {
        /* line 144 */
        printf(" %d", it2->id);
        /* line 145 */
        it2 = ((sc_task*)(sc_slist_next(&lst, (sc_tnode *)(it2))));
    }
    /* line 146 */
    printf("\n");
    /* line 149 */
    int32_t *pid = ((int32_t*)(((void *)&((&(a[0]))->id))));
    /* line 150 */
    printf("base(&a[0]) -> id=%d\n", *(pid));
    /* line 152 */
    return 0;
}
