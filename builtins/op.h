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

#ifdef __cplusplus
}
#endif

#endif /* SC_OP_H */
