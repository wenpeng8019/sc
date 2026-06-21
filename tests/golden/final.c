/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
    sc_fat child;
} node;

int32_t pick(int32_t n);
int32_t loopy(int32_t n);
int32_t withfat(void);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static inline node *node__new(void) {
    node *_p = (node *)malloc(sizeof(node));
    if (_p) {
        memset(_p, 0, sizeof(node));
    }
    return _p;
}

static inline node *node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)malloc(SC_REF_HDR + sizeof(node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    node *_p = (node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(node));
    return _p;
}

int32_t pick(int32_t n) {
    /* line 12 */
    /* line 14 */
    if (n > 0) {
        /* line 15 */
        {
            int32_t _ret = 1;
            {
                /* line 13 */
                printf("final A\n");
            }
            return _ret;
        }
    }
    /* line 16 */
    {
        int32_t _ret = 0;
        {
            /* line 13 */
            printf("final A\n");
        }
        return _ret;
    }
}

int32_t loopy(int32_t n) {
    /* line 20 */
    int32_t i = 0;
    /* line 21 */
    for (i = 0; i < n; i++) {
        /* line 22 */
        /* line 24 */
        if (i == 1) {
            /* line 25 */
            {
                /* line 23 */
                printf("iter %d\n", i);
            }
            continue;
        }
        /* line 26 */
        if (i == 2) {
            /* line 27 */
            {
                /* line 23 */
                printf("iter %d\n", i);
            }
            break;
        }
        {
            /* line 23 */
            printf("iter %d\n", i);
        }
    }
    /* line 28 */
    return 0;
}

int32_t withfat(void) {
    /* line 32 */
    sc_fat p = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 33 */
    /* line 35 */
    ((node *)(p).p)->v = 9;
    /* line 36 */
    {
        int32_t _ret = 0;
        {
            /* line 34 */
            printf("v=%d\n", ((node *)(p).p)->v);
        }
        sc_fat_unbind(&p);
        return _ret;
    }
}
