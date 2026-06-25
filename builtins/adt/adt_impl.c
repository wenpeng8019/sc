/* adt_impl.c —— sc 内置 ADT 的默认实现
 *
 * 编译器在单元图包含 builtins/adt/adt.sc 时自动编译并链接本文件；
 * 可通过 scc --adt <x.c|x.o|x.a> 替换为自定义实现（契约见 adt.h）。
 */
#include "adt.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */
#include "mem/mem.h"    /* list 段式存储用 chunk/chunk0/refit/recycle（不受全局 -DSC_POOL 影响） */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>

/* ---------------- string ---------------- */

/* 确保容量至少 need+1（含 NUL），按 2 倍扩容 */
static uint8_t str_grow(string *s, uint64_t need) {
    if (s->cap > need) return 1;
    uint64_t cap = s->cap ? s->cap : 16;
    while (cap <= need) cap *= 2;
    char *p = (char *)sc_realloc(s->data, cap);
    if (!p) return 0;
    s->data = p;
    s->cap = cap;
    return 1;
}

void string_init(string *_this, const char *s) {
    _this->data = NULL;
    _this->size = 0;
    _this->cap = 0;
    if (s) string_append(_this, s);
}

void string_drop(string *_this) {
    sc_free(_this->data);
    string_init(_this, NULL);
}

uint64_t string_len(string *_this) { return _this->size; }

char *string_cstr(string *_this) {
    return _this->data ? _this->data : (char *)"";
}

void string_clear(string *_this) {
    _this->size = 0;
    if (_this->data) _this->data[0] = 0;
}

uint8_t string_reserve(string *_this, uint64_t n) { return str_grow(_this, n); }

uint8_t string_assign(string *_this, const char *s) {
    string_clear(_this);
    return string_append(_this, s);
}

uint8_t string_append(string *_this, const char *s) {
    return string_append_n(_this, s, s ? strlen(s) : 0);
}

uint8_t string_append_n(string *_this, const char *s, uint64_t n) {
    if (!n) { /* 仍保证缓冲区存在，便于 cstr() */ }
    if (!str_grow(_this, _this->size + n)) return 0;
    if (n) memcpy(_this->data + _this->size, s, n);
    _this->size += n;
    _this->data[_this->size] = 0;
    return 1;
}

uint8_t string_append_char(string *_this, char c) {
    return string_append_n(_this, &c, 1);
}

uint8_t string_insert(string *_this, uint64_t index, const char *s) {
    if (index > _this->size) return 0;
    uint64_t n = s ? strlen(s) : 0;
    if (!n) return 1;
    if (!str_grow(_this, _this->size + n)) return 0;
    memmove(_this->data + index + n, _this->data + index, _this->size - index);
    memcpy(_this->data + index, s, n);
    _this->size += n;
    _this->data[_this->size] = 0;
    return 1;
}

uint8_t string_erase(string *_this, uint64_t index, uint64_t n) {
    if (index >= _this->size) return 0;
    if (n > _this->size - index) n = _this->size - index;
    memmove(_this->data + index, _this->data + index + n,
            _this->size - index - n);
    _this->size -= n;
    _this->data[_this->size] = 0;
    return 1;
}

char string_at(string *_this, uint64_t index) {
    return index < _this->size ? _this->data[index] : 0;
}

/* ---- KMP 双向子串搜索（移植自 uthash/utstring.h）---- */

/* 左→右 KMP 失配表（needle 长 n，表长 n+1）。 */
static void sc__kmp_build(const char *p, size_t n, long *t) {
    long i = 0, j = -1;
    t[0] = -1;
    while (i < (long)n) {
        while (j > -1 && p[i] != p[j]) j = t[j];
        i++; j++;
        if (i < (long)n && p[i] == p[j]) t[i] = t[j];
        else t[i] = j;
    }
}

/* 右→左 KMP 失配表。 */
static void sc__kmp_buildR(const char *p, size_t n, long *t) {
    long i = (long)n - 1, j = (long)n;
    t[i + 1] = j;
    while (i >= 0) {
        while (j < (long)n && p[i] != p[j]) j = t[j + 1];
        i--; j--;
        if (i >= 0 && p[i] == p[j]) t[i + 1] = t[j + 1];
        else t[i + 1] = j;
    }
}

/* 左→右搜索：命中返回相对 haystack 的偏移，未命中 -1。 */
static long sc__kmp_find(const char *hay, size_t hlen, const char *ndl,
                         size_t nlen, const long *t) {
    long i = 0, j = 0;
    while (j < (long)hlen && ((long)hlen - j + i) >= (long)nlen) {
        while (i > -1 && ndl[i] != hay[j]) i = t[i];
        i++; j++;
        if (i >= (long)nlen) return j - i;
    }
    return -1;
}

/* 右→左搜索：命中返回相对 haystack 的偏移，未命中 -1。 */
static long sc__kmp_findR(const char *hay, size_t hlen, const char *ndl,
                          size_t nlen, const long *t) {
    long i = (long)nlen - 1, j = (long)hlen - 1;
    while (j >= 0 && j >= i) {
        while (i < (long)nlen && ndl[i] != hay[j]) i = t[i + 1];
        i--; j--;
        if (i < 0) return j + 1;
    }
    return -1;
}

int64_t string_find(string *_this, const char *sub, uint64_t start) {
    if (!sub) return -1;
    uint64_t nlen = strlen(sub);
    if (nlen == 0) return start <= _this->size ? (int64_t)start : -1;
    if (start > _this->size || _this->size - start < nlen) return -1;
    long *t = (long *)sc_alloc(sizeof(long) * (nlen + 1));
    if (!t) return -1;
    sc__kmp_build(sub, nlen, t);
    long pos = sc__kmp_find(_this->data + start, _this->size - start, sub, nlen, t);
    sc_free(t);
    return pos >= 0 ? (int64_t)(pos + (long)start) : -1;
}

int64_t string_rfind(string *_this, const char *sub) {
    if (!sub) return -1;
    uint64_t nlen = strlen(sub);
    if (nlen == 0) return (int64_t)_this->size;
    if (nlen > _this->size || !_this->data) return -1;
    long *t = (long *)sc_alloc(sizeof(long) * (nlen + 1));
    if (!t) return -1;
    sc__kmp_buildR(sub, nlen, t);
    long pos = sc__kmp_findR(_this->data, _this->size, sub, nlen, t);
    sc_free(t);
    return (int64_t)pos;
}

/* 追加 printf 格式化文本：vsnprintf 探测所需长度→扩容→重试（移植 utstring_printf_va）。 */
uint8_t string_printf(string *_this, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    uint8_t ok = 1;
    for (;;) {
        va_list cp;
        va_copy(cp, ap);
        uint64_t avail = _this->cap > _this->size ? _this->cap - _this->size : 0;
        int n = vsnprintf(_this->data ? _this->data + _this->size : NULL,
                          (size_t)avail, fmt, cp);
        va_end(cp);
        if (n < 0) { ok = 0; break; }
        if ((uint64_t)n < avail) { _this->size += (uint64_t)n; break; }
        if (!str_grow(_this, _this->size + (uint64_t)n)) { ok = 0; break; }
    }
    va_end(ap);
    return ok;
}

uint8_t string_equals(string *_this, const char *s) {
    return strcmp(string_cstr(_this), s ? s : "") == 0;
}

uint8_t string_starts_with(string *_this, const char *s) {
    if (!s) return 0;
    uint64_t n = strlen(s);
    return n <= _this->size && memcmp(string_cstr(_this), s, n) == 0;
}

uint8_t string_ends_with(string *_this, const char *s) {
    if (!s) return 0;
    uint64_t n = strlen(s);
    return n <= _this->size &&
           memcmp(string_cstr(_this) + _this->size - n, s, n) == 0;
}

/* 负索引从尾部计；区间 [start, stop)，stop 超界截断 */
uint8_t string_slice(string *_this, int64_t start, int64_t stop, string *out) {
    int64_t len = (int64_t)_this->size;
    if (start < 0) start += len;
    if (stop < 0) stop += len;
    if (start < 0) start = 0;
    if (stop > len) stop = len;
    string_clear(out);
    if (start >= stop) return str_grow(out, 0);
    return string_append_n(out, _this->data + start, (uint64_t)(stop - start));
}

void string_strip(string *_this) {
    if (!_this->size) return;
    uint64_t b = 0, e = _this->size;
    while (b < e && isspace((unsigned char)_this->data[b])) b++;
    while (e > b && isspace((unsigned char)_this->data[e - 1])) e--;
    if (b) memmove(_this->data, _this->data + b, e - b);
    _this->size = e - b;
    _this->data[_this->size] = 0;
}

void string_lower(string *_this) {
    for (uint64_t i = 0; i < _this->size; i++)
        _this->data[i] = (char)tolower((unsigned char)_this->data[i]);
}

void string_upper(string *_this) {
    for (uint64_t i = 0; i < _this->size; i++)
        _this->data[i] = (char)toupper((unsigned char)_this->data[i]);
}

uint8_t string_clone(string *_this, string *out) {
    string_clear(out);
    return string_append_n(out, _this->data, _this->size);
}

/* ---------------- array ---------------- */

/* 第 i 个元素槽位地址 */
static char *arr_eltptr(array *a, uint64_t i) { return a->buf + i * a->elem_sz; }

/* 确保容量至少 need 个槽位，按 2 倍扩容 */
static uint8_t arr_grow(array *a, uint64_t need) {
    if (a->cap >= need) return 1;
    uint64_t cap = a->cap ? a->cap : 8;
    while (cap < need) cap *= 2;
    char *p = (char *)sc_realloc(a->buf, cap * a->elem_sz);
    if (!p) return 0;
    a->buf = p;
    a->cap = cap;
    return 1;
}

void array_init(array *_this, uint32_t elem_sz) {
    _this->buf = NULL;
    _this->size = 0;
    _this->cap = 0;
    _this->elem_sz = elem_sz ? elem_sz : 1;
}

