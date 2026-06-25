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


