/* adt.h —— sc 内置 ADT 的 C ABI 契约（与 builtins/adt/adt.sc 同步维护）
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后通过 scc --adt <path>
 *  （或环境变量 SCC_ADT、.sc 配置 adt = <path>）替换内置默认实现。
 *
 * 约定：
 *   - string 内部始终 NUL 结尾，cstr() 永不返回 NULL（空串返回 ""）
 *   - array 元素为定长值块（逐字节复制），以 elem_sz 参数化；get/find 返回内部指针
 *   - list 拥有元素：元素为裸自动指针 @（sc_afat），push 取一份 retain（目标 in++），
 *     pop/remove_at/set/clear/drop 释放对应 retain（目标 in--，触零自析构）。取出语义
 *     「取用分离」：get 借用（返回句柄，不改计数），pop/remove_at 仅删除并 release（返回
 *     bool）；要取并保留先 get 借用、还原绑入 T@（retain）再 pop（release）。
 *   - 返回 uint8_t（sc 的 b 类型）的函数：1 成功 / 0 失败（内存不足等）
 */
#ifndef SC_ADT_H
#define SC_ADT_H

#include <stdint.h>
#include <stddef.h>
#include "op.h"        /* sc_afat / sc_afat_bind / sc_afat_unbind / SC_OWN_RAW（list 元素为裸 @） */

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

/* ---------------- ring：SPSC 无锁循环队列（kfifo 风格） ---------------- */
/* 单生产者单消费者无锁有界队列：元素为定长值块（elem_sz 字节），容量为 2 的幂。
 * head（消费者独写）/ tail（生产者独写）为自由递增计数器，槽位下标 = idx & mask，
 * 元素数 = tail - head（无符号回绕）。生产者以 release 发布 tail、消费者以 acquire
 * 观测 → 数据写入先于索引可见；空 = (tail==head)，满 = (tail-head > mask)。
 * 约束：仅 SPSC 安全；多生产者/多消费者须外部加锁。init/drop/clear 仅在无并发时调用。
 * 原子读写经 platform.h 的 sc_* 宏（C11 stdatomic / 平台特化）。 */

typedef struct ring {
    char    *buf;      /* 连续值块缓冲区（cap = mask+1 个 elem_sz 字节槽，2 的幂） */
    uint32_t head;     /* 消费者索引（自由递增；& mask 取槽） */
    uint32_t tail;     /* 生产者索引（自由递增） */
    uint32_t mask;     /* cap - 1（cap 为 2 的幂） */
    uint32_t elem_sz;  /* 单元素字节数 */
} ring;

uint8_t  ring_init(ring *_this, uint32_t elem_sz, uint32_t capacity);  /* capacity 向上取 2 幂；分配失败返回 0 */
void     ring_drop(ring *_this);                      /* 释放缓冲区 */
uint64_t ring_cap(ring *_this);                       /* 容量（2 的幂；未初始化 0） */
uint64_t ring_len(ring *_this);                       /* 当前元素数快照（tail - head） */
uint8_t  ring_is_empty(ring *_this);
uint8_t  ring_is_full(ring *_this);
void     ring_clear(ring *_this);                     /* 复位 head/tail（仅无并发时安全） */
uint8_t  ring_push(ring *_this, void *value);         /* 生产者入队一个元素（拷贝 elem_sz 字节）；满返回 0 */
uint8_t  ring_pop(ring *_this, void *out);            /* 消费者出队到 out（拷贝 elem_sz 字节）；空返回 0 */
void    *ring_peek(ring *_this);                      /* 消费者借用队首元素指针；空返回 NULL */

/* ---------------- list：段式裸 @ 自动指针容器 ---------------- */
/* 元素为裸自动指针 @（sc_afat，32B）；list 拥有元素（每元素一份 retain）。
 * 段式存储：元素 i 住 segs[i / LIST_SEG][i % LIST_SEG]，段索引表与各段内存均来自
 * mem chunk（不受全局 -DSC_POOL 影响）。≈ array 的接口，区别仅在元素是 ref 句柄而非值块。 */

typedef int32_t (*list_cmp)(void *a, void *b);        /* sort 比较回调（实参为元素 .p 实体基址；与 adt.sc @fnc 生成形一致） */

typedef struct list {
    sc_afat **segs;    /* 段索引表（每段 LIST_SEG 个 sc_afat 槽；段内存来自 mem chunk） */
    uint32_t  nsegs;   /* 已分配段数 */
    uint32_t  size;    /* 元素个数 */
    uint32_t  cap;     /* 总槽位（nsegs * LIST_SEG） */
} list;

