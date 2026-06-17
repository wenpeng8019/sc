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
    task *it = ((task*)(chain_first(l)));
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
    chain_append(&l, &(t[2]));
    /* line 50 */
    chain_append(&l, &(t[3]));
    /* line 51 */
    chain_push(&l, &(t[1]));
    /* line 52 */
    dump("append/push", &(l));
    /* line 54 */
    chain_before(&l, &(t[1]), &(t[0]));
    /* line 55 */
    chain_after(&l, &(t[3]), &(t[4]));
    /* line 56 */
    dump("before/after", &(l));
    /* line 58 */
    task *f = ((task*)(chain_first(&l)));
    /* line 59 */
    task *b = ((task*)(chain_last(&l)));
    /* line 61 */
    printf("first=%d last=%d head_prev_nil=%d\n", f->id, b->id, ((void *)chain_prev(f)) == NULL);
    /* line 63 */
    chain_remove(&l, &(t[2]));
    /* line 64 */
    task *p = ((task*)(chain_pop(&l)));
    /* line 65 */
    printf("pop=%d\n", p->id);
    /* line 66 */
    dump("remove/pop", &(l));
    /* line 68 */
    chain_revert(&l);
    /* line 69 */
    dump("revert", &(l));
    /* line 71 */
    chain seg = {0};
    /* line 72 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 73 */
    dump("cut-out", &(seg));
    /* line 74 */
    dump("cut-rest", &(l));
    /* line 76 */
    chain_append_to(&seg, &(l));
    /* line 77 */
    dump("append_to", &(l));
    /* line 78 */
    printf("seg empty=%d\n", chain_first(&seg) == NULL);
    /* line 79 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 80 */
    chain_push_to(&seg, &(l));
    /* line 81 */
    dump("push_to", &(l));
    /* line 84 */
    task *h = task__new();
    /* line 85 */
    h->id = 9;
    /* line 86 */
    chain_append(&l, h);
    /* line 87 */
    dump("heap", &(l));
    /* line 88 */
    chain_remove(&l, h);
    /* line 89 */
    free(((void*)(h)));
    /* line 93 */
    chain l2 = {0};
    /* line 94 */
    node n[3] = {0};
    /* line 95 */
    for (i = 0; i < 3; i++) {
        /* line 96 */
        n[i].id = i;
    }
    /* line 97 */
    chain_append(&l2, &(n[0]));
    /* line 98 */
    chain_append(&l2, &(n[1]));
    /* line 99 */
    chain_append(&l2, &(n[2]));
    /* line 101 */
    node *it = ((node*)(chain_first(&l2)));
    /* line 102 */
    printf("正向:");
    /* line 103 */
    while (it != NULL) {
        /* line 104 */
        printf(" %d", it->id);
        /* line 105 */
        it = it->_next;
    }
    /* line 106 */
    printf("\n");
    /* line 109 */
    node *rear = ((node*)(chain_last(&l2)));
    /* line 110 */
    printf("反向:");
    /* line 111 */
    while (rear != NULL) {
        /* line 112 */
        printf(" %d", rear->id);
        /* line 113 */
        rear = ((void *)chain_prev(rear));
    }
    /* line 114 */
    printf("\n");
    /* line 116 */
    return 0;
}
