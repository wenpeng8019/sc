/* op.h —— op.sc 语法机制的 C ABI 契约与默认运行时声明
 *
 * 角色：op.sc 是默认导入的「语言底层（语法层面）机制」声明模块；本头文件是其
 *       C 侧伴随头，声明这些机制需要的 C 结构体与运行时原型。scc 生成的每个 C
 *       单元都默认带入本头（经 platform.h），其实现由 builtins/op_impl.c 提供
 *       （编译器自动编译并链接，无需 inc）。
 *
 * 约定：
 *   - chain 不拥有元素：remove/pop/cut 等不释放元素指针本身
 *   - chain 元素为 sc 链表结构体（def T: ~ {}，首位有 void *_prev, *_next）
 */
#ifndef SC_OP_H
#define SC_OP_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

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

/* ---------------- future / async：单线程协作式异步机制 ----------------
 * 语言底层机制：future 类型 + async/await/done 关键字 + async_init/loop/final。
 * future 是异步结果句柄（类型擦除：result 为 void*）。含 await 的 rpc 被编译为
 * 状态机；async 把 rpc 调用登记进当前线程事件循环并返回 future&；done 标记 future
 * 就绪并唤醒等待者。
 *
 * 声明在此（默认带入每个 C 单元），但默认运行时实现（libuv）在 builtins/async/
 * async_impl.c —— 仅当源码 inc async.sc 时才编译链接（不污染普通程序）。故用到
 * 这些原语却未 inc async.sc 将报链接错误（符合"异步是可选模块"的取舍）。
 *
 * 可 await 契约：任何自定义异步原语在"完成时"调 future_done 即可接入 await。
 * 线程安全：future_done 可被任意线程调用；运行时在锁内置位并经事件循环唤醒，
 *           所有状态机 resume 只在循环线程串行发生 → 生成的状态机代码无需加锁。 */

typedef struct future {
    int    ready;            /* 0=未就绪, 1=已就绪 */
    void  *result;           /* 类型擦除结果（标量经 intptr_t 往返） */
    void  *frame;            /* 等待者状态机帧（NULL=无人等待） */
    void (*resume)(void *);  /* 等待者恢复入口（状态机函数） */
    void  *next;             /* 就绪队列链接（运行时内部） */
} future;

/* future 方法（op.sc 内 @def future 的成员函数，C 侧实现） */
void     future_init(future *_this);    /* future()：登记到当前事件循环（pending +1） */
uint8_t  future_ready(future *_this);   /* f.ready()：是否已就绪 */
void    *future_get(future *_this);     /* f.get()：取结果（调用点 : T& 强转还原） */

/* 当前线程事件循环生命周期 */
void     async_init(void);              /* 建立事件循环 */
void     async_loop(void);              /* 驱动事件循环至全部异步完成 */
void     async_final(void);             /* 销毁事件循环 */

/* 运行时内部原语（编译器生成码与可 await 契约使用） */
future  *future_new(void);              /* 造未就绪 future（pending +1）：async 启动器内部用 */
void     future_done(future *f, void *result);  /* done 关键字：置就绪 + 唤醒（任意线程安全） */
/* await 握手（生成的状态机调用）：登记本帧为 waiter，返回是否已就绪。
 * 1=已就绪（不让出、直接续跑）；0=未就绪（保存状态后让出）。 */
uint8_t  future_await(future *f, void *frame, void (*resume)(void *));

/* ---------------- com / limit / ioq：设备通讯机制 ----------------
 * com 是机制框架：具体 io 依赖设备，由 com 的 read/write/error 每对象方法指针
 * （MethodPtr，C 侧为结构体函数指针字段）实现——非成员函数（伪类无派生）。
 * limit 是 com 的分身/切片，充当一次 read 的截止边界视图（com 默认 endless io）。
 * ioq 是读写缓存队列（自动膨胀的循环缓冲），com 提供 rq/wq（非 NULL）即支持异步 io。
 *
 * 声明在 op.sc（默认导入）；本头提供 C ABI 结构体与方法原型。limit、ioq 的方法
 * （limit_xxx、ioq_xxx）与通用收发框架的运行时实现属后续阶段（op_impl.c，不依赖 libuv）；
 * 具体设备 io 实现属可选模块（inc com.sc）。未调用即不链接。 */

typedef struct com   com;
typedef struct limit limit;
typedef struct ioq   ioq;

/* limit：com 读截止边界（com 的分身/切片；首位为分身回指 _self，与注入对齐） */
struct limit {
    struct com *_self;       /* 分身回指本体 com（分身机制注入） */
    void *_data;             /* 边界数据起始（运行时填充；limit_data 返回） */
    uint32_t _len;           /* 边界数据长度（运行时填充；limit_len 返回） */
};

void    *limit_data(limit *_this);   /* limit.data()：数据起始地址 */
uint32_t limit_len(limit *_this);    /* limit.len()：数据长度（不含边界本身） */

/* ioq：com 读写缓存队列（循环缓冲）
 * item 为连续一组值，依首值判类型：[size, buf] size≠0=io 缓冲（pull 执行 io）；
 *                                  [0, callback] size=0=io 完成回调地址。 */
struct ioq {
    struct com *com;         /* 所属 com */
    void   **_buf;           /* 循环缓冲存储（运行时分配，自动膨胀） */
    uint32_t _cap;           /* 容量（槽数） */
    uint32_t _head;          /* 队首索引 */
    uint32_t _tail;          /* 队尾索引 */
};

void  ioq_push(ioq *_this, void *buf, int32_t size);        /* 入队一段 io 缓冲 */
void  ioq_notify(ioq *_this, void *cb, void *data);         /* 入队一个完成回调（cb 为函数地址） */
void *ioq_pull(ioq *_this);                                 /* 取队首并执行 io（空则阻塞） */

/* com：设备通讯端点。字段顺序与 op.sc 的 @def com 一致。
 * alloc/free/read/write/error 为每对象方法指针（首参为隐藏接收者 com*）。 */
struct com {
    void    *dev;            /* 设备句柄（设备相关） */
    struct ioq *rq;          /* 读队列（NULL=不支持异步读） */
    struct ioq *wq;          /* 写队列（NULL=不支持异步写） */
    struct limit *(*alloc)(struct com *_this);                              /* 分身构造 → limit& */
    void   (*free)(struct com *_this, struct limit *s);                     /* 分身析构 */
    int32_t (*read)(struct com *_this, void *data, uint32_t *size);         /* 设备读 */
    int32_t (*write)(struct com *_this, void *buf, uint32_t *size);         /* 设备写 */
    int32_t (*error)(struct com *_this);                                    /* 错误回调 */
};

#ifdef __cplusplus
}
#endif

#endif /* SC_OP_H */