void array_drop(array *_this) {
    sc_free(_this->buf);
    _this->buf = NULL;
    _this->size = 0;
    _this->cap = 0;
}

uint64_t array_len(array *_this) { return _this->size; }

void *array_data(array *_this) { return _this->buf; }

void array_clear(array *_this) { _this->size = 0; }

uint8_t array_reserve(array *_this, uint64_t n) { return arr_grow(_this, n); }

uint8_t array_resize(array *_this, uint64_t n) {
    if (n > _this->size) {
        if (!arr_grow(_this, n)) return 0;
        memset(arr_eltptr(_this, _this->size), 0,
               (n - _this->size) * _this->elem_sz);
    }
    _this->size = n;
    return 1;
}

uint8_t array_assign(array *_this, void *src, uint64_t n) {
    _this->size = 0;
    return array_append(_this, src, n);
}

uint8_t array_append(array *_this, void *src, uint64_t n) {
    if (!n) return 1;
    if (!arr_grow(_this, _this->size + n)) return 0;
    if (src) memcpy(arr_eltptr(_this, _this->size), src, n * _this->elem_sz);
    _this->size += n;
    return 1;
}

uint8_t array_push(array *_this, void *value) {
    if (!arr_grow(_this, _this->size + 1)) return 0;
    if (value) memcpy(arr_eltptr(_this, _this->size), value, _this->elem_sz);
    _this->size++;
    return 1;
}

void *array_pop(array *_this) {
    return _this->size ? arr_eltptr(_this, --_this->size) : NULL;
}

uint8_t array_insert(array *_this, uint64_t index, void *value) {
    if (index > _this->size) return 0;
    if (!arr_grow(_this, _this->size + 1)) return 0;
    memmove(arr_eltptr(_this, index + 1), arr_eltptr(_this, index),
            (_this->size - index) * _this->elem_sz);
    if (value) memcpy(arr_eltptr(_this, index), value, _this->elem_sz);
    _this->size++;
    return 1;
}

uint8_t array_erase(array *_this, uint64_t index, uint64_t n) {
    if (index >= _this->size || !n) return index >= _this->size ? 0 : 1;
    if (index + n > _this->size) n = _this->size - index;
    memmove(arr_eltptr(_this, index), arr_eltptr(_this, index + n),
            (_this->size - index - n) * _this->elem_sz);
    _this->size -= n;
    return 1;
}

void *array_at(array *_this, uint64_t index) {
    return index < _this->size ? arr_eltptr(_this, index) : NULL;
}

uint8_t array_set(array *_this, uint64_t index, void *value) {
    if (index >= _this->size) return 0;
    if (value) memcpy(arr_eltptr(_this, index), value, _this->elem_sz);
    return 1;
}

void *array_front(array *_this) {
    return _this->size ? arr_eltptr(_this, 0) : NULL;
}

void *array_back(array *_this) {
    return _this->size ? arr_eltptr(_this, _this->size - 1) : NULL;
}

/* 元素相等：cmp 非空用回调（==0 即相等），否则逐字节 memcmp */
static int arr_eq(array *a, void *x, void *y, array_cmp cmp) {
    return cmp ? cmp(x, y) == 0 : memcmp(x, y, a->elem_sz) == 0;
}

int64_t array_find(array *_this, void *key, uint64_t start, array_cmp cmp) {
    for (uint64_t i = start; i < _this->size; i++)
        if (arr_eq(_this, arr_eltptr(_this, i), key, cmp)) return (int64_t)i;
    return -1;
}

int64_t array_rfind(array *_this, void *key, array_cmp cmp) {
    for (uint64_t i = _this->size; i > 0; i--)
        if (arr_eq(_this, arr_eltptr(_this, i - 1), key, cmp)) return (int64_t)(i - 1);
    return -1;
}

uint8_t array_equals(array *_this, array *other, array_cmp cmp) {
    if (_this->size != other->size || _this->elem_sz != other->elem_sz) return 0;
    for (uint64_t i = 0; i < _this->size; i++)
        if (!arr_eq(_this, arr_eltptr(_this, i), arr_eltptr(other, i), cmp)) return 0;
    return 1;
}

uint8_t array_starts_with(array *_this, array *other, array_cmp cmp) {
    if (other->size > _this->size || _this->elem_sz != other->elem_sz) return 0;
    for (uint64_t i = 0; i < other->size; i++)
        if (!arr_eq(_this, arr_eltptr(_this, i), arr_eltptr(other, i), cmp)) return 0;
    return 1;
}

uint8_t array_ends_with(array *_this, array *other, array_cmp cmp) {
    if (other->size > _this->size || _this->elem_sz != other->elem_sz) return 0;
    uint64_t off = _this->size - other->size;
    for (uint64_t i = 0; i < other->size; i++)
        if (!arr_eq(_this, arr_eltptr(_this, off + i), arr_eltptr(other, i), cmp)) return 0;
    return 1;
}

/* 负索引从尾部计；区间 [start, stop)，stop 超界截断。out 沿用本数组 elem_sz */
uint8_t array_slice(array *_this, int64_t start, int64_t stop, array *out) {
    int64_t len = (int64_t)_this->size;
    if (start < 0) start += len;
    if (stop < 0) stop += len;
    if (start < 0) start = 0;
    if (stop > len) stop = len;
    out->elem_sz = _this->elem_sz;
    out->size = 0;
    if (start >= stop) return 1;
    return array_append(out, arr_eltptr(_this, (uint64_t)start),
                        (uint64_t)(stop - start));
}

void array_reverse(array *_this) {
    if (_this->size < 2) return;
    /* 借栈上小缓冲交换；元素较大时退化为逐字节 */
    char tmp[64];
    uint64_t sz = _this->elem_sz;
    char *heap = sz > sizeof(tmp) ? (char *)sc_alloc(sz) : NULL;
    char *t = heap ? heap : tmp;
    for (uint64_t i = 0, j = _this->size; i + 1 < j; i++, j--) {
        char *a = arr_eltptr(_this, i), *b = arr_eltptr(_this, j - 1);
        memcpy(t, a, sz);
        memcpy(a, b, sz);
        memcpy(b, t, sz);
    }
    if (heap) sc_free(heap);
}

uint8_t array_clone(array *_this, array *out) {
    out->elem_sz = _this->elem_sz;
    out->size = 0;
    return array_append(out, _this->buf, _this->size);
}

void array_sort(array *_this, array_cmp cmp) {
    if (!cmp || _this->size < 2) return;
    qsort(_this->buf, _this->size, _this->elem_sz,
          (int (*)(const void *, const void *))cmp);
}

void *array_bsearch(array *_this, void *key, array_cmp cmp) {
    if (!cmp || !_this->size) return NULL;
    return bsearch(key, _this->buf, _this->size, _this->elem_sz,
                   (int (*)(const void *, const void *))cmp);
}

/* ================= ring：SPSC 无锁循环队列（kfifo 风格） ================= */
/* 单生产者单消费者无锁：head（消费者独写）/ tail（生产者独写）自由递增，容量 2 的幂，
 * 槽位下标 = idx & mask，元素数 = tail - head（无符号回绕）。生产者 release 发布 tail、
 * 消费者 acquire 观测 tail → 数据先于索引可见；反向 head 同理释放槽位。
 * 原子读写用 platform.h 的 sc_* 宏（C11 stdatomic / 平台特化）。
 * 仅 SPSC 安全；多生产者/多消费者须外部加锁。init/drop/clear 仅在无并发时调用。 */

static uint32_t ring_pow2(uint32_t n) {
    uint32_t p = 2;                          /* 最小容量 2 */
    while (p < n) {
        if (p & 0x80000000u) return 0x80000000u;  /* 溢出兜底：封顶 2^31 */
        p <<= 1;
    }
    return p;
}

static inline char *ring_slot(ring *r, uint32_t idx) {
    return r->buf + (size_t)(idx & r->mask) * r->elem_sz;
}

uint8_t ring_init(ring *_this, uint32_t elem_sz, uint32_t capacity) {
    uint32_t es = elem_sz ? elem_sz : 1;
    uint32_t cap = ring_pow2(capacity ? capacity : 2);
    _this->head = 0;
    _this->tail = 0;
    _this->elem_sz = es;
    _this->buf = (char *)sc_alloc((uint64_t)cap * es);
    if (!_this->buf) { _this->mask = 0; return 0; }
    _this->mask = cap - 1;
    return 1;
}

void ring_drop(ring *_this) {
    sc_free(_this->buf);
    _this->buf = NULL;
    _this->head = 0;
    _this->tail = 0;
    _this->mask = 0;
}

uint64_t ring_cap(ring *_this) {
    return _this->buf ? (uint64_t)_this->mask + 1 : 0;
}

uint64_t ring_len(ring *_this) {
    uint32_t tail = sc_get_acq(&_this->tail);
    uint32_t head = sc_get_acq(&_this->head);
    return (uint64_t)(tail - head);
}

uint8_t ring_is_empty(ring *_this) {
    return sc_get_acq(&_this->tail) == sc_get_acq(&_this->head);
}

uint8_t ring_is_full(ring *_this) {
    uint32_t tail = sc_get_acq(&_this->tail);
    uint32_t head = sc_get_acq(&_this->head);
    return (tail - head) > _this->mask;      /* count == mask+1 == cap → 满 */
}

void ring_clear(ring *_this) {
    sc_set(&_this->head, 0);
    sc_set(&_this->tail, 0);
}

uint8_t ring_push(ring *_this, void *value) {
    uint32_t tail = sc_get(&_this->tail);        /* 生产者独占 tail：relaxed 读自身 */
    uint32_t head = sc_get_acq(&_this->head);    /* acquire 观测消费者已释放的槽 */
    if ((tail - head) > _this->mask) return 0;   /* 满 */
    if (value) memcpy(ring_slot(_this, tail), value, _this->elem_sz);
    sc_set_rel(&_this->tail, tail + 1);          /* release：数据写入先于 tail 可见 */
    return 1;
}

