/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct view view;
typedef struct dev dev;

typedef struct view {
    dev *_self;
    char *p;
    int32_t n;
} view;

typedef struct dev {
    char data[64];
    view *(*alloc)(struct dev *, int32_t off, int32_t len);
    void (*free)(struct dev *, view *v);
} dev;

typedef struct dev__project {
    int32_t off;
    int32_t len;
    view *_;
} dev__project;

static view * dev_alloc(dev *_this, int32_t off, int32_t len);
static void dev_free(dev *_this, view *v);
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static view * dev_alloc(dev *_this, int32_t off, int32_t len) {
    /* line 26 */
    view *v = ((view*)(malloc(sizeof(view))));
    /* line 27 */
    v->p = &(_this->data[off]);
    /* line 28 */
    v->n = len;
    /* line 29 */
    return v;
}

static void dev_free(dev *_this, view *v) {
    /* line 32 */
    free(v);
}

int32_t main(void) {
    /* line 35 */
    dev d = {0};
    /* line 36 */
    int32_t i;
    /* line 37 */
    for (i = 0; i < 26; i++) {
        /* line 38 */
        d.data[i] = ('a' + i);
    }
    /* line 41 */
    d.alloc = dev_alloc;
    /* line 42 */
    d.free = dev_free;
    /* line 45 */
    struct dev__project s = {2, 5, NULL};
    /* line 48 */
    s._ = d.alloc(&d, s.off, s.len);
    s._->_self = &d;
    /* line 49 */
    printf("切片:");
    /* line 50 */
    int32_t j;
    /* line 51 */
    for (j = 0; j < s._->n; j++) {
        /* line 52 */
        printf(" %c", s._->p[j]);
    }
    /* line 53 */
    printf("\n");
    /* line 56 */
    if (s._) { s._->_self->free(s._->_self, s._); s._ = NULL; }
    /* line 57 */
    printf("已解绑: %d\n", s._ == NULL);
    /* line 58 */
    return 0;
}
