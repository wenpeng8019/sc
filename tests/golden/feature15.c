/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_view sc_view;
typedef struct sc_dev sc_dev;

typedef struct sc_view {
    sc_dev *_self;
    char *p;
    int32_t n;
} sc_view;

typedef struct sc_dev {
    char data[64];
    sc_view *(*alloc)(struct sc_dev *, int32_t off, int32_t len);
    void (*free)(struct sc_dev *, sc_view *v);
} sc_dev;

typedef struct sc_dev__project {
    int32_t off;
    int32_t len;
    sc_view *_;
} sc_dev__project;

static sc_view * sc_dev_alloc(sc_dev *_this, int32_t off, int32_t len);
static void sc_dev_free(sc_dev *_this, sc_view *v);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static sc_view * sc_dev_alloc(sc_dev *_this, int32_t off, int32_t len) {
    /* line 23 */
    sc_view *v = ((sc_view*)(malloc(sizeof(sc_view))));
    /* line 24 */
    v->p = &(_this->data[off]);
    /* line 25 */
    v->n = len;
    /* line 26 */
    return v;
}

static void sc_dev_free(sc_dev *_this, sc_view *v) {
    /* line 29 */
    free(v);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 32 */
    sc_dev d = {0};
    /* line 33 */
    int32_t i;
    /* line 34 */
    for (i = 0; i < 26; i++) {
        /* line 35 */
        d.data[i] = ('a' + i);
    }
    /* line 38 */
    d.alloc = sc_dev_alloc;
    /* line 39 */
    d.free = sc_dev_free;
    /* line 42 */
    struct sc_dev__project s = {2, 5, NULL};
    /* line 45 */
    s._ = d.alloc(&d, s.off, s.len);
    s._->_self = &d;
    /* line 46 */
    printf("切片:");
    /* line 47 */
    int32_t j;
    /* line 48 */
    for (j = 0; j < s._->n; j++) {
        /* line 49 */
        printf(" %c", s._->p[j]);
    }
    /* line 50 */
    printf("\n");
    /* line 53 */
    if (s._) { s._->_self->free(s._->_self, s._); s._ = NULL; }
    /* line 54 */
    printf("已解绑: %d\n", s._ == NULL);
    /* line 55 */
    return 0;
}
