/* 由 scc 生成，请勿手工修改 */
#include "platform.h"
#include "builtins/adt/adt.h"
#include "builtins/io/io.h"

typedef struct sc_point sc_point;
typedef struct sc_node sc_node;

typedef enum { /* base: int8_t */
    sc_Red = 0,
    sc_Green,
    sc_Blue
} sc_color;

typedef struct sc_point {
    int32_t x;
    int32_t y;
} sc_point;

typedef struct sc_node {
    void *_prev;
    void *_next;
    int32_t id;
    char name[8];
    sc_point pos;
    sc_point *link;
    int32_t *ref;
    double score;
    bool ok;
    char *tag;
} sc_node;

typedef struct sc_com__project {
    uint32_t size;
    void *ending;
    sc_limit *_;
} sc_com__project;


void sc_mod_adt_init(void); void sc_mod_adt_drop(void);
void sc_mod_io_init(void); void sc_mod_io_drop(void);

static inline sc_string *sc_string__new(void) {
    sc_string *_p = (sc_string *)sc_alloc(sizeof(sc_string));
    if (_p) {
        memset(_p, 0, sizeof(sc_string));
    }
    return _p;
}

static inline sc_string *sc_string__new_init(const char *s) {
    sc_string *_p = sc_string__new();
    if (_p) sc_string_init(_p, s);
    return _p;
}

/* ---- stringify 关键字支撑：格式化原语与按类型生成的格式化器（JSON） ---- */
static inline void sc__sof_i64(sc_string *_o, long long _v) {
    char _b[24]; snprintf(_b, sizeof(_b), "%lld", _v); sc_string_append(_o, _b); }
static inline void sc__sof_u64(sc_string *_o, unsigned long long _v) {
    char _b[24]; snprintf(_b, sizeof(_b), "%llu", _v); sc_string_append(_o, _b); }
static inline void sc__sof_f64(sc_string *_o, double _v) {
    char _b[40]; snprintf(_b, sizeof(_b), "%g", _v); sc_string_append(_o, _b); }
static inline void sc__sof_bool(sc_string *_o, unsigned char _v) {
    sc_string_append(_o, _v ? "true" : "false"); }
static inline void sc__sof_char(sc_string *_o, char _v) {
    sc_string_append_char(_o, '\''); sc_string_append_char(_o, _v); sc_string_append_char(_o, '\''); }
static inline void sc__sof_cstr(sc_string *_o, const char *_v) {
    if (!_v) { sc_string_append(_o, "nil"); return; }
    sc_string_append_char(_o, '"'); sc_string_append(_o, (char *)_v); sc_string_append_char(_o, '"'); }
static inline void sc__sof_ptr(sc_string *_o, const void *_v) {
    if (!_v) { sc_string_append(_o, "nil"); return; }
    char _b[24]; snprintf(_b, sizeof(_b), "0x%llx", (unsigned long long)(uintptr_t)_v);
    sc_string_append(_o, _b); }
static inline void sc__sof_named_ptr(sc_string *_o, const char *_tn, const void *_v) {
    if (!_v) { sc_string_append(_o, "nil"); return; }
    char _b[28]; snprintf(_b, sizeof(_b), "@0x%llx", (unsigned long long)(uintptr_t)_v);
    sc_string_append_char(_o, '"'); sc_string_append(_o, (char *)_tn);
    sc_string_append(_o, _b); sc_string_append_char(_o, '"'); }
static inline void sc__sof_amp_i64(sc_string *_o, long long _v) {
    char _b[28]; snprintf(_b, sizeof(_b), "\"&%lld\"", _v); sc_string_append(_o, _b); }
static inline void sc__sof_amp_u64(sc_string *_o, unsigned long long _v) {
    char _b[28]; snprintf(_b, sizeof(_b), "\"&%llu\"", _v); sc_string_append(_o, _b); }
static inline void sc__sof_amp_f64(sc_string *_o, double _v) {
    char _b[44]; snprintf(_b, sizeof(_b), "\"&%g\"", _v); sc_string_append(_o, _b); }
