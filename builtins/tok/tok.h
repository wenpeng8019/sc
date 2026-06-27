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
 *   ctx    —— 注册时透传的用户上下文（dep 关系的私有边状态；无则空 &）。
 *   dep 的 a:"id" 局部名糖由编译器注入 `var a: token& = this->toks[i]`。 */
typedef struct __scdep_in {
    token  **toks;
    int32_t  count;
    int32_t  active;
    void    *ctx;
} __scdep_in;

/* combine 回调：form 候选据上下文 this（base/input/sender/tag）算出新值（@ 擦除）。 */
typedef sc_afat (*token_combine)(__sctok_in *self);
/* follow 回调：依赖项 ts[0..n) 之一就绪/变更时触发；返回下次门逻辑（非 0=与门 all / 0=或门 any）。
 *   ctx=注册时透传的用户上下文。acting=触发动作（对齐 c_prototype.h）：
 *     -2 (ALL_READY)  ：与门首次全部就绪（form 触发）；
 *     -3 (ALL_CHANGED)：与门全部已变更（set 触发）；
 *     -1 (ANY_READY)  ：或门首次任一就绪 / 任一变更（acting 退化）；
 *     -4 (BACK)       ：反向遍历（back t）——this->active==-4 即走反向计算（读目标写源）；
 *                       BACK 下返回值另作「中止反向遍历」信号：非 0=停止本轮 back 扫描
 *                       （drain 协作层用：认领并处理一节点后中止重扫），0=继续遍历（如反向传播）；
 *     idx >= 0        ：或门，本次变更的依赖项下标。
 *   编译器生成的蹦床把本签名打包为 __scdep_in& 后调用合成的 follow 体。 */
typedef int (*token_follow)(token **ts, int n, int acting, void *ctx);
/* exec 钩子：节点（token）私有的处理回调，统一拉取/推送两模式（模式由谁驱动决定，非钩子属性）：
 *   · 拉取（back sink）：按反拓扑序对每个注册了 exec 的节点唤起 exec(t, t->ctx)——从侧车（ctx）
 *     出队一帧、跑节点 kernel、t.set 产出（触发前向路由），返回非 0 = 「已认领并处理一节点」即
 *     请求中止本轮 back 扫描。
 *   · 推送（token_set）：值变更落定后、于锁外（combine 临界区已退出）、向下游 dep 传播之前唤起
 *     exec(t, t->ctx)——节点级副作用/观察点（sink 产出、统计、日志、外部推送）；仅在值变更时唤起。
 *   与 combine/follow 正交：combine 须纯（锁内只算值），dep 只管前向路由，节点处理/副作用归 exec
 *   （锁外，MT 安全）。一个节点只会被其一种模式驱动（取决于模板用 back 还是 set）。 */
typedef int (*token_exec)(token *t, void *ctx);

