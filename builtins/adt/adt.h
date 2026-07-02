/* adt.h —— sc 内置 ADT 的 C ABI 契约（与 builtins/adt/adt.sc 同步维护）
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后通过 scc --adt <path>
 *  （或环境变量 SCC_ADT、.sc 配置 adt = <path>）替换内置默认实现。
 *
 * 约定：
 *   - string 内部始终 NUL 结尾，cstr() 永不返回 NULL（空串返回 ""）
 *   - array 元素为定长值块（逐字节复制），以 elem_sz 参数化；get/find 返回内部指针
 *   - list 拥有元素：元素为裸自动指针 @（sc_thin），push 取一份 retain（目标 in++），
 *     pop/remove_at/set/clear/drop 释放对应 retain（目标 in--，触零自析构）。取出语义
 *     「取用分离」：get 借用（返回句柄，不改计数），pop/remove_at 仅删除并 release（返回
 *     bool）；要取并保留先 get 借用、还原绑入 T@（retain）再 pop（release）。
 *   - 返回 uint8_t（sc 的 b 类型）的函数：1 成功 / 0 失败（内存不足等）
 */
#ifndef SC_ADT_H
#define SC_ADT_H

#include "op.h"        /* sc_thin / sc_thin_bind / sc_thin_unbind / SC_OWN_RAW（list 元素为裸 @） */

