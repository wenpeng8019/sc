/* proto_impl.c —— sc 内置 proto 构件的默认实现
 *
 * 编译器在单元图包含 builtins/proto/proto.sc 时自动编译并链接本文件；
 * 可通过 scc --proto <x.c|x.o|x.a> 替换为自定义实现（契约见 proto.h）。
 *
 * 存储布局（参考 c_prototype 的 stk）：
 *   proto = 双向 chunk 链。每块 chunk = [proto_chunk 头 | cap 字节载荷]。
 *   载荷内：数据区从偏移 0 向前增长；每条记录一个 4 字节索引项从块尾向后增长。
 *   索引项 i（1-based）位于载荷偏移 cap - i*4，值 = (kind<<24) | data_offset(低24位)。
 *   记录 i 数据大小由相邻偏移之差推出：末条 = data_used - off[count]，其余 = off[i+1]-off[i]。
 *   data_used = cap - remain - count*4（不显式存 size，省空间）。单块最大寻址 16MB。
 *
 * 消费纪律：
 *   FILO —— feed 追加到 tail、drain 消费 tail 顶（就地回收数据与索引空间）。
 *   FIFO —— feed 追加到 tail、drain 消费 head（head 游标前进，整块耗尽时再回收）。
 *   两向遍历（each/build）恒按插入顺序：chunk head→tail，块内记录 head..count。
 */
#include "proto.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */
#include "mem/mem.h"    /* chunk / recycle（分块内存与结果缓冲；不受全局 -DSC_POOL 影响） */
#include <string.h>     /* memcpy / strlen / strchr */
#include <stdio.h>      /* snprintf（build 内置格式化） */

#define PSZ ((uint32_t)sizeof(void *))   /* 内联指针字段字节宽 */

/* 记录 kind 码（存于索引项高 8 位） */
#define K_FEED 0x01u   /* 通用 feed：载荷 [u4 tag][data] */
#define K_B    0x02u
#define K_I1   0x03u
#define K_I2   0x04u
#define K_I4   0x05u
#define K_I8   0x06u
#define K_U1   0x07u
#define K_U2   0x08u
#define K_U4   0x09u
#define K_U8   0x0Au
#define K_F4   0x0Bu
#define K_F8   0x0Cu   /* 标量：载荷 [const char* fmt][value] */
#define K_STR  0x0Du   /* 字符串：载荷 [bytes...][NUL] */
#define K_BLOB 0x0Eu   /* 二进制块：载荷 [xform cb][data] */
#define K_PTR  0x0Fu   /* 裸指针：载荷 [xform cb][void* ptr] */

typedef struct proto_chunk {
    struct proto_chunk *next;
    struct proto_chunk *prev;
    uint32_t cap;      /* 载荷容量字节 */
    uint32_t remain;   /* 数据区尾与索引区首之间的空闲字节 */
    uint32_t count;    /* 本块记录条数（含已被 FIFO 消费但未回收者） */
    uint32_t head;     /* FIFO 游标：首条存活记录的 1-based 序号（FILO 恒为 1） */
} proto_chunk;

#define PC_PAYLOAD(c) ((uint8_t *)((c) + 1))

/* ---------------- 索引/记录读写 ---------------- */

static uint32_t pc_entry(proto_chunk *c, uint32_t i /*1-based*/) {
    uint32_t e;
    memcpy(&e, PC_PAYLOAD(c) + c->cap - i * 4u, 4);
    return e;
}
static uint32_t pc_off(uint32_t e)  { return e & 0xFFFFFFu; }
static uint8_t  pc_kind(uint32_t e) { return (uint8_t)(e >> 24); }

/* 记录 i 的数据指针，*size 回填其字节数 */
static uint8_t *pc_rec(proto_chunk *c, uint32_t i, uint32_t *size) {
    uint32_t off = pc_off(pc_entry(c, i));
    uint32_t end;
    if (i < c->count) end = pc_off(pc_entry(c, i + 1));
    else              end = c->cap - c->remain - c->count * 4u;  /* data_used */
    if (size) *size = end - off;
    return PC_PAYLOAD(c) + off;
}