token      *token_bind(const char *id, token_combine combine);  /* create-or-get：按 id intern 句柄，combine 非空则挂为 form 候选 */
sc_afat     token_get(token *t);                                /* t.get()：取当前值（@，调用点 (e:T@) 还原） */
void        token_set(token *t, sc_afat v, int32_t tag);        /* t.set(v, tag)：设值（随附 tag）；唯新值≠原值才落值并触发依赖级联（记忆化/去抖） */
void        token_pulse(token *t, sc_afat v, int32_t tag);      /* t.pulse(v, tag)：脉冲设值——绕过相等抑制，即便同值也落值并强制传播（拉取流水线/迭代：每次 set 皆事件） */
sc_afat     tok_modified(void);                                 /* modified 哨兵 @：combine 体内 return tok_modified() → 强制刷新传播（即使值未变） */
void        token_form(token *t, sc_afat v, int32_t tag, void *ctx, token_exec exec);  /* form t, v[, ctx[, exec]]：灌初值并升格为 form 主；ctx 非空则绑定节点私有上下文（侧车），exec 非空则挂节点处理钩子 */
void       *token_ctx(token *t);                                /* t.ctx()：取本 token 的私有上下文（form 绑定的侧车；未绑定=空 &） */
void        token_depend(token **ts, int n, int all, token_follow follow, void *ctx);  /* dep：注册依赖关系（all=与门/或门） */
void        token_depend_map(token **ts, int nsrc, int ntgt, int all, token_follow follow, void *ctx);  /* dep…map：源(nsrc)→目标(ntgt) 显式图边；ts=源++目标，仅源触发 */
void        token_set_depth(token *t, int depth);               /* 烘焙：编译期算好的依赖图深度写入句柄（注册时调用，常量入参） */
int         token_depth(token *t);                              /* t.depth()：读依赖图深度（源=0；O(1)，无图遍历） */
void        token_set_crit(token *t, int critical, int slack);  /* 烘焙：编译期算好的关键路径标志 + 松弛写入句柄 */
int         token_critical(token *t);                           /* t.critical()：是否在关键路径（最长链）上（O(1)） */
int         token_slack(token *t);                              /* t.slack()：松弛余量（可深多少跳而不拖慢全局；0=关键） */
void        token_set_degree(token *t, int fanin, int fanout);  /* 烘焙：编译期算好的扇入/扇出度写入句柄（枢纽识别） */
int         token_fanin(token *t);                              /* t.fanin()：扇入度（被多少上游 map 依赖；O(1)） */
int         token_fanout(token *t);                             /* t.fanout()：扇出度（驱动多少下游 map 目标；高=枢纽） */
void        token_set_reach(token *t, int reach);               /* 烘焙：编译期算好的可达下游数写入句柄（脏标记影响范围） */
int         token_reach(token *t);                              /* t.reach()：变更后须重算的下游 token 总数（失效爆炸半径；O(1)） */
void        token_set_batch(token *t, int width);               /* 烘焙：编译期算好的拓扑波次并行宽度写入句柄（接 MT 调度） */
int         token_batch(token *t);                              /* t.batch()：拓扑波次编号（=depth；同波可并行；O(1)） */
int         token_batch_width(token *t);                        /* t.batch_width()：本波次并行宽度（同深度可并行 token 数） */
void        token_set_dom(token *t, int checkpoint, int dom_size); /* 烘焙：编译期支配树算好的检查点标志 + 支配子树规模写入句柄 */
int         token_checkpoint(token *t);                         /* t.checkpoint()：是否为支配咽喉（缓存边界；O(1)） */
int         token_dom_size(token *t);                           /* t.dom_size()：支配子树规模（缓存可覆盖的下游 token 数） */
void        token_back(token *t, sc_afat seed, int32_t tag);    /* back t[, seed]：反向遍历（反向传播 / drain 骨架）——自 t 沿反向邻接收上游 dep，按深度降序（反拓扑）以 acting=TOK_BACK 唤起 follow；follow 返回非 0 即提前中止本轮遍历（drain 拉取）；seed 非空先灌入 t */
void        token_depend_loop(token **ts, int nsrc, int ntgt, int all, token_follow follow, void *ctx);  /* dep loop：受控反馈环——源不反挂（不自动级联），登记全局 loop 表，由 token_loop_run 按 SCC 簇驱动 */
void        token_set_scc(token *t, int scc_id, int scc_size); /* 烘焙：编译期 Tarjan 算好的 SCC 反馈簇划分写入句柄（注册时调用，常量入参） */
int         token_scc(token *t);                                /* t.scc()：读受控反馈簇编号（O(1)；非反馈/未烘焙=0） */
int         token_scc_size(token *t);                           /* 所属反馈簇大小（>1 或含自环=反馈簇；0/1=非反馈） */
int         token_loop_run(token *t, int max);                  /* t.loop_run(max)：驱动 t 所在 SCC 反馈簇迭代至多 max 轮（acting=TOK_LOOP），返回实际轮数 */

#ifdef __cplusplus
}
#endif

#endif /* SC_TOK_H */
