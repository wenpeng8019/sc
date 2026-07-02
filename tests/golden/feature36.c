/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;
typedef struct sc_tree sc_tree;

typedef struct sc_node {
    int32_t v;
    sc_fat left;
    sc_fat right;
} sc_node;

void sc_node_drop(sc_node *_this);
typedef struct sc_tree {
    sc_fat root;
} sc_tree;

void sc_tree_init(sc_tree *_this, int32_t *_self_own);
void sc_tree_drop(sc_tree *_this);
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

static inline sc_tree *sc_tree__new(void) {
    sc_tree *_p = (sc_tree *)sc_alloc(sizeof(sc_tree));
    if (_p) {
        memset(_p, 0, sizeof(sc_tree));
        sc_tree_init(_p, SC_OWN_RAW);
    }
    return _p;
}

static inline sc_tree *sc_tree__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(sc_tree));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    sc_tree *_p = (sc_tree *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_tree));
    sc_tree_init(_p, &_h->out);
    return _p;
}

void sc_node_drop(sc_node *_this) {
    /* line 24 */
    if ((_this->left).p != NULL) {
        /* line 25 */
        printf("drop v=%d -> 释放左子 v=%d\n", _this->v, ((sc_node *)(_this->left).p)->v);
        /* line 26 */
        sc_fat_unbind_d(&(_this)->left, (void (*)(void *))sc_node_drop);
    }
    /* line 27 */
    if ((_this->right).p != NULL) {
        /* line 28 */
        printf("drop v=%d -> 释放右子 v=%d\n", _this->v, ((sc_node *)(_this->right).p)->v);
        /* line 29 */
        sc_fat_unbind_d(&(_this)->right, (void (*)(void *))sc_node_drop);
    }
}

void sc_tree_init(sc_tree *_this, int32_t *_self_own) {
    /* line 37 */
    sc_fat_unbind_d(&(_this)->root, (void (*)(void *))sc_node_drop);
    if (SC_OWN_REAL(_self_own)) {
        sc_node *_fat0 = sc_node__new_ref(0);
        sc_fat_bind(&(_this)->root, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), _self_own);
    } else {
        ((_this)->root).p = sc_node__new();
        ((_this)->root).tar = (int32_t *)0;
        ((_this)->root).own = SC_OWN_RAW;
    }
    /* line 38 */
    ((sc_node *)(_this->root).p)->v = 1;
    /* line 39 */
    sc_fat_unbind_d(&((sc_node *)(_this->root).p)->left, (void (*)(void *))sc_node_drop);
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(_this->root).p)->left, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(_this->root).tar)->out);
    /* line 40 */
    ((sc_node *)(((sc_node *)(_this->root).p)->left).p)->v = 2;
    /* line 41 */
    sc_fat_unbind_d(&((sc_node *)(_this->root).p)->right, (void (*)(void *))sc_node_drop);
    sc_node *_fat2 = sc_node__new_ref(0);
    sc_fat_bind(&((sc_node *)(_this->root).p)->right, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), &((sc_ref *)(_this->root).tar)->out);
    /* line 42 */
    ((sc_node *)(((sc_node *)(_this->root).p)->right).p)->v = 3;
}

void sc_tree_drop(sc_tree *_this) {
    /* line 45 */
    if ((_this->root).p != NULL) {
        /* line 46 */
        printf("drop tree -> 释放根 v=%d\n", ((sc_node *)(_this->root).p)->v);
        /* line 47 */
        sc_fat_unbind_d(&(_this)->root, (void (*)(void *))sc_node_drop);
    }
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 51 */
    sc_fat t = {0};
    sc_tree *_fat3 = sc_tree__new_ref(0);
    sc_fat_bind(&t, _fat3, (sc_ref *)((char *)_fat3 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 52 */
    printf("树已由 init 建好：root=%d left=%d right=%d\n", ((sc_node *)(((sc_tree *)(t).p)->root).p)->v, ((sc_node *)(((sc_node *)(((sc_tree *)(t).p)->root).p)->left).p)->v, ((sc_node *)(((sc_node *)(((sc_tree *)(t).p)->root).p)->right).p)->v);
    /* line 54 */
    printf("即将退出作用域\n");
    /* line 55 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&t, (void (*)(void *))sc_tree_drop);
        return _ret;
    }
}
