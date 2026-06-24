/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"
#include "builtins/io/io.h"

typedef struct point point;
typedef struct node node;

typedef enum { /* base: int8_t */
    Red = 0,
    Green,
    Blue
} color;

typedef struct point {
    int32_t x;
    int32_t y;
} point;

typedef struct node {
    void *_prev;
    void *_next;
    int32_t id;
    char name[8];
    point pos;
    point *link;
    int32_t *ref;
    double score;
    uint8_t ok;
    char *tag;
} node;

typedef struct com__project {
    uint32_t size;
    void *ending;
    limit *_;
} com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);
void sc_mod_io_init(void); void sc_mod_io_drop(void);

static inline string *string__new(void) {
    string *_p = (string *)sc_alloc(sizeof(string));
    if (_p) {
        memset(_p, 0, sizeof(string));
        string_init(_p);
    }
    return _p;
}

/* ---- stringify 关键字支撑：格式化原语与按类型生成的格式化器（JSON） ---- */
static inline void sc__sof_i64(string *_o, long long _v) {
    char _b[24]; snprintf(_b, sizeof(_b), "%lld", _v); string_append(_o, _b); }
static inline void sc__sof_u64(string *_o, unsigned long long _v) {
    char _b[24]; snprintf(_b, sizeof(_b), "%llu", _v); string_append(_o, _b); }
static inline void sc__sof_f64(string *_o, double _v) {
    char _b[40]; snprintf(_b, sizeof(_b), "%g", _v); string_append(_o, _b); }
static inline void sc__sof_bool(string *_o, unsigned char _v) {
    string_append(_o, _v ? "true" : "false"); }
static inline void sc__sof_char(string *_o, char _v) {
    string_append_char(_o, '\''); string_append_char(_o, _v); string_append_char(_o, '\''); }
static inline void sc__sof_cstr(string *_o, const char *_v) {
    if (!_v) { string_append(_o, "nil"); return; }
    string_append_char(_o, '"'); string_append(_o, (char *)_v); string_append_char(_o, '"'); }
static inline void sc__sof_ptr(string *_o, const void *_v) {
    if (!_v) { string_append(_o, "nil"); return; }
    char _b[24]; snprintf(_b, sizeof(_b), "0x%llx", (unsigned long long)(uintptr_t)_v);
    string_append(_o, _b); }
static inline void sc__sof_named_ptr(string *_o, const char *_tn, const void *_v) {
    if (!_v) { string_append(_o, "nil"); return; }
    char _b[28]; snprintf(_b, sizeof(_b), "@0x%llx", (unsigned long long)(uintptr_t)_v);
    string_append_char(_o, '"'); string_append(_o, (char *)_tn);
    string_append(_o, _b); string_append_char(_o, '"'); }
static inline void sc__sof_amp_i64(string *_o, long long _v) {
    char _b[28]; snprintf(_b, sizeof(_b), "\"&%lld\"", _v); string_append(_o, _b); }
static inline void sc__sof_amp_u64(string *_o, unsigned long long _v) {
    char _b[28]; snprintf(_b, sizeof(_b), "\"&%llu\"", _v); string_append(_o, _b); }
static inline void sc__sof_amp_f64(string *_o, double _v) {
    char _b[44]; snprintf(_b, sizeof(_b), "\"&%g\"", _v); string_append(_o, _b); }
static inline void sc__sof_amp_bool(string *_o, unsigned char _v) {
    string_append(_o, _v ? "\"&true\"" : "\"&false\""); }
static inline void sc__sof_str(string *_o, string *_v) {
    string_append_char(_o, '"');
    if (_v->data) string_append_n(_o, _v->data, _v->size);
    string_append_char(_o, '"'); }
static inline void sc__sof_nl(string *_o, stringify_t _opt, int _depth) {
    if (_opt.compact) return;
    string_append_char(_o, '\n');
    for (int _i = 0; _i < _depth; _i++) string_append(_o, "  "); }

static void sc__sof_node(string *_o, node *_v, stringify_t _opt, int _depth);
static void sc__sof_point(string *_o, point *_v, stringify_t _opt, int _depth);

static void sc__sof_node(string *_o, node *_v, stringify_t _opt, int _depth) {
    string_append(_o, "{");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"id\":" : "\"id\": ");
    sc__sof_i64(_o, (long long)(_v->id));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"name\":" : "\"name\": ");
    string_append_char(_o, '"');
    string_append_n(_o, (char *)(_v->name), strnlen(_v->name, (size_t)(8)));
    string_append_char(_o, '"');
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"pos\":" : "\"pos\": ");
    sc__sof_point(_o, &(_v->pos), _opt, _depth + 1);
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"link\":" : "\"link\": ");
    sc__sof_named_ptr(_o, "point", (const void *)(_v->link));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"ref\":" : "\"ref\": ");
    if (!(_v->ref)) string_append(_o, "nil");
    else sc__sof_amp_i64(_o, (long long)(*(_v->ref)));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"score\":" : "\"score\": ");
    sc__sof_f64(_o, (double)(_v->score));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"ok\":" : "\"ok\": ");
    sc__sof_bool(_o, (unsigned char)(_v->ok));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"tag\":" : "\"tag\": ");
    sc__sof_cstr(_o, _v->tag);
    sc__sof_nl(_o, _opt, _depth);
    string_append(_o, "}");
}

static void sc__sof_point(string *_o, point *_v, stringify_t _opt, int _depth) {
    string_append(_o, "{");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"x\":" : "\"x\": ");
    sc__sof_i64(_o, (long long)(_v->x));
    string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    string_append(_o, _opt.compact ? "\"y\":" : "\"y\": ");
    sc__sof_i64(_o, (long long)(_v->y));
    sc__sof_nl(_o, _opt, _depth);
    string_append(_o, "}");
}

