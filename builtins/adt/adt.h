/* adt.h —— sc 内置 ADT 的 C ABI 契约（与 builtins/adt/adt.sc 同步维护）
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后通过 scc --adt <path>
 *  （或环境变量 SCC_ADT、.sc 配置 adt = <path>）替换内置默认实现。
 *
 * 约定：
 *   - string 内部始终 NUL 结尾，cstr() 永不返回 NULL（空串返回 ""）
 *   - array 元素为定长值块（逐字节复制），以 elem_sz 参数化；get/find 返回内部指针
 *   - list 不拥有元素：drop/clear/remove_at 不释放元素指针本身
 *   - 返回 uint8_t（sc 的 b 类型）的函数：1 成功 / 0 失败（内存不足等）
 */
#ifndef SC_ADT_H
#define SC_ADT_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- string：动态字符串 ---------------- */

typedef struct string {
    char    *data;     /* 缓冲区（NUL 结尾；空串可为 NULL） */
    uint32_t size;     /* 字符数（不含 NUL） */
    uint32_t cap;      /* 缓冲区容量 */
} string;

void     string_init(string *_this, const char *s);   /* 构造（s 为 NULL → 空串） */
void     string_drop(string *_this);                  /* 释放缓冲区 */
uint64_t string_len(string *_this);
char    *string_cstr(string *_this);                  /* 始终非 NULL */
void     string_clear(string *_this);
uint8_t  string_reserve(string *_this, uint64_t n);
uint8_t  string_assign(string *_this, const char *s);
uint8_t  string_append(string *_this, const char *s);
uint8_t  string_append_n(string *_this, const char *s, uint64_t n);
uint8_t  string_append_char(string *_this, char c);
uint8_t  string_printf(string *_this, const char *fmt, ...);  /* 追加格式化文本（自动扩容） */
uint8_t  string_insert(string *_this, uint64_t index, const char *s);
uint8_t  string_erase(string *_this, uint64_t index, uint64_t n);
char     string_at(string *_this, uint64_t index);    /* 越界返回 0 */
int64_t  string_find(string *_this, const char *sub, uint64_t start);
int64_t  string_rfind(string *_this, const char *sub);
uint8_t  string_equals(string *_this, const char *s);
uint8_t  string_starts_with(string *_this, const char *s);
uint8_t  string_ends_with(string *_this, const char *s);
uint8_t  string_slice(string *_this, int64_t start, int64_t stop, string *out);
void     string_strip(string *_this);
void     string_lower(string *_this);
void     string_upper(string *_this);
uint8_t  string_clone(string *_this, string *out);

/* ---------------- array：动态值数组 ---------------- */

typedef int32_t (*array_cmp)(void *a, void *b);       /* 比较回调（与 qsort/bsearch 契约一致） */

typedef struct array {
    char    *buf;      /* 连续值块缓冲区（cap 个 elem_sz 字节槽位） */
    uint32_t size;     /* 元素个数 */
    uint32_t cap;      /* 已分配槽位 */
    uint32_t elem_sz;  /* 单元素字节数 */
} array;

void     array_init(array *_this, uint32_t elem_sz);  /* 构造（指定单元素字节数） */
void     array_drop(array *_this);                    /* 释放缓冲区 */
uint64_t array_len(array *_this);
void    *array_data(array *_this);                    /* 缓冲区视图（≈ string_cstr；空可为 NULL） */
void     array_clear(array *_this);
uint8_t  array_reserve(array *_this, uint64_t n);
uint8_t  array_resize(array *_this, uint64_t n);      /* 调整元素数，新增槽位清零 */
uint8_t  array_assign(array *_this, void *src, uint64_t n);   /* 赋值为 n 个元素 */
uint8_t  array_append(array *_this, void *src, uint64_t n);   /* 追加 n 个元素 */
uint8_t  array_push(array *_this, void *value);       /* 追加单元素（拷贝 elem_sz 字节） */
void    *array_pop(array *_this);                     /* 弹出并返回尾元素指针；空返回 NULL */
uint8_t  array_insert(array *_this, uint64_t index, void *value);
uint8_t  array_erase(array *_this, uint64_t index, uint64_t n);
void    *array_at(array *_this, uint64_t index);      /* 越界返回 NULL */
uint8_t  array_set(array *_this, uint64_t index, void *value);
void    *array_front(array *_this);                   /* 空返回 NULL */
void    *array_back(array *_this);                    /* 空返回 NULL */
int64_t  array_find(array *_this, void *key, uint64_t start, array_cmp cmp);  /* 线性查找，未找到 -1 */
int64_t  array_rfind(array *_this, void *key, array_cmp cmp);                 /* 反向线性查找，未找到 -1 */
uint8_t  array_equals(array *_this, array *other, array_cmp cmp);
uint8_t  array_starts_with(array *_this, array *other, array_cmp cmp);
uint8_t  array_ends_with(array *_this, array *other, array_cmp cmp);
uint8_t  array_slice(array *_this, int64_t start, int64_t stop, array *out);  /* 负索引从尾部计 */
void     array_reverse(array *_this);
uint8_t  array_clone(array *_this, array *out);       /* 深拷贝 */
void     array_sort(array *_this, array_cmp cmp);     /* qsort */
void    *array_bsearch(array *_this, void *key, array_cmp cmp);  /* bsearch（须已排序），未找到 NULL */

/* ---------------- list：动态指针数组 ---------------- */

typedef int32_t (*list_cmp)(void *a, void *b);        /* sort 比较回调（函数指针，与 adt.sc @fnc 生成形一致） */

typedef struct list {
    void   **items;    /* 元素数组 */
    uint32_t size;     /* 元素个数 */
    uint32_t cap;      /* 已分配槽位 */
} list;

void     list_init(list *_this);
void     list_drop(list *_this);                      /* 不释放元素本身 */
uint64_t list_len(list *_this);
void     list_clear(list *_this);
uint8_t  list_reserve(list *_this, uint64_t n);
uint8_t  list_push(list *_this, void *value);
void    *list_pop(list *_this);                       /* 空返回 NULL */
void    *list_get(list *_this, uint64_t index);       /* 越界返回 NULL */
uint8_t  list_set(list *_this, uint64_t index, void *value);
uint8_t  list_insert(list *_this, uint64_t index, void *value);
void    *list_remove_at(list *_this, uint64_t index);
int64_t  list_index_of(list *_this, void *value);     /* 未找到 -1 */
void     list_reverse(list *_this);
uint8_t  list_clone(list *_this, list *out);
void     list_sort(list *_this, list_cmp cmp);

#ifdef __cplusplus
}
#endif

#endif /* SC_ADT_H */
