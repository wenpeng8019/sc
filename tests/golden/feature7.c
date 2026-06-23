/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct tnode tnode;
typedef struct slist slist;
typedef struct task task;

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

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static void slist_init(slist *_this) {
    /* line 33 */
    _this->head = NULL;
    /* line 34 */
    _this->tail = NULL;
}

static int32_t slist_insert(slist *_this, tnode *item, int32_t tag) {
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

static int32_t slist_remove(slist *_this, tnode *item) {
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

static int32_t slist_find(slist *_this, tnode **out, int32_t key) {
    /* line 67 */
    tnode *p = _this->head;
    /* line 68 */
    while (p != NULL) {
        /* line 69 */
        task *t = ((task*)(p));
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

static tnode * slist_first(slist *_this) {
    /* line 78 */
    return _this->head;
}

static tnode * slist_last(slist *_this) {
    /* line 81 */
    return _this->tail;
}

static tnode * slist_next(slist *_this, tnode *item) {
    /* line 84 */
    return item->next;
}

static tnode * slist_prev(slist *_this, tnode *item) {
    /* line 87 */
    return item->prev;
}

int32_t main(void) {
    /* line 97 */
    slist lst = {0};
    slist_init(&lst);
    /* line 98 */
    task a[4] = {0};
    /* line 99 */
    int32_t i;
    /* line 100 */
    for (i = 0; i < 4; i++) {
        /* line 101 */
        a[i].id = ((i + 1) * 10);
    }
    /* line 105 */
    slist_insert(&lst, (tnode *)(&(a[0])), 0);
    /* line 106 */
    slist_insert(&lst, (tnode *)(&(a[1])), 0);
    /* line 107 */
    slist_insert(&lst, (tnode *)(&(a[2])), 0);
    /* line 108 */
    slist_insert(&lst, (tnode *)(&(a[3])), 1);
    /* line 111 */
    task *it = ((task*)(slist_first(&lst)));
    /* line 112 */
    printf("正向:");
    /* line 113 */
    while (it != NULL) {
        /* line 114 */
        printf(" %d", it->id);
        /* line 115 */
        it = ((task*)(slist_next(&lst, (tnode *)(it))));
    }
    /* line 116 */
    printf("\n");
    /* line 119 */
    task *rit = ((task*)(slist_last(&lst)));
    /* line 120 */
    printf("反向:");
    /* line 121 */
    while (rit != NULL) {
        /* line 122 */
        printf(" %d", rit->id);
        /* line 123 */
        rit = ((task*)(slist_prev(&lst, (tnode *)(rit))));
    }
    /* line 124 */
    printf("\n");
    /* line 127 */
    task *found = {0};
    /* line 128 */
    if (slist_find(&lst, (tnode * *)(&(found)), 20) == 0) {
        /* line 129 */
        printf("find 20 -> id=%d\n", found->id);
    }
    /* line 132 */
    task *hit = ((task*)((__extension__ ({ tnode *_scfo = (void *)0; (slist_find(&lst, &_scfo, 30) == 0) ? _scfo : (void *)0; }))));
    /* line 133 */
    if (hit != NULL) {
        /* line 134 */
        printf("lst[30] -> id=%d\n", hit->id);
    }
    /* line 135 */
    task *miss = ((task*)((__extension__ ({ tnode *_scfo = (void *)0; (slist_find(&lst, &_scfo, 99) == 0) ? _scfo : (void *)0; }))));
    /* line 136 */
    if (miss == NULL) {
        /* line 137 */
        printf("lst[99] -> nil\n");
    }
    /* line 140 */
    slist_remove(&lst, (tnode *)(&(a[1])));
    /* line 141 */
    task *it2 = ((task*)(slist_first(&lst)));
    /* line 142 */
    printf("remove 20 后:");
    /* line 143 */
    while (it2 != NULL) {
        /* line 144 */
        printf(" %d", it2->id);
        /* line 145 */
        it2 = ((task*)(slist_next(&lst, (tnode *)(it2))));
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
