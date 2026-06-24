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
    node *_p = (node *)sc_alloc(sizeof(node));
    if (_p) {
        memset(_p, 0, sizeof(node));
    }
    return _p;
}

static inline node *node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    node *_p = (node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(node));
    return _p;
}

int32_t pick(int32_t n) {
    /* line 11 */
    /* line 13 */
    if (n > 0) {
        /* line 14 */
        {
            int32_t _ret = 1;
            {
                /* line 12 */
                printf("final A\n");
            }
            return _ret;
        }
    }
    /* line 15 */
    {
        int32_t _ret = 0;
        {
            /* line 12 */
            printf("final A\n");
        }
        return _ret;
    }
}

int32_t loopy(int32_t n) {
    /* line 19 */
    int32_t i = 0;
    /* line 20 */
    for (i = 0; i < n; i++) {
        /* line 21 */
        /* line 23 */
        if (i == 1) {
            /* line 24 */
            {
                /* line 22 */
                printf("iter %d\n", i);
            }
            continue;
        }
        /* line 25 */
        if (i == 2) {
            /* line 26 */
            {
                /* line 22 */
                printf("iter %d\n", i);
            }
            break;
        }
        {
            /* line 22 */
            printf("iter %d\n", i);
        }
    }
    /* line 27 */
    return 0;
}

int32_t withfat(void) {
    /* line 31 */
    sc_fat p = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&p, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 32 */
    /* line 34 */
    ((node *)(p).p)->v = 9;
    /* line 35 */
    {
        int32_t _ret = 0;
        {
            /* line 33 */
            printf("v=%d\n", ((node *)(p).p)->v);
        }
        sc_fat_unbind(&p);
        return _ret;
    }
}
