/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct node node;

typedef struct node {
    int32_t v;
} node;

void node_bump(node *_this);
void node_drop(node *_this);
int32_t take(sc_afat p);
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
    /* line 12 */
    _this->v = (_this->v + 1);
}

void node_drop(node *_this) {
    /* line 14 */
    printf("drop %d\n", _this->v);
}

int32_t take(sc_afat p) {
    /* line 19 */
    node_bump(((node *)(sc_afat_as_fat(p)).p));
    /* line 20 */
    return ((node *)(sc_afat_as_fat(p)).p)->v;
}

int32_t main(void) {
    /* line 23 */
    sc_afat d = {0};
    node *_fat0 = node__new_ref(0);
    sc_afat_bind(&d, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT, (void (*)(void *))node_drop);
    /* line 24 */
    ((node *)(sc_afat_as_fat(d)).p)->v = 7;
    /* line 26 */
    sc_fat t = {0};
    node *_fat1 = node__new_ref(0);
    sc_fat_bind(&t, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((node *)(t).p)->v = 40;
    /* line 28 */
    sc_afat e = {0};
    sc_afat_bind(&e, (t).p, (sc_ref *)(t).tar, SC_OWN_ROOT, (void (*)(void *))node_drop);
    /* line 29 */
    int32_t r = take(sc_fat_as_afat(t, (void (*)(void *))node_drop));
    /* line 30 */
    printf("d=%d e=%d r=%d\n", ((node *)(sc_afat_as_fat(d)).p)->v, ((node *)(sc_afat_as_fat(e)).p)->v, r);
    /* line 32 */
    sc_afat f = {0};
    /* line 33 */
    sc_afat_unbind(&f);
    sc_afat_bind(&f, (d).p, (sc_ref *)(d).tar, SC_OWN_ROOT, (d).dtor);
    /* line 34 */
    printf("f=%d\n", ((node *)(sc_afat_as_fat(f)).p)->v);
    /* line 35 */
    {
        int32_t _ret = 0;
        sc_afat_unbind(&f);
        sc_afat_unbind(&e);
        sc_fat_unbind_d(&t, (void (*)(void *))node_drop);
        sc_afat_unbind(&d);
        return _ret;
    }
}
