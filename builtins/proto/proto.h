/* proto.h —— sc 协议解析/转换构件的 C ABI 契约（与 builtins/proto/proto.sc 同步维护）
 *
 * 自定义实现指南：
 *   按本头文件实现全部函数，编译为 .c/.o/.a 后经 scc 链接机制替换内置默认实现
 *  （默认实现见同目录 proto_impl.c，编译器自动编译并链接）。
 *
 * 模型概述：
 *   一个 proto 实例 = 分块存储的「类型化字节记录」序列 + 一种消费纪律（FILO / FIFO）。
 *   记录三元组 (tag, data, size)：tag 为上下文/角色标记，data/size 为原始字节。
 *
 * 内存创新（参考 c_prototype 的 stk）：
 *   每块 chunk 固定大小，数据从块首向前排、每条 4 字节索引项从块尾向后排（反向）；
 *   块满即追加新块（永不 realloc/搬移已有数据，指针稳定），释放的整块进空闲缓存复用。
 *   索引项 = kind(高8位) | data_offset(低24位)，故单块最大寻址 16MB；单条记录大小由相邻
 *   索引项偏移之差推出（不显式存 size，省空间）。底层内存走 mem 模块 chunk/recycle。
 *
 * 返回约定：
 *   · 谓词          → bool（1 成功/真 / 0 失败/假）
 *   · drain/peek    → int32_t：>=0 为记录数据字节数，-1 表示空
 *   · each/build_to → int32_t：转换写出的总字节数（out 太小则回报「所需字节」，即可探测扩容）
 */
#ifndef SC_PROTO_H
#define SC_PROTO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 消费纪律：采用 proto.sc 的 @def proto_mode 枚举常量的 sc 命名域拼写（sc_PROTO_FILO...）作为
 * 首选，供 inc proto.sc 的消费单元直接引用；SC_PROTO_* 为等价别名，供纯 C 实现书写。 */
enum {
    sc_PROTO_FILO = 0,   /* 栈：drain 从顶（最后 feed）取 */
    sc_PROTO_FIFO = 1    /* 队列：drain 从头（最早 feed）取 */
};
#define SC_PROTO_FILO sc_PROTO_FILO
#define SC_PROTO_FIFO sc_PROTO_FIFO

/* 协议转换回调：把一条记录 (tag,data,size) 转换写入 out（容量 cap），返回写入/所需字节数。
 * 两趟约定：out==NULL 时只测长（返回该条所需字节，不写入），out!=NULL 时实际写入并返回写入数。
 * tag：generic feed 记录回报用户 tag；类型化/字符串/blob 记录回报其 kind 码（见 proto_impl.c）。 */
typedef int32_t (*sc_proto_xform)(uint32_t tag, void *data, uint32_t size,
                                  void *out, uint32_t cap, void *ctx);

/* proto 实例（纯数据布局；内部字段由实现私有维护，sc 侧勿直接读写）。 */
typedef struct sc_proto {
    void    *head;      /* chunk 链头（最旧块） */
    void    *tail;      /* chunk 链尾（最新块，feed 目标） */
    void    *cache;     /* 空闲 chunk 缓存单链 */
    uint32_t num;       /* 当前记录条数（sc_proto_depth 回报） */
    uint32_t chunk_sz;  /* 标准 chunk 载荷字节 */
    uint32_t cache_sz;  /* 空闲缓存上限（标准块数） */
    uint32_t cache_n;   /* 当前缓存块数 */
    uint32_t mode;      /* SC_PROTO_FILO / SC_PROTO_FIFO */
} sc_proto;

/* 构造：mode 见上；chunk_sz 为单块载荷字节（0 用默认 4KiB，向上取整对齐）；
 * cache_sz 为空闲块缓存上限（0 = 不缓存，释放即归还 mem）。成功返回 true。 */
bool     sc_proto_init(sc_proto *_this, uint32_t mode, uint32_t chunk_sz, uint32_t cache_sz);
/* 析构：释放全部 chunk 与缓存，归还 mem。 */
void     sc_proto_drop(sc_proto *_this);
/* 清空全部记录（保留缓存，便于复用）。 */
void     sc_proto_clear(sc_proto *_this);
/* 当前记录条数。 */
uint64_t sc_proto_depth(sc_proto *_this);
/* 是否为空。 */
bool     sc_proto_is_empty(sc_proto *_this);

