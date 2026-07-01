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
typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


static int32_t view_capacity(view *_this) {
    /* line 25 */
    return sizeof((_this->_self)->data);
}

static view * buffer_alloc(buffer *_this, int32_t off, int32_t len) {
    /* line 34 */
    view *v = ((view*)(malloc(sizeof(view))));
    /* line 35 */
    v->p = &(_this->data[off]);
    /* line 36 */
    v->n = len;
    /* line 37 */
    return v;
}

static void buffer_free(buffer *_this, view *v) {
    /* line 41 */
    free(v);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 46 */
    buffer b = {0};
    /* line 47 */
    int32_t i;
    /* line 48 */
    for (i = 0; i < 26; i++) {
        /* line 49 */
        b.data[i] = ('a' + i);
    }
    /* line 52 */
    struct buffer__project s = {2, 5, NULL};
    /* line 55 */
    s._ = buffer_alloc(&b, s.off, s.len);
    s._->_self = &b;
    /* line 57 */
    printf("切片:");
    /* line 58 */
    int32_t j;
    /* line 59 */
    for (j = 0; j < s._->n; j++) {
        /* line 60 */
        printf(" %c", s._->p[j]);
    }
    /* line 61 */
    printf("\n");
    /* line 64 */
    printf("本体容量: %d\n", view_capacity(s._));
    /* line 67 */
    if (s._) { buffer_free(s._->_self, s._); s._ = NULL; }
    /* line 68 */
    printf("已解绑: %d\n", s._ == NULL);
    /* line 70 */
    return 0;
}