uint8_t ring_pop(ring *_this, void *out) {
    uint32_t head = sc_get(&_this->head);        /* 消费者独占 head：relaxed 读自身 */
    uint32_t tail = sc_get_acq(&_this->tail);    /* acquire 观测生产者写入的数据 */
    if (tail == head) return 0;                  /* 空 */
    if (out) memcpy(out, ring_slot(_this, head), _this->elem_sz);
    sc_set_rel(&_this->head, head + 1);          /* release：释放该槽供生产者复用 */
    return 1;
}

void *ring_peek(ring *_this) {
    uint32_t head = sc_get(&_this->head);
    uint32_t tail = sc_get_acq(&_this->tail);
    if (tail == head) return NULL;
    return ring_slot(_this, head);
}

/* ---------------- list：段式裸 @ 自动指针容器 ----------------
 * 元素为 sc_afat（裸自动指针 @），list 拥有每元素一份 retain（own=SC_OWN_RAW，
 * 仅经目标 in++/in-- 记账，不挂 out 边）。段式存储：元素 i 住 segs[i/LIST_SEG][i%LIST_SEG]，
 * 段索引表与各段均经 mem chunk/refit/recycle 分配（不受全局 -DSC_POOL 影响）。
 * 句柄在槽间搬移（insert/remove_at/reverse/sort）按裸字节移动——边记在目标/持有者对象上、
 * 不在句柄里，故搬移不改任何计数；仅 push/set/clone 经 bind 增计数，pop/remove_at/set/clear/drop
 * 经 unbind 减计数（触零读句柄自带 dtor 自析构）。 */

#define LIST_SEG 64    /* 每段元素数（64 * sizeof(sc_afat)=64*32=2KiB，命中 mem 尺寸类） */

static inline sc_afat *list_slot(list *l, uint64_t i) {
    return &l->segs[i / LIST_SEG][i % LIST_SEG];
}

/* 确保总容量至少 need 个槽位（按段粒度增长，段内存来自 mem chunk0 清零）。 */
static uint8_t list_grow(list *l, uint64_t need) {
    if (l->cap >= need) return 1;
    uint64_t want = (need + LIST_SEG - 1) / LIST_SEG;   /* 所需段数 */
    sc_afat **t = (sc_afat **)refit(l->segs, want * sizeof(sc_afat *));
    if (!t) return 0;
    l->segs = t;
    while (l->nsegs < want) {
        sc_afat *seg = (sc_afat *)chunk0(LIST_SEG * sizeof(sc_afat));
        if (!seg) return 0;                              /* 部分增长：nsegs/cap 反映已分配段 */
        l->segs[l->nsegs++] = seg;
        l->cap = (uint32_t)((uint64_t)l->nsegs * LIST_SEG);
    }
    return 1;
}

void list_init(list *_this) {
    _this->segs = NULL;
    _this->nsegs = 0;
    _this->size = 0;
    _this->cap = 0;
}

void list_clear(list *_this) {
    for (uint64_t i = 0; i < _this->size; i++)
        sc_afat_unbind(list_slot(_this, i));            /* release 每条 retain */
    _this->size = 0;
}

void list_drop(list *_this) {
    list_clear(_this);
    for (uint32_t s = 0; s < _this->nsegs; s++)
        recycle(_this->segs[s]);
    recycle(_this->segs);
    list_init(_this);
}

uint64_t list_len(list *_this) { return _this->size; }

uint8_t list_reserve(list *_this, uint64_t n) { return list_grow(_this, n); }

