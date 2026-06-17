/* op_impl.c —— op.sc 语法机制的默认运行时实现
 *
 * 编译器对每个工程都自动编译并链接本文件（op.sc 为默认导入模块，无需 inc）。
 * 契约见同目录 op.h。
 */
#include "op.h"
#include "platform.h"   /* builtins 跨平台基础头（编译时 -I builtins 根目录） */

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
