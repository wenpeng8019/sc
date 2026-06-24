/* adt.h —— sc 内置 ADT 的 C ABI 契约（与 builtins/adt/adt.sc 同步维护）
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后通过 scc --adt <path>
 *  （或环境变量 SCC_ADT、.sc 配置 adt = <path>）替换内置默认实现。
 *
 * 约定：
 *   - string 内部始终 NUL 结尾，cstr() 永不返回 NULL（空串返回 ""）
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
    uint64_t size;     /* 字符数（不含 NUL） */
    uint64_t cap;      /* 缓冲区容量 */
} string;

void     string_init(string *_this);                  /* 构造为空串 */
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

/* ---------------- list：动态指针数组 ---------------- */

typedef int32_t (*list_cmp)(void *a, void *b);        /* sort 比较回调（函数指针，与 adt.sc @fnc 生成形一致） */

typedef struct list {
    void   **items;    /* 元素数组 */
    uint64_t size;     /* 元素个数 */
    uint64_t cap;      /* 已分配槽位 */
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
