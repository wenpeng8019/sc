/* op_impl.c —— op.sc 语法机制的默认运行时实现
 *
 * 编译器对每个工程都自动编译并链接本文件（op.sc 为默认导入模块，无需 inc）。
 * 契约见同目录 op.h。
 */
#include "op.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */
#include <stdlib.h>     /* ioq 循环缓冲：malloc/free */

/* ---------------- chain：侵入式双向链表 ---------------- */
/* 元素首部内嵌 _prev/_next（sc 编译器注入），偏移固定在结构体最前面。
 * 约定：首元素 _prev = 尾元素（rear），尾元素 _next = NULL。 */

/* _prev/_next 固定在结构体最前面（void *_prev, *_next）*/
#define LPREV(it) (*(void **)((char *)(it) + 0))       /* _prev at offset 0 */
#define LNEXT(it) (*(void **)((char *)(it) + sizeof(void *))) /* _next at offset 8 */

/* 边界安全逻辑前驱：head 无前驱 → 返回 NULL（供编译器 prev(o) 内置导航调用）。
 * 约定 head._prev = rear（复用指针省一个 rear 字段），故裸 _prev 不能直接当前驱。
 * 判定：真前驱 p 满足 LNEXT(p) == it；head 的 _prev(=rear) 其 _next == NULL ≠ it → NULL。 */
void *chain_prev(void *it) {
    if (!it) return NULL;
    void *p = LPREV(it);
    return (p && LNEXT(p) == it) ? p : NULL;
}

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

/* ---------------- limit：com 读截止边界（com 的分身/切片）---------------- */
/* _data/_len 由运行时按边界规格填充（P3）；此处提供访问器（cImpl 类方法）。 */
void    *limit_data(limit *_this) { return _this ? _this->_data : NULL; }
uint32_t limit_len (limit *_this) { return _this ? _this->_len  : 0;    }

/* ---------------- ioq：com 读写缓存队列（自膨胀循环缓冲）---------------- */
/* 槽统一为 void*（size 以 (void*)(uintptr_t) 编码）。_head/_tail 为单调递增的
 * 逻辑槽计数，物理下标取 % _cap。每项变长，靠首槽 tag/size 自身区分：
 *   io 缓冲项 : [size(!=0), buf]      —— 2 槽，pull 时执行 io（按 rq/wq 方向 read/write）
 *   完成回调项: [0, cb, data]         —— 3 槽，pull 时回调 ((void(*)(void*))cb)(data)
 * （无 libuv：pull 同步执行 io、空队列返回 NULL 不阻塞；真实异步驱动应自行延迟兑现。）*/

/* 确保至少 need 个空闲槽，不足则倍增扩容并把现存项搬到新缓冲首部。 */
static void ioq_ensure(ioq *q, uint32_t need) {
    uint32_t used = q->_tail - q->_head;
    if (q->_cap && q->_cap - used >= need) return;
    uint32_t ncap = q->_cap ? q->_cap : 8;
    while (ncap - used < need) ncap *= 2;
    void **nb = (void **)malloc((size_t)ncap * sizeof(void *));
    for (uint32_t i = 0; i < used; i++) nb[i] = q->_buf[(q->_head + i) % q->_cap];
    free(q->_buf);
    q->_buf  = nb;
    q->_cap  = ncap;
    q->_head = 0;
    q->_tail = used;
}

/* 入队一段 io 缓冲项 [size, buf]（size 须 != 0）。 */
void ioq_push(ioq *_this, void *buf, int32_t size) {
    ioq_ensure(_this, 2);
    _this->_buf[ _this->_tail      % _this->_cap] = (void *)(uintptr_t)(uint32_t)size;
    _this->_buf[(_this->_tail + 1) % _this->_cap] = buf;
    _this->_tail += 2;
}

/* 入队一个完成回调项 [0, cb, data]。 */
void ioq_notify(ioq *_this, void *cb, void *data) {
    ioq_ensure(_this, 3);
    _this->_buf[ _this->_tail      % _this->_cap] = (void *)(uintptr_t)0;
    _this->_buf[(_this->_tail + 1) % _this->_cap] = cb;
    _this->_buf[(_this->_tail + 2) % _this->_cap] = data;
    _this->_tail += 3;
}

/* 取队首并执行 io：缓冲项按 rq/wq 方向调 com 的 read/write 并返回 buf；
 * 回调项调用 cb(data) 返回 NULL；空队列返回 NULL（无 libuv 不阻塞）。 */
void *ioq_pull(ioq *_this) {
    if (_this->_head == _this->_tail) return NULL;       /* 空 */
    uintptr_t tag = (uintptr_t)_this->_buf[_this->_head % _this->_cap];
    if (tag != 0) {                                      /* io 缓冲项 [size, buf] */
        void *buf = _this->_buf[(_this->_head + 1) % _this->_cap];
        _this->_head += 2;
        com *c = _this->com;
        uint32_t n = (uint32_t)tag;
        if (c) {
            if (c->rq == _this) { if (c->read)  c->read (c, buf, &n); }
            else                { if (c->write) c->write(c, buf, &n); }
        }
        return buf;
    }
    /* 完成回调项 [0, cb, data] */
    void *cb   = _this->_buf[(_this->_head + 1) % _this->_cap];
    void *data = _this->_buf[(_this->_head + 2) % _this->_cap];
    _this->_head += 3;
    if (cb) ((void (*)(void *))cb)(data);
    return NULL;
}
