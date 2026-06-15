/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"

typedef struct task task;

typedef struct task {
    void *_prev;
    void *_next;
    int32_t id;
} task;

static void dump(char *tag, chain *l);

static inline task *task__new(void) {
    task *_p = (task *)malloc(sizeof(task));
    if (_p) {
        memset(_p, 0, sizeof(task));
    }
    return _p;
}

static void dump(char *tag, chain *l) {
    /* line 16 */
    printf("%s:", tag);
    /* line 17 */
    task *it = ((task*)(chain_first(l)));
    /* line 18 */
    while (it != NULL) {
        /* line 19 */
        printf(" %d", it->id);
        /* line 20 */
        it = ((task*)(((void *)*(void **)((char *)(it) + sizeof(void *)))));
    }
    /* line 21 */
    printf("\n");
}

int32_t main(void) {
    /* line 24 */
    chain l = {0};
    /* line 25 */
    task t[6] = {0};
    /* line 26 */
    int32_t i;
    /* line 27 */
    for (i = 0; i < 6; i++) {
        /* line 28 */
        t[i].id = i;
    }
    /* line 31 */
    chain_append(&l, &(t[2]));
    /* line 32 */
    chain_append(&l, &(t[3]));
    /* line 33 */
    chain_push(&l, &(t[1]));
    /* line 34 */
    dump("append/push", &(l));
    /* line 37 */
    chain_before(&l, &(t[1]), &(t[0]));
    /* line 38 */
    chain_after(&l, &(t[3]), &(t[4]));
    /* line 39 */
    dump("before/after", &(l));
    /* line 42 */
    task *f = ((task*)(chain_first(&l)));
    /* line 43 */
    task *b = ((task*)(chain_last(&l)));
    /* line 44 */
    task *r = ((task*)(((void *)*(void **)((char *)(f) + 0))));
    /* line 45 */
    printf("first=%d last=%d rear=%d\n", f->id, b->id, r->id);
    /* line 48 */
    chain_remove(&l, &(t[2]));
    /* line 49 */
    task *p = ((task*)(chain_pop(&l)));
    /* line 50 */
    printf("pop=%d\n", p->id);
    /* line 51 */
    dump("remove/pop", &(l));
    /* line 54 */
    chain_revert(&l);
    /* line 55 */
    dump("revert", &(l));
    /* line 58 */
    chain seg = {0};
    /* line 59 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 60 */
    dump("cut-out", &(seg));
    /* line 61 */
    dump("cut-rest", &(l));
    /* line 64 */
    chain_append_to(&seg, &(l));
    /* line 65 */
    dump("append_to", &(l));
    /* line 66 */
    printf("seg empty=%d\n", chain_first(&seg) == NULL);
    /* line 67 */
    chain_cut(&l, &(t[3]), &(t[1]), &(seg));
    /* line 68 */
    chain_push_to(&seg, &(l));
    /* line 69 */
    dump("push_to", &(l));
    /* line 72 */
    task *h = task__new();
    /* line 73 */
    h->id = 9;
    /* line 74 */
    chain_append(&l, h);
    /* line 75 */
    dump("heap", &(l));
    /* line 76 */
    chain_remove(&l, h);
    /* line 77 */
    free(((void*)(h)));
    /* line 78 */
    return 0;
}