/* ---------------- chunk 生命周期 ---------------- */

/* 归还整块：容量为标准且缓存未满则入缓存，否则交还 mem */
static void proto_release(sc_proto *p, proto_chunk *c) {
    if (c->cap == p->chunk_sz && p->cache_n < p->cache_sz) {
        c->next = (proto_chunk *)p->cache;
        p->cache = c;
        p->cache_n++;
    } else {
        sc_recycle(c);
    }
}

/* 追加一条记录：预留 datasize 字节数据 + 4 字节索引，返回数据写入指针；失败 NULL */
static uint8_t *proto_reserve(sc_proto *p, uint8_t kind, uint32_t datasize) {
    uint32_t need = datasize + 4u;                 /* 数据 + 索引项 */
    proto_chunk *c = (proto_chunk *)p->tail;
    if (!c || c->remain < need) {
        proto_chunk *nc = NULL;
        uint32_t cap;
        if (need <= p->chunk_sz) {
            cap = p->chunk_sz;
            if (p->cache) {                        /* 复用缓存块 */
                nc = (proto_chunk *)p->cache;
                p->cache = nc->next;
                p->cache_n--;
            }
        } else {
            cap = need;                            /* 超标记录独立成块 */
        }
        if (!nc) {
            nc = (proto_chunk *)sc_chunk(sizeof(proto_chunk) + cap);
            if (!nc) return NULL;
            nc->cap = cap;
        }
        nc->remain = nc->cap;
        nc->count  = 0;
        nc->head   = 1;
        nc->next   = NULL;
        nc->prev   = (proto_chunk *)p->tail;
        if (p->tail) ((proto_chunk *)p->tail)->next = nc;
        else         p->head = nc;
        p->tail = nc;
        c = nc;
    }
    {
        uint32_t off = c->cap - c->remain - c->count * 4u;   /* 当前 data_used */
        uint32_t idx = c->cap - (c->count + 1u) * 4u;        /* 新索引项位置 */
        uint32_t entry = ((uint32_t)kind << 24) | (off & 0xFFFFFFu);
        memcpy(PC_PAYLOAD(c) + idx, &entry, 4);
        c->count++;
        c->remain -= need;
        p->num++;
        return PC_PAYLOAD(c) + off;
    }
}

/* 摘除并归还两端已耗空的块（在下一次操作起始处惰性执行，保证刚 drain 的数据有效期至此） */
static void trim_tail_empty(sc_proto *p) {
    proto_chunk *c = (proto_chunk *)p->tail;
    while (c && c->count < c->head) {          /* FILO：count 归 0 即空 */
        proto_chunk *pv = c->prev;
        if (pv) pv->next = NULL; else p->head = NULL;
        p->tail = pv;
        proto_release(p, c);
        c = pv;
    }
}
static void trim_head_empty(sc_proto *p) {
    proto_chunk *c = (proto_chunk *)p->head;
    while (c && c->head > c->count) {          /* FIFO：游标越过末条即空 */
        proto_chunk *nx = c->next;
        if (nx) nx->prev = NULL; else p->tail = NULL;
        p->head = nx;
        proto_release(p, c);
        c = nx;
    }
}

/* ---------------- 构造/析构/清空 ---------------- */

bool sc_proto_init(sc_proto *p, uint32_t mode, uint32_t chunk_sz, uint32_t cache_sz) {
    if (!p) return false;
    if (chunk_sz == 0) chunk_sz = 4096u;
    chunk_sz = (chunk_sz + 63u) & ~63u;              /* 上对齐 64 */
    if (chunk_sz > 0xFFF000u) chunk_sz = 0xFFF000u;  /* 保 offset < 16MB */
    p->head = p->tail = p->cache = NULL;
    p->num = 0;
    p->chunk_sz = chunk_sz;
    p->cache_sz = cache_sz;
    p->cache_n = 0;
    p->mode = (mode == SC_PROTO_FIFO) ? SC_PROTO_FIFO : SC_PROTO_FILO;
    return true;
}

