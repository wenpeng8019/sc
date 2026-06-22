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

static string stringify_color(color _v, stringify_t _opt) {
    string _s;
    string_init(&_s);
    string *_o = &_s;
    sc__sof_i64(_o, (long long)(_v));
    return _s;
}

static string stringify_i4_a4(int32_t *_v, stringify_t _opt) {
    string _s;
    string_init(&_s);
    string *_o = &_s;
    string_append_char(_o, '[');
    for (size_t _i0 = 0; _i0 < (size_t)(4); _i0++) {
        if (_i0) string_append(_o, ",");
        sc__sof_nl(_o, _opt, (0) + 1);
        sc__sof_i64(_o, (long long)(_v[_i0]));
    }
    if ((size_t)(4)) sc__sof_nl(_o, _opt, 0);
    string_append_char(_o, ']');
    return _s;
}

static string stringify_node(node _v, stringify_t _opt) {
    string _s;
    string_init(&_s);
    string *_o = &_s;
    sc__sof_node(_o, &(_v), _opt, 0);
    return _s;
}

static string stringify_node_p(node *_v, stringify_t _opt) {
    string _s;
    string_init(&_s);
    string *_o = &_s;
    if (!_v) string_append(_o, "nil");
    else sc__sof_node(_o, _v, _opt, 0);
    return _s;
}

static string stringify_point(point _v, stringify_t _opt) {
    string _s;
    string_init(&_s);
    string *_o = &_s;
    sc__sof_point(_o, &(_v), _opt, 0);
    return _s;
}

static char *stringify_point_buf(point _v, char *_buf, uint64_t _n, stringify_t _opt) {
    if (!_buf || !_n) return _buf;
    string _s = stringify_point(_v, _opt);
    uint64_t _l = _s.size < _n - 1 ? _s.size : _n - 1;
    if (_l && _s.data) memcpy(_buf, _s.data, (size_t)_l);
    _buf[_l] = 0;
    string_drop(&_s);
    return _buf;
}

int32_t main(void) {
    sc_mod_adt_init();
    sc_mod_io_init();
    /* line 37 */
    int32_t nn = 42;
    /* line 38 */
    char *nm = "hello";
    /* line 39 */
    print((uint8_t)(0), "print 基础输出 n=%d s=%s", (int)(nn), nm);
    /* line 40 */
    print((uint8_t)(0), "E: 错误级别示例 code=%d", -(1));
    /* line 41 */
    print((uint8_t)(0), "W: 警告级别示例");
    /* line 42 */
    print((uint8_t)(0), "V: 详细级别（默认 SC_LOG=D 下本行不输出）");
    /* line 43 */
    print((uint8_t)(7), "通道 7：自定义日志通道");
    /* line 44 */
    double pi = 3.14159;
    /* line 45 */
    print((uint8_t)(0), "默认浮点=%f 定点=%.2f", (double)(pi), pi);
    /* line 51 */
    string s = {0};
    string_init(&s);
    /* line 53 */
    point p = {0};
    /* line 54 */
    p.x = 3;
    /* line 55 */
    p.y = 4;
    /* line 56 */
    s = stringify_point(p, (stringify_t){ .compact = 1 });
    /* line 57 */
    print((uint8_t)(0), "point 值: %s", string_cstr(&s));
    /* line 58 */
    string_drop(&s);
    /* line 60 */
    node n = {0};
    /* line 61 */
    n.id = 1;
    /* line 62 */
    n.name[0] = 'A';
    /* line 63 */
    n.name[1] = 'B';
    /* line 64 */
    n.pos = p;
    /* line 65 */
    n.link = &(p);
    /* line 66 */
    n.ref = &(n.id);
    /* line 67 */
    n.score = 9.5;
    /* line 68 */
    n.ok = true;
    /* line 69 */
    n.tag = "hot";
    /* line 70 */
    s = stringify_node(n, (stringify_t){ .compact = 1 });
    /* line 71 */
    print((uint8_t)(0), "node 值: %s", string_cstr(&s));
    /* line 72 */
    string_drop(&s);
    /* line 75 */
    s = stringify_node(n, (stringify_t){ .compact = 0 });
    /* line 76 */
    print((uint8_t)(0), "node 美化:\n%s", string_cstr(&s));
    /* line 77 */
    string_drop(&s);
    /* line 79 */
    node *pn = &(n);
    /* line 80 */
    s = stringify_node_p(pn, (stringify_t){ .compact = 1 });
    /* line 81 */
    print((uint8_t)(0), "node 指针: %s", string_cstr(&s));
    /* line 82 */
    string_drop(&s);
    /* line 85 */
    int32_t arr[4];
    /* line 86 */
    int32_t i;
    /* line 87 */
    for (i = 0; i < 4; i++) {
        /* line 88 */
        arr[i] = ((i + 1) * 10);
    }
    /* line 89 */
    s = stringify_i4_a4(arr, (stringify_t){ .compact = 1 });
    /* line 90 */
    print((uint8_t)(0), "一维数组: %s", string_cstr(&s));
    /* line 91 */
    string_drop(&s);
    /* line 93 */
    color c = Green;
    /* line 94 */
    s = stringify_color(c, (stringify_t){ .compact = 1 });
    /* line 95 */
    print((uint8_t)(0), "枚举: %s", string_cstr(&s));
    /* line 96 */
    string_drop(&s);
    /* line 99 */
    char buf[64];
    /* line 100 */
    print((uint8_t)(0), "缓存形态: %s", stringify_point_buf(p, buf, (uint64_t)(64), (stringify_t){ .compact = 1 }));
    /* line 102 */
    {
        int32_t _ret = 0;
        sc_mod_io_drop();
        sc_mod_adt_drop();
        return _ret;
    }
}
