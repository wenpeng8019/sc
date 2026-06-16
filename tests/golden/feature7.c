/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"
#include "builtins/io/io.h"

typedef struct point point;
typedef struct node node;

extern void print(const char *, ...);

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
    /* line 34 */
    print("print 基础输出 n=%d s=%s", 42, "hello");
    /* line 35 */
    print("E: 错误级别示例 code=%d", -(1));
    /* line 36 */
    print("W: 警告级别示例");
    /* line 37 */
    print("V: 详细级别（默认 SC_LOG=D 下本行不输出）");
    /* line 43 */
    string s = {0};
    string_init(&s);
    /* line 45 */
    point p = {0};
    /* line 46 */
    p.x = 3;
    /* line 47 */
    p.y = 4;
    /* line 48 */
    s = stringify_point(p, (stringify_t){ .compact = 1 });
    /* line 49 */
    print("point 值: %s", s.cstr());
    /* line 50 */
    s.drop();
    /* line 52 */
    node n = {0};
    /* line 53 */
    n.id = 1;
    /* line 54 */
    n.name[0] = 'A';
    /* line 55 */
    n.name[1] = 'B';
    /* line 56 */
    n.pos = p;
    /* line 57 */
    n.link = &(p);
    /* line 58 */
    n.ref = &(n.id);
    /* line 59 */
    n.score = 9.5;
    /* line 60 */
    n.ok = true;
    /* line 61 */
    n.tag = "hot";
    /* line 62 */
    s = stringify_node(n, (stringify_t){ .compact = 1 });
    /* line 63 */
    print("node 值: %s", s.cstr());
    /* line 64 */
    s.drop();
    /* line 67 */
    s = stringify_node(n, (stringify_t){ .compact = 0 });
    /* line 68 */
    print("node 美化:\n%s", s.cstr());
    /* line 69 */
    s.drop();
    /* line 71 */
    node *pn = &(n);
    /* line 72 */
    s = stringify_node_p(pn, (stringify_t){ .compact = 1 });
    /* line 73 */
    print("node 指针: %s", s.cstr());
    /* line 74 */
    s.drop();
    /* line 77 */
    int32_t arr[4];
    /* line 78 */
    int32_t i;
    /* line 79 */
    for (i = 0; i < 4; i++) {
        /* line 80 */
        arr[i] = ((i + 1) * 10);
    }
    /* line 81 */
    s = stringify_i4_a4(arr, (stringify_t){ .compact = 1 });
    /* line 82 */
    print("一维数组: %s", s.cstr());
    /* line 83 */
    s.drop();
    /* line 85 */
    color c = Green;
    /* line 86 */
    s = stringify_color(c, (stringify_t){ .compact = 1 });
    /* line 87 */
    print("枚举: %s", s.cstr());
    /* line 88 */
    s.drop();
    /* line 91 */
    char buf[64];
    /* line 92 */
    print("缓存形态: %s", stringify_point_buf(p, buf, (uint64_t)(64), (stringify_t){ .compact = 1 }));
    /* line 94 */
    return 0;
}
