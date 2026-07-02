/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_task sc_task;
typedef struct sc_node sc_node;
typedef struct sc_point sc_point;

typedef struct sc_task {
    void *_prev;
    void *_next;
    int32_t id;
} sc_task;

typedef struct sc_point {
    int32_t x;
    int32_t y;
} sc_point;

typedef struct sc_node {
    void *_prev;
    void *_next;
    int32_t id;
    char name[8];
    sc_point pos;
    double score;
} sc_node;

static void sc_dump(char *tag, sc_chain *l);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static inline sc_task *sc_task__new(void) {
    sc_task *_p = (sc_task *)sc_alloc(sizeof(sc_task));
    if (_p) {
        memset(_p, 0, sizeof(sc_task));
    }
    return _p;
}

static void sc_dump(char *tag, sc_chain *l) {
    /* line 30 */
    printf("%s:", tag);
    /* line 31 */
    sc_task *it = ((sc_task*)(sc_chain_first(l)));
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
    SC_CONSOLE_UTF8();
    /* line 41 */
    sc_chain l = {0};
    /* line 42 */
    sc_task t[6] = {0};
    /* line 43 */
    int32_t i;
    /* line 44 */
    for (i = 0; i < 6; i++) {
        /* line 45 */
        t[i].id = i;
    }
    /* line 47 */
    sc_chain_append(&l, &(t[2]));
    /* line 48 */
    sc_chain_append(&l, &(t[3]));
    /* line 49 */
    sc_chain_push(&l, &(t[1]));
    /* line 50 */
    sc_dump("append/push", &(l));
    /* line 52 */
    sc_chain_before(&l, &(t[1]), &(t[0]));
    /* line 53 */
    sc_chain_after(&l, &(t[3]), &(t[4]));
    /* line 54 */
    sc_dump("before/after", &(l));
    /* line 56 */
    sc_task *f = ((sc_task*)(sc_chain_first(&l)));
    /* line 57 */
    sc_task *b = ((sc_task*)(sc_chain_last(&l)));
    /* line 59 */
    printf("first=%d last=%d head_prev_nil=%d\n", f->id, b->id, ((void *)sc_chain_prev(f)) == NULL);
    /* line 61 */
    sc_chain_remove(&l, &(t[2]));
    /* line 62 */
    sc_task *p = ((sc_task*)(sc_chain_pop(&l)));
    /* line 63 */
    printf("pop=%d\n", p->id);
    /* line 64 */
    sc_dump("remove/pop", &(l));
    /* line 66 */
    sc_chain_revert(&l);
    /* line 67 */
    sc_dump("revert", &(l));
    /* line 69 */
    sc_chain seg = {0};
    /* line 70 */
    sc_chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 71 */
    sc_dump("cut-out", &(seg));
    /* line 72 */
    sc_dump("cut-rest", &(l));
    /* line 74 */
    sc_chain_append_to(&seg, &(l));
    /* line 75 */
    sc_dump("append_to", &(l));
    /* line 76 */
    printf("seg empty=%d\n", sc_chain_first(&seg) == NULL);
    /* line 77 */
    sc_chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 78 */
    sc_chain_push_to(&seg, &(l));
    /* line 79 */
    sc_dump("push_to", &(l));
    /* line 82 */
    sc_task *h = sc_task__new();
    /* line 83 */
    h->id = 9;
    /* line 84 */
    sc_chain_append(&l, h);
    /* line 85 */
    sc_dump("heap", &(l));
    /* line 86 */
    sc_chain_remove(&l, h);
    /* line 87 */
    free(((void*)(h)));
    /* line 91 */
    sc_chain l2 = {0};
    /* line 92 */
    sc_node n[3] = {0};
    /* line 93 */
    for (i = 0; i < 3; i++) {
        /* line 94 */
        n[i].id = i;
    }
    /* line 95 */
    sc_chain_append(&l2, &(n[0]));
    /* line 96 */
    sc_chain_append(&l2, &(n[1]));
    /* line 97 */
    sc_chain_append(&l2, &(n[2]));
    /* line 99 */
    sc_node *it = ((sc_node*)(sc_chain_first(&l2)));
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
    sc_node *rear = ((sc_node*)(sc_chain_last(&l2)));
    /* line 108 */
    printf("反向:");
    /* line 109 */
    while (rear != NULL) {
        /* line 110 */
        printf(" %d", rear->id);
        /* line 111 */
        rear = ((void *)sc_chain_prev(rear));
    }
    /* line 112 */
    printf("\n");
    /* line 114 */
    return 0;
}
