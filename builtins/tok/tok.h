/* tok.h —— 分布式 token（tok / dep / form 机制）的 C ABI 契约
 *           （与 builtins/tok/tok.sc 协议、builtins/tok/tok_impl.c 实现同步维护）
 *
 * tok 是「分布式 token」：跨进程以字符串 id 唯一标识的共享量，值为类型擦除 @（sc_afat）。
 *   · tok t: "id"        声明一个 token 句柄（enforce 纯从）；
 *   · tok t: "id"<换行+缩进体>  声明并挂 combine 回调（form 候选：缩进体即 combine）；
 *   · form t, v          初始化 form token：灌初值并升格为 form 主；
 *   · dep all/any: ...   声明 token 间依赖关系（follow 回调，all=与门 / any=或门）。
 * 均为模块域静态对象，注册延迟到模块 init（编译器生成 token_bind / token_depend）。
 *
 * 句柄类型 token 的 sc 侧协议（@def token，方法 get/set）声明在 op.sc（语言内核机制，
 *   默认导入，供编译器识别方法分派）；本头由 op.h 默认带入（随 platform.h 进入每个 C
 *   单元），实现见同目录 tok_impl.c（经 op→tok 隐式依赖始终随工程编译链接）。
 *
 * v2 运行时（见 tok_impl.c）：全局表按字符串 id intern（adt 哈希 dict，O(1)）；form 触发
 *   「就绪(ready)」依赖更新（区别于 set 的「变更(changed)」）；未 form 的 form 候选其 set
 *   入挂起队列、form 时回放；id 以 '/' 开头的 token 持每-token 可重入深锁支持多线程并发。 */
#ifndef SC_TOK_H
#define SC_TOK_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct token token;

/* combine 上下文（this）：form 候选 combine 体的唯一形参 __sctok_in&。
 *   值均为 @（sc_afat，类型擦除自描述胖指针，体内 (e:T@) 还原典型对象）：
 *     sender —— 发送者（当前由 set 触发时恒为空 @，预留扩展）；
 *     base   —— 当前值（form 主上一轮结果）；
 *     input  —— 本次 set 输入；
 *     tag    —— set 随附的整型标签（t.set(v, tag) 透传，体内分流用）。
 *   combine 返回新值（@）。 */
typedef struct __sctok_in {
    sc_afat sender;
    sc_afat base;
    sc_afat input;
    int32_t tag;
} __sctok_in;

/* follow 上下文（this）：dep follow 体的唯一形参 __scdep_in&。
 *   toks   —— 依赖项句柄数组（token**，下标 [i] 取第 i 项 token&）；
 *   count  —— 依赖项个数；
 *   active —— 本次触发动作码（见下 acting）：负数为门事件码，>=0 为或门变更项下标。
 *   dep 的 a:"id" 局部名糖由编译器注入 `var a: token& = this->toks[i]`。 */
typedef struct __scdep_in {
    token  **toks;
    int32_t  count;
    int32_t  active;
} __scdep_in;

/* combine 回调：form 候选据上下文 this（base/input/sender/tag）算出新值（@ 擦除）。 */
typedef sc_afat (*token_combine)(__sctok_in *self);
/* follow 回调：依赖项 ts[0..n) 之一就绪/变更时触发；返回下次门逻辑（非 0=与门 all / 0=或门 any）。
 *   ctx=注册时透传的用户上下文。acting=触发动作（对齐 c_prototype.h）：
 *     -2 (ALL_READY)  ：与门首次全部就绪（form 触发）；
 *     -3 (ALL_CHANGED)：与门全部已变更（set 触发）；
 *     -1 (ANY_READY)  ：或门首次任一就绪 / 任一变更（acting 退化）；
 *     idx >= 0        ：或门，本次变更的依赖项下标。
 *   编译器生成的蹦床把本签名打包为 __scdep_in& 后调用合成的 follow 体。 */
typedef int (*token_follow)(token **ts, int n, int acting, void *ctx);

token      *token_bind(const char *id, token_combine combine);  /* create-or-get：按 id intern 句柄，combine 非空则挂为 form 候选 */
sc_afat     token_get(token *t);                                /* t.get()：取当前值（@，调用点 (e:T@) 还原） */
void        token_set(token *t, sc_afat v, int32_t tag);        /* t.set(v, tag)：设值（随附 tag）并触发依赖级联 */
void        token_form(token *t, sc_afat v, int32_t tag);       /* form t, v：灌初值并升格为 form 主 */
void        token_depend(token **ts, int n, int all, token_follow follow, void *ctx);  /* dep：注册依赖关系（all=与门/或门） */

#ifdef __cplusplus
}
#endif

#endif /* SC_TOK_H */
