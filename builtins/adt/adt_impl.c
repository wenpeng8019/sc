/* adt_impl.c —— sc 内置 ADT 的默认实现
 *
 * 编译器在单元图包含 builtins/adt/adt.sc 时自动编译并链接本文件；
 * 可通过 scc --adt <x.c|x.o|x.a> 替换为自定义实现（契约见 adt.h）。
 */
#include "adt.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */

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

/* ---------------- list ---------------- */

static uint8_t list_grow(list *l, uint64_t need) {
    if (l->cap >= need) return 1;
    uint64_t cap = l->cap ? l->cap : 8;
    while (cap < need) cap *= 2;
    void **p = (void **)sc_realloc(l->items, cap * sizeof(void *));
    if (!p) return 0;
    l->items = p;
    l->cap = cap;
    return 1;
}

void list_init(list *_this) {
    _this->items = NULL;
    _this->size = 0;
    _this->cap = 0;
}

void list_drop(list *_this) {
    sc_free(_this->items);
    list_init(_this);
}

uint64_t list_len(list *_this) { return _this->size; }

void list_clear(list *_this) { _this->size = 0; }

uint8_t list_reserve(list *_this, uint64_t n) { return list_grow(_this, n); }

uint8_t list_push(list *_this, void *value) {
    if (!list_grow(_this, _this->size + 1)) return 0;
    _this->items[_this->size++] = value;
    return 1;
}

void *list_pop(list *_this) {
    return _this->size ? _this->items[--_this->size] : NULL;
}

void *list_get(list *_this, uint64_t index) {
    return index < _this->size ? _this->items[index] : NULL;
}

uint8_t list_set(list *_this, uint64_t index, void *value) {
    if (index >= _this->size) return 0;
    _this->items[index] = value;
    return 1;
}

uint8_t list_insert(list *_this, uint64_t index, void *value) {
    if (index > _this->size) return 0;
    if (!list_grow(_this, _this->size + 1)) return 0;
    memmove(_this->items + index + 1, _this->items + index,
            (_this->size - index) * sizeof(void *));
    _this->items[index] = value;
    _this->size++;
    return 1;
}

void *list_remove_at(list *_this, uint64_t index) {
    if (index >= _this->size) return NULL;
    void *v = _this->items[index];
    memmove(_this->items + index, _this->items + index + 1,
            (_this->size - index - 1) * sizeof(void *));
    _this->size--;
    return v;
}

int64_t list_index_of(list *_this, void *value) {
    for (uint64_t i = 0; i < _this->size; i++)
        if (_this->items[i] == value) return (int64_t)i;
    return -1;
}

void list_reverse(list *_this) {
    for (uint64_t i = 0, j = _this->size; i + 1 < j; i++, j--) {
        void *t = _this->items[i];
        _this->items[i] = _this->items[j - 1];
        _this->items[j - 1] = t;
    }
}

uint8_t list_clone(list *_this, list *out) {
    list_clear(out);
    if (!list_grow(out, _this->size)) return 0;
    if (_this->size)
        memcpy(out->items, _this->items, _this->size * sizeof(void *));
    out->size = _this->size;
    return 1;
}

/* 插入排序：元素数通常不大，且保持稳定。cmp 为 list_cmp（函数指针） */
void list_sort(list *_this, list_cmp cmp) {
    if (!cmp) return;
    for (uint64_t i = 1; i < _this->size; i++) {
        void *v = _this->items[i];
        uint64_t j = i;
        while (j > 0 && cmp(_this->items[j - 1], v) > 0) {
            _this->items[j] = _this->items[j - 1];
            j--;
        }
        _this->items[j] = v;
    }
}