void sc_proto_clear(sc_proto *p) {
    proto_chunk *c;
    if (!p) return;
    c = (proto_chunk *)p->head;
    while (c) { proto_chunk *nx = c->next; proto_release(p, c); c = nx; }
    p->head = p->tail = NULL;
    p->num = 0;
}

void sc_proto_drop(sc_proto *p) {
    proto_chunk *c;
    if (!p) return;
    c = (proto_chunk *)p->head;
    while (c) { proto_chunk *nx = c->next; sc_recycle(c); c = nx; }
    c = (proto_chunk *)p->cache;
    while (c) { proto_chunk *nx = c->next; sc_recycle(c); c = nx; }
    p->head = p->tail = p->cache = NULL;
    p->num = 0;
    p->cache_n = 0;
}

uint64_t sc_proto_depth(sc_proto *p)  { return p ? p->num : 0; }
bool     sc_proto_is_empty(sc_proto *p) { return !p || p->num == 0; }

/* ---------------- feed ---------------- */

bool sc_proto_feed(sc_proto *p, uint32_t tag, const void *data, uint32_t size) {
    uint8_t *dst = proto_reserve(p, (uint8_t)K_FEED, 4u + size);
    if (!dst) return false;
    memcpy(dst, &tag, 4);
    if (size && data) memcpy(dst + 4, data, size);
    return true;
}

/* 标量：载荷 [const char* fmt][value] */
#define PUSH_SCALAR(FN, KIND, CTYPE)                                       \
    bool FN(sc_proto *p, CTYPE v, const char *fmt) {                       \
        uint8_t *dst = proto_reserve(p, (uint8_t)(KIND), PSZ + (uint32_t)sizeof(CTYPE)); \
        if (!dst) return false;                                            \
        memcpy(dst, &fmt, PSZ);                                            \
        memcpy(dst + PSZ, &v, sizeof(CTYPE));                              \
        return true;                                                       \
    }

/* bool 单独处理（存 1 字节） */
bool sc_proto_push_b(sc_proto *p, bool v, const char *fmt) {
    uint8_t b = v ? 1u : 0u;
    uint8_t *dst = proto_reserve(p, (uint8_t)K_B, PSZ + 1u);
    if (!dst) return false;
    memcpy(dst, &fmt, PSZ);
    dst[PSZ] = b;
    return true;
}
PUSH_SCALAR(sc_proto_push_i1, K_I1, int8_t)
PUSH_SCALAR(sc_proto_push_i2, K_I2, int16_t)
PUSH_SCALAR(sc_proto_push_i4, K_I4, int32_t)
PUSH_SCALAR(sc_proto_push_i8, K_I8, int64_t)
PUSH_SCALAR(sc_proto_push_u1, K_U1, uint8_t)
PUSH_SCALAR(sc_proto_push_u2, K_U2, uint16_t)
PUSH_SCALAR(sc_proto_push_u4, K_U4, uint32_t)
PUSH_SCALAR(sc_proto_push_u8, K_U8, uint64_t)
PUSH_SCALAR(sc_proto_push_f4, K_F4, float)
PUSH_SCALAR(sc_proto_push_f8, K_F8, double)

bool sc_proto_push_str(sc_proto *p, const char *v, const char *trim) {
    uint32_t len;
    uint8_t *dst;
    if (!v) v = "";
    len = (uint32_t)strlen(v);
    if (trim && *trim)
        while (len && strchr(trim, v[len - 1])) len--;
    dst = proto_reserve(p, (uint8_t)K_STR, len + 1u);
    if (!dst) return false;
    memcpy(dst, v, len);
    dst[len] = 0;
    return true;
}

bool sc_proto_push_blob(sc_proto *p, const void *data, uint32_t size, sc_proto_xform cb) {
    uint8_t *dst = proto_reserve(p, (uint8_t)K_BLOB, PSZ + size);
    if (!dst) return false;
    memcpy(dst, &cb, PSZ);
    if (size && data) memcpy(dst + PSZ, data, size);
    return true;
}

bool sc_proto_push_ptr(sc_proto *p, const void *v, sc_proto_xform cb) {
    uint8_t *dst = proto_reserve(p, (uint8_t)K_PTR, PSZ * 2u);
    if (!dst) return false;
    memcpy(dst, &cb, PSZ);
    memcpy(dst + PSZ, &v, PSZ);
    return true;
}

