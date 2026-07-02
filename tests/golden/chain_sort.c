/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_item sc_item;

typedef struct sc_item {
    void *_prev;
    void *_next;
    int32_t key;
    int32_t seq;
} sc_item;

int32_t sc_by_key_asc(void *a, void *b);
int32_t sc_by_key_desc(void *a, void *b);
static void sc_dump(char *tag, sc_chain *l);
static void sc_dump_rev(char *tag, sc_chain *l);
static void sc_dump_stable(char *tag, sc_chain *l);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


int32_t sc_by_key_asc(void *a, void *b) {
    /* line 16 */
    sc_item *x = ((sc_item*)(a));
    /* line 17 */
    sc_item *y = ((sc_item*)(b));
    /* line 18 */
    return x->key - y->key;
}

int32_t sc_by_key_desc(void *a, void *b) {
    /* line 22 */
    sc_item *x = ((sc_item*)(a));
    /* line 23 */
    sc_item *y = ((sc_item*)(b));
    /* line 24 */
    return y->key - x->key;
}

static void sc_dump(char *tag, sc_chain *l) {
    /* line 27 */
    printf("%s:", tag);
    /* line 28 */
    sc_item *it = ((sc_item*)(sc_chain_first(l)));
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

static void sc_dump_rev(char *tag, sc_chain *l) {
    /* line 36 */
    printf("%s:", tag);
    /* line 37 */
    sc_item *it = ((sc_item*)(sc_chain_last(l)));
    /* line 38 */
    while (it != NULL) {
        /* line 39 */
        printf(" %d", it->key);
        /* line 40 */
        it = ((void *)sc_chain_prev(it));
    }
    /* line 41 */
    printf("\n");
}

static void sc_dump_stable(char *tag, sc_chain *l) {
    /* line 45 */
    printf("%s:", tag);
    /* line 46 */
    sc_item *it = ((sc_item*)(sc_chain_first(l)));
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
    sc_item n[8] = {0};
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
    sc_chain l = {0};
    /* line 70 */
    for (i = 0; i < 8; i++) {
        /* line 71 */
        sc_chain_append(&l, &(n[i]));
    }
    /* line 72 */
    sc_dump("before  ", &(l));
    /* line 73 */
    sc_chain_sort(&l, sc_by_key_asc);
    /* line 74 */
    sc_dump("asc     ", &(l));
    /* line 75 */
    sc_dump_rev("asc(rev)", &(l));
    /* line 76 */
    sc_dump_stable("stable  ", &(l));
    /* line 79 */
    sc_chain_sort(&l, sc_by_key_desc);
    /* line 80 */
    sc_dump("desc    ", &(l));
    /* line 81 */
    sc_dump_rev("desc(rev", &(l));
    /* line 84 */
    sc_chain e = {0};
    /* line 85 */
    sc_chain_sort(&e, sc_by_key_asc);
    /* line 86 */
    printf("empty head_nil=%d\n", sc_chain_first(&e) == NULL);
    /* line 89 */
    sc_chain s = {0};
    /* line 90 */
    sc_item one = {0};
    /* line 91 */
    one.key = 42;
    /* line 92 */
    one.seq = 0;
    /* line 93 */
    sc_chain_append(&s, &(one));
    /* line 94 */
    sc_chain_sort(&s, sc_by_key_asc);
    /* line 95 */
    sc_item *sf = ((sc_item*)(sc_chain_first(&s)));
    /* line 96 */
    sc_item *sl = ((sc_item*)(sc_chain_last(&s)));
    /* line 97 */
    printf("single key=%d first==last=%d rear_next_nil=%d\n", sf->key, sf == sl, sf->_next == NULL);
    /* line 100 */
    sc_dump("sorted2 ", &(l));
    /* line 101 */
    sc_chain_sort(&l, sc_by_key_asc);
    /* line 102 */
    sc_dump("re-asc  ", &(l));
    /* line 103 */
    return 0;
}
