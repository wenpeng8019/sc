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

/* ---------------- string ---------------- */

/* 确保容量至少 need+1（含 NUL），按 2 倍扩容 */
static uint8_t str_grow(string *s, uint64_t need) {
    if (s->cap > need) return 1;
    uint64_t cap = s->cap ? s->cap : 16;
    while (cap <= need) cap *= 2;
    char *p = (char *)realloc(s->data, cap);
    if (!p) return 0;
    s->data = p;
    s->cap = cap;
    return 1;
}

void string_init(string *_this) {
    _this->data = NULL;
    _this->size = 0;
    _this->cap = 0;
}

void string_drop(string *_this) {
    free(_this->data);
    string_init(_this);
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

uint8_t string_assign(string *_this, char *s) {
    string_clear(_this);
    return string_append(_this, s);
}

uint8_t string_append(string *_this, char *s) {
    return string_append_n(_this, s, s ? strlen(s) : 0);
}

uint8_t string_append_n(string *_this, char *s, uint64_t n) {
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

uint8_t string_insert(string *_this, uint64_t index, char *s) {
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

int64_t string_find(string *_this, char *sub, uint64_t start) {
    if (!sub || start > _this->size) return -1;
    if (!_this->data) return *sub ? -1 : 0;
    const char *p = strstr(_this->data + start, sub);
    return p ? (int64_t)(p - _this->data) : -1;
}

int64_t string_rfind(string *_this, char *sub) {
    if (!sub || !_this->data) return -1;
    uint64_t n = strlen(sub);
    if (n > _this->size) return -1;
    for (int64_t i = (int64_t)(_this->size - n); i >= 0; i--)
        if (memcmp(_this->data + i, sub, n) == 0) return i;
    return -1;
}

uint8_t string_equals(string *_this, char *s) {
    return strcmp(string_cstr(_this), s ? s : "") == 0;
}

uint8_t string_starts_with(string *_this, char *s) {
    if (!s) return 0;
    uint64_t n = strlen(s);
    return n <= _this->size && memcmp(string_cstr(_this), s, n) == 0;
}

uint8_t string_ends_with(string *_this, char *s) {
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

/* ---------------- list ---------------- */

static uint8_t list_grow(list *l, uint64_t need) {
    if (l->cap >= need) return 1;
    uint64_t cap = l->cap ? l->cap : 8;
    while (cap < need) cap *= 2;
    void **p = (void **)realloc(l->items, cap * sizeof(void *));
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
    free(_this->items);
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

/* 插入排序：元素数通常不大，且保持稳定 */
void list_sort(list *_this, list_cmp *cmp) {
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

/* ---------------- chain：侵入式双向链表 ---------------- */
/* 元素末尾内嵌 _prev/_next（sc 编译器注入），偏移随类型不同 → 经 off 间接寻址。
 * LKP/LKN 展开为单次加法+解引用，与直接成员访问同等机器码。
 * 约定：首元素 _prev = 尾元素（rear），尾元素 _next = NULL。 */

/* _prev/_next 固定在结构体最前面（void *_prev, *_next）*/
#define LPREV(it) (*(void **)((char *)(it) + 0))       /* _prev at offset 0 */
#define LNEXT(it) (*(void **)((char *)(it) + sizeof(void *))) /* _next at offset 8 */

void chain_append(chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    void *r = LPREV(h);        /* rear */
    LNEXT(r) = it;
    LPREV(it) = r;
    LNEXT(it) = NULL;
    LPREV(h) = it;             /* 首元素 _prev 始终指向新 rear */
}

void chain_push(chain *_this, void *it) {
    if (!it) return;
    void *h = _this->head;
    if (!h) { _this->head = it; LPREV(it) = it; LNEXT(it) = NULL; return; }
    LPREV(it) = LPREV(h);      /* 继承 rear */
    LNEXT(it) = h;
    LPREV(h) = it;
    _this->head = it;
}

void *chain_pop(chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    void *n = LNEXT(h);
    if (n) { LPREV(n) = LPREV(h); _this->head = n; }
    else _this->head = NULL;
    LPREV(h) = LNEXT(h) = NULL;
    return h;
}

void chain_before(chain *_this, void *pos, void *it) {
    void *h = _this->head;
    if (!h || !pos || !it) return;
    if (pos == h) {            /* 等价 push */
        LPREV(it) = LPREV(h);
        LNEXT(it) = h;
        LPREV(h) = it;
        _this->head = it;
        return;
    }
    void *p = LPREV(pos);
    LNEXT(p) = it;
    LPREV(it) = p;
    LNEXT(it) = pos;
    LPREV(pos) = it;
}

void chain_after(chain *_this, void *pos, void *it) {
    void *h = _this->head;
    if (!h || !pos || !it) return;
    void *n = LNEXT(pos);
    LNEXT(pos) = it;
    LPREV(it) = pos;
    LNEXT(it) = n;
    if (n) LPREV(n) = it;
    else LPREV(h) = it;        /* it 成为新 rear */
}

void chain_remove(chain *_this, void *it) {
    void *h = _this->head;
    if (!h || !it) return;
    if (it == h) { chain_pop(_this); return; }
    void *p = LPREV(it), *n = LNEXT(it);
    LNEXT(p) = n;
    if (n) LPREV(n) = p;
    else LPREV(h) = p;         /* it 原为 rear */
    LPREV(it) = LNEXT(it) = NULL;
}

void *chain_first(chain *_this) { return _this->head; }

void *chain_last(chain *_this) {
    void *h = _this->head;
    if (!h) return NULL;
    return LPREV(h);
}

void chain_revert(chain *_this) {
    void *h = _this->head;
    if (!h) return;
    void *r = LPREV(h);
    if (r == h) return;        /* 单元素 */
    void *cur = h;
    while (cur) {              /* 逐节点交换 prev/next */
        void *nx = LNEXT(cur);
        LNEXT(cur) = LPREV(cur);
        LPREV(cur) = nx;
        cur = nx;
    }
    _this->head = r;
    LPREV(r) = h;              /* 新首元素 _prev = 新 rear（原首元素） */
    LNEXT(h) = NULL;           /* 新 rear _next = NULL */
}

void chain_append_to(chain *_this, chain *dst) {
    void *lh = _this->head;
    if (!lh) return;
    void *dh = dst->head;
    if (!dh) { dst->head = lh; _this->head = NULL; return; }
    void *dr = LPREV(dh), *lr = LPREV(lh);
    LNEXT(dr) = lh;
    LPREV(lh) = dr;
    LPREV(dh) = lr;            /* dst 新 rear = 原 self rear */
    _this->head = NULL;
}

void chain_push_to(chain *_this, chain *dst) {
    void *lh = _this->head;
    if (!lh) return;
    void *dh = dst->head;
    if (!dh) { dst->head = lh; _this->head = NULL; return; }
    void *dr = LPREV(dh), *lr = LPREV(lh);
    LNEXT(lr) = dh;
    LPREV(dh) = lr;
    LPREV(lh) = dr;            /* 新首元素（self 首）继承 dst rear */
    dst->head = lh;
    _this->head = NULL;
}

void chain_cut(chain *_this, void *from, void *to, chain *out) {
    void *h = _this->head;
    if (!h || !from || !to || !out) return;
    void *p = LPREV(from), *n = LNEXT(to);
    int fromHead = (from == h);
    /* 从原链摘除 [from..to] */
    if (fromHead && !n) _this->head = NULL;
    else if (fromHead) { LPREV(n) = p; _this->head = n; }      /* p 即原 rear */
    else if (!n) { LNEXT(p) = NULL; LPREV(h) = p; }            /* to 原为 rear */
    else { LNEXT(p) = n; LPREV(n) = p; }
    /* 构成 out */
    out->head = from;
    LPREV(from) = to;
    LNEXT(to) = NULL;
}
