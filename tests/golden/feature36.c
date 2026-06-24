/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;
typedef struct tree tree;

typedef struct node {
    int32_t v;
    sc_fat left;
    sc_fat right;
} node;

void node_drop(node *_this);
typedef struct tree {
    sc_fat root;
} tree;

void tree_init(tree *_this, int32_t *_self_own);
void tree_drop(tree *_this);
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

static inline tree *tree__new(void) {
    tree *_p = (tree *)sc_alloc(sizeof(tree));
    if (_p) {
        memset(_p, 0, sizeof(tree));
        tree_init(_p, SC_OWN_RAW);
    }
    return _p;
}

static inline tree *tree__new_ref(int32_t _atom) {
    sc_ref *_h = (sc_ref *)sc_alloc(SC_REF_HDR + sizeof(tree));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _atom;
    tree *_p = (tree *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(tree));
    tree_init(_p, &_h->out);
    return _p;
}

void node_drop(node *_this) {
    /* line 24 */
    if ((_this->left).p != NULL) {
        /* line 25 */
        printf("drop v=%d -> 释放左子 v=%d\n", _this->v, ((node *)(_this->left).p)->v);
        /* line 26 */
        sc_fat_unbind_d(&(_this)->left, (void (*)(void *))node_drop);
    }
    /* line 27 */
    if ((_this->right).p != NULL) {
        /* line 28 */
        printf("drop v=%d -> 释放右子 v=%d\n", _this->v, ((node *)(_this->right).p)->v);
        /* line 29 */
        sc_fat_unbind_d(&(_this)->right, (void (*)(void *))node_drop);
    }
}

void tree_init(tree *_this, int32_t *_self_own) {
    /* line 37 */
    sc_fat_unbind_d(&(_this)->root, (void (*)(void *))node_drop);
    if (SC_OWN_REAL(_self_own)) {
        node *_fat0 = node__new_ref(0);
        sc_fat_bind(&(_this)->root, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), _self_own);
    } else {
        ((_this)->root).p = node__new();
        ((_this)->root).tar = (int32_t *)0;
        ((_this)->root).own = SC_OWN_RAW;
    }
    /* line 38 */
    ((node *)(_this->root).p)->v = 1;
    /* line 39 */
    sc_fat_unbind_d(&((node *)(_this->root).p)->left, (void (*)(void *))node_drop);
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&((node *)(_this->root).p)->left, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), &((sc_ref *)(_this->root).tar)->out);
    /* line 40 */
    ((node *)(((node *)(_this->root).p)->left).p)->v = 2;
    /* line 41 */
    sc_fat_unbind_d(&((node *)(_this->root).p)->right, (void (*)(void *))node_drop);
    node *_fat2 = node__new_ref(0);
    sc_fat_bind(&((node *)(_this->root).p)->right, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), &((sc_ref *)(_this->root).tar)->out);
    /* line 42 */
    ((node *)(((node *)(_this->root).p)->right).p)->v = 3;
}

void tree_drop(tree *_this) {
    /* line 45 */
    if ((_this->root).p != NULL) {
        /* line 46 */
        printf("drop tree -> 释放根 v=%d\n", ((node *)(_this->root).p)->v);
        /* line 47 */
        sc_fat_unbind_d(&(_this)->root, (void (*)(void *))node_drop);
    }
}

int32_t main(void) {
    /* line 51 */
    sc_fat t = {0};
    tree *_fat3 = tree__new_ref(0);
    sc_fat_bind(&t, _fat3, (sc_ref *)((char *)_fat3 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 52 */
    printf("树已由 init 建好：root=%d left=%d right=%d\n", ((node *)(((tree *)(t).p)->root).p)->v, ((node *)(((node *)(((tree *)(t).p)->root).p)->left).p)->v, ((node *)(((node *)(((tree *)(t).p)->root).p)->right).p)->v);
    /* line 54 */
    printf("即将退出作用域\n");
    /* line 55 */
    {
        int32_t _ret = 0;
        sc_fat_unbind_d(&t, (void (*)(void *))tree_drop);
        return _ret;
    }
}