/* ---------------- drain / peek / back ---------------- */

/* 解码一条记录并回填：generic 回报用户 tag + 纯数据；其余回报 kind 码 + 内联载荷 */
static int32_t decode_record(proto_chunk *c, uint32_t i, uint32_t *r_tag, void **r_data) {
    uint32_t sz;
    uint8_t *d = pc_rec(c, i, &sz);
    uint8_t k = pc_kind(pc_entry(c, i));
    if (k == K_FEED) {
        uint32_t tag;
        memcpy(&tag, d, 4);
        if (r_tag)  *r_tag = tag;
        if (r_data) *r_data = d + 4;
        return (int32_t)(sz - 4u);
    }
    if (r_tag)  *r_tag = k;
    if (r_data) *r_data = d;
    return (int32_t)sz;
}

int32_t sc_proto_peek(sc_proto *p, uint32_t *r_tag, void **r_data) {
    proto_chunk *c;
    if (!p) return -1;
    if (p->mode == SC_PROTO_FIFO) {
        trim_head_empty(p);
        c = (proto_chunk *)p->head;
        if (!c) return -1;
        return decode_record(c, c->head, r_tag, r_data);
    }
    trim_tail_empty(p);
    c = (proto_chunk *)p->tail;
    if (!c) return -1;
    return decode_record(c, c->count, r_tag, r_data);
}

/* 消费一条（假定已 trim、存在存活记录）。数据字节保持完整，有效期至下次变更操作。 */
static void pop_one(sc_proto *p) {
    if (p->mode == SC_PROTO_FIFO) {
        proto_chunk *c = (proto_chunk *)p->head;
        c->head++;             /* 游标前进；整块耗尽由下次 trim 回收 */
    } else {
        proto_chunk *c = (proto_chunk *)p->tail;
        uint32_t sz;
        pc_rec(c, c->count, &sz);
        c->count--;
        c->remain += sz + 4u;  /* 回收顶部数据 + 索引空间 */
    }
    p->num--;
}

int32_t sc_proto_drain(sc_proto *p, uint32_t *r_tag, void **r_data) {
    int32_t r = sc_proto_peek(p, r_tag, r_data);
    if (r < 0) return -1;
    pop_one(p);
    return r;
}

bool sc_proto_back(sc_proto *p, int32_t n) {
    if (!p || n <= 0) return true;
    while (n-- > 0 && p->num > 0) {
        if (p->mode == SC_PROTO_FIFO) {
            trim_head_empty(p);
            if (!p->head) break;
        } else {
            trim_tail_empty(p);
            if (!p->tail) break;
        }
        pop_one(p);
    }
    return true;
}

/* ---------------- each / build ---------------- */

int32_t sc_proto_each(sc_proto *p, sc_proto_xform cb, void *ctx, void *out, uint32_t cap) {
    proto_chunk *c;
    uint32_t total = 0;
    if (!p || !cb) return 0;
    for (c = (proto_chunk *)p->head; c; c = c->next) {
        uint32_t i;
        for (i = c->head; i <= c->count; i++) {
            uint32_t sz, dsz, tag, room;
            uint8_t *d = pc_rec(c, i, &sz);
            uint8_t *data;
            uint8_t k = pc_kind(pc_entry(c, i));
            int32_t w;
            if (k == K_FEED) { memcpy(&tag, d, 4); data = d + 4; dsz = sz - 4u; }
            else             { tag = k; data = d; dsz = sz; }
            room = (out && total < cap) ? (cap - total) : 0;
            w = cb(tag, data, dsz, out ? (uint8_t *)out + total : NULL, room, ctx);
            if (w < 0) return w;
            total += (uint32_t)w;
        }
    }
    return (int32_t)total;
}

/* 写 src[0..n) 入 out（wcap 已预留 NUL），越界只累计不写 */
static void emit(char *out, uint32_t wcap, uint32_t *total, const char *src, uint32_t n) {
    if (out) {
        uint32_t t = *total, j;
        for (j = 0; j < n; j++)
            if (t + j < wcap) out[t + j] = src[j];
    }
    *total += n;
}

