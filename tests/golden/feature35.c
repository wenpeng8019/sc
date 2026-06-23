/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
    sc_fat child;
} node;

void node_drop(node *_this);
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

void node_drop(node *_this) {
    /* line 21 */
    if ((_this->child).p != NULL) {
        /* line 22 */
        printf("drop v=%d -> 释放子节点 v=%d\n", _this->v, ((node *)(_this->child).p)->v);
        /* line 23 */
        sc_fat_unbind_d(&(_this)->child, (void (*)(void *))node_drop);
    } else {
        /* line 25 */
        printf("drop v=%d（叶子）\n", _this->v);
    }
}

int32_t main(void) {
    /* line 29 */
    sc_fat root = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&root, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 30 */
    ((node *)(root).p)->v = 1;
    /* line 31 */
    sc_fat_unbind_d(&((node *)(root).p)->child, (void (*)(void *))node_drop);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 32 */
    ((node *)(((node *)(root).p)->child).p)->v = 2;
    /* line 33 */
    sc_fat_unbind_d(&((node *)(((node *)(root).p)->child).p)->child, (void (*)(void *))node_drop);
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&((node *)(((node *)(root).p)->child).p)->child, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), &((sc_ref *)(((node *)(root).p)->child).tar)->out);
    /* line 34 */
    ((node *)(((node *)(((node *)(root).p)->child).p)->child).p)->v = 3;
    /* line 35 */
    printf("构建链 1 -> 2 -> 3，即将退出作用域\n");
    /* line 36 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&root, (void (*)(void *))node_drop);
        return _ret;
    }
}
