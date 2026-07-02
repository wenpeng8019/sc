/* 由 scc 生成，请勿手工修改 */
#include "platform.h"

typedef struct sc_view sc_view;
typedef struct sc_buffer sc_buffer;

typedef struct sc_view {
    sc_buffer *_self;
    char *p;
    int32_t n;
} sc_view;

static int32_t sc_view_capacity(sc_view *_this);
typedef struct sc_buffer {
    char data[256];
} sc_buffer;

typedef struct sc_buffer__project {
    int32_t off;
    int32_t len;
    sc_view *_;
} sc_buffer__project;

static sc_view * sc_buffer_alloc(sc_buffer *_this, int32_t off, int32_t len);
static void sc_buffer_free(sc_buffer *_this, sc_view *v);
typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


static int32_t sc_view_capacity(sc_view *_this) {
    /* line 25 */
    return sizeof((_this->_self)->data);
}

static sc_view * sc_buffer_alloc(sc_buffer *_this, int32_t off, int32_t len) {
    /* line 34 */
    sc_view *v = ((sc_view*)(malloc(sizeof(sc_view))));
    /* line 35 */
    v->p = &(_this->data[off]);
    /* line 36 */
    v->n = len;
    /* line 37 */
    return v;
}

static void sc_buffer_free(sc_buffer *_this, sc_view *v) {
    /* line 41 */
    free(v);
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    /* line 46 */
    sc_buffer b = {0};
    /* line 47 */
    int32_t i;
    /* line 48 */
    for (i = 0; i < 26; i++) {
        /* line 49 */
        b.data[i] = ('a' + i);
    }
    /* line 52 */
    struct sc_buffer__project s = {2, 5, NULL};
    /* line 55 */
    s._ = sc_buffer_alloc(&b, s.off, s.len);
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
    printf("本体容量: %d\n", sc_view_capacity(s._));
    /* line 67 */
    if (s._) { sc_buffer_free(s._->_self, s._); s._ = NULL; }
    /* line 68 */
    printf("已解绑: %d\n", s._ == NULL);
    /* line 70 */
    return 0;
}