static inline void sc__sof_amp_bool(sc_string *_o, unsigned char _v) {
    sc_string_append(_o, _v ? "\"&true\"" : "\"&false\""); }
static inline void sc__sof_str(sc_string *_o, sc_string *_v) {
    sc_string_append_char(_o, '"');
    if (_v->data) sc_string_append_n(_o, _v->data, _v->size);
    sc_string_append_char(_o, '"'); }
static inline void sc__sof_nl(sc_string *_o, sc_stringify_t _opt, int _depth) {
    if (_opt.compact) return;
    sc_string_append_char(_o, '\n');
    for (int _i = 0; _i < _depth; _i++) sc_string_append(_o, "  "); }

static void sc__sof_node(sc_string *_o, sc_node *_v, sc_stringify_t _opt, int _depth);
static void sc__sof_point(sc_string *_o, sc_point *_v, sc_stringify_t _opt, int _depth);

static void sc__sof_node(sc_string *_o, sc_node *_v, sc_stringify_t _opt, int _depth) {
    sc_string_append(_o, "{");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"id\":" : "\"id\": ");
    sc__sof_i64(_o, (long long)(_v->id));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"name\":" : "\"name\": ");
    sc_string_append_char(_o, '"');
    sc_string_append_n(_o, (char *)(_v->name), strnlen(_v->name, (size_t)(8)));
    sc_string_append_char(_o, '"');
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"pos\":" : "\"pos\": ");
    sc__sof_point(_o, &(_v->pos), _opt, _depth + 1);
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"link\":" : "\"link\": ");
    sc__sof_named_ptr(_o, "point", (const void *)(_v->link));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"ref\":" : "\"ref\": ");
    if (!(_v->ref)) sc_string_append(_o, "nil");
    else sc__sof_amp_i64(_o, (long long)(*(_v->ref)));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"score\":" : "\"score\": ");
    sc__sof_f64(_o, (double)(_v->score));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"ok\":" : "\"ok\": ");
    sc__sof_bool(_o, (unsigned char)(_v->ok));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"tag\":" : "\"tag\": ");
    sc__sof_cstr(_o, _v->tag);
    sc__sof_nl(_o, _opt, _depth);
    sc_string_append(_o, "}");
}

static void sc__sof_point(sc_string *_o, sc_point *_v, sc_stringify_t _opt, int _depth) {
    sc_string_append(_o, "{");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"x\":" : "\"x\": ");
    sc__sof_i64(_o, (long long)(_v->x));
    sc_string_append(_o, ",");
    sc__sof_nl(_o, _opt, _depth + 1);
    sc_string_append(_o, _opt.compact ? "\"y\":" : "\"y\": ");
    sc__sof_i64(_o, (long long)(_v->y));
    sc__sof_nl(_o, _opt, _depth);
    sc_string_append(_o, "}");
}

static sc_string *stringify_color(sc_color _v, sc_stringify_t _opt) {
    sc_string *_o = sc_string__new();
    sc__sof_i64(_o, (long long)(_v));
    return _o;
}

static sc_string *stringify_i4_a4(int32_t *_v, sc_stringify_t _opt) {
    sc_string *_o = sc_string__new();
    sc_string_append_char(_o, '[');
    for (size_t _i0 = 0; _i0 < (size_t)(4); _i0++) {
        if (_i0) sc_string_append(_o, ",");
        sc__sof_nl(_o, _opt, (0) + 1);
        sc__sof_i64(_o, (long long)(_v[_i0]));
    }
    if ((size_t)(4)) sc__sof_nl(_o, _opt, 0);
    sc_string_append_char(_o, ']');
    return _o;
}

static sc_string *stringify_node(sc_node _v, sc_stringify_t _opt) {
    sc_string *_o = sc_string__new();
    sc__sof_node(_o, &(_v), _opt, 0);
    return _o;
}

static sc_string *stringify_node_p(sc_node *_v, sc_stringify_t _opt) {
    sc_string *_o = sc_string__new();
    if (!_v) sc_string_append(_o, "nil");
    else sc__sof_node(_o, _v, _opt, 0);
    return _o;
}

