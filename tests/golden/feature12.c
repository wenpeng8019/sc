/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct view view;
typedef struct buffer buffer;

typedef struct view {
    buffer *_self;
    char *p;
    int32_t n;
} view;

static int32_t view_capacity(view *_this);
typedef struct buffer {
    char data[256];
} buffer;

typedef struct buffer__project {
    int32_t off;
    int32_t len;
    view *_;
} buffer__project;

static view * buffer_alloc(buffer *_this, int32_t off, int32_t len);
static void buffer_free(buffer *_this, view *v);

static int32_t view_capacity(view *_this) {
    /* line 28 */
    return sizeof((_this->_self)->data);
}

static view * buffer_alloc(buffer *_this, int32_t off, int32_t len) {
    /* line 37 */
    view *v = ((view*)(malloc(sizeof(view))));
    /* line 38 */
    v->p = &(_this->data[off]);
    /* line 39 */
    v->n = len;
    /* line 40 */
    return v;
}

static void buffer_free(buffer *_this, view *v) {
    /* line 44 */
    free(v);
}

int32_t main(void) {
    /* line 49 */
    buffer b = {0};
    /* line 50 */
    int32_t i;
    /* line 51 */
    for (i = 0; i < 26; i++) {
        /* line 52 */
        b.data[i] = ('a' + i);
    }
    /* line 55 */
    struct buffer__project s = {2, 5, NULL};
    /* line 58 */
    s._ = buffer_alloc(&b, s.off, s.len);
    s._->_self = &b;
    /* line 60 */
    printf("切片:");
    /* line 61 */
    int32_t j;
    /* line 62 */
    for (j = 0; j < s._->n; j++) {
        /* line 63 */
        printf(" %c", s._->p[j]);
    }
    /* line 64 */
    printf("\n");
    /* line 67 */
    printf("本体容量: %d\n", view_capacity(s._));
    /* line 70 */
    if (s._) { buffer_free(s._->_self, s._); s._ = NULL; }
    /* line 71 */
    printf("已解绑: %d\n", s._ == NULL);
    /* line 73 */
    return 0;
}