/* 内置渲染单条记录 */
static void fmt_record(proto_chunk *c, uint32_t i, char *out, uint32_t wcap, uint32_t *total) {
    uint32_t sz;
    uint8_t *d = pc_rec(c, i, &sz);
    uint8_t k = pc_kind(pc_entry(c, i));
    char buf[64];
    const char *fmt;
    int n;
    switch (k) {
    case K_FEED: emit(out, wcap, total, (char *)(d + 4), sz - 4u); return;
    case K_STR:  emit(out, wcap, total, (char *)d, sz ? sz - 1u : 0u); return;  /* 去尾 NUL */
    case K_BLOB:
    case K_PTR: {
        sc_proto_xform cb;
        uint8_t *data;
        uint32_t dsz;
        int need;
        memcpy(&cb, d, PSZ);
        if (k == K_BLOB) { data = d + PSZ; dsz = sz - PSZ; }
        else             { data = d + PSZ; dsz = 0; }  /* ptr：data 指向存储的指针值 */
        need = cb ? cb(k, data, dsz, NULL, 0, NULL) : 0;
        if (need < 0) need = 0;
        if (out && cb) {
            uint32_t room = (*total < wcap) ? wcap - *total : 0;
            cb(k, data, dsz, out + *total, room, NULL);
        }
        *total += (uint32_t)need;
        return;
    }
    default: break;
    }
    /* 标量 */
    memcpy(&fmt, d, PSZ);
    switch (k) {
    case K_B:  n = fmt ? snprintf(buf, sizeof buf, fmt, (int)d[PSZ])
                       : snprintf(buf, sizeof buf, "%s", d[PSZ] ? "true" : "false"); break;
    case K_I1: { int8_t   v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%d",   (int)v); } break;
    case K_I2: { int16_t  v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%d",   (int)v); } break;
    case K_I4: { int32_t  v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%d",   (int)v); } break;
    case K_I8: { int64_t  v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%lld", (long long)v); } break;
    case K_U1: { uint8_t  v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%u",   (unsigned)v); } break;
    case K_U2: { uint16_t v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%u",   (unsigned)v); } break;
    case K_U4: { uint32_t v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%u",   (unsigned)v); } break;
    case K_U8: { uint64_t v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%llu", (unsigned long long)v); } break;
    case K_F4: { float    v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%g",   (double)v); } break;
    case K_F8: { double   v; memcpy(&v, d + PSZ, sizeof v); n = snprintf(buf, sizeof buf, fmt ? fmt : "%g",   v); } break;
    default:   n = 0; break;
    }
    if (n < 0) n = 0;
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    emit(out, wcap, total, buf, (uint32_t)n);
}

int32_t sc_proto_build_to(sc_proto *p, const char *delim, char *out, uint32_t cap) {
    proto_chunk *c;
    uint32_t dlen = delim ? (uint32_t)strlen(delim) : 0u;
    uint32_t wcap = cap ? cap - 1u : 0u;   /* 预留 NUL */
    uint32_t total = 0;
    int first = 1;
    if (!p) { if (out && cap) out[0] = 0; return 0; }
    for (c = (proto_chunk *)p->head; c; c = c->next) {
        uint32_t i;
        for (i = c->head; i <= c->count; i++) {
            if (!first && dlen) emit(out, wcap, &total, delim, dlen);
            first = 0;
            fmt_record(c, i, out, wcap, &total);
        }
    }
    if (out && cap) out[total < wcap ? total : (cap ? cap - 1u : 0u)] = 0;
    return (int32_t)total;
}

char *sc_proto_build(sc_proto *p, const char *delim) {
    int32_t need = sc_proto_build_to(p, delim, NULL, 0);
    char *buf;
    if (need < 0) return NULL;
    buf = (char *)sc_chunk((uint32_t)need + 1u);
    if (!buf) return NULL;
    sc_proto_build_to(p, delim, buf, (uint32_t)need + 1u);
    return buf;
}
