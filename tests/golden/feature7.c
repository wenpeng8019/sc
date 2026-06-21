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
    /* line 35 */
    _this->head = NULL;
    /* line 36 */
    _this->tail = NULL;
}

static int32_t slist_insert(slist *_this, tnode *item, int32_t tag) {
    /* line 39 */
    if (tag != 0) {
        /* line 40 */
        item->prev = NULL;
        /* line 41 */
        item->next = _this->head;
        /* line 42 */
        if (_this->head != NULL) {
            /* line 43 */
            _this->head->prev = item;
        } else {
            /* line 45 */
            _this->tail = item;
        }
        /* line 46 */
        _this->head = item;
    } else {
        /* line 48 */
        item->prev = _this->tail;
        /* line 49 */
        item->next = NULL;
        /* line 50 */
        if (_this->tail != NULL) {
            /* line 51 */
            _this->tail->next = item;
        } else {
            /* line 53 */
            _this->head = item;
        }
        /* line 54 */
        _this->tail = item;
    }
    /* line 55 */
    return 0;
}

static int32_t slist_remove(slist *_this, tnode *item) {
    /* line 58 */
    if (item->prev != NULL) {
        /* line 59 */
        item->prev->next = item->next;
    } else {
        /* line 61 */
        _this->head = item->next;
    }
    /* line 62 */
    if (item->next != NULL) {
        /* line 63 */
        item->next->prev = item->prev;
    } else {
        /* line 65 */
        _this->tail = item->prev;
    }
    /* line 66 */
    return 0;
}

static int32_t slist_find(slist *_this, tnode **out, int32_t key) {
    /* line 69 */
    tnode *p = _this->head;
    /* line 70 */
    while (p != NULL) {
        /* line 71 */
        task *t = ((task*)(p));
        /* line 72 */
        if (t->id != key) {
            /* line 73 */
            p = p->next;
        } else {
            /* line 75 */
            *(out) = p;
            /* line 76 */
            return 0;
        }
    }
    /* line 77 */
    return -(1);
}

static tnode * slist_first(slist *_this) {
    /* line 80 */
    return _this->head;
}

static tnode * slist_last(slist *_this) {
    /* line 83 */
    return _this->tail;
}

static tnode * slist_next(slist *_this, tnode *item) {
    /* line 86 */
    return item->next;
}

static tnode * slist_prev(slist *_this, tnode *item) {
    /* line 89 */
    return item->prev;
}

int32_t main(void) {
    /* line 99 */
    slist lst = {0};
    slist_init(&lst);
    /* line 100 */
    task a[4] = {0};
    /* line 101 */
    int32_t i;
    /* line 102 */
    for (i = 0; i < 4; i++) {
        /* line 103 */
        a[i].id = ((i + 1) * 10);
    }
    /* line 107 */
    slist_insert(&lst, (tnode *)(&(a[0])), 0);
    /* line 108 */
    slist_insert(&lst, (tnode *)(&(a[1])), 0);
    /* line 109 */
    slist_insert(&lst, (tnode *)(&(a[2])), 0);
    /* line 110 */
    slist_insert(&lst, (tnode *)(&(a[3])), 1);
    /* line 113 */
    task *it = ((task*)(slist_first(&lst)));
    /* line 114 */
    printf("正向:");
    /* line 115 */
    while (it != NULL) {
        /* line 116 */
        printf(" %d", it->id);
        /* line 117 */
        it = ((task*)(slist_next(&lst, (tnode *)(it))));
    }
    /* line 118 */
    printf("\n");
    /* line 121 */
    task *rit = ((task*)(slist_last(&lst)));
    /* line 122 */
    printf("反向:");
    /* line 123 */
    while (rit != NULL) {
        /* line 124 */
        printf(" %d", rit->id);
        /* line 125 */
        rit = ((task*)(slist_prev(&lst, (tnode *)(rit))));
    }
    /* line 126 */
    printf("\n");
    /* line 129 */
    task *found = {0};
    /* line 130 */
    if (slist_find(&lst, (tnode * *)(&(found)), 20) == 0) {
        /* line 131 */
        printf("find 20 -> id=%d\n", found->id);
    }
    /* line 134 */
    task *hit = ((task*)((__extension__ ({ tnode *_scfo = (void *)0; (slist_find(&lst, &_scfo, 30) == 0) ? _scfo : (void *)0; }))));
    /* line 135 */
    if (hit != NULL) {
        /* line 136 */
        printf("lst[30] -> id=%d\n", hit->id);
    }
    /* line 137 */
    task *miss = ((task*)((__extension__ ({ tnode *_scfo = (void *)0; (slist_find(&lst, &_scfo, 99) == 0) ? _scfo : (void *)0; }))));
    /* line 138 */
    if (miss == NULL) {
        /* line 139 */
        printf("lst[99] -> nil\n");
    }
    /* line 142 */
    slist_remove(&lst, (tnode *)(&(a[1])));
    /* line 143 */
    task *it2 = ((task*)(slist_first(&lst)));
    /* line 144 */
    printf("remove 20 后:");
    /* line 145 */
    while (it2 != NULL) {
        /* line 146 */
        printf(" %d", it2->id);
        /* line 147 */
        it2 = ((task*)(slist_next(&lst, (tnode *)(it2))));
    }
    /* line 148 */
    printf("\n");
    /* line 151 */
    int32_t *pid = ((int32_t*)(((void *)&((&(a[0]))->id))));
    /* line 152 */
    printf("base(&a[0]) -> id=%d\n", *(pid));
    /* line 154 */
    return 0;
}