#ifdef __cplusplus
extern "C" {
#endif

/* ---------------- string：动态字符串 ---------------- */

typedef struct sc_string {
    char    *data;     /* 缓冲区（NUL 结尾；空串可为 NULL） */
    uint32_t size;     /* 字符数（不含 NUL） */
    uint32_t cap;      /* 缓冲区容量 */
} sc_string;

void     sc_string_init(sc_string *_this, const char *s);   /* 构造（s 为 NULL → 空串） */
void     sc_string_drop(sc_string *_this);                  /* 释放缓冲区 */
uint64_t sc_string_len(sc_string *_this);
char    *sc_string_cstr(sc_string *_this);                  /* 始终非 NULL */
void     sc_string_clear(sc_string *_this);
bool     sc_string_reserve(sc_string *_this, uint64_t n);
bool     sc_string_assign(sc_string *_this, const char *s);
bool     sc_string_append(sc_string *_this, const char *s);
bool     sc_string_append_n(sc_string *_this, const char *s, uint64_t n);
bool     sc_string_append_char(sc_string *_this, char c);
bool     sc_string_printf(sc_string *_this, const char *fmt, ...);  /* 追加格式化文本（自动扩容） */
bool     sc_string_insert(sc_string *_this, uint64_t index, const char *s);
bool     sc_string_erase(sc_string *_this, uint64_t index, uint64_t n);
char     sc_string_at(sc_string *_this, uint64_t index);    /* 越界返回 0 */
int64_t  sc_string_find(sc_string *_this, const char *sub, uint64_t start);
int64_t  sc_string_rfind(sc_string *_this, const char *sub);
bool     sc_string_equals(sc_string *_this, const char *s);
bool     sc_string_starts_with(sc_string *_this, const char *s);
bool     sc_string_ends_with(sc_string *_this, const char *s);
bool     sc_string_slice(sc_string *_this, int64_t start, int64_t stop, sc_string *out);
void     sc_string_strip(sc_string *_this);
void     sc_string_lower(sc_string *_this);
void     sc_string_upper(sc_string *_this);
bool     sc_string_clone(sc_string *_this, sc_string *out);

/* ---------------- array：动态值数组 ---------------- */

typedef int32_t (*sc_array_cmp)(void *a, void *b);       /* 比较回调（与 qsort/bsearch 契约一致） */

typedef struct sc_array {
    char    *buf;      /* 连续值块缓冲区（cap 个 elem_sz 字节槽位） */
    uint32_t size;     /* 元素个数 */
    uint32_t cap;      /* 已分配槽位 */
    uint32_t elem_sz;  /* 单元素字节数 */
} sc_array;

void     sc_array_init(sc_array *_this, uint32_t elem_sz);  /* 构造（指定单元素字节数） */
void     sc_array_drop(sc_array *_this);                    /* 释放缓冲区 */
uint64_t sc_array_len(sc_array *_this);
void    *sc_array_data(sc_array *_this);                    /* 缓冲区视图（≈ string_cstr；空可为 NULL） */
void     sc_array_clear(sc_array *_this);
bool     sc_array_reserve(sc_array *_this, uint64_t n);
bool     sc_array_resize(sc_array *_this, uint64_t n);      /* 调整元素数，新增槽位清零 */
bool     sc_array_assign(sc_array *_this, void *src, uint64_t n);   /* 赋值为 n 个元素 */
bool     sc_array_append(sc_array *_this, void *src, uint64_t n);   /* 追加 n 个元素 */
bool     sc_array_push(sc_array *_this, void *value);       /* 追加单元素（拷贝 elem_sz 字节） */
void    *sc_array_pop(sc_array *_this);                     /* 弹出并返回尾元素指针；空返回 NULL */
bool     sc_array_insert(sc_array *_this, uint64_t index, void *value);
bool     sc_array_erase(sc_array *_this, uint64_t index, uint64_t n);
void    *sc_array_at(sc_array *_this, uint64_t index);      /* 越界返回 NULL */
bool     sc_array_set(sc_array *_this, uint64_t index, void *value);
void    *sc_array_front(sc_array *_this);                   /* 空返回 NULL */
void    *sc_array_back(sc_array *_this);                    /* 空返回 NULL */
int64_t  sc_array_find(sc_array *_this, void *key, uint64_t start, sc_array_cmp cmp);  /* 线性查找，未找到 -1 */
int64_t  sc_array_rfind(sc_array *_this, void *key, sc_array_cmp cmp);                 /* 反向线性查找，未找到 -1 */
bool     sc_array_equals(sc_array *_this, sc_array *other, sc_array_cmp cmp);
bool     sc_array_starts_with(sc_array *_this, sc_array *other, sc_array_cmp cmp);
bool     sc_array_ends_with(sc_array *_this, sc_array *other, sc_array_cmp cmp);
bool     sc_array_slice(sc_array *_this, int64_t start, int64_t stop, sc_array *out);  /* 负索引从尾部计 */
void     sc_array_reverse(sc_array *_this);
bool     sc_array_clone(sc_array *_this, sc_array *out);       /* 深拷贝 */
void     sc_array_sort(sc_array *_this, sc_array_cmp cmp);     /* qsort */
void    *sc_array_bsearch(sc_array *_this, void *key, sc_array_cmp cmp);  /* bsearch（须已排序），未找到 NULL */

/* ---------------- ring：SPSC 无锁循环队列（kfifo 风格） ---------------- */
/* 单生产者单消费者无锁有界队列：元素为定长值块（elem_sz 字节），容量为 2 的幂。
 * head（消费者独写）/ tail（生产者独写）为自由递增计数器，槽位下标 = idx & mask，
 * 元素数 = tail - head（无符号回绕）。生产者以 release 发布 tail、消费者以 acquire
 * 观测 → 数据写入先于索引可见；空 = (tail==head)，满 = (tail-head > mask)。
 * 约束：仅 SPSC 安全；多生产者/多消费者须外部加锁。init/drop/clear 仅在无并发时调用。
 * 原子读写经 platform.h 的 sc_* 宏（C11 stdatomic / 平台特化）。 */

typedef struct sc_ring {
    char    *buf;      /* 连续值块缓冲区（cap = mask+1 个 elem_sz 字节槽，2 的幂） */
    uint32_t head;     /* 消费者索引（自由递增；& mask 取槽） */
    uint32_t tail;     /* 生产者索引（自由递增） */
    uint32_t mask;     /* cap - 1（cap 为 2 的幂） */
    uint32_t elem_sz;  /* 单元素字节数 */
} sc_ring;

bool     sc_ring_init(sc_ring *_this, uint32_t elem_sz, uint32_t capacity);  /* capacity 向上取 2 幂；分配失败返回 0 */
void     sc_ring_drop(sc_ring *_this);                      /* 释放缓冲区 */
uint64_t sc_ring_cap(sc_ring *_this);                       /* 容量（2 的幂；未初始化 0） */
uint64_t sc_ring_len(sc_ring *_this);                       /* 当前元素数快照（tail - head） */
bool     sc_ring_is_empty(sc_ring *_this);
bool     sc_ring_is_full(sc_ring *_this);
void     sc_ring_clear(sc_ring *_this);                     /* 复位 head/tail（仅无并发时安全） */
bool     sc_ring_push(sc_ring *_this, void *value);         /* 生产者入队一个元素（拷贝 elem_sz 字节）；满返回 0 */
bool     sc_ring_pop(sc_ring *_this, void *out);            /* 消费者出队到 out（拷贝 elem_sz 字节）；空返回 0 */
void    *sc_ring_peek(sc_ring *_this);                      /* 消费者借用队首元素指针；空返回 NULL */

/* ---------------- list：段式裸 @ 自动指针容器 ---------------- */
/* 元素为裸自动指针 @（sc_thin，32B）；list 拥有元素（每元素一份 retain）。
 * 段式存储：元素 i 住 segs[i / LIST_SEG][i % LIST_SEG]，段索引表与各段内存均来自
 * mem chunk（不受全局 -DSC_POOL 影响）。≈ array 的接口，区别仅在元素是 ref 句柄而非值块。 */

typedef int32_t (*sc_list_cmp)(void *a, void *b);        /* sort 比较回调（实参为元素 .p 实体基址；与 adt.sc @fnc 生成形一致） */

typedef struct sc_list {
    sc_thin **segs;    /* 段索引表（每段 LIST_SEG 个 sc_thin 槽；段内存来自 mem chunk） */
    uint32_t  nsegs;   /* 已分配段数 */
    uint32_t  size;    /* 元素个数 */
    uint32_t  cap;     /* 总槽位（nsegs * LIST_SEG） */
} sc_list;

void     sc_list_init(sc_list *_this);
void     sc_list_drop(sc_list *_this);                      /* 释放全部 retain + 回收段内存 */
uint64_t sc_list_len(sc_list *_this);
void     sc_list_clear(sc_list *_this);                     /* 释放全部 retain，保留段容量 */
bool     sc_list_reserve(sc_list *_this, uint64_t n);
bool     sc_list_push(sc_list *_this, sc_thin value);       /* 尾部追加，retain 元素 */
bool     sc_list_pop(sc_list *_this);                       /* 删除并 release 尾元素；空返回 0 */
sc_thin  sc_list_get(sc_list *_this, uint64_t index);       /* 借用视图（不改计数）；越界返回空句柄 */
bool     sc_list_set(sc_list *_this, uint64_t index, sc_thin value);  /* 改写：retain 新、release 旧 */
bool     sc_list_insert(sc_list *_this, uint64_t index, sc_thin value);
bool     sc_list_remove_at(sc_list *_this, uint64_t index); /* 删除并 release 该元素；越界返回 0 */
int64_t  sc_list_index_of(sc_list *_this, sc_thin value);   /* 按 .p 实体基址比对；未找到 -1 */
void     sc_list_reverse(sc_list *_this);
bool     sc_list_clone(sc_list *_this, sc_list *out);          /* 逐元素 retain 到 out */
void     sc_list_sort(sc_list *_this, sc_list_cmp cmp);

/* ---------------- dict：开放寻址裸 @ 自动指针映射 ---------------- */
/* value 为裸自动指针 @（sc_thin，32B）；dict 拥有 value（每条一份 retain）。
 * key 类型由 init 的 key_size 三态决定（key 经接口以 const void* 裸指针传入，按 key_size 解读）：
 *   key_size  > 0：定长数值/POD，内联 key_size 字节，memcmp 比较（浮点键不安全，限整数/指针类）；
 *   key_size == 0：引用字符串，仅存 const char* 借用指针——dict 不拥有，字符串本体须由 value 对象自持；
 *   key_size == -1：拷贝字符串，put 时 chunk 复制一份、remove/clear/drop 时 recycle。
 * 开放寻址（线性探测 + SwissTable 风格控制字节）：全部 item 内联住一整块桶数组，无 per-item 分配；
 *   整张表仅 ctrl + slots 两块（mem chunk），resize 整体 rehash 重建。每桶布局 [sc_thin value][key]，
 *   value 在前保 8 对齐，stride = align8(sizeof(sc_thin) + (key_size>0 ? key_size : sizeof(char*)))。
 * 取出语义同 list「取用分离」：get 借用返回句柄（不改计数），remove 删除并 release（返回 bool）。
 * 因 init 带 key_size 参数，不参与「声明即构造」——须显式 d.init(key_size)。 */

typedef bool    (*sc_dict_each_fn)(const void *key, sc_thin value, void *ctx);  /* 遍历回调：返回 0 提前终止 */

typedef struct sc_dict {
    uint8_t *ctrl;      /* 控制字节数组（nbuckets 个；空 0xFF / 墓碑 0xFE / 占用 = hash 低 7 位） */
    char    *slots;     /* 桶数据（nbuckets * stride，每桶 [sc_thin value][key]） */
    int32_t  key_size;  /* >0 定长 / 0 引用字符串 / -1 拷贝字符串 */
    uint32_t stride;    /* 单桶字节 = align8(sizeof(sc_thin) + keylen) */
    uint32_t size;      /* 元素数 */
    uint32_t used;      /* 占用 + 墓碑（rehash 阈值用） */
    uint32_t nbuckets;  /* 桶数（2 的幂；0 = 未分配） */
} sc_dict;

void     sc_dict_init(sc_dict *_this, int32_t key_size);    /* 构造（指定 key 模式） */
void     sc_dict_drop(sc_dict *_this);                      /* 释放全部 retain + 回收桶/控制块（保留 key_size） */
uint64_t sc_dict_len(sc_dict *_this);
bool     sc_dict_has(sc_dict *_this, const void *key);
sc_thin  sc_dict_get(sc_dict *_this, const void *key);      /* 借用视图（不改计数）；未命中返回空句柄 */
bool     sc_dict_put(sc_dict *_this, const void *key, sc_thin value);  /* 插入/替换：retain 新、替换时 release 旧 */
bool     sc_dict_remove(sc_dict *_this, const void *key);   /* 删除并 release value；未命中返回 0 */
void     sc_dict_clear(sc_dict *_this);                     /* 清空并 release 全部 value（保留桶容量） */
void     sc_dict_each(sc_dict *_this, sc_dict_each_fn fn, void *ctx);  /* 无序遍历占用桶；回调返 0 即停 */

/* 整数游标双向遍历（游标 = 桶下标；空集/越界返回 -1 或空键/句柄）。
 * 游标在 get/has/each 期间稳定；put/remove 可能 rehash 使其失效，遍历期间勿增删。
 * each 与 next 走同一桶序。典型用法：for (i=sc_dict_first(d); i>=0; i=sc_dict_next(d,i)) {...} */
int64_t      sc_dict_first(sc_dict *_this);                 /* 首个占用桶游标；空集 -1 */
int64_t      sc_dict_last(sc_dict *_this);                  /* 末个占用桶游标；空集 -1 */
int64_t      sc_dict_next(sc_dict *_this, int64_t cur);     /* cur 之后的占用桶；无则 -1 */
int64_t      sc_dict_prev(sc_dict *_this, int64_t cur);     /* cur 之前的占用桶；无则 -1 */
const void  *sc_dict_key_at(sc_dict *_this, int64_t cur);   /* 游标处 key（无效返回 NULL） */
sc_thin      sc_dict_value_at(sc_dict *_this, int64_t cur); /* 游标处 value 借用（无效返回空句柄） */

/* ---------------- bst：AVL/红黑 融合的有序映射 ---------------- */
/* 单棵树以 red_depth 区分 AVL(0) 与红黑(1)——本质是「容忍不平衡的深度」不同（AVL 最多差 1 层，
 * 红黑多容忍 1 层）。value 为裸自动指针 @（sc_thin），bst 拥有每节点一份 retain；key 三态同 dict：
 *   key_size  > 0：定长数值/POD，内联 key_size 字节；默认按宽度（1/2/4/8）做有符号整数比较，
 *                  其余宽度退化为字节序；浮点/无符号/复合键须传 cmp 自定义比较器；
 *   key_size == 0：引用字符串，仅存 const char* 借用指针（bst 不拥有）；strcmp 比较；
 *   key_size == -1：拷贝字符串，put 时 chunk 复制、remove/clear/drop 时 recycle；strcmp 比较。
 * cmp 非空时一律走自定义比较（签名 sign(a-b)，a/b 为「逻辑键」：数值模式为键字节指针、字符串模式为 char*）。
 * 对齐安全：节点用「内部父指针」自然对齐设计，无 pack(1) 外置检索栈，数值比较经 memcpy 装载，杜绝非对齐 UB。
 * 取出语义同 dict「取用分离」：get 借用（不改计数）；remove 删除并 release（返回 bool）。
 * 因 init 带参数，不参与「声明即构造」——须显式 t.init(red_depth, key_size, cmp, ctx)。 */

typedef int32_t (*sc_bst_cmp)(const void *a, const void *b, void *ctx);          /* 自定义比较器（NULL = 内置） */
typedef bool    (*sc_bst_each_fn)(const void *key, sc_thin value, void *ctx);    /* 中序遍历回调：返回 0 提前终止 */

typedef struct sc_bst {
    void    *root;       /* 根节点（不透明 bst_node*） */
    void    *head;       /* 中序首节点 */
    void    *rear;       /* 中序末节点 */
    sc_bst_cmp  cmp;        /* 自定义比较器（NULL = 内置数值/字符串比较） */
    void    *cmp_ctx;    /* 比较器上下文 */
    uint64_t size;       /* 元素数 */
    int32_t  key_size;   /* >0 定长数值 / 0 引用字符串 / -1 拷贝字符串 */
    uint8_t  red_depth;  /* 0 = AVL / 1 = 红黑（>1 预留，按红黑处理） */
} sc_bst;

void     sc_bst_init(sc_bst *_this, uint8_t red_depth, int32_t key_size, sc_bst_cmp cmp, void *cmp_ctx);
void     sc_bst_drop(sc_bst *_this);                        /* 释放全部 retain + 回收全部节点 */
uint64_t sc_bst_len(sc_bst *_this);
bool     sc_bst_has(sc_bst *_this, const void *key);
sc_thin  sc_bst_get(sc_bst *_this, const void *key);        /* 借用视图（不改计数）；未命中返回空句柄 */
bool     sc_bst_put(sc_bst *_this, const void *key, sc_thin value);  /* 插入/替换：retain 新、替换时 release 旧 */
bool     sc_bst_remove(sc_bst *_this, const void *key);     /* 删除并 release value；未命中返回 0 */
void     sc_bst_clear(sc_bst *_this);                       /* 清空并 release 全部 value */
void     sc_bst_each(sc_bst *_this, sc_bst_each_fn fn, void *ctx);  /* 中序（升序）遍历；回调返 0 即停 */

/* 游标双向遍历（游标 = 不透明节点 token；空集/越界返回 0）。中序升序，O(1) 取前驱后继。
 * 游标在只读操作期间稳定；put/remove 可能回收节点使其失效，遍历期间勿增删。
 * 典型用法：for (c=sc_bst_first(t); c; c=sc_bst_next(t,c)) {...} */
int64_t      sc_bst_first(sc_bst *_this);                   /* 中序首节点游标；空集 0 */
int64_t      sc_bst_last(sc_bst *_this);                    /* 中序末节点游标；空集 0 */
int64_t      sc_bst_next(sc_bst *_this, int64_t cur);       /* 后继游标；无则 0 */
int64_t      sc_bst_prev(sc_bst *_this, int64_t cur);       /* 前驱游标；无则 0 */
const void  *sc_bst_key_at(sc_bst *_this, int64_t cur);     /* 游标处 key（无效返回 NULL） */
sc_thin      sc_bst_value_at(sc_bst *_this, int64_t cur);   /* 游标处 value 借用（无效返回空句柄） */
int64_t      sc_bst_index_of(sc_bst *_this, const void *key);  /* key 的 0 基中序序号；未命中 -1 */
int64_t      sc_bst_at(sc_bst *_this, uint64_t index);      /* 0 基中序序号处的游标；越界 0 */
int64_t      sc_bst_most(sc_bst *_this, const void *key);   /* <= key 的最接近项游标（前驱或等于）；无 0 */
int64_t      sc_bst_least(sc_bst *_this, const void *key);  /* >= key 的最接近项游标（后继或等于）；无 0 */

/* ---------------- heap：数组背二叉堆 / 优先队列 ---------------- */
/* (key, value) 对：key 决定优先级，value 为裸自动指针 @（sc_thin），heap 拥有每元素一份 retain。
 * 数组背完全二叉树（隐式编码：父 (i-1)/2、左右子 2i+1/2i+2），槽连续 [sc_thin value][key]，
 * value 在前保 8 对齐，stride = align8(sizeof(sc_thin) + (key_size>0 ? key_size : sizeof(char*)))。
 * push 末尾追加后上滤、pop 末尾补根后下滤，均 O(log n)；扩容几何增长（refit 裸搬移句柄）。
 * min 决定堆向：min=1 小键在顶（最小堆，典型用于 Dijkstra/定时器）、min=0 大键在顶（最大堆）。
 * key 三态/比较器语义同 bst：key_size >0 定长数值（按宽度有符号）/ ==0 引用字符串 / ==-1 拷贝字符串；
 *   cmp 非空时一律走自定义比较（签名 sign(a-b)，a/b 为逻辑键指针）。
 * 取出语义同 dict「取用分离」：peek 借用堆顶（不改计数），pop 删除并 release（返回 bool）。
 * 不提供遍历游标——堆数组非优先序。decrease-key 未内置，优先级变更用「推新+pop 时跳过陈旧」惰性删除。
 * 因 init 带参数，不参与「声明即构造」——须显式 h.init(min, key_size, cmp, ctx)。 */

typedef int32_t (*sc_heap_cmp)(const void *a, const void *b, void *ctx);  /* 自定义比较器（NULL = 内置） */

typedef struct sc_heap {
    char    *slots;     /* 连续槽数组（cap * stride，每槽 [sc_thin value][key]） */
    sc_heap_cmp cmp;       /* 自定义比较器（NULL = 内置数值/字符串比较） */
    void    *cmp_ctx;   /* 比较器上下文 */
    uint32_t stride;    /* 单槽字节 = align8(sizeof(sc_thin) + keylen) */
    uint32_t size;      /* 元素数 */
    uint32_t cap;       /* 槽容量 */
    int32_t  key_size;  /* >0 定长数值 / 0 引用字符串 / -1 拷贝字符串 */
    uint8_t  min;       /* 1 = 最小堆（小键在顶）/ 0 = 最大堆 */
} sc_heap;

void     sc_heap_init(sc_heap *_this, uint8_t min, int32_t key_size, sc_heap_cmp cmp, void *cmp_ctx);
void     sc_heap_drop(sc_heap *_this);                      /* 释放全部 retain + 回收槽数组 */
uint64_t sc_heap_len(sc_heap *_this);
bool     sc_heap_is_empty(sc_heap *_this);
void     sc_heap_clear(sc_heap *_this);                     /* 清空并 release 全部 value（保留容量） */
bool     sc_heap_reserve(sc_heap *_this, uint64_t n);       /* 预留至少 n 槽 */
bool     sc_heap_push(sc_heap *_this, const void *key, sc_thin value);  /* 入堆：retain value，上滤；失败 0 */
bool     sc_heap_pop(sc_heap *_this);                       /* 弹出堆顶并 release，下滤；空返回 0 */
sc_thin  sc_heap_peek(sc_heap *_this);                      /* 借用堆顶 value 句柄（不改计数）；空返回空句柄 */
const void *sc_heap_peek_key(sc_heap *_this);               /* 借用堆顶 key（空返回 NULL） */

/* ---------------- trie：前缀树（字符串键 → 裸 @ 映射） ---------------- */
/* 键恒为 NUL 结尾字符串，逐字节分解进树路径（路径即键，不另存键串）；value 为裸自动指针 @，
 * trie 拥有每键一份 retain（put retain、替换/remove/clear/drop release）。
 * 节点采「首子/次兄」有序链 + 父指针：每节点存一个边字节，兄弟按字节升序——故遍历天然字典序，
 *   子查找在兄弟链上线性（分支因子通常小）。节点 subkeys 记子树内键数，使 count_prefix O(prefix)。
 * 取出语义同 dict「取用分离」：get 借用（不改计数），remove 删除并 release（返回 bool）。
 * 前缀能力：has_prefix/count_prefix/each_prefix（自动补全）/longest_prefix（路由/最长匹配）。
 * 不提供整数游标——键串须沿路径重建，each/each_prefix 用回调在 DFS 中增量拼键，O(总字符数)。
 * init 无参——参与「声明即构造」：var t: trie 自动 trie_init(&t)。 */

typedef bool    (*sc_trie_each_fn)(const char *key, sc_thin value, void *ctx);  /* 遍历回调：完整键串；返回 0 提前终止 */

typedef struct sc_trie {
    void    *root;   /* trie_node*（首次插入前为 NULL；代表空串路径） */
    uint64_t size;   /* 键数 */
} sc_trie;

void     sc_trie_init(sc_trie *_this);                       /* 构造（空树） */
void     sc_trie_drop(sc_trie *_this);                       /* 释放全部 retain + 回收全部节点 */
uint64_t sc_trie_len(sc_trie *_this);
bool     sc_trie_has(sc_trie *_this, const char *key);       /* 是否含精确键 */
sc_thin  sc_trie_get(sc_trie *_this, const char *key);       /* 借用键对应 value（未命中空句柄；不改计数） */
bool     sc_trie_put(sc_trie *_this, const char *key, sc_thin value);  /* 插入/替换：retain 新、替换 release 旧 */
bool     sc_trie_remove(sc_trie *_this, const char *key);    /* 删除并 release；未命中 0；剪枝空节点 */
void     sc_trie_clear(sc_trie *_this);                      /* 清空并 release 全部 value */
bool     sc_trie_has_prefix(sc_trie *_this, const char *prefix);   /* 是否存在以 prefix 开头的键 */
uint64_t sc_trie_count_prefix(sc_trie *_this, const char *prefix); /* 以 prefix 开头的键数（O(prefix)） */
void     sc_trie_each(sc_trie *_this, sc_trie_each_fn fn, void *ctx);  /* 按字典序遍历全部键；回调返 0 即停 */
void     sc_trie_each_prefix(sc_trie *_this, const char *prefix, sc_trie_each_fn fn, void *ctx);  /* 字典序遍历 prefix 开头的键 */
int64_t  sc_trie_longest_prefix(sc_trie *_this, const char *text); /* text 的最长「键前缀」长度；无 -1；空串键 0 */

/* ---------------- lru：LRU 缓存（有界容量 + 最近最少使用淘汰） ---------------- */
/* 组合容器：内嵌 dict（key → 节点指针，O(1) 查找）+ 侵入双向链表（head=MRU、tail=LRU）。
 * value 为裸自动指针 @，lru 拥有每键一份 retain（put 插入/替换 retain、remove/淘汰/clear/drop release）。
 * 节点独立 chunk 分配（地址稳定，不随 dict rehash 移动）：dict 仅存「借用句柄」（不计数）指向节点，
 *   节点拥有 value 与键拷贝（按 key_size 三态，dict 在字符串模式借用节点的键、定长模式各存一份）。
 * 访问语义：get 命中即「触顶」（移到 MRU）并借用 value；peek 不改最近度；has 不触顶。
 * cap=0 表无界（退化为可保插入顺序的 dict）；cap>0 时 put 新键超容即淘汰队尾（LRU）；
 *   set_cap 缩容立即淘汰至 len<=cap。key 三态同 dict。
 * 取出语义同 dict「取用分离」：get/peek 借用（返回句柄，不改计数），remove 删除并 release（返回 bool）。
 * each 按 MRU→LRU 顺序遍历。因 init 带参数，不参与「声明即构造」——须显式 c.init(key_size, cap)。 */

typedef bool    (*sc_lru_each_fn)(const void *key, sc_thin value, void *ctx);  /* 遍历回调（MRU→LRU）：返回 0 提前终止 */

typedef struct sc_lru {
    sc_dict  map;      /* 内嵌字典：key → 节点指针（借用句柄，不计数） */
    void    *head;     /* MRU 端节点（sc_lru_node*；空为 NULL） */
    void    *tail;     /* LRU 端节点（下一个被淘汰） */
    uint64_t cap;      /* 容量上限（0 = 无界） */
    int32_t  key_size; /* 原始三态（>0 定长 / 0 引用串 / -1 拷贝串） */
} sc_lru;

void     sc_lru_init(sc_lru *_this, int32_t key_size, uint64_t cap);  /* 构造（key 模式 + 容量；cap=0 无界） */
void     sc_lru_drop(sc_lru *_this);                        /* 释放全部 retain + 回收全部节点/字典 */
uint64_t sc_lru_len(sc_lru *_this);
bool     sc_lru_is_empty(sc_lru *_this);
uint64_t sc_lru_cap(sc_lru *_this);                         /* 当前容量上限（0 = 无界） */
void     sc_lru_set_cap(sc_lru *_this, uint64_t cap);       /* 调整容量（缩容立即淘汰 LRU 至 len<=cap） */
bool     sc_lru_has(sc_lru *_this, const void *key);        /* 是否含键（不触顶） */
sc_thin  sc_lru_get(sc_lru *_this, const void *key);        /* 取值并触顶（移至 MRU）；未命中空句柄；借用 */
sc_thin  sc_lru_peek(sc_lru *_this, const void *key);       /* 取值不触顶；未命中空句柄；借用 */
bool     sc_lru_put(sc_lru *_this, const void *key, sc_thin value);  /* 插入/替换 + 触顶：retain 新、替换 release 旧；超容淘汰 */
bool     sc_lru_remove(sc_lru *_this, const void *key);     /* 删除并 release value；未命中返回 0 */
void     sc_lru_clear(sc_lru *_this);                       /* 清空并 release 全部 value（保留容量） */
const void *sc_lru_mru_key(sc_lru *_this);                  /* 最近使用键（空返回 NULL） */
const void *sc_lru_lru_key(sc_lru *_this);                  /* 最久未用键（下一被淘汰；空返回 NULL） */
void     sc_lru_each(sc_lru *_this, sc_lru_each_fn fn, void *ctx);  /* 按 MRU→LRU 遍历；回调返 0 即停 */
/* 游标双向遍历（游标 = 不透明节点 token；空集/越界返回 0）。MRU→LRU，只读借用不触顶。
 * 典型用法：for (c=sc_lru_first(l); c; c=sc_lru_next(l,c)) {...}；供 for k, v in lru 降解。 */
int64_t  sc_lru_first(sc_lru *_this);                       /* MRU 端节点游标；空集 0 */
int64_t  sc_lru_last(sc_lru *_this);                        /* LRU 端节点游标；空集 0 */
int64_t  sc_lru_next(sc_lru *_this, int64_t cur);           /* MRU→LRU 方向后继；无则 0 */
int64_t  sc_lru_prev(sc_lru *_this, int64_t cur);           /* 反向前驱；无则 0 */
const void  *sc_lru_key_at(sc_lru *_this, int64_t cur);     /* 游标处 key（无效返回 NULL） */
sc_thin      sc_lru_value_at(sc_lru *_this, int64_t cur);   /* 游标处 value 借用（无效返回空句柄） */


#ifdef __cplusplus
}
#endif

#endif /* SC_ADT_H */
