/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct item item;

typedef struct item {
    void *_prev;
    void *_next;
    int32_t key;
    int32_t seq;
} item;

int32_t by_key_asc(void *a, void *b);
int32_t by_key_desc(void *a, void *b);
static void dump(char *tag, chain *l);
static void dump_rev(char *tag, chain *l);
static void dump_stable(char *tag, chain *l);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


int32_t by_key_asc(void *a, void *b) {
    /* line 16 */
    item *x = ((item*)(a));
    /* line 17 */
    item *y = ((item*)(b));
    /* line 18 */
    return x->key - y->key;
}

int32_t by_key_desc(void *a, void *b) {
    /* line 22 */
    item *x = ((item*)(a));
    /* line 23 */
    item *y = ((item*)(b));
    /* line 24 */
    return y->key - x->key;
}

static void dump(char *tag, chain *l) {
    /* line 27 */
    printf("%s:", tag);
    /* line 28 */
    item *it = ((item*)(chain_first(l)));
    /* line 29 */
    while (it != NULL) {
        /* line 30 */
        printf(" %d", it->key);
        /* line 31 */
        it = it->_next;
    }
    /* line 32 */
    printf("\n");
}

static void dump_rev(char *tag, chain *l) {
    /* line 36 */
    printf("%s:", tag);
    /* line 37 */
    item *it = ((item*)(chain_last(l)));
    /* line 38 */
    while (it != NULL) {
        /* line 39 */
        printf(" %d", it->key);
        /* line 40 */
        it = ((void *)chain_prev(it));
    }
    /* line 41 */
    printf("\n");
}

static void dump_stable(char *tag, chain *l) {
    /* line 45 */
    printf("%s:", tag);
    /* line 46 */
    item *it = ((item*)(chain_first(l)));
    /* line 47 */
    while (it != NULL) {
        /* line 48 */
        printf(" %d.%d", it->key, it->seq);
        /* line 49 */
        it = it->_next;
    }
    /* line 50 */
    printf("\n");
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 53 */
    item n[8] = {0};
    /* line 54 */
    int32_t i;
    /* line 55 */
    int32_t keys[8];
    /* line 56 */
    keys[0] = 5;
    /* line 57 */
    keys[1] = 2;
    /* line 58 */
    keys[2] = 8;
    /* line 59 */
    keys[3] = 2;
    /* line 60 */
    keys[4] = 9;
    /* line 61 */
    keys[5] = 1;
    /* line 62 */
    keys[6] = 5;
    /* line 63 */
    keys[7] = 2;
    /* line 64 */
    for (i = 0; i < 8; i++) {
        /* line 65 */
        n[i].key = keys[i];
        /* line 66 */
        n[i].seq = i;
    }
    /* line 69 */
    chain l = {0};
    /* line 70 */
    for (i = 0; i < 8; i++) {
        /* line 71 */
        chain_append(&l, &(n[i]));
    }
    /* line 72 */
    dump("before  ", &(l));
    /* line 73 */
    chain_sort(&l, by_key_asc);
    /* line 74 */
    dump("asc     ", &(l));
    /* line 75 */
    dump_rev("asc(rev)", &(l));
    /* line 76 */
    dump_stable("stable  ", &(l));
    /* line 79 */
    chain_sort(&l, by_key_desc);
    /* line 80 */
    dump("desc    ", &(l));
    /* line 81 */
    dump_rev("desc(rev", &(l));
    /* line 84 */
    chain e = {0};
    /* line 85 */
    chain_sort(&e, by_key_asc);
    /* line 86 */
    printf("empty head_nil=%d\n", chain_first(&e) == NULL);
    /* line 89 */
    chain s = {0};
    /* line 90 */
    item one = {0};
    /* line 91 */
    one.key = 42;
    /* line 92 */
    one.seq = 0;
    /* line 93 */
    chain_append(&s, &(one));
    /* line 94 */
    chain_sort(&s, by_key_asc);
    /* line 95 */
    item *sf = ((item*)(chain_first(&s)));
    /* line 96 */
    item *sl = ((item*)(chain_last(&s)));
    /* line 97 */
    printf("single key=%d first==last=%d rear_next_nil=%d\n", sf->key, sf == sl, sf->_next == NULL);
    /* line 100 */
    dump("sorted2 ", &(l));
    /* line 101 */
    chain_sort(&l, by_key_asc);
    /* line 102 */
    dump("re-asc  ", &(l));
    /* line 103 */
    return 0;
}
