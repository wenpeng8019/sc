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
    node_bump(((node *)(({ sc_afat _rc0 = p; (sc_fat){_rc0.p, _rc0.tar, _rc0.own}; })).p));
    /* line 20 */
    return ((node *)(({ sc_afat _rc1 = p; (sc_fat){_rc1.p, _rc1.tar, _rc1.own}; })).p)->v;
}

int32_t main(void) {
    /* line 23 */
    sc_afat d = {0};
    node *_fat2 = node__new_ref(0);
    sc_afat_bind(&d, _fat2, (sc_ref *)((char *)_fat2 - SC_REF_HDR), SC_OWN_ROOT, (void (*)(void *))node_drop);
    /* line 24 */
    ((node *)(({ sc_afat _rc3 = d; (sc_fat){_rc3.p, _rc3.tar, _rc3.own}; })).p)->v = 7;
    /* line 26 */
    sc_fat t = {0};
    node *_fat4 = node__new_ref(0);
    sc_fat_bind(&t, _fat4, (sc_ref *)((char *)_fat4 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((node *)(t).p)->v = 40;
    /* line 28 */
    sc_afat e = {0};
    sc_afat_bind(&e, (t).p, (sc_ref *)(t).tar, SC_OWN_ROOT, (void (*)(void *))node_drop);
    /* line 29 */
    int32_t r = take(({ sc_fat _ec5 = t; (sc_afat){_ec5.p, _ec5.tar, _ec5.own, (void (*)(void *))node_drop}; }));
    /* line 30 */
    printf("d=%d e=%d r=%d\n", ((node *)(({ sc_afat _rc6 = d; (sc_fat){_rc6.p, _rc6.tar, _rc6.own}; })).p)->v, ((node *)(({ sc_afat _rc7 = e; (sc_fat){_rc7.p, _rc7.tar, _rc7.own}; })).p)->v, r);
    /* line 32 */
    sc_afat f = {0};
    /* line 33 */
    sc_afat_unbind(&f);
    sc_afat_bind(&f, (d).p, (sc_ref *)(d).tar, SC_OWN_ROOT, (d).dtor);
    /* line 34 */
    printf("f=%d\n", ((node *)(({ sc_afat _rc8 = f; (sc_fat){_rc8.p, _rc8.tar, _rc8.own}; })).p)->v);
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