/* ---------------- feed：入栈/队列 ----------------
 * 通用三元组：拷贝 size 字节 + 用户 tag。size 上限为单块可容（超标者独立成块）。 */
bool     sc_proto_feed(sc_proto *_this, uint32_t tag, const void *data, uint32_t size);

/* 类型化 feed（可选 printf format 串；fmt==NULL 用内置默认格式）。
 * fmt 指针被内联存储、不拷贝——须在 build/each 前保持有效（通常为字符串字面量）。 */
bool     sc_proto_push_b(sc_proto *_this, bool v, const char *fmt);
bool     sc_proto_push_i1(sc_proto *_this, int8_t v, const char *fmt);
bool     sc_proto_push_i2(sc_proto *_this, int16_t v, const char *fmt);
bool     sc_proto_push_i4(sc_proto *_this, int32_t v, const char *fmt);
bool     sc_proto_push_i8(sc_proto *_this, int64_t v, const char *fmt);
bool     sc_proto_push_u1(sc_proto *_this, uint8_t v, const char *fmt);
bool     sc_proto_push_u2(sc_proto *_this, uint16_t v, const char *fmt);
bool     sc_proto_push_u4(sc_proto *_this, uint32_t v, const char *fmt);
bool     sc_proto_push_u8(sc_proto *_this, uint64_t v, const char *fmt);
bool     sc_proto_push_f4(sc_proto *_this, float v, const char *fmt);
bool     sc_proto_push_f8(sc_proto *_this, double v, const char *fmt);
/* 字符串：拷贝内容（内部 NUL 结尾）；trim!=NULL 时去除末尾属于 trim 集合的字符。 */
bool     sc_proto_push_str(sc_proto *_this, const char *v, const char *trim);
/* 二进制块：拷贝 size 字节 + 内联 transform 回调（build 时对本条调用 cb）。 */
bool     sc_proto_push_blob(sc_proto *_this, const void *data, uint32_t size, sc_proto_xform cb);
/* 裸指针：内联存储指针 + transform 回调（不拷贝指向内容）。 */
bool     sc_proto_push_ptr(sc_proto *_this, const void *v, sc_proto_xform cb);

/* ---------------- drain：按 mode 取出/消费 ----------------
 * 取出一条并消费（FILO 顶 / FIFO 头）：回填 *r_tag、*r_data（指向内部缓冲，有效期至下次
 * 变更操作），返回数据字节数；空返回 -1。generic 记录 *r_data 指向纯数据、*r_tag 为用户 tag；
 * 其它记录 *r_data 指向内联载荷、*r_tag 为 kind 码。 */
int32_t  sc_proto_drain(sc_proto *_this, uint32_t *r_tag, void **r_data);
/* 看一条不消费（语义同 drain 的回填）。 */
int32_t  sc_proto_peek(sc_proto *_this, uint32_t *r_tag, void **r_data);
/* 丢弃 n 条（FILO 从顶 / FIFO 从头）；n<=0 为空操作；n 超过 depth 时清空。 */
bool     sc_proto_back(sc_proto *_this, int32_t n);

/* ---------------- 重组转换（不消费）----------------
 * 按插入顺序（最旧→最新）遍历每条记录，逐条经 cb 转换累加写入 out：out==NULL 只测长。
 * 返回总字节数（若 > cap 表示 out 不足，已按 cap 截断，返回值即所需总字节）。 */
int32_t  sc_proto_each(sc_proto *_this, sc_proto_xform cb, void *ctx, void *out, uint32_t cap);

/* 内置格式化重组：按插入顺序把各条渲染为文本，条间以 delim 分隔（delim==NULL 无分隔）。
 * 类型化记录经 snprintf（用其 format 串或内置默认格式）；str 直出；blob/ptr 经其内联 cb；
 * generic feed 记录按原始字节直出。写入 out（NUL 结尾），返回不含 NUL 的字节数（可测长探测）。 */
int32_t  sc_proto_build_to(sc_proto *_this, const char *delim, char *out, uint32_t cap);
/* 同 build_to，但由 mem 分配恰好容量的缓冲返回（NUL 结尾）；调用方用 mem recycle 释放；失败 NULL。 */
char    *sc_proto_build(sc_proto *_this, const char *delim);

#ifdef __cplusplus
}
#endif

#endif /* SC_PROTO_H */