uint8_t list_push(list *_this, sc_afat value) {
    if (!list_grow(_this, _this->size + 1)) return 0;
    sc_afat_bind(list_slot(_this, _this->size), value.p,
                 (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);   /* retain：目标 in++ */
    _this->size++;
    return 1;
}

uint8_t list_pop(list *_this) {
    if (!_this->size) return 0;
    sc_afat_unbind(list_slot(_this, --_this->size));    /* release 尾元素 */
    return 1;
}

sc_afat list_get(list *_this, uint64_t index) {
    if (index < _this->size) return *list_slot(_this, index);  /* 借用：原样返回，不改计数 */
    sc_afat empty; empty.p = NULL; empty.tar = NULL; empty.own = NULL; empty.dtor = NULL;
    return empty;
}

uint8_t list_set(list *_this, uint64_t index, sc_afat value) {
    if (index >= _this->size) return 0;
    sc_afat *slot = list_slot(_this, index);
    sc_afat old = *slot;                                /* 复制旧边记录 */
    sc_afat_bind(slot, value.p, (sc_ref *)value.tar,    /* 先 retain 新（防同元素自赋时瞬时归零 UAF） */
                 SC_OWN_RAW, value.dtor);
    sc_afat_unbind(&old);                               /* 再 release 旧 */
    return 1;
}

uint8_t list_insert(list *_this, uint64_t index, sc_afat value) {
    if (index > _this->size) return 0;
    if (!list_grow(_this, _this->size + 1)) return 0;
    for (uint64_t i = _this->size; i > index; i--)      /* 尾→前裸搬移让位（不改计数） */
        *list_slot(_this, i) = *list_slot(_this, i - 1);
    sc_afat_bind(list_slot(_this, index), value.p,      /* retain 新元素 */
                 (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
    _this->size++;
    return 1;
}

uint8_t list_remove_at(list *_this, uint64_t index) {
    if (index >= _this->size) return 0;
    sc_afat_unbind(list_slot(_this, index));            /* release 该元素 */
    for (uint64_t i = index; i + 1 < _this->size; i++)  /* 后续裸搬移前移（不改计数） */
        *list_slot(_this, i) = *list_slot(_this, i + 1);
    _this->size--;
    sc_afat *last = list_slot(_this, _this->size);      /* 清空腾出的尾槽，防重复 unbind */
    last->p = NULL; last->tar = NULL; last->own = NULL; last->dtor = NULL;
    return 1;
}

int64_t list_index_of(list *_this, sc_afat value) {
    for (uint64_t i = 0; i < _this->size; i++)
        if (list_slot(_this, i)->p == value.p) return (int64_t)i;
    return -1;
}

void list_reverse(list *_this) {
    for (uint64_t i = 0, j = _this->size; i + 1 < j; i++, j--) {
        sc_afat t = *list_slot(_this, i);               /* 裸交换句柄（不改计数） */
        *list_slot(_this, i) = *list_slot(_this, j - 1);
        *list_slot(_this, j - 1) = t;
    }
}

uint8_t list_clone(list *_this, list *out) {
    list_clear(out);
    if (!list_grow(out, _this->size)) return 0;
    for (uint64_t i = 0; i < _this->size; i++)
        if (!list_push(out, *list_slot(_this, i))) return 0;  /* 逐元素 retain 到 out */
    return 1;
}

/* 插入排序：元素数通常不大，且保持稳定。cmp 收元素 .p（实体基址）。句柄裸搬移不改计数。 */
void list_sort(list *_this, list_cmp cmp) {
    if (!cmp) return;
    for (uint64_t i = 1; i < _this->size; i++) {
        sc_afat v = *list_slot(_this, i);
        uint64_t j = i;
        while (j > 0 && cmp(list_slot(_this, j - 1)->p, v.p) > 0) {
            *list_slot(_this, j) = *list_slot(_this, j - 1);
            j--;
        }
        *list_slot(_this, j) = v;
    }
}

/* ================= dict：开放寻址裸 @ 自动指针映射 ================= */
/* 控制字节：空 0xFF / 墓碑 0xFE（高位 1）；占用 = hash 低 7 位 0x00..0x7F（高位 0）。 */
#define DICT_EMPTY 0xFFu
#define DICT_TOMB  0xFEu
#define DICT_OCCUPIED(c) (((c) & 0x80u) == 0u)
#define DICT_MIN_BUCKETS 8u

static inline sc_afat *dict_val(dict *d, uint64_t i) {
    return (sc_afat *)(d->slots + i * (uint64_t)d->stride);
}
static inline void *dict_keyslot(dict *d, uint64_t i) {
    return d->slots + i * (uint64_t)d->stride + sizeof(sc_afat);
}
/* 桶 i 处的「逻辑键」指针：定长内联取槽地址；字符串模式取槽内存的 char* 借用 */
static inline const void *dict_logkey(dict *d, uint64_t i) {
    return d->key_size > 0 ? (const void *)dict_keyslot(d, i)
                           : *(const void **)dict_keyslot(d, i);
}

/* ---------------- 可选哈希算法（编译期选择，默认 FNV-1a）----------------
 * 用 -DDICT_HASH=dict_hash_xxx 切换（经 SCC_CFLAGS / cflags 传入），全进程统一。
 * 7 种均为 (const unsigned char *p, size_t n) -> uint64_t，仅低位入桶（表为 2 幂掩码）。
 * 候选：dict_hash_fnv / _ber / _sax / _oat / _jen / _sfh / _mur（见 REFERENCE.md 选型表）。
 * 全部 static inline：未选中者不发出代码、不触发 -Wunused-function。 */

static inline uint64_t dict_hash_fnv(const unsigned char *p, size_t n) {  /* FNV-1a 64 */
    uint64_t h = 1469598103934665603ULL;
    size_t i; for (i = 0; i < n; i++) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
static inline uint64_t dict_hash_ber(const unsigned char *p, size_t n) {  /* Bernstein djb2 */
    uint64_t h = 0;
    size_t i; for (i = 0; i < n; i++) h = h * 33 + p[i];
    return h;
}
static inline uint64_t dict_hash_sax(const unsigned char *p, size_t n) {  /* Shift-Add-XOR */
    uint64_t h = 0;
    size_t i; for (i = 0; i < n; i++) h ^= (h << 5) + (h >> 2) + p[i];
    return h;
}
static inline uint64_t dict_hash_oat(const unsigned char *p, size_t n) {  /* Jenkins one-at-a-time */
    uint32_t h = 0;
    size_t i;
    for (i = 0; i < n; i++) { h += p[i]; h += (h << 10); h ^= (h >> 6); }
    h += (h << 3); h ^= (h >> 11); h += (h << 15);
    return h;
}
static inline uint64_t dict_hash_jen(const unsigned char *p, size_t n) {  /* Jenkins lookup2（uthash JEN） */
    uint32_t a, b, c, k = (uint32_t)n;
    a = b = 0x9e3779b9u; c = 0xfeedbeefu;
#define DICT_JEN_MIX(a,b,c) do {                                       \
        a -= b; a -= c; a ^= (c >> 13); b -= c; b -= a; b ^= (a << 8); \
        c -= a; c -= b; c ^= (b >> 13); a -= b; a -= c; a ^= (c >> 12);\
        b -= c; b -= a; b ^= (a << 16); c -= a; c -= b; c ^= (b >> 5); \
        a -= b; a -= c; a ^= (c >> 3); b -= c; b -= a; b ^= (a << 10); \
        c -= a; c -= b; c ^= (b >> 15);                               \
    } while (0)
    while (k >= 12u) {
        a += p[0] + ((uint32_t)p[1] << 8) + ((uint32_t)p[2] << 16) + ((uint32_t)p[3] << 24);
        b += p[4] + ((uint32_t)p[5] << 8) + ((uint32_t)p[6] << 16) + ((uint32_t)p[7] << 24);
        c += p[8] + ((uint32_t)p[9] << 8) + ((uint32_t)p[10] << 16) + ((uint32_t)p[11] << 24);
        DICT_JEN_MIX(a, b, c);
        p += 12; k -= 12u;
    }
    c += (uint32_t)n;
    switch (k) {
        case 11: c += ((uint32_t)p[10] << 24); /* fallthrough */
        case 10: c += ((uint32_t)p[9] << 16);  /* fallthrough */
        case 9:  c += ((uint32_t)p[8] << 8);   /* fallthrough */
        case 8:  b += ((uint32_t)p[7] << 24);  /* fallthrough */
        case 7:  b += ((uint32_t)p[6] << 16);  /* fallthrough */
        case 6:  b += ((uint32_t)p[5] << 8);   /* fallthrough */
        case 5:  b += p[4];                    /* fallthrough */
        case 4:  a += ((uint32_t)p[3] << 24);  /* fallthrough */
        case 3:  a += ((uint32_t)p[2] << 16);  /* fallthrough */
        case 2:  a += ((uint32_t)p[1] << 8);   /* fallthrough */
        case 1:  a += p[0];                    /* fallthrough */
        default: break;
    }
    DICT_JEN_MIX(a, b, c);
#undef DICT_JEN_MIX
    return c;
}
static inline uint64_t dict_hash_sfh(const unsigned char *p, size_t n) {  /* SuperFastHash（Paul Hsieh） */
    uint32_t h = (uint32_t)n, tmp;
    size_t rem = n & 3u;
    n >>= 2;
#define DICT_SFH_G16(d) ((uint32_t)((d)[0]) + ((uint32_t)((d)[1]) << 8))
    for (; n > 0; n--) {
        h += DICT_SFH_G16(p);
        tmp = (DICT_SFH_G16(p + 2) << 11) ^ h;
        h = (h << 16) ^ tmp;
        p += 4;
        h += (h >> 11);
    }
    switch (rem) {
        case 3: h += DICT_SFH_G16(p); h ^= (h << 16);
                h ^= ((uint32_t)p[2] << 18); h += (h >> 11); break;
        case 2: h += DICT_SFH_G16(p); h ^= (h << 11); h += (h >> 17); break;
        case 1: h += p[0]; h ^= (h << 10); h += (h >> 1); break;
        default: break;
    }
#undef DICT_SFH_G16
    h ^= (h << 3); h += (h >> 5); h ^= (h << 4);
    h += (h >> 17); h ^= (h << 25); h += (h >> 6);
    return h;
}
static inline uint64_t dict_hash_mur(const unsigned char *p, size_t n) {  /* MurmurHash3 x86_32 */
    const uint32_t c1 = 0xcc9e2d51u, c2 = 0x1b873593u;
    uint32_t h = 0, k;
    size_t nb = n / 4, i;
    for (i = 0; i < nb; i++) {
        memcpy(&k, p + i * 4, 4);                  /* memcpy 避免非对齐读 UB */
        k *= c1; k = (k << 15) | (k >> 17); k *= c2;
        h ^= k; h = (h << 13) | (h >> 19); h = h * 5 + 0xe6546b64u;
    }
    k = 0;
    {
        const unsigned char *t = p + nb * 4;
        switch (n & 3u) {
            case 3: k ^= (uint32_t)t[2] << 16; /* fallthrough */
            case 2: k ^= (uint32_t)t[1] << 8;  /* fallthrough */
            case 1: k ^= (uint32_t)t[0];
                    k *= c1; k = (k << 15) | (k >> 17); k *= c2; h ^= k; break;
            default: break;
        }
    }
    h ^= (uint32_t)n;
    h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
    return h;
}

#ifndef DICT_HASH
#define DICT_HASH dict_hash_fnv                    /* 默认 FNV-1a：小巧、短键分布稳 */
#endif

static uint64_t dict_hash(const dict *d, const void *key) {
    const unsigned char *p = (const unsigned char *)key;
    size_t n = d->key_size > 0 ? (size_t)d->key_size
                               : (key ? strlen((const char *)key) : 0);
    return DICT_HASH(p, n);                         /* 编译期定死，直接内联，无间接调用 */
}


static int dict_keyeq(dict *d, uint64_t i, const void *key) {
    if (d->key_size > 0)
        return memcmp(dict_keyslot(d, i), key, (size_t)d->key_size) == 0;
    {
        const char *stored = *(const char **)dict_keyslot(d, i);
        const char *probe = (const char *)key;
        if (stored == probe) return 1;
        if (!stored || !probe) return 0;
        return strcmp(stored, probe) == 0;
    }
}

/* 查找已存在键，返回桶下标；未命中 -1 */
static int64_t dict_find(dict *d, const void *key) {
    uint64_t h, mask, i;
    uint8_t h7;
    uint32_t probes;
    if (!d->nbuckets) return -1;
    h = dict_hash(d, key);
    h7 = (uint8_t)(h & 0x7Fu);
    mask = d->nbuckets - 1;
    i = h & mask;
    for (probes = 0; probes < d->nbuckets; probes++) {
        uint8_t c = d->ctrl[i];
        if (c == DICT_EMPTY) return -1;            /* 探测链遇空即止 */
        if (c == h7 && dict_keyeq(d, i, key)) return (int64_t)i;
        i = (i + 1) & mask;                        /* 墓碑跳过继续 */
    }
    return -1;
}

/* 为插入定位：命中已存在键则 *found=1 返其桶；否则 *found=0 返首个可用（墓碑优先于空尾）。表满 -1。 */
static int64_t dict_find_slot(dict *d, const void *key, int *found) {
    uint64_t h = dict_hash(d, key), mask = d->nbuckets - 1, i = h & mask;
    uint8_t h7 = (uint8_t)(h & 0x7Fu);
    int64_t tomb = -1;
    uint32_t probes;
    for (probes = 0; probes < d->nbuckets; probes++) {
        uint8_t c = d->ctrl[i];
        if (c == DICT_EMPTY) { *found = 0; return tomb >= 0 ? tomb : (int64_t)i; }
        if (c == DICT_TOMB) { if (tomb < 0) tomb = (int64_t)i; }
        else if (c == h7 && dict_keyeq(d, i, key)) { *found = 1; return (int64_t)i; }
        i = (i + 1) & mask;
    }
    *found = 0;
    return tomb;                                    /* 无空桶；有墓碑可复用，否则 -1 */
}

/* 重建为 newcap 桶（2 的幂）：占用项整体搬移（裸字节移动句柄，不改 retain），清除墓碑 */
static uint8_t dict_rehash(dict *d, uint32_t newcap) {
    uint8_t *nctrl = (uint8_t *)chunk(newcap);
    char *nslots;
    uint8_t *octrl = d->ctrl;
    char *oslots = d->slots;
    uint32_t ocap = d->nbuckets, k;
    uint64_t mask = newcap - 1;
    if (!nctrl) return 0;
    nslots = (char *)chunk((uint64_t)newcap * d->stride);
    if (!nslots) { recycle(nctrl); return 0; }
    memset(nctrl, DICT_EMPTY, newcap);
    for (k = 0; k < ocap; k++) {
        uint64_t h, i;
        const void *lk;
        if (!DICT_OCCUPIED(octrl[k])) continue;
        lk = d->key_size > 0
                 ? (const void *)(oslots + (uint64_t)k * d->stride + sizeof(sc_afat))
                 : *(const void **)(oslots + (uint64_t)k * d->stride + sizeof(sc_afat));
        h = dict_hash(d, lk);
        i = h & mask;
        while (nctrl[i] != DICT_EMPTY) i = (i + 1) & mask;
        nctrl[i] = (uint8_t)(h & 0x7Fu);
        memcpy(nslots + i * (uint64_t)d->stride,
               oslots + (uint64_t)k * d->stride, d->stride);
    }
    recycle(octrl);
    recycle(oslots);
    d->ctrl = nctrl;
    d->slots = nslots;
    d->nbuckets = newcap;
    d->used = d->size;                              /* 墓碑已清 */
    return 1;
}

/* 负载因子 7/8（按 used 含墓碑判定）；超阈值则按 size 决定扩容或同尺寸清墓碑 */
static uint8_t dict_ensure(dict *d) {
    if (d->nbuckets == 0) return dict_rehash(d, DICT_MIN_BUCKETS);
    if ((uint64_t)(d->used + 1) * 8 >= (uint64_t)d->nbuckets * 7) {
        uint32_t newcap = d->nbuckets;
        if ((uint64_t)(d->size + 1) * 8 >= (uint64_t)d->nbuckets * 7) newcap *= 2;
        return dict_rehash(d, newcap);
    }
    return 1;
}

static uint8_t dict_storekey(dict *d, uint64_t i, const void *key) {
    if (d->key_size > 0) {
        memcpy(dict_keyslot(d, i), key, (size_t)d->key_size);
        return 1;
    }
    if (d->key_size == 0) {
        *(const char **)dict_keyslot(d, i) = (const char *)key;   /* 借用，不拥有 */
        return 1;
    }
    {                                               /* key_size == -1：拷贝一份 */
        const char *s = (const char *)key;
        size_t n = s ? strlen(s) : 0;
        char *cp = (char *)chunk(n + 1);
        if (!cp) return 0;
        if (n) memcpy(cp, s, n);
        cp[n] = 0;
        *(char **)dict_keyslot(d, i) = cp;
        return 1;
    }
}
static inline void dict_freekey(dict *d, uint64_t i) {
    if (d->key_size == -1) recycle(*(char **)dict_keyslot(d, i));
}

void dict_init(dict *_this, int32_t key_size) {
    uint32_t keylen = key_size > 0 ? (uint32_t)key_size : (uint32_t)sizeof(char *);
    uint32_t st = (uint32_t)sizeof(sc_afat) + keylen;
    _this->ctrl = NULL;
    _this->slots = NULL;
    _this->key_size = key_size;
    _this->stride = (st + 7u) & ~7u;                /* align8 */
    _this->size = 0;
    _this->used = 0;
    _this->nbuckets = 0;
}

void dict_clear(dict *_this) {
    uint32_t i;
    if (!_this->nbuckets) return;
    for (i = 0; i < _this->nbuckets; i++) {
        if (DICT_OCCUPIED(_this->ctrl[i])) {
            sc_afat_unbind(dict_val(_this, i));      /* release value */
            dict_freekey(_this, i);                  /* -1 模式回收 key 拷贝 */
        }
        _this->ctrl[i] = DICT_EMPTY;
    }
    _this->size = 0;
    _this->used = 0;
}

void dict_drop(dict *_this) {
    dict_clear(_this);
    recycle(_this->ctrl);
    recycle(_this->slots);
    _this->ctrl = NULL;
    _this->slots = NULL;
    _this->nbuckets = 0;                             /* 保留 key_size/stride 以便复用 */
}

uint64_t dict_len(dict *_this) { return _this->size; }

uint8_t dict_has(dict *_this, const void *key) { return dict_find(_this, key) >= 0; }

sc_afat dict_get(dict *_this, const void *key) {
    int64_t i = dict_find(_this, key);
    if (i >= 0) return *dict_val(_this, (uint64_t)i);
    { sc_afat e; e.p = NULL; e.tar = NULL; e.own = NULL; e.dtor = NULL; return e; }
}

uint8_t dict_put(dict *_this, const void *key, sc_afat value) {
    int found;
    int64_t i;
    sc_afat *slot;
    uint8_t wastomb;
    uint64_t h;
    if (!dict_ensure(_this)) return 0;
    i = dict_find_slot(_this, key, &found);
    if (i < 0) {                                     /* 全满（理论不达，扩容兜底） */
        if (!dict_rehash(_this, _this->nbuckets * 2)) return 0;
        i = dict_find_slot(_this, key, &found);
        if (i < 0) return 0;
    }
    slot = dict_val(_this, (uint64_t)i);
    if (found) {                                     /* 替换：先 retain 新、再 release 旧（防自赋 UAF） */
        sc_afat old = *slot;
        sc_afat_bind(slot, value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
        sc_afat_unbind(&old);
        return 1;
    }
    wastomb = (_this->ctrl[i] == DICT_TOMB);
    if (!dict_storekey(_this, (uint64_t)i, key)) return 0;
    sc_afat_bind(slot, value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
    h = dict_hash(_this, key);
    _this->ctrl[i] = (uint8_t)(h & 0x7Fu);
    _this->size++;
    if (!wastomb) _this->used++;                     /* 复用墓碑不增 used */
    return 1;
}

uint8_t dict_remove(dict *_this, const void *key) {
    int64_t i = dict_find(_this, key);
    if (i < 0) return 0;
    sc_afat_unbind(dict_val(_this, (uint64_t)i));    /* release value */
    dict_freekey(_this, (uint64_t)i);
    _this->ctrl[i] = DICT_TOMB;                      /* 留墓碑保探测链；rehash 时清理 */
    _this->size--;
    return 1;
}

void dict_each(dict *_this, dict_each_fn fn, void *ctx) {
    uint32_t i;
    if (!fn || !_this->nbuckets) return;
    for (i = 0; i < _this->nbuckets; i++) {
        if (!DICT_OCCUPIED(_this->ctrl[i])) continue;
        if (!fn(dict_logkey(_this, i), *dict_val(_this, i), ctx)) return;
    }
}

int64_t dict_first(dict *_this) {
    uint32_t i;
    for (i = 0; i < _this->nbuckets; i++)
        if (DICT_OCCUPIED(_this->ctrl[i])) return (int64_t)i;
    return -1;
}
int64_t dict_last(dict *_this) {
    uint32_t i;
    for (i = _this->nbuckets; i-- > 0; )
        if (DICT_OCCUPIED(_this->ctrl[i])) return (int64_t)i;
    return -1;
}
int64_t dict_next(dict *_this, int64_t cur) {
    int64_t i;
    for (i = cur + 1; i < (int64_t)_this->nbuckets; i++)
        if (DICT_OCCUPIED(_this->ctrl[i])) return i;
    return -1;
}
int64_t dict_prev(dict *_this, int64_t cur) {
    int64_t i, top = cur < (int64_t)_this->nbuckets ? cur : (int64_t)_this->nbuckets;
    for (i = top - 1; i >= 0; i--)
        if (DICT_OCCUPIED(_this->ctrl[i])) return i;
    return -1;
}
const void *dict_key_at(dict *_this, int64_t cur) {
    if (cur < 0 || (uint64_t)cur >= _this->nbuckets || !DICT_OCCUPIED(_this->ctrl[cur]))
        return NULL;
    return dict_logkey(_this, (uint64_t)cur);
}
sc_afat dict_value_at(dict *_this, int64_t cur) {
    if (cur < 0 || (uint64_t)cur >= _this->nbuckets || !DICT_OCCUPIED(_this->ctrl[cur])) {
        sc_afat e; e.p = NULL; e.tar = NULL; e.own = NULL; e.dtor = NULL; return e;
    }
    return *dict_val(_this, (uint64_t)cur);
}

/* ================= bst：AVL/红黑 融合的有序映射 =================
 * 设计：单棵树用 red_depth 区分 AVL(0) 与红黑(1)——二者本质是「容忍不平衡的深度」不同
 *   （AVL 最多差 1 层，红黑多容忍 1 层）。节点 balance 字段在 AVL 下记 -1/0/1 倾斜，
 *   在红黑下记 'B'/'R' 颜色。weight_l = 左子树权重（含自身 1），用于 index<->entry 双向换算。
 * 对齐安全：节点采用「内部父指针」设计（每节点 parent 字段、自然对齐），不使用任何 pack(1)
 *   外置检索栈——这正是原型在严格对齐核（如 TI）上崩溃、而本实现规避的根因；数值键比较一律
 *   走 memcpy 装载，杜绝非对齐强转 UB。
 * 值为裸自动指针 @（sc_afat），bst 拥有每节点一份 retain；key 三态同 dict。
 * 中序双向链表（prev/next）维护 first/last/next/prev 与顺序遍历，O(1) 取前驱后继。 */

typedef struct bst_node bst_node;
struct bst_node {
    sc_afat   value;     /* 裸 @ 值（32B，置首保 8 对齐） */
    union {              /* key 存储三态共用 8B（与 left/right 同 cache line） */
        const void *kptr;    /* 字符串模式：char*（借用或拷贝）；key_size>8：指向尾随存储 */
        char        kbuf[8]; /* 数值模式 key_size<=8：内联键（查找下降仅触一条 cache line） */
    } k;
    bst_node *parent;    /* 父节点（根节点为 NULL） */
    bst_node *left;
    bst_node *right;
    bst_node *prev;      /* 中序前驱 */
    bst_node *next;      /* 中序后继 */
    uint32_t  weight_l;  /* 左子树权重（含自身 1） */
    int8_t    balance;   /* AVL: -1/0/1；红黑: 'B'/'R' */
};

/* 键装载点（logkey）：key_size<=8 数值内联于 k.kbuf（首 cache line，与 left/right 同行）；
   key_size>8 尾随节点（n+1，自然 8 对齐）；字符串模式取 k.kptr。 */
static inline const void *bst_logkey(bst *t, bst_node *n) {
    if (t->key_size > 8) return (const void *)(n + 1);
    if (t->key_size > 0) return n->k.kbuf;
    return n->k.kptr;
}

/* 比较：返回 sign(probe - node_key)。cmp 非空走自定义；否则内置数值（有符号，按宽度装载）/字符串。 */
static int bst_cmp_key(bst *t, const void *probe, bst_node *n) {
    const void *nk = bst_logkey(t, n);
    if (t->cmp) return t->cmp(probe, nk, t->cmp_ctx);
    if (t->key_size > 0) {
        switch (t->key_size) {
            case 1: { int8_t a = *(const int8_t *)probe, b = *(const int8_t *)nk;
                      return a < b ? -1 : a > b ? 1 : 0; }
            case 2: { int16_t a, b; memcpy(&a, probe, 2); memcpy(&b, nk, 2);
                      return a < b ? -1 : a > b ? 1 : 0; }
            case 4: { int32_t a, b; memcpy(&a, probe, 4); memcpy(&b, nk, 4);
                      return a < b ? -1 : a > b ? 1 : 0; }
            case 8: { int64_t a, b; memcpy(&a, probe, 8); memcpy(&b, nk, 8);
                      return a < b ? -1 : a > b ? 1 : 0; }
            default: return memcmp(probe, nk, (size_t)t->key_size);  /* 非 1/2/4/8 宽度退化为字节序 */
        }
    }
    { const char *a = (const char *)probe, *b = (const char *)nk;
      if (a == b) return 0; if (!a) return -1; if (!b) return 1; return strcmp(a, b); }
}

static bst_node *bst_node_new(bst *t) {
    size_t extra = t->key_size > 8 ? (size_t)t->key_size : 0;   /* 仅 key_size>8 才尾随分配 */
    return (bst_node *)chunk(sizeof(bst_node) + extra);
}
static uint8_t bst_storekey(bst *t, bst_node *n, const void *key) {
    if (t->key_size > 8) { memcpy((char *)(n + 1), key, (size_t)t->key_size); return 1; }  /* 大数值键尾随 */
    if (t->key_size > 0) { memcpy(n->k.kbuf, key, (size_t)t->key_size); return 1; }          /* 小数值键内联 */
    if (t->key_size == 0) { n->k.kptr = (const char *)key; return 1; }   /* 借用 */
    { const char *s = (const char *)key; size_t len = s ? strlen(s) : 0;
      char *cp = (char *)chunk(len + 1); if (!cp) return 0;
      if (len) memcpy(cp, s, len); cp[len] = 0; n->k.kptr = cp; return 1; }
}
static inline void bst_freekey(bst *t, bst_node *n) {
    if (t->key_size == -1) recycle((void *)n->k.kptr);
}

/* ---------------- 旋转（内部父指针版；同时维护 weight_l） ---------------- */
static inline void bst_rr1(bst_node *root, bst_node *left) {       /* 单层右旋 */
    bst_node *n;
    if ((n = left->right)) n->parent = root;
    root->left = n;
    root->parent = left;
    left->right = root;
    root->weight_l -= left->weight_l;
}
static inline void bst_rr2(bst_node *root, bst_node *left, bst_node *lr) {  /* 双层右旋 */
    bst_node *n;
    if ((n = lr->left)) n->parent = left;
    left->right = n;
    left->parent = lr;
    lr->left = left;
    if ((n = lr->right)) n->parent = root;
    root->left = n;
    root->parent = lr;
    lr->right = root;
    lr->weight_l += left->weight_l;
    root->weight_l -= lr->weight_l;
}
static inline void bst_lr1(bst_node *root, bst_node *right) {      /* 单层左旋 */
    bst_node *n;
    if ((n = right->left)) n->parent = root;
    root->right = n;
    root->parent = right;
    right->left = root;
    right->weight_l += root->weight_l;
}
static inline void bst_lr2(bst_node *root, bst_node *right, bst_node *rl) {  /* 双层左旋 */
    bst_node *n;
    if ((n = rl->right)) n->parent = right;
    right->left = n;
    right->parent = rl;
    rl->right = right;
    if ((n = rl->left)) n->parent = root;
    root->right = n;
    root->parent = rl;
    rl->left = root;
    right->weight_l -= rl->weight_l;
    rl->weight_l += root->weight_l;
}

/* ---------------- AVL 插入维护 ---------------- */
static void bst_avl_inserted(bst *t, bst_node *node, bst_node *parent, bst_node *mbs_parent) {
    bst_node *mbs_root, *up, *newroot;
    int8_t inc;

    /* 根整体平衡：新节点令全树高度 +1，沿途记倾斜 + 维护 weight */
    if (!mbs_parent) {
        do {
            if (parent->left == node) { ++parent->weight_l; parent->balance = -1; }
            else parent->balance = 1;
            node = parent;
        } while ((parent = node->parent) != NULL);
        return;
    }
    /* 维护最大平衡子树（MBS）路径上各节点的平衡因子 + weight */
    while (parent != mbs_parent) {
        if (parent->left == node) { ++parent->weight_l; parent->balance = -1; }
        else parent->balance = 1;
        node = parent; parent = node->parent;
    }
    mbs_root = node;
    /* MBS 之上仅维护 weight（结构不变） */
    do {
        if (parent->left == node) ++parent->weight_l;
        node = parent;
    } while ((parent = node->parent) != NULL);

    /* 重建 MBS 子树平衡 */
    inc = mbs_parent->left == mbs_root ? -1 : 1;
    if (inc != mbs_parent->balance) { mbs_parent->balance = 0; return; }
    up = mbs_parent->parent;
    if (inc == mbs_root->balance) {                /* 单旋 */
        newroot = mbs_root;
        if (inc == -1) bst_rr1(mbs_parent, mbs_root); else bst_lr1(mbs_parent, mbs_root);
        mbs_parent->balance = mbs_root->balance = 0;
    } else {                                       /* 双旋 */
        if (inc == -1) bst_rr2(mbs_parent, mbs_root, newroot = mbs_root->right);
        else           bst_lr2(mbs_parent, mbs_root, newroot = mbs_root->left);
        if (newroot->balance == inc)       { mbs_parent->balance = (int8_t)-inc; mbs_root->balance = 0; }
        else if (newroot->balance == -inc) { mbs_parent->balance = 0; mbs_root->balance = inc; }
        else                                 mbs_parent->balance = mbs_root->balance = 0;
        newroot->balance = 0;
    }
    if (!up) { newroot->parent = NULL; t->root = newroot; }
    else {
        newroot->parent = up;
        if (up->left == mbs_parent) up->left = newroot; else up->right = newroot;
    }
}

/* ---------------- 红黑 插入维护 ---------------- */
static void bst_rb_inserted(bst *t, bst_node *node, bst_node *parent) {
    bst_node *root = t->root, *sup, *uncle, *rotate_parent;
    int8_t pinc, sinc;
    for (;;) {
        if (parent->balance == 'B') break;
        if (parent->left == node) { pinc = -1; ++parent->weight_l; } else pinc = 1;
        sup = parent->parent;
        if (sup->left == parent) { uncle = sup->right; sinc = -1; ++sup->weight_l; }
        else                     { uncle = sup->left;  sinc = 1; }

        if (uncle && uncle->balance == 'R') {       /* 叔红：变色上浮 */
            parent->balance = uncle->balance = 'B';
            if (sup == root) return;
            sup->balance = 'R';
            node = sup; parent = sup->parent;
            continue;
        }
        rotate_parent = sup->parent;
        if (pinc == sinc) {                         /* 同侧：单旋 */
            parent->balance = 'B'; sup->balance = 'R';
            if (sinc < 0) bst_rr1(sup, parent); else bst_lr1(sup, parent);
            node = parent;
        } else {                                    /* 异侧：双旋 */
            node->balance = 'B'; parent->balance = sup->balance = 'R';
            if (sinc < 0) bst_rr2(sup, parent, node); else bst_lr2(sup, parent, node);
        }
        node->parent = rotate_parent;
        if (!rotate_parent) { t->root = node; return; }
        if (rotate_parent->left == sup) rotate_parent->left = node; else rotate_parent->right = node;
        parent = rotate_parent;
        break;
    }
    /* 维护剩余路径的 weight 直至根 */
    for (;;) {
        if (parent->left == node) ++parent->weight_l;
        if (parent == t->root) break;
        parent = (node = parent)->parent;
    }
}

/* ---------------- AVL 删除维护 ---------------- */
static void bst_avl_removed(bst *t, bst_node *parent, int8_t inc) {
    bst_node *rparent, *rroot, *decline;
    for (;;) {
        if (parent->balance == 0) { parent->balance = (int8_t)-inc; return; }
        if (parent->balance == inc) {               /* 恢复平衡，递归向上 */
            parent->balance = 0;
            decline = parent;
            if (!(parent = decline->parent)) return;
            inc = (decline == parent->left) ? -1 : 1;
            continue;
        }
        decline = (inc == 1) ? parent->left : parent->right;
        rparent = parent->parent;
        if (decline->balance == 0) {                /* 旋转后高度不变 */
            decline->parent = rparent;
            if (inc == -1) bst_lr1(parent, decline); else bst_rr1(parent, decline);
            decline->balance = inc;
            if (!rparent) t->root = decline;
            else if (rparent->left == parent) rparent->left = decline; else rparent->right = decline;
            return;
        }
        if (decline->balance == parent->balance) {  /* 单旋 */
            rroot = decline;
            rroot->parent = rparent;
            if (inc == -1) bst_lr1(parent, rroot); else bst_rr1(parent, rroot);
            rroot->balance = parent->balance = 0;
        } else {                                    /* 双旋 */
            if (inc == -1) bst_lr2(parent, decline, rroot = decline->left);
            else           bst_rr2(parent, decline, rroot = decline->right);
            rroot->parent = rparent;
            if (rroot->balance == inc)       { decline->balance = (int8_t)-inc; parent->balance = 0; }
            else if (rroot->balance == -inc) { decline->balance = 0; parent->balance = inc; }
            else                               decline->balance = parent->balance = 0;
            rroot->balance = 0;
        }
        if (!rparent) { t->root = rroot; return; }
        if (rparent->left == parent) { rparent->left = rroot; inc = -1; }
        else                         { rparent->right = rroot; inc = 1; }
        parent = rparent;
    }
}

/* ---------------- 红黑 删除维护（仅删黑节点时调用） ---------------- */
static void bst_rb_removed(bst *t, bst_node *parent, int8_t inc) {
    bst_node *sup, *sibling, *sub;
    for (;;) {
        sibling = inc < 0 ? parent->right : parent->left;
        if (sibling->balance == 'R') {              /* 兄红：先旋转转化 */
            sup = parent->parent;
            if (!sup) t->root = sibling;
            else if (sup->left == parent) sup->left = sibling; else sup->right = sibling;
            sibling->parent = sup;
            sibling->balance = 'B'; parent->balance = 'R';
            if (inc < 0) { bst_lr1(parent, sibling); sibling = parent->right; }
            else         { bst_rr1(parent, sibling); sibling = parent->left; }
        }
        for (;;) {
            sup = parent->parent;                   /* 旋转会改写 parent->parent，先存 */
            if (inc < 0) {
                if ((sub = sibling->right) && sub->balance == 'R') {
                    sibling->balance = parent->balance; parent->balance = 'B'; sub->balance = 'B';
                    bst_lr1(parent, sibling);
                } else if ((sub = sibling->left) && sub->balance == 'R') {
                    sub->balance = parent->balance; sibling->balance = 'B'; parent->balance = 'B';
                    bst_lr2(parent, sibling, sub); sibling = sub;
                } else break;
            } else {
                if ((sub = sibling->left) && sub->balance == 'R') {
                    sibling->balance = parent->balance; sub->balance = 'B'; parent->balance = 'B';
                    bst_rr1(parent, sibling);
                } else if ((sub = sibling->right) && sub->balance == 'R') {
                    sub->balance = parent->balance; sibling->balance = parent->balance = 'B';
                    bst_rr2(parent, sibling, sub); sibling = sub;
                } else break;
            }
            if (!sup) t->root = sibling;
            else if (sup->left == parent) sup->left = sibling; else sup->right = sibling;
            sibling->parent = sup;
            return;
        }
        sibling->balance = 'R';                      /* 兄两子皆黑：同删，递归向上 */
        if (parent->balance == 'R') { parent->balance = 'B'; return; }
        if (!(sup = parent->parent)) return;
        inc = sup->left == parent ? -1 : 1;
        parent = sup;
    }
}

/* ---------------- 公共接口 ---------------- */
void bst_init(bst *_this, uint8_t red_depth, int32_t key_size, bst_cmp cmp, void *cmp_ctx) {
    _this->root = _this->head = _this->rear = NULL;
    _this->cmp = cmp;
    _this->cmp_ctx = cmp_ctx;
    _this->size = 0;
    _this->key_size = key_size;
    _this->red_depth = red_depth;
}

void bst_clear(bst *_this) {
    bst_node *n = (bst_node *)_this->head, *nx;
    while (n) {
        nx = n->next;
        sc_afat_unbind(&n->value);                  /* release value */
        bst_freekey(_this, n);                       /* -1 模式回收 key 拷贝 */
        recycle(n);
        n = nx;
    }
    _this->root = _this->head = _this->rear = NULL;
    _this->size = 0;
}

void bst_drop(bst *_this) { bst_clear(_this); }      /* 内部父指针设计无外置缓冲，drop 同 clear */

uint64_t bst_len(bst *_this) { return _this->size; }

uint8_t bst_has(bst *_this, const void *key) {
    bst_node *n = (bst_node *)_this->root;
    while (n) { int c = bst_cmp_key(_this, key, n); if (c == 0) return 1; n = c < 0 ? n->left : n->right; }
    return 0;
}

sc_afat bst_get(bst *_this, const void *key) {
    bst_node *n = (bst_node *)_this->root;
    while (n) {
        int c = bst_cmp_key(_this, key, n);
        if (c == 0) return n->value;
        n = c < 0 ? n->left : n->right;
    }
    { sc_afat e; e.p = NULL; e.tar = NULL; e.own = NULL; e.dtor = NULL; return e; }
}

uint8_t bst_put(bst *_this, const void *key, sc_afat value) {
    bst_node *nn, *parent, *child, *mbs_parent = NULL;
    int c;

    if (!_this->root) {                             /* 空树：建根 */
        if (!(nn = bst_node_new(_this))) return 0;
        if (!bst_storekey(_this, nn, key)) { recycle(nn); return 0; }
        nn->left = nn->right = nn->prev = nn->next = NULL;
        nn->parent = NULL;
        nn->weight_l = 1;
        nn->balance = _this->red_depth ? 'B' : 0;
        sc_afat_bind(&nn->value, value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
        _this->root = _this->head = _this->rear = nn;
        _this->size++;
        return 1;
    }

    parent = (bst_node *)_this->root;
    for (;;) {
        c = bst_cmp_key(_this, key, parent);
        if (c == 0) {                               /* 键已存在：替换值（先 retain 新、再 release 旧） */
            sc_afat old = parent->value;
            sc_afat_bind(&parent->value, value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
            sc_afat_unbind(&old);
            return 1;
        }
        if (!_this->red_depth && parent->balance) mbs_parent = parent;  /* AVL：记最后失衡子树父 */
        if (c < 0) {
            if (!(child = parent->left)) {
                if (!(nn = bst_node_new(_this))) return 0;
                if (!bst_storekey(_this, nn, key)) { recycle(nn); return 0; }
                parent->left = nn;
                nn->next = parent;                  /* 中序链表插入 */
                if ((child = parent->prev)) child->next = nn; else _this->head = nn;
                nn->prev = child;
                parent->prev = nn;
                break;
            }
        } else {
            if (!(child = parent->right)) {
                if (!(nn = bst_node_new(_this))) return 0;
                if (!bst_storekey(_this, nn, key)) { recycle(nn); return 0; }
                parent->right = nn;
                nn->prev = parent;
                if ((child = parent->next)) child->prev = nn; else _this->rear = nn;
                nn->next = child;
                parent->next = nn;
                break;
            }
        }
        parent = child;
    }
    nn->left = nn->right = NULL;
    nn->weight_l = 1;
    nn->parent = parent;
    nn->balance = _this->red_depth ? 'R' : 0;        /* 红黑新节点默认红 */
    sc_afat_bind(&nn->value, value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);
    if (_this->red_depth) bst_rb_inserted(_this, nn, parent);
    else                  bst_avl_inserted(_this, nn, parent, mbs_parent);
    _this->size++;
    return 1;
}

uint8_t bst_remove(bst *_this, const void *key) {
    bst_node *node, *child, *parent;
    int8_t removed_balance, inc;

    if (!(node = (bst_node *)_this->root)) return 0;

    /* 二分查找；左降途中预减 weight_l（预期从左子树删除） */
    for (;;) {
        int c = bst_cmp_key(_this, key, node);
        if (c == 0) break;
        if (c > 0) child = node->right;
        else if ((child = node->left)) --node->weight_l;
        if (!child) {                               /* 未命中：回滚 weight */
            parent = node->parent;
            while (parent) { if (node == parent->left) ++parent->weight_l; parent = (node = parent)->parent; }
            return 0;
        }
        node = child;
    }

    /* 命中 node：先处置其值/键（逻辑删除） */
    sc_afat_unbind(&node->value);
    bst_freekey(_this, node);

    /* 双子：逻辑变换为删前驱（前驱 = 左子树最右节点，O(1) 经 prev 取） */
    if (node->left && node->right) {
        child = node->prev;
        --node->weight_l;                            /* 前驱在 node 左子树，仅此一处需调 */
        node->value = child->value;                  /* 裸搬移（所有权转移，不改计数） */
        if (_this->key_size > 8) memcpy((char *)(node + 1), (const char *)(child + 1), (size_t)_this->key_size);
        else if (_this->key_size > 0) memcpy(node->k.kbuf, child->k.kbuf, (size_t)_this->key_size);
        else node->k.kptr = child->k.kptr;           /* 移交字符串指针（-1 模式所有权随之转移） */
        node = child;                                /* 实际物理删除前驱 */
    }

    /* 中序链表摘除 */
    child = node->prev; parent = node->next;
    if (child) child->next = parent; else _this->head = parent;
    if (parent) parent->prev = child; else _this->rear = child;

    /* 树结构摘除 */
    parent = node->parent;
    if ((child = node->left) || (child = node->right)) child->parent = parent;
    removed_balance = node->balance;
    recycle(node);                                   /* 物理回收（值/键已处置或已移出） */
    _this->size--;

    if (!parent) {                                   /* 删的是根 */
        _this->root = child;
        if (child && child->balance == 'R') child->balance = 'B';
        return 1;
    }

    if (node == parent->left) { parent->left = child; inc = -1; }
    else                      { parent->right = child; inc = 1; }

    if (!_this->red_depth) bst_avl_removed(_this, parent, inc);
    else if (removed_balance == 'B') {               /* 红黑：仅删黑需重建 */
        if (child && child->balance == 'R') child->balance = 'B';
        else bst_rb_removed(_this, parent, inc);
    }
    return 1;
}

void bst_each(bst *_this, bst_each_fn fn, void *ctx) {
    bst_node *n;
    if (!fn) return;
    for (n = (bst_node *)_this->head; n; n = n->next)
        if (!fn(bst_logkey(_this, n), n->value, ctx)) return;
}

/* 游标 = 节点指针（不透明 token，0 = 无）；遍历期间勿增删（增删可能回收节点使其失效）。 */
int64_t bst_first(bst *_this) { return (int64_t)(intptr_t)_this->head; }
int64_t bst_last(bst *_this)  { return (int64_t)(intptr_t)_this->rear; }
int64_t bst_next(bst *_this, int64_t cur) {
    bst_node *n = (bst_node *)(intptr_t)cur; (void)_this;
    return n ? (int64_t)(intptr_t)n->next : 0;
}
int64_t bst_prev(bst *_this, int64_t cur) {
    bst_node *n = (bst_node *)(intptr_t)cur; (void)_this;
    return n ? (int64_t)(intptr_t)n->prev : 0;
}
const void *bst_key_at(bst *_this, int64_t cur) {
    bst_node *n = (bst_node *)(intptr_t)cur;
    return n ? bst_logkey(_this, n) : NULL;
}
sc_afat bst_value_at(bst *_this, int64_t cur) {
    bst_node *n = (bst_node *)(intptr_t)cur; (void)_this;
    if (n) return n->value;
    { sc_afat e; e.p = NULL; e.tar = NULL; e.own = NULL; e.dtor = NULL; return e; }
}

/* key -> 0 基中序序号；未命中返回 -1 */
int64_t bst_index_of(bst *_this, const void *key) {
    bst_node *n = (bst_node *)_this->root;
    int64_t i;
    if (!n) return -1;
    i = (int64_t)n->weight_l;
    for (;;) {
        int c = bst_cmp_key(_this, key, n);
        if (c == 0) return i - 1;
        if (c < 0) { if (!n->left) return -1; i = i - (int64_t)n->weight_l + (int64_t)n->left->weight_l; n = n->left; }
        else       { if (!n->right) return -1; i += (int64_t)n->right->weight_l; n = n->right; }
    }
}

/* 0 基中序序号 -> 游标；越界返回 0 */
int64_t bst_at(bst *_this, uint64_t index) {
    bst_node *n = (bst_node *)_this->root;
    int64_t i;
    if (!n) return 0;
    i = (int64_t)index + 1;
    for (;;) {
        int64_t w = (int64_t)n->weight_l;
        if (i == w) return (int64_t)(intptr_t)n;
        if (i < w) n = n->left; else { i -= w; n = n->right; }
        if (!n) return 0;
    }
}

/* 最接近且 <= key 的项（前驱或等于）；无返回 0 */
int64_t bst_most(bst *_this, const void *key) {
    bst_node *n = (bst_node *)_this->root, *cand = NULL;
    while (n) {
        int c = bst_cmp_key(_this, key, n);
        if (c == 0) { cand = n; break; }
        if (c > 0) { cand = n; n = n->right; } else n = n->left;
    }
    return (int64_t)(intptr_t)cand;
}

/* 最接近且 >= key 的项（后继或等于）；无返回 0 */
int64_t bst_least(bst *_this, const void *key) {
    bst_node *n = (bst_node *)_this->root, *cand = NULL;
    while (n) {
        int c = bst_cmp_key(_this, key, n);
        if (c == 0) { cand = n; break; }
        if (c < 0) { cand = n; n = n->left; } else n = n->right;
    }
    return (int64_t)(intptr_t)cand;
}

/* ==================================================================== */
/* heap —— 数组背二叉堆 / 优先队列                                       */
/* 槽数组连续存放（cap * stride，每槽 [sc_afat value][key]，value 在前保 8 对齐）。
 * 完全二叉树隐式编码：节点 i 的父 = (i-1)/2，左右子 = 2i+1 / 2i+2。
 * push 末尾追加后上滤（sift-up），pop 末尾补根后下滤（sift-down）；均 O(log n)。
 * min 决定堆向：min=1 小键在顶（最小堆），min=0 大键在顶（最大堆）。
 * key 三态/比较器同 bst：数值按宽度有符号、字符串 strcmp、cmp 非空走自定义。
 * value 拥有（每元素一份 retain，SC_OWN_RAW）；取出语义「取用分离」：
 *   peek 借用堆顶（不改计数），pop 删除并 release（返回 bool）。
 * 不提供遍历游标——堆数组非优先序，迭代会误导（与 C++ priority_queue 一致）。 */

#define HEAP_MIN_CAP 8u

static inline char   *heap_slot(heap *h, uint32_t i) { return h->slots + (size_t)i * h->stride; }
static inline sc_afat *heap_val(heap *h, uint32_t i)  { return (sc_afat *)heap_slot(h, i); }
static inline char   *heap_keyslot(heap *h, uint32_t i) { return heap_slot(h, i) + sizeof(sc_afat); }
/* 逻辑键：数值模式为键字节指针；字符串模式为存于槽内的 char* */
static inline const void *heap_logkey(heap *h, uint32_t i) {
    return h->key_size > 0 ? (const void *)heap_keyslot(h, i)
                           : *(const char **)heap_keyslot(h, i);
}

/* 比较两逻辑键：返回 sign(a - b)。cmp 非空走自定义；否则数值按宽度有符号 / 字符串 strcmp。 */
static int heap_cmp_key(heap *h, const void *a, const void *b) {
    if (h->cmp) return h->cmp(a, b, h->cmp_ctx);
    if (h->key_size > 0) {
        switch (h->key_size) {
            case 1: { int8_t x = *(const int8_t *)a, y = *(const int8_t *)b;
                      return x < y ? -1 : x > y ? 1 : 0; }
            case 2: { int16_t x, y; memcpy(&x, a, 2); memcpy(&y, b, 2);
                      return x < y ? -1 : x > y ? 1 : 0; }
            case 4: { int32_t x, y; memcpy(&x, a, 4); memcpy(&y, b, 4);
                      return x < y ? -1 : x > y ? 1 : 0; }
            case 8: { int64_t x, y; memcpy(&x, a, 8); memcpy(&y, b, 8);
                      return x < y ? -1 : x > y ? 1 : 0; }
            default: return memcmp(a, b, (size_t)h->key_size);
        }
    }
    { const char *x = (const char *)a, *y = (const char *)b;
      if (x == y) return 0; if (!x) return -1; if (!y) return 1; return strcmp(x, y); }
}

/* i 是否应在 j 之上（更靠近根）：最小堆 = i 键更小，最大堆 = i 键更大。 */
static inline int heap_above(heap *h, uint32_t i, uint32_t j) {
    int c = heap_cmp_key(h, heap_logkey(h, i), heap_logkey(h, j));
    return h->min ? (c < 0) : (c > 0);
}

/* 交换两槽全部字节（裸搬移句柄+键，move 语义，不改计数）；分块经栈缓冲，支持任意 stride。 */
static void heap_swap(heap *h, uint32_t i, uint32_t j) {
    char *a = heap_slot(h, i), *b = heap_slot(h, j);
    char tmp[64];
    uint32_t n = h->stride, k;
    if (i == j) return;
    for (k = 0; k < n; k += (uint32_t)sizeof(tmp)) {
        uint32_t m = n - k < (uint32_t)sizeof(tmp) ? n - k : (uint32_t)sizeof(tmp);
        memcpy(tmp, a + k, m);
        memcpy(a + k, b + k, m);
        memcpy(b + k, tmp, m);
    }
}

static void heap_sift_up(heap *h, uint32_t i) {
    while (i > 0) {
        uint32_t parent = (i - 1) / 2;
        if (!heap_above(h, i, parent)) break;
        heap_swap(h, i, parent);
        i = parent;
    }
}

static void heap_sift_down(heap *h, uint32_t i) {
    uint32_t n = h->size;
    for (;;) {
        uint32_t l = 2 * i + 1, r = l + 1, best = i;
        if (l < n && heap_above(h, l, best)) best = l;
        if (r < n && heap_above(h, r, best)) best = r;
        if (best == i) break;
        heap_swap(h, i, best);
        i = best;
    }
}

static uint8_t heap_storekey(heap *h, uint32_t i, const void *key) {
    if (h->key_size > 0) { memcpy(heap_keyslot(h, i), key, (size_t)h->key_size); return 1; }
    if (h->key_size == 0) { *(const char **)heap_keyslot(h, i) = (const char *)key; return 1; }  /* 借用 */
    { const char *s = (const char *)key; size_t n = s ? strlen(s) : 0;  /* -1：拷贝一份 */
      char *cp = (char *)chunk(n + 1); if (!cp) return 0;
      if (n) memcpy(cp, s, n); cp[n] = 0; *(const char **)heap_keyslot(h, i) = cp; return 1; }
}
static inline void heap_freekey(heap *h, uint32_t i) {
    if (h->key_size == -1) recycle(*(char **)heap_keyslot(h, i));
}

/* 扩容到至少 want 槽（几何增长）；裸字节搬移句柄，不改计数。失败返回 0。 */
static uint8_t heap_grow(heap *h, uint32_t want) {
    uint32_t ncap = h->cap ? h->cap : HEAP_MIN_CAP;
    char *ns;
    while (ncap < want) ncap *= 2;
    ns = (char *)refit(h->slots, (uint64_t)ncap * h->stride);
    if (!ns) return 0;
    h->slots = ns;
    h->cap = ncap;
    return 1;
}

void heap_init(heap *_this, uint8_t min, int32_t key_size, heap_cmp cmp, void *cmp_ctx) {
    uint32_t keylen = key_size > 0 ? (uint32_t)key_size : (uint32_t)sizeof(char *);
    uint32_t st = (uint32_t)sizeof(sc_afat) + keylen;
    _this->slots = NULL;
    _this->cmp = cmp;
    _this->cmp_ctx = cmp_ctx;
    _this->stride = (st + 7u) & ~7u;                /* align8 */
    _this->size = 0;
    _this->cap = 0;
    _this->key_size = key_size;
    _this->min = min ? 1 : 0;
}

void heap_clear(heap *_this) {
    uint32_t i;
    for (i = 0; i < _this->size; i++) {
        sc_afat_unbind(heap_val(_this, i));         /* release value */
        heap_freekey(_this, i);                     /* -1 模式回收 key 拷贝 */
    }
    _this->size = 0;
}

void heap_drop(heap *_this) {
    heap_clear(_this);
    recycle(_this->slots);
    _this->slots = NULL;
    _this->cap = 0;
}

uint64_t heap_len(heap *_this) { return _this->size; }
uint8_t  heap_is_empty(heap *_this) { return _this->size == 0; }

uint8_t heap_reserve(heap *_this, uint64_t n) {
    if (n <= _this->cap) return 1;
    if (n > 0xFFFFFFFFu) return 0;
    return heap_grow(_this, (uint32_t)n);
}

uint8_t heap_push(heap *_this, const void *key, sc_afat value) {
    uint32_t i = _this->size;
    if (i == _this->cap && !heap_grow(_this, i + 1)) return 0;
    if (!heap_storekey(_this, i, key)) return 0;
    sc_afat_bind(heap_val(_this, i), value.p, (sc_ref *)value.tar, SC_OWN_RAW, value.dtor);  /* retain */
    _this->size = i + 1;
    heap_sift_up(_this, i);
    return 1;
}

uint8_t heap_pop(heap *_this) {
    uint32_t last;
    if (!_this->size) return 0;
    sc_afat_unbind(heap_val(_this, 0));             /* release 堆顶 value */
    heap_freekey(_this, 0);                         /* -1 模式回收堆顶 key */
    last = --_this->size;
    if (last) {                                     /* 末槽裸搬移到根后下滤（move，不改计数） */
        memcpy(heap_slot(_this, 0), heap_slot(_this, last), _this->stride);
        heap_sift_down(_this, 0);
    }
    return 1;
}

sc_afat heap_peek(heap *_this) {
    if (_this->size) return *heap_val(_this, 0);    /* 借用，不改计数 */
    { sc_afat e; e.p = NULL; e.tar = NULL; e.own = NULL; e.dtor = NULL; return e; }
}

const void *heap_peek_key(heap *_this) {
    return _this->size ? heap_logkey(_this, 0) : NULL;
}


