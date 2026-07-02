/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
    sc_fat child;
} sc_node;

void sc_node_drop(sc_node *_this);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static inline sc_node *sc_node__new(void) {
    sc_node *_p = (sc_node *)sc_alloc(sizeof(sc_node));
    if (_p) {
        memset(_p, 0, sizeof(sc_node));
    }
    return _p;
}

static inline sc_node *sc_node__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(sc_node));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    sc_node *_p = (sc_node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_node));
    return _p;
}

void sc_node_drop(sc_node *_this) {
    /* line 21 */
    if ((_this->child).p != NULL) {
        /* line 22 */
        printf("drop v=%d -> 释放子节点 v=%d\n", _this->v, ((sc_node *)(_this->child).p)->v);
        /* line 23 */
        sc_fat_unbind_d(&(_this)->child, (void (*)(void *))sc_node_drop);
    } else {
        /* line 25 */
        printf("drop v=%d（叶子）\n", _this->v);
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 29 */
    sc_fat root = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_fat_bind(&root, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 30 */
    ((sc_node *)(root).p)->v = 1;
    /* line 31 */
    sc_fat_unbind_d(&((sc_node *)(root).p)->child, (void (*)(void *))sc_node_drop);
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(root).p)->child, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(root).tar)->out);
    /* line 32 */
    ((sc_node *)(((sc_node *)(root).p)->child).p)->v = 2;
    /* line 33 */
    sc_fat_unbind_d(&((sc_node *)(((sc_node *)(root).p)->child).p)->child, (void (*)(void *))sc_node_drop);
    sc_node *_fat2 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(((sc_node *)(root).p)->child).p)->child, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), &((sc_ref *)(((sc_node *)(root).p)->child).tar)->out);
    /* line 34 */
    ((sc_node *)(((sc_node *)(((sc_node *)(root).p)->child).p)->child).p)->v = 3;
    /* line 35 */
    printf("构建链 1 -> 2 -> 3，即将退出作用域\n");
    /* line 36 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&root, (void (*)(void *))sc_node_drop);
        return _ret;
    }
}
