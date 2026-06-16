/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct task task;
typedef struct node node;
typedef struct point point;

typedef struct task {
    void *_prev;
    void *_next;
    int32_t id;
} task;

typedef struct node {
    void *_prev;
    void *_next;
    int32_t id;
    char name[8];
    point pos;
    double score;
} node;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

static void dump(char *tag, chain *l);

static inline task *task__new(void) {
    task *_p = (task *)malloc(sizeof(task));
    if (_p) {
        memset(_p, 0, sizeof(task));
    }
    return _p;
}

static void dump(char *tag, chain *l) {
    /* line 32 */
    printf("%s:", tag);
    /* line 33 */
    task *it = ((task*)(l->first()));
    /* line 34 */
    while (it != NULL) {
        /* line 35 */
        printf(" %d", it->id);
        /* line 36 */
        it = it->_next;
    }
    /* line 37 */
    printf("\n");
}

int32_t main(void) {
    /* line 43 */
    chain l = {0};
    /* line 44 */
    task t[6] = {0};
    /* line 45 */
    int32_t i;
    /* line 46 */
    for (i = 0; i < 6; i++) {
        /* line 47 */
        t[i].id = i;
    }
    /* line 49 */
    l.append(&(t[2]));
    /* line 50 */
    l.append(&(t[3]));
    /* line 51 */
    l.push(&(t[1]));
    /* line 52 */
    dump("append/push", &(l));
    /* line 54 */
    l.before(&(t[1]), &(t[0]));
    /* line 55 */
    l.after(&(t[3]), &(t[4]));
    /* line 56 */
    dump("before/after", &(l));
    /* line 58 */
    task *f = ((task*)(l.first()));
    /* line 59 */
    task *b = ((task*)(l.last()));
    /* line 61 */
    task *r = ((task*)(f->_prev));
    /* line 62 */
    printf("first=%d last=%d rear=%d\n", f->id, b->id, r->id);
    /* line 64 */
    l.remove(&(t[2]));
    /* line 65 */
    task *p = ((task*)(l.pop()));
    /* line 66 */
    printf("pop=%d\n", p->id);
    /* line 67 */
    dump("remove/pop", &(l));
    /* line 69 */
    l.revert();
    /* line 70 */
    dump("revert", &(l));
    /* line 72 */
    chain seg = {0};
    /* line 73 */
    l.cut(&(t[3]), &(t[1]), &(seg));
    /* line 74 */
    dump("cut-out", &(seg));
    /* line 75 */
    dump("cut-rest", &(l));
    /* line 77 */
    seg.append_to(&(l));
    /* line 78 */
    dump("append_to", &(l));
    /* line 79 */
    printf("seg empty=%d\n", seg.first() == NULL);
    /* line 80 */
    l.cut(&(t[3]), &(t[1]), &(seg));
    /* line 81 */
    seg.push_to(&(l));
    /* line 82 */
    dump("push_to", &(l));
    /* line 85 */
    task *h = task__new();
    /* line 86 */
    h->id = 9;
    /* line 87 */
    l.append(h);
    /* line 88 */
    dump("heap", &(l));
    /* line 89 */
    l.remove(h);
    /* line 90 */
    free(((void*)(h)));
    /* line 94 */
    chain l2 = {0};
    /* line 95 */
    node n[3] = {0};
    /* line 96 */
    for (i = 0; i < 3; i++) {
        /* line 97 */
        n[i].id = i;
    }
    /* line 98 */
    l2.append(&(n[0]));
    /* line 99 */
    l2.append(&(n[1]));
    /* line 100 */
    l2.append(&(n[2]));
    /* line 102 */
    node *it = ((node*)(l2.first()));
    /* line 103 */
    printf("正向:");
    /* line 104 */
    while (it != NULL) {
        /* line 105 */
        printf(" %d", it->id);
        /* line 106 */
        it = it->_next;
    }
    /* line 107 */
    printf("\n");
    /* line 110 */
    node *rear = ((node*)(l2.last()));
    /* line 111 */
    printf("反向:");
    /* line 112 */
    while (rear != NULL) {
        /* line 113 */
        printf(" %d", rear->id);
        /* line 114 */
        rear = rear->_prev;
    }
    /* line 115 */
    printf("\n");
    /* line 117 */
    return 0;
}
