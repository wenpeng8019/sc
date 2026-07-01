/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_bump(node *_this);
void node_drop(node *_this);
int32_t take(sc_thin p);
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

void node_bump(node *_this) {
    /* line 11 */
    _this->v = (_this->v + 1);
}

void node_drop(node *_this) {
    /* line 13 */
    printf("drop %d\n", _this->v);
}

int32_t take(sc_thin p) {
    /* line 17 */
    node_bump(((node *)(p).p));
    /* line 18 */
    return ((node *)(p).p)->v;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 21 */
    sc_fat a = {0};
    node *_fat0 = node__new_ref(0);
    sc_fat_bind(&a, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 22 */
    ((node *)(a).p)->v = 7;
    /* line 23 */
    sc_thin t = {0};
    sc_thin_bind(&t, (a).p, (sc_ref *)(a).tar, (void (*)(void *))node_drop);
    /* line 24 */
    sc_fat c = {0};
    sc_fat_bind(&c, (t).p, (sc_ref *)(t).tar, SC_OWN_ROOT);
    /* line 25 */
    sc_thin b = {0};
    node *_fat1 = node__new_ref(0);
    sc_thin_bind(&b, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), (void (*)(void *))node_drop);
    /* line 26 */
    ((node *)(b).p)->v = 40;
    /* line 27 */
    sc_afat e = {0};
    sc_afat_bind(&e, (b).p, (sc_ref *)(b).tar, SC_OWN_ROOT, (void (*)(void *))node_drop);
    /* line 28 */
    int32_t r = take(b);
    /* line 29 */
    printf("a=%d c=%d r=%d\n", ((node *)(a).p)->v, ((node *)(c).p)->v, r);
    /* line 30 */
    sc_thin_unbind(&t);
    /* line 31 */
    {
        int32_t _ret = 0;
        sc_afat_unbind(&e);
        sc_thin_unbind(&b);
        sc_fat_unbind_d(&c, (void (*)(void *))node_drop);
        sc_thin_unbind(&t);
        sc_fat_unbind_d(&a, (void (*)(void *))node_drop);
        return _ret;
    }
}
