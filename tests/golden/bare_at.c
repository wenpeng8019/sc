/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_node sc_node;

typedef struct sc_node {
    int32_t v;
} sc_node;

void sc_node_bump(sc_node *_this);
void sc_node_drop(sc_node *_this);
int32_t sc_take(sc_afat p);
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

static inline sc_node *sc_node__new_ref(int32_t _flags) {
    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)
        ? sc_alloc(SC_REF_HDR + sizeof(sc_node))
        : sc_chunk(SC_REF_HDR + sizeof(sc_node)));
    if (!_h) return 0;
    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;
    sc_node *_p = (sc_node *)((char *)_h + SC_REF_HDR);
    memset(_p, 0, sizeof(sc_node));
    return _p;
}

void sc_node_bump(sc_node *_this) {
    /* line 12 */
    _this->v = (_this->v + 1);
}

void sc_node_drop(sc_node *_this) {
    /* line 14 */
    printf("drop %d\n", _this->v);
}

int32_t sc_take(sc_afat p) {
    /* line 19 */
    sc_node_bump(((sc_node *)(sc_afat_as_fat(p)).p));
    /* line 20 */
    return ((sc_node *)(sc_afat_as_fat(p)).p)->v;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 23 */
    sc_afat d = {0};
    sc_node *_fat0 = sc_node__new_ref(0);
    sc_afat_bind(&d, _fat0, (sc_ref *)((char *)_fat0 - SC_REF_HDR), SC_OWN_ROOT, (void (*)(void *))sc_node_drop);
    /* line 24 */
    ((sc_node *)(sc_afat_as_fat(d)).p)->v = 7;
    /* line 26 */
    sc_fat t = {0};
    sc_node *_fat1 = sc_node__new_ref(0);
    sc_fat_bind(&t, _fat1, (sc_ref *)((char *)_fat1 - SC_REF_HDR), SC_OWN_ROOT);
    /* line 27 */
    ((sc_node *)(t).p)->v = 40;
    /* line 28 */
    sc_afat e = {0};
    sc_afat_bind(&e, (t).p, (sc_ref *)(t).tar, SC_OWN_ROOT, (void (*)(void *))sc_node_drop);
    /* line 29 */
    int32_t r = sc_take(sc_fat_as_afat(t, (void (*)(void *))sc_node_drop));
    /* line 30 */
    printf("d=%d e=%d r=%d\n", ((sc_node *)(sc_afat_as_fat(d)).p)->v, ((sc_node *)(sc_afat_as_fat(e)).p)->v, r);
    /* line 32 */
    sc_afat f = {0};
    /* line 33 */
    sc_afat_unbind(&f);
    sc_afat_bind(&f, (d).p, (sc_ref *)(d).tar, SC_OWN_ROOT, (d).dtor);
    /* line 34 */
    printf("f=%d\n", ((sc_node *)(sc_afat_as_fat(f)).p)->v);
    /* line 35 */
    {
        int32_t _ret = 0;
        sc_afat_unbind(&f);
        sc_afat_unbind(&e);
        sc_fat_unbind_d(&t, (void (*)(void *))sc_node_drop);
        sc_afat_unbind(&d);
        return _ret;
    }
}