void     list_init(list *_this);
void     list_drop(list *_this);                      /* 释放全部 retain + 回收段内存 */
uint64_t list_len(list *_this);
void     list_clear(list *_this);                     /* 释放全部 retain，保留段容量 */
uint8_t  list_reserve(list *_this, uint64_t n);
uint8_t  list_push(list *_this, sc_afat value);       /* 尾部追加，retain 元素 */
uint8_t  list_pop(list *_this);                       /* 删除并 release 尾元素；空返回 0 */
sc_afat  list_get(list *_this, uint64_t index);       /* 借用视图（不改计数）；越界返回空句柄 */
uint8_t  list_set(list *_this, uint64_t index, sc_afat value);  /* 改写：retain 新、release 旧 */
uint8_t  list_insert(list *_this, uint64_t index, sc_afat value);
uint8_t  list_remove_at(list *_this, uint64_t index); /* 删除并 release 该元素；越界返回 0 */
int64_t  list_index_of(list *_this, sc_afat value);   /* 按 .p 实体基址比对；未找到 -1 */
void     list_reverse(list *_this);
uint8_t  list_clone(list *_this, list *out);          /* 逐元素 retain 到 out */
void     list_sort(list *_this, list_cmp cmp);

/* ---------------- dict：开放寻址裸 @ 自动指针映射 ---------------- */
/* value 为裸自动指针 @（sc_afat，32B）；dict 拥有 value（每条一份 retain）。
 * key 类型由 init 的 key_size 三态决定（key 经接口以 const void* 裸指针传入，按 key_size 解读）：
 *   key_size  > 0：定长数值/POD，内联 key_size 字节，memcmp 比较（浮点键不安全，限整数/指针类）；
 *   key_size == 0：引用字符串，仅存 const char* 借用指针——dict 不拥有，字符串本体须由 value 对象自持；
 *   key_size == -1：拷贝字符串，put 时 chunk 复制一份、remove/clear/drop 时 recycle。
 * 开放寻址（线性探测 + SwissTable 风格控制字节）：全部 item 内联住一整块桶数组，无 per-item 分配；
 *   整张表仅 ctrl + slots 两块（mem chunk），resize 整体 rehash 重建。每桶布局 [sc_afat value][key]，
 *   value 在前保 8 对齐，stride = align8(sizeof(sc_afat) + (key_size>0 ? key_size : sizeof(char*)))。
 * 取出语义同 list「取用分离」：get 借用返回句柄（不改计数），remove 删除并 release（返回 bool）。
 * 因 init 带 key_size 参数，不参与「声明即构造」——须显式 d.init(key_size)。 */

typedef uint8_t (*dict_each_fn)(const void *key, sc_afat value, void *ctx);  /* 遍历回调：返回 0 提前终止 */

typedef struct dict {
    uint8_t *ctrl;      /* 控制字节数组（nbuckets 个；空 0xFF / 墓碑 0xFE / 占用 = hash 低 7 位） */
    char    *slots;     /* 桶数据（nbuckets * stride，每桶 [sc_afat value][key]） */
    int32_t  key_size;  /* >0 定长 / 0 引用字符串 / -1 拷贝字符串 */
    uint32_t stride;    /* 单桶字节 = align8(sizeof(sc_afat) + keylen) */
    uint32_t size;      /* 元素数 */
    uint32_t used;      /* 占用 + 墓碑（rehash 阈值用） */
    uint32_t nbuckets;  /* 桶数（2 的幂；0 = 未分配） */
} dict;

void     dict_init(dict *_this, int32_t key_size);    /* 构造（指定 key 模式） */
void     dict_drop(dict *_this);                      /* 释放全部 retain + 回收桶/控制块（保留 key_size） */
uint64_t dict_len(dict *_this);
uint8_t  dict_has(dict *_this, const void *key);
sc_afat  dict_get(dict *_this, const void *key);      /* 借用视图（不改计数）；未命中返回空句柄 */
uint8_t  dict_put(dict *_this, const void *key, sc_afat value);  /* 插入/替换：retain 新、替换时 release 旧 */
uint8_t  dict_remove(dict *_this, const void *key);   /* 删除并 release value；未命中返回 0 */
void     dict_clear(dict *_this);                     /* 清空并 release 全部 value（保留桶容量） */
void     dict_each(dict *_this, dict_each_fn fn, void *ctx);  /* 无序遍历占用桶；回调返 0 即停 */

/* 整数游标双向遍历（游标 = 桶下标；空集/越界返回 -1 或空键/句柄）。
 * 游标在 get/has/each 期间稳定；put/remove 可能 rehash 使其失效，遍历期间勿增删。
 * each 与 next 走同一桶序。典型用法：for (i=dict_first(d); i>=0; i=dict_next(d,i)) {...} */
int64_t      dict_first(dict *_this);                 /* 首个占用桶游标；空集 -1 */
int64_t      dict_last(dict *_this);                  /* 末个占用桶游标；空集 -1 */
int64_t      dict_next(dict *_this, int64_t cur);     /* cur 之后的占用桶；无则 -1 */
int64_t      dict_prev(dict *_this, int64_t cur);     /* cur 之前的占用桶；无则 -1 */
const void  *dict_key_at(dict *_this, int64_t cur);   /* 游标处 key（无效返回 NULL） */
sc_afat      dict_value_at(dict *_this, int64_t cur); /* 游标处 value 借用（无效返回空句柄） */



#ifdef __cplusplus
}
#endif

#endif /* SC_ADT_H */