static sc_string *stringify_point(sc_point _v, sc_stringify_t _opt) {
    sc_string *_o = sc_string__new();
    sc__sof_point(_o, &(_v), _opt, 0);
    return _o;
}

static char *stringify_point_buf(sc_point _v, char *_buf, uint64_t _n, sc_stringify_t _opt) {
    if (!_buf || !_n) return _buf;
    sc_string *_s = stringify_point(_v, _opt);
    uint64_t _l = _s->size < _n - 1 ? _s->size : _n - 1;
    if (_l && _s->data) memcpy(_buf, _s->data, (size_t)_l);
    _buf[_l] = 0;
    sc_string_drop(_s);
    sc_free(_s);
    return _buf;
}

int32_t main(void) {
    SC_CONSOLE_UTF8();
    sc_mod_adt_init();
    sc_mod_io_init();
    /* line 36 */
    int32_t nn = 42;
    /* line 37 */
    char *nm = "hello";
    /* line 38 */
    sc_print((uint8_t)(0), 0, "print 基础输出 n=%d s=%s", (int)(nn), nm);
    /* line 39 */
    sc_print((uint8_t)(sc_E), 0, "错误级别示例 code=%d", -(1));
    /* line 40 */
    sc_print((uint8_t)(sc_W), 0, "警告级别示例");
    /* line 41 */
    sc_print((uint8_t)(sc_V), 0, "详细级别（默认 SC_LOG=D 下本行不输出）");
    /* line 42 */
    sc_print((uint8_t)(sc_D), 0, "调试级别示例");
    /* line 43 */
    double pi = 3.14159;
    /* line 44 */
    sc_print((uint8_t)(0), 0, "默认浮点=%f 定点=%.2f", (double)(pi), pi);
    /* line 50 */
    sc_string *s = {0};
    /* line 52 */
    sc_point p = {0};
    /* line 53 */
    p.x = 3;
    /* line 54 */
    p.y = 4;
    /* line 55 */
    s = stringify_point(p, (sc_stringify_t){ .compact = 1 });
    /* line 56 */
    sc_print((uint8_t)(0), 0, "point 值: %s", sc_string_cstr(s));
    /* line 57 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
    /* line 59 */
    sc_node n = {0};
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
    s = stringify_node(n, (sc_stringify_t){ .compact = 1 });
    /* line 70 */
    sc_print((uint8_t)(0), 0, "node 值: %s", sc_string_cstr(s));
    /* line 71 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
    /* line 74 */
    s = stringify_node(n, (sc_stringify_t){ .compact = 0 });
    /* line 75 */
    sc_print((uint8_t)(0), 0, "node 美化:\n%s", sc_string_cstr(s));
    /* line 76 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
    /* line 78 */
    sc_node *pn = &(n);
    /* line 79 */
    s = stringify_node_p(pn, (sc_stringify_t){ .compact = 1 });
    /* line 80 */
    sc_print((uint8_t)(0), 0, "node 指针: %s", sc_string_cstr(s));
    /* line 81 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
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
    s = stringify_i4_a4(arr, (sc_stringify_t){ .compact = 1 });
    /* line 89 */
    sc_print((uint8_t)(0), 0, "一维数组: %s", sc_string_cstr(s));
    /* line 90 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
    /* line 92 */
    sc_color c = sc_Green;
    /* line 93 */
    s = stringify_color(c, (sc_stringify_t){ .compact = 1 });
    /* line 94 */
    sc_print((uint8_t)(0), 0, "枚举: %s", sc_string_cstr(s));
    /* line 95 */
    sc_ptr_drop_slot((void *)&(s), (void (*)(void *))sc_string_drop);
    /* line 98 */
    char buf[64];
    /* line 99 */
    sc_print((uint8_t)(0), 0, "缓存形态: %s", stringify_point_buf(p, buf, (uint64_t)(64), (sc_stringify_t){ .compact = 1 }));
    /* line 101 */
    {
        int32_t _ret = 0;
        sc_mod_io_drop();
        sc_mod_adt_drop();
        return _ret;
    }
}
