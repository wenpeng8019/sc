/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct task task;
typedef struct node node;
typedef struct point point;

typedef struct task {
    void *_prev;
    void *_next;
    int32_t id;
} task;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct node {
    void *_prev;
    void *_next;
    int32_t id;
    char name[8];
    point pos;
    double score;
} node;

static void dump(char *tag, chain *l);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static inline task *task__new(void) {
    task *_p = (task *)sc_alloc(sizeof(task));
    if (_p) {
        memset(_p, 0, sizeof(task));
    }
    return _p;
}

static void dump(char *tag, chain *l) {
    /* line 30 */
    printf("%s:", tag);
    /* line 31 */
    task *it = ((task*)(chain_first(l)));
    /* line 32 */
    while (it != NULL) {
        /* line 33 */
        printf(" %d", it->id);
        /* line 34 */
        it = it->_next;
    }
    /* line 35 */
    printf("\n");
}

int32_t main(void) {
    /* line 41 */
    chain l = {0};
    /* line 42 */
    task t[6] = {0};
    /* line 43 */
    int32_t i;
    /* line 44 */
    for (i = 0; i < 6; i++) {
        /* line 45 */
        t[i].id = i;
    }
    /* line 47 */
    chain_append(&l, &(t[2]));
    /* line 48 */
    chain_append(&l, &(t[3]));
    /* line 49 */
    chain_push(&l, &(t[1]));
    /* line 50 */
    dump("append/push", &(l));
    /* line 52 */
    chain_before(&l, &(t[1]), &(t[0]));
    /* line 53 */
    chain_after(&l, &(t[3]), &(t[4]));
    /* line 54 */
    dump("before/after", &(l));
    /* line 56 */
    task *f = ((task*)(chain_first(&l)));
    /* line 57 */
    task *b = ((task*)(chain_last(&l)));
    /* line 59 */
    printf("first=%d last=%d head_prev_nil=%d\n", f->id, b->id, ((void *)chain_prev(f)) == NULL);
    /* line 61 */
    chain_remove(&l, &(t[2]));
    /* line 62 */
    task *p = ((task*)(chain_pop(&l)));
    /* line 63 */
    printf("pop=%d\n", p->id);
    /* line 64 */
    dump("remove/pop", &(l));
    /* line 66 */
    chain_revert(&l);
    /* line 67 */
    dump("revert", &(l));
    /* line 69 */
    chain seg = {0};
    /* line 70 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 71 */
    dump("cut-out", &(seg));
    /* line 72 */
    dump("cut-rest", &(l));
    /* line 74 */
    chain_append_to(&seg, &(l));
    /* line 75 */
    dump("append_to", &(l));
    /* line 76 */
    printf("seg empty=%d\n", chain_first(&seg) == NULL);
    /* line 77 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 78 */
    chain_push_to(&seg, &(l));
    /* line 79 */
    dump("push_to", &(l));
    /* line 82 */
    task *h = task__new();
    /* line 83 */
    h->id = 9;
    /* line 84 */
    chain_append(&l, h);
    /* line 85 */
    dump("heap", &(l));
    /* line 86 */
    chain_remove(&l, h);
    /* line 87 */
    free(((void*)(h)));
    /* line 91 */
    chain l2 = {0};
    /* line 92 */
    node n[3] = {0};
    /* line 93 */
    for (i = 0; i < 3; i++) {
        /* line 94 */
        n[i].id = i;
    }
    /* line 95 */
    chain_append(&l2, &(n[0]));
    /* line 96 */
    chain_append(&l2, &(n[1]));
    /* line 97 */
    chain_append(&l2, &(n[2]));
    /* line 99 */
    node *it = ((node*)(chain_first(&l2)));
    /* line 100 */
    printf("正向:");
    /* line 101 */
    while (it != NULL) {
        /* line 102 */
        printf(" %d", it->id);
        /* line 103 */
        it = it->_next;
    }
    /* line 104 */
    printf("\n");
    /* line 107 */
    node *rear = ((node*)(chain_last(&l2)));
    /* line 108 */
    printf("反向:");
    /* line 109 */
    while (rear != NULL) {
        /* line 110 */
        printf(" %d", rear->id);
        /* line 111 */
        rear = ((void *)chain_prev(rear));
    }
    /* line 112 */
    printf("\n");
    /* line 114 */
    return 0;
}
