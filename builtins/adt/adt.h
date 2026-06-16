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
uint8_t  string_assign(string *_this, char *s);
uint8_t  string_append(string *_this, char *s);
uint8_t  string_append_n(string *_this, char *s, uint64_t n);
uint8_t  string_append_char(string *_this, char c);
uint8_t  string_insert(string *_this, uint64_t index, char *s);
uint8_t  string_erase(string *_this, uint64_t index, uint64_t n);
char     string_at(string *_this, uint64_t index);    /* 越界返回 0 */
int64_t  string_find(string *_this, char *sub, uint64_t start);
int64_t  string_rfind(string *_this, char *sub);
uint8_t  string_equals(string *_this, char *s);
uint8_t  string_starts_with(string *_this, char *s);
uint8_t  string_ends_with(string *_this, char *s);
uint8_t  string_slice(string *_this, int64_t start, int64_t stop, string *out);
void     string_strip(string *_this);
void     string_lower(string *_this);
void     string_upper(string *_this);
uint8_t  string_clone(string *_this, string *out);

/* ---------------- list：动态指针数组 ---------------- */

typedef int32_t list_cmp(void *a, void *b);           /* sort 比较回调 */

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
void     list_sort(list *_this, list_cmp *cmp);

/* ---------------- chain：侵入式双向链表 ----------------
 * 元素为 sc 链表结构体（def T: ~ {}，首位有 void *_prev, *_next）
 * 首元素 _prev = 尾元素（rear），尾元素 _next = NULL；不拥有元素
 * 导航：用内置 base(o), prev(o), next(o) 函数访问首真实成员和邻接节点 */

typedef struct chain {
    void    *head;     /* 首元素（空链为 NULL） */
} chain;

void *chain_prev(void *it);                           /* 边界安全逻辑前驱：head→NULL（内置 prev(o) 后端） */
void  chain_append(chain *_this, void *it);           /* 队尾 */
void  chain_push(chain *_this, void *it);             /* 队首 */
void *chain_pop(chain *_this);                        /* 移除并返回首元素 */
void  chain_before(chain *_this, void *pos, void *it);
void  chain_after(chain *_this, void *pos, void *it);
void  chain_remove(chain *_this, void *it);
void *chain_first(chain *_this);
void *chain_last(chain *_this);
void  chain_revert(chain *_this);
void  chain_append_to(chain *_this, chain *dst);     /* 自身清空 */
void  chain_push_to(chain *_this, chain *dst);       /* 自身清空 */
void  chain_cut(chain *_this, void *from, void *to, chain *out);

#ifdef __cplusplus
}
#endif

#endif /* SC_ADT_H */