static string *stringify_color(color _v, stringify_t _opt) {
    string *_o = string__new();
    sc__sof_i64(_o, (long long)(_v));
    return _o;
}

static string *stringify_i4_a4(int32_t *_v, stringify_t _opt) {
    string *_o = string__new();
    string_append_char(_o, '[');
    for (size_t _i0 = 0; _i0 < (size_t)(4); _i0++) {
        if (_i0) string_append(_o, ",");
        sc__sof_nl(_o, _opt, (0) + 1);
        sc__sof_i64(_o, (long long)(_v[_i0]));
    }
    if ((size_t)(4)) sc__sof_nl(_o, _opt, 0);
    string_append_char(_o, ']');
    return _o;
}

static string *stringify_node(node _v, stringify_t _opt) {
    string *_o = string__new();
    sc__sof_node(_o, &(_v), _opt, 0);
    return _o;
}

static string *stringify_node_p(node *_v, stringify_t _opt) {
    string *_o = string__new();
    if (!_v) string_append(_o, "nil");
    else sc__sof_node(_o, _v, _opt, 0);
    return _o;
}

static string *stringify_point(point _v, stringify_t _opt) {
    string *_o = string__new();
    sc__sof_point(_o, &(_v), _opt, 0);
    return _o;
}

static char *stringify_point_buf(point _v, char *_buf, uint64_t _n, stringify_t _opt) {
    if (!_buf || !_n) return _buf;
    string *_s = stringify_point(_v, _opt);
    uint64_t _l = _s->size < _n - 1 ? _s->size : _n - 1;
    if (_l && _s->data) memcpy(_buf, _s->data, (size_t)_l);
    _buf[_l] = 0;
    string_drop(_s);
    sc_free(_s);
    return _buf;
}

int32_t main(void) {
    sc_mod_adt_init();
    sc_mod_io_init();
    /* line 36 */
    int32_t nn = 42;
    /* line 37 */
    char *nm = "hello";
    /* line 38 */
    print((uint8_t)(0), "print 基础输出 n=%d s=%s", (int)(nn), nm);
    /* line 39 */
    print((uint8_t)(0), "E: 错误级别示例 code=%d", -(1));
    /* line 40 */
    print((uint8_t)(0), "W: 警告级别示例");
    /* line 41 */
    print((uint8_t)(0), "V: 详细级别（默认 SC_LOG=D 下本行不输出）");
    /* line 42 */
    print((uint8_t)(7), "通道 7：自定义日志通道");
    /* line 43 */
    double pi = 3.14159;
    /* line 44 */
    print((uint8_t)(0), "默认浮点=%f 定点=%.2f", (double)(pi), pi);
    /* line 50 */
    string *s = {0};
    /* line 52 */
    point p = {0};
    /* line 53 */
    p.x = 3;
    /* line 54 */
    p.y = 4;
    /* line 55 */
    s = stringify_point(p, (stringify_t){ .compact = 1 });
    /* line 56 */
    print((uint8_t)(0), "point 值: %s", string_cstr(s));
    /* line 57 */
    (string_drop(s), sc_free(s));
    /* line 59 */
    node n = {0};
    /* line 60 */
    n.id = 1;
    /* line 61 */
    n.name[0] = 'A';
    /* line 62 */
    n.name[1] = 'B';
    /* line 63 */
    n.pos = p;
    /* line 64 */
    n.link = &(p);
    /* line 65 */
    n.ref = &(n.id);
    /* line 66 */
    n.score = 9.5;
    /* line 67 */
    n.ok = true;
    /* line 68 */
    n.tag = "hot";
    /* line 69 */
    s = stringify_node(n, (stringify_t){ .compact = 1 });
    /* line 70 */
    print((uint8_t)(0), "node 值: %s", string_cstr(s));
    /* line 71 */
    (string_drop(s), sc_free(s));
    /* line 74 */
    s = stringify_node(n, (stringify_t){ .compact = 0 });
    /* line 75 */
    print((uint8_t)(0), "node 美化:\n%s", string_cstr(s));
    /* line 76 */
    (string_drop(s), sc_free(s));
    /* line 78 */
    node *pn = &(n);
    /* line 79 */
    s = stringify_node_p(pn, (stringify_t){ .compact = 1 });
    /* line 80 */
    print((uint8_t)(0), "node 指针: %s", string_cstr(s));
    /* line 81 */
    (string_drop(s), sc_free(s));
    /* line 84 */
    int32_t arr[4];
    /* line 85 */
    int32_t i;
    /* line 86 */
    for (i = 0; i < 4; i++) {
        /* line 87 */
        arr[i] = ((i + 1) * 10);
    }
    /* line 88 */
    s = stringify_i4_a4(arr, (stringify_t){ .compact = 1 });
    /* line 89 */
    print((uint8_t)(0), "一维数组: %s", string_cstr(s));
    /* line 90 */
    (string_drop(s), sc_free(s));
    /* line 92 */
    color c = Green;
    /* line 93 */
    s = stringify_color(c, (stringify_t){ .compact = 1 });
    /* line 94 */
    print((uint8_t)(0), "枚举: %s", string_cstr(s));
    /* line 95 */
    (string_drop(s), sc_free(s));
    /* line 98 */
    char buf[64];
    /* line 99 */
    print((uint8_t)(0), "缓存形态: %s", stringify_point_buf(p, buf, (uint64_t)(64), (stringify_t){ .compact = 1 }));
    /* line 101 */
    {
        int32_t _ret = 0;
        sc_mod_io_drop();
        sc_mod_adt_drop();
        return _ret;
    }
}
