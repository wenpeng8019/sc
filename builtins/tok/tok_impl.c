/* tok_impl.c —— 分布式 token（tok / dep / form 机制）的默认运行时实现 v2
 *
 * 契约见同目录 tok.h（C ABI）与 op.sc（@def token 协议，方法 get/set）。
 * 经 op→tok 隐式依赖随工程始终编译链接（tok/dep/form 为语言关键字，恒可用）；
 * tok→adt 隐式依赖使 g_toks 哈希表（adt dict）随工程链接。
 *
 * v2 相对 v1 的增强（对齐 c_prototype.h/.c 的 C_Form/Enforce/Depend/input/output）：
 *   1. form 触发「就绪(ready)」依赖更新：token 形成（form）是区别于「变更(changed)」的
 *      特殊状态，首次满足门逻辑时以 TOK_ALL_READY/TOK_ANY_READY 唤起 follow
 *      （对齐 C_ALL_READY=-2 / C_ANY_READY=-1）。
 *   2. MT：id 以 '/' 开头的 token 进入多线程模式，细粒度无锁同步（对齐 '/' 前缀 MT 约定）：
 *      · 值：每-token 序列锁（seqlock）——读无锁乐观重试，写者经 seq 自旋独占；
 *      · 依赖门：每-dep 自旋锁，仅护门计数（armed/remain/all），临界区纳秒级；
 *      · follow 回调一律在释放所有锁后调用——任一线程任一时刻零持锁跑用户码，
 *        根除跨 token 锁序死锁；独立 token 子图真并行，无全局串行点。
 *      约束：combine 须纯（仅 base/input→value，不得 set/get 其他 token，它在写者独占下运行）；
 *      跨 token 副作用一律写在 follow（锁外运行）。
 *   3. g_toks 全局表改用 adt 哈希（dict，字符串键拷贝 → 裸 token*），O(1) 均摊 intern，
 *      取代 v1 线性查找。
 *   4. token 的依赖表 deps 动态增长（可增长数组），取代 v1 定长 deps[16]。
 *   5. form 前挂起 action：未 form 的 form 候选其 set 入挂起队列，form 时按 combine
 *      回放后再触发就绪——pending 动态数组与依赖表结构分离（对齐 c_prototype 的
 *      「form 前挂起 action + 依赖结构优化」）。
 *
 * 句柄、依赖记录均为进程生命周期对象（模块域静态），不单独回收。 */

#include "platform.h"   /* sc_mutex_* / sc_thread_id（全局可重入锁）；末尾带入 op.h（sc_afat/SC_OWN_RAW） */
#include "adt/adt.h"    /* dict：g_toks 哈希表（裸 token* 经 sc_afat 存取） */
#include "tok.h"        /* token C ABI（亦经 op.h→tok/tok.h 带入，幂等） */

/* follow 的 acting 动作码（对齐 c_prototype.h）：
 *   TOK_ALL_READY  (-2)：与门首次全部就绪（form 触发）
 *   TOK_ALL_CHANGED(-3)：与门全部已变更（set 触发）
 *   TOK_ANY_READY  (-1)：或门首次任一就绪 / 任一变更（acting 退化）
 *   TOK_BACK       (-4)：反向遍历（back t）——this->active==TOK_BACK 走反向计算
 *   TOK_LOOP       (-5)：受控反馈环迭代（token_loop_run）——this->active==TOK_LOOP 走一轮环体计算
 *   idx >= 0        ：或门，本次变更的依赖项下标 */
#define TOK_ALL_READY   (-2)
#define TOK_ALL_CHANGED (-3)
#define TOK_ANY_READY   (-1)
#define TOK_BACK        (-4)
#define TOK_LOOP        (-5)

enum { TOK_NEW = 0, TOK_READY = 1 };  /* 生命状态：NEW=未就绪（待 form） / READY=已被 form 激活 */

/* MT 同步：细粒度无锁。值用每-token seqlock（t->seq），依赖门用每-dep 自旋锁（d->glock），
 * follow 一律锁外调用（见各函数）。所有锁状态字段 calloc 零值即初始（seq 偶=可读、glock 空闲），
 * 无需运行时 init；非 MT token 全程不触原子（零开销）。下方为 seqlock / 门锁的原子助手。 */

typedef struct tok_dep {
    token        **ts;      /* 句柄数组（注册时拷贝）：[0..n) 为触发源，[n..ntot) 为 map 目标 */
    int            n;       /* 触发源数：门/武装/反挂仅覆盖 [0..n)，目标不触发本 dep */
    int            ntot;    /* 传给 follow 的总句柄数（含 map 目标）；非 map 时 ntot==n */
    int            all;     /* 当前门逻辑：1=与门 / 0=或门（follow 返回值更新） */
    int            remain;  /* 与门：本轮尚未武装(arm)的依赖项数；归零即触发 */
    unsigned char *armed;   /* 每依赖项本轮是否已武装（n 字节；与门去重计数用） */
    int            mt;      /* 任一成员（含目标）为 MT 则整条 dep 走自旋门锁（glock） */
    int            glock;   /* 门状态自旋锁（0=空闲 / 1=持有）：仅护 all/remain/armed */
    token_follow   follow;
    void          *ctx;
} tok_dep;

/* form 前挂起队列（懒分配）：未 form 的 token 收到 set 才创建本结构挂到 t->pending；
 * form-first（先 form 再 set，如 dnn 训练）全程 pending==NULL，零分配零占用。池化分配。 */
typedef struct tok_pending {
    sc_afat *vals;          /* 挂起的 set 输入值（可增长，池化） */
    int32_t *tags;          /* 对应挂起输入的 tag */
    int      n, cap;
} tok_pending;

/* back 反向调度缓存（懒构建 + graph-epoch 失效刷新）：token_back 的「反向可达 dep/节点
 * 有序表」只依赖静态图结构，与值无关——故按 sink token 缓存，建图后恒命中，零重算零分配。
 * 仅当 g_graph_epoch 变化（注册了新 dep / 新挂 exec）才重建。池化分配。 */
typedef struct tok_back_cache {
    uint32_t epoch;         /* 构建时的 g_graph_epoch；!= 当前则重建 */
    int      mode;          /* 1=边反向（list 为 tok_dep*，按 rank 降序）/ 2=节点 drain（list 为 token*，按 depth 降序） */
    int      n, cap;        /* 有序表长 / 容量 */
    void   **list;          /* 有序调度表（tok_dep*[] 或 token*[]，池化） */
} tok_back_cache;

struct token {
    /* —— 8 字节成员先排（消除与 4 字节字段交错产生的填充）—— */
    char         *id;        /* 字符串唯一键 */
    token_combine combine;   /* 非空=有 combine 合成体；空=enforce 直赋。与就绪无关：就绪唯凭 form */
    tok_dep     **deps;      /* 动态：引用本 token 的依赖关系（可增长数组） */
    tok_dep     **producers; /* 动态：以本 token 为 map 目标的依赖关系（反向邻接：目标←dep，供 back 回溯上游） */
    void         *ctx;       /* 节点私有上下文（侧车）：form 绑定，follow/exec 经 token_ctx 取用（拉取式流水线的队列+状态机+kernel，或推送式的观察统计等） */
    token_exec    exec;      /* 节点处理钩子（form 绑定）：拉取(back)模式反拓扑唤起、推送(set)模式值变更落定后锁外唤起；dep 只管路由、combine 须纯，节点处理/副作用归此 */
    tok_pending  *pending;   /* form 前挂起队列（懒分配，池化）：NULL=无挂起；form-first 全程 NULL */
    tok_back_cache *back;    /* 反向调度缓存（懒构建，池化）：NULL=未建；按 graph-epoch 刷新 */
    sc_afat       value;     /* 当前值（@ 类型擦除自描述胖指针；32B = 4 指针） */
    /* —— 4 字节成员（紧凑收尾）—— */
    int32_t       tag;       /* 当前值随附标签（最近一次 set/form 的 tag） */
    int           state;     /* TOK_NEW / TOK_READY */
    int           ndeps, capdeps;
    int           nprod, capprod;
    int           depth;     /* dep…map 依赖图深度（源=0；编译期烘焙的常量，token_set_depth 写入，token_depth 读） */
    int           scc_id;    /* dep loop 受控反馈簇编号（编译期 Tarjan 烘焙；token_set_scc 写，token_scc 读） */
    int           scc_size;  /* 所属 SCC 簇大小（>1 或含自环 = 反馈簇；0/1 = 非反馈，不迭代） */
    int           critical;  /* 关键路径标志（dep…map 最长链上；编译期烘焙，token_set_crit 写，token_critical 读） */
    int           slack;     /* 松弛余量（可深多少跳而不拖慢全局；0=关键；token_slack 读） */
    int           fanin;     /* 扇入度：被多少上游 map 依赖（编译期烘焙，token_set_degree 写，token_fanin 读） */
    int           fanout;    /* 扇出度：驱动多少下游 map 目标（枢纽识别；token_fanout 读） */
    int           reach;     /* 可达下游数：变更后须重算的 token 总数（脏标记影响范围；token_set_reach 写，token_reach 读） */
    int           batch_width; /* 同波次并行宽度：与本 token 同深度的 token 数（拓扑分批；token_set_batch 写，token_batch_width 读） */
    int           checkpoint; /* 支配检查点标志：是否为缓存边界咽喉（编译期烘焙，token_set_dom 写，token_checkpoint 读） */
    int           dom_size;  /* 支配子树规模：缓存可覆盖的下游 token 数（token_dom_size 读） */
    int           mt;        /* MT token（id 以 '/' 开头）：值经 seqlock 同步；0=非 MT，零开销 */
    uint32_t      seq;       /* seqlock 序列号：偶=稳定可读 / 奇=写者占用中（兼写者自旋互斥） */
};

/* ---- 全局 token 表：adt 哈希（拷贝字符串键 → 裸 token*） ---- */
static dict       g_toks;          /* key_size=-1：dict 自持 id 拷贝 */
static sc_mutex_t g_toks_mtx;      /* 守护 bind 的 intern 并发 */
static int        g_toks_init = 0; /* 惰性构造（首次 bind 单线程，模块 init 阶段） */

/* 依赖图世代计数：任一拓扑变更（注册 dep / 挂 exec）即自增，使各 token 的 back 调度缓存失效。
 * 建图在模块 init 单线程完成；运行期（训练循环）拓扑不变 → epoch 稳定 → back 缓存恒命中。 */
static uint32_t   g_graph_epoch = 0;

/* dep loop 全局注册表：所有 token_depend_loop 登记于此，token_loop_run 按 SCC 簇筛选驱动迭代。
 * 注册在模块 init 单线程（建图先于任何迭代），故无并发；运行期只读遍历。 */
static tok_dep  **g_loop_deps = NULL;
static int        g_nloop = 0, g_caploop = 0;

static void toks_ensure(void) {
    if (!g_toks_init) {            /* 模块 init 单线程：首次 bind 时构造，无并发竞争 */
        dict_init(&g_toks, -1);
        sc_mutex_init(&g_toks_mtx);
        g_toks_init = 1;
    }
}

/* 把裸 token* 包成 sc_afat：tar=NULL 不计数、own=SC_OWN_RAW 不记 out、dtor=NULL 不析构，
 * 纯当裸指针存取，dict 的 retain/release 对其为 no-op（零 ARC 干扰）。 */
static sc_afat tok_afat(token *t) {
    sc_afat f;
    f.p = t; f.tar = NULL; f.own = SC_OWN_RAW; f.dtor = NULL;
    return f;
}

/* 空 @（无值）：sender 恒空、未 form 的 form 主初值。纯结构零值，无 ARC 干扰。 */
static sc_afat tok_empty_afat(void) {
    sc_afat f;
    f.p = NULL; f.tar = NULL; f.own = SC_OWN_RAW; f.dtor = NULL;
    return f;
}

/* modified 哨兵（对齐 c_prototype 的 g_ccModified）：强制刷新传播的特殊 @ 值。
 * 取一不与任何真实标量值/堆指针冲突的唯一地址作 .p（本进程唯一），任一侧出现即判「不等」。 */
static char g_tok_modified_marker;
sc_afat tok_modified(void) {
    sc_afat f;
    f.p = (void *)&g_tok_modified_marker; f.tar = NULL; f.own = SC_OWN_RAW; f.dtor = NULL;
    return f;
}

/* @ 值相等判定（对齐 c_prototype 的 C_equal）：按 .p 比对——标量经 (x:@) 装箱即值入 .p，
 * 故比 .p 对标量即值相等；堆对象则为指针同一性。modified 哨兵任一侧出现恒判「不等」→ 强制传播。 */
static int tok_afat_equal(sc_afat a, sc_afat b) {
    if (a.p == (void *)&g_tok_modified_marker || b.p == (void *)&g_tok_modified_marker) return 0;
    return a.p == b.p;
}

/* 调用 form 候选的 combine：据基值 base、输入 input、tag 打包上下文（sender 恒空）算新值。
 * base 由调用方供给——非 MT 直接传 t->value（裸读）；MT 须传原子读出的当前值（写者独占下
 * 经 sc_get_ord 读，避免裸读跨线程原子写的数据竞争 / 编译器缓存导致的陈旧 base 破坏单调性）。 */
static sc_afat tok_run_combine(token *t, sc_afat base, sc_afat input, int32_t tag) {
    __sctok_in self;
    self.sender = tok_empty_afat();
    self.base   = base;
    self.input  = input;
    self.tag    = tag;
    return t->combine(&self);
}

static char *tok_strdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)sc_chunk(n);          /* 池化：id 进程生命周期，随建图一次性分配 */
    if (p) memcpy(p, s, n);
    return p;
}

/* ---- MT 值单元：seqlock（序列锁）——读无锁、写者经 seq 自旋独占（仅 MT token 走此路径） ----
 * t->seq 偶=稳定可读 / 奇=写者占用中。写者 CAS 偶→奇取得独占（兼写者互斥），改值后 +2 置回偶
 * （release 发布）；读者乐观重试：读 seq→读各字段→复读 seq，奇或前后不等则重试。值各字段以原子
 * （seq_cst）逐项读写——TSan 干净、无撕裂（x86 上 seq_cst 读即 plain mov，读路径近乎零成本）。 */
static void tok_store_value(token *t, sc_afat v, int32_t tag) {
    sc_set_ord(&t->value.p,    v.p);
    sc_set_ord(&t->value.tar,  v.tar);
    sc_set_ord(&t->value.own,  v.own);
    sc_set_ord(&t->value.dtor, v.dtor);
    sc_set_ord(&t->tag, tag);
}
static sc_afat tok_load_value(token *t) {
    sc_afat v;
    for (;;) {
        uint32_t s1 = sc_get_ord(&t->seq);
        if (s1 & 1u) continue;                 /* 写者占用中：重试 */
        v.p    = sc_get_ord(&t->value.p);
        v.tar  = sc_get_ord(&t->value.tar);
        v.own  = sc_get_ord(&t->value.own);
        v.dtor = sc_get_ord(&t->value.dtor);
        uint32_t s2 = sc_get_ord(&t->seq);
        if (s1 == s2) return v;                /* 期间无写者：快照一致 */
    }
}
static uint32_t tok_write_begin(token *t) {    /* CAS 偶→奇：取得写者独占（兼互斥） */
    for (;;) {
        uint32_t s = sc_get(&t->seq) & ~1u;    /* 期望偶 */
        uint32_t exp = s;
        if (sc_test_and_set_ord(&t->seq, &exp, s + 1u)) return s;
        /* 失败：他写者占用，自旋重试 */
    }
}
static void tok_write_end(token *t, uint32_t s) {
    sc_set_ord(&t->seq, s + 2u);               /* 偶：发布新值 */
}
/* 写者独占下读当前值：经 sc_get_ord 原子逐项读（无需 seqlock 重试——本线程即唯一写者）。
 * 供 combine 取 base：与其他写者经 seq 互斥、与读者经原子读相容，TSan 干净。 */
static sc_afat tok_value_excl(token *t) {
    sc_afat v;
    v.p    = sc_get_ord(&t->value.p);
    v.tar  = sc_get_ord(&t->value.tar);
    v.own  = sc_get_ord(&t->value.own);
    v.dtor = sc_get_ord(&t->value.dtor);
    return v;
}

/* ---- 依赖门自旋锁：仅护 d->armed/remain/all（纳秒级临界区，绝不跨 follow） ----
 * 仅 MT dep（d->mt）生效；非 MT 为 no-op。门锁恒为一次只持一把的叶子锁，不嵌套任何锁。 */
static void dep_lock(tok_dep *d) {
    if (!d->mt) return;
    for (;;) { int exp = 0; if (sc_test_and_set_acq(&d->glock, &exp, 1)) return; }
}
static void dep_unlock(tok_dep *d) {
    if (!d->mt) return;
    sc_set_rel(&d->glock, 0);
}

token *token_bind(const char *id, token_combine combine) {
    toks_ensure();
    sc_mutex_lock(&g_toks_mtx);
    token *t = (token *)dict_get(&g_toks, id).p;     /* 哈希 O(1) intern 查找 */
    if (t) {                                          /* 已存在：幂等返回，按需补挂 combine */
        if (combine && !t->combine) t->combine = combine;
        sc_mutex_unlock(&g_toks_mtx);
        return t;
    }
    t = (token *)sc_chunk0(sizeof(token));           /* 池化清零：token 句柄进程生命周期 */
    t->id = tok_strdup(id);
    t->combine = combine;
    t->state = TOK_NEW;                               /* 对齐 c_prototype：bind 仅取共享壳，未就绪；唯 form 升 READY */
    t->mt = (id[0] == '/');                           /* MT token：值经 seqlock 同步，依赖门走自旋锁 */
    dict_put(&g_toks, id, tok_afat(t));
    sc_mutex_unlock(&g_toks_mtx);
    return t;
}

sc_afat token_get(token *t) {
    if (!t) return tok_empty_afat();
    if (!t->mt) return t->value;        /* 非 MT 快路径：裸读 */
    return tok_load_value(t);           /* MT：seqlock 无锁读 */
}

/* 武装与门依赖项 idx：首次置位则 remain--；返回 remain 是否归零（与门达成）。 */
static int dep_arm(tok_dep *d, int idx) {
    if (idx < 0 || idx >= d->n || d->armed[idx]) return 0;
    d->armed[idx] = 1;
    return (--d->remain == 0);
}
static void dep_reset(tok_dep *d) {            /* 门达成后重置：下一轮需全部重新武装 */
    if (d->n) memset(d->armed, 0, (size_t)d->n);
    d->remain = d->n;
}

/* 触发 t 的依赖级联：ready=1 为就绪事件（form 首次），ready=0 为变更事件（set）。
 *   与门：依赖项逐一武装，全部到位（remain 归零）才唤起 follow（ALL_READY / ALL_CHANGED）；
 *   或门：每事件即唤起 follow（ANY_READY / 变更项下标）。
 * follow 返回值更新下次门逻辑（非 0=与门 / 0=或门）。
 * 并发：门状态读改在 dep 自旋锁内完成；follow 一律在锁外调用（调用方亦已释放写者锁）——
 *   任一线程任一时刻零持锁跑用户码，根除跨 token 锁序死锁。 */
static void tok_fire(token *t, int ready) {
    for (int i = 0; i < t->ndeps; i++) {
        tok_dep *d = t->deps[i];
        int idx = -1;
        for (int j = 0; j < d->n; j++) if (d->ts[j] == t) { idx = j; break; }
        int fire = 0, acting = 0;
        dep_lock(d);
        if (d->all) {                                  /* 与门 */
            if (dep_arm(d, idx)) {                      /* 全部依赖项到位 */
                fire = 1;
                acting = ready ? TOK_ALL_READY : TOK_ALL_CHANGED;
                dep_reset(d);                          /* 重置进入下一轮（变更门） */
            }
        } else {                                       /* 或门：任一事件即触发 */
            fire = 1;
            acting = ready ? TOK_ANY_READY : (idx >= 0 ? idx : TOK_ANY_READY);
        }
        dep_unlock(d);
        if (!fire) continue;
        int next = d->follow ? d->follow(d->ts, d->ntot, acting, d->ctx) : 0;  /* 锁外回调（传全句柄数，含 map 目标） */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);             /* 或门→与门：重新计数 */
        dep_unlock(d);
    }
}

/* form 前挂起一条 set 输入：懒分配 tok_pending（首次挂起才创建，池化）。
 * form-first（先 form 后 set）全程不触此路径，t->pending 恒 NULL，零分配零占用。 */
static void tok_pending_push(token *t, sc_afat v, int32_t tag) {
    tok_pending *q = t->pending;
    if (!q) { q = (tok_pending *)sc_chunk0(sizeof(tok_pending)); t->pending = q; }
    if (q->n == q->cap) {
        q->cap = q->cap ? q->cap * 2 : 4;
        q->vals = (sc_afat *)sc_refit(q->vals, (size_t)q->cap * sizeof(sc_afat));
        q->tags = (int32_t *)sc_refit(q->tags, (size_t)q->cap * sizeof(int32_t));
    }
    q->vals[q->n] = v;
    q->tags[q->n] = tag;
    q->n++;
}
/* form 时丢弃挂起队列（回放后调用）：释放池化缓冲并清空指针。 */
static void tok_pending_drop(token *t) {
    tok_pending *q = t->pending;
    if (!q) return;
    sc_recycle(q->vals); sc_recycle(q->tags); sc_recycle(q);
    t->pending = NULL;
}

/* set 内核：force=0 记忆化（同值抑制，对齐 c_prototype 的 C_input）；force=1 脉冲（绕过抑制,
 *   同值也落值并强制传播）。未 form 一律入挂起队列（脉冲亦然，待 form 回放）。 */
static void tok_set_impl(token *t, sc_afat v, int32_t tag, int force) {
    if (!t) return;
    if (!t->mt) {                                      /* 非 MT 快路径：无锁无原子 */
        if (t->state == TOK_NEW) {                      /* 未 form（含 enforce 未被主 form）：入挂起队列，不落值不传播 */
            tok_pending_push(t, v, tag);
            return;
        }
        sc_afat oldv = t->value;
        sc_afat newv = t->combine ? tok_run_combine(t, oldv, v, tag) : v;
        if (!force && tok_afat_equal(newv, oldv)) return;  /* 新值==原值：未变更，不落值不传播（脉冲跳过此抑制） */
        t->value = newv;
        t->tag = tag;
        if (t->exec) t->exec(t, t->ctx);               /* 锁外：节点处理钩子（值落定后、向下游传播前） */
        tok_fire(t, 0);
        return;
    }
    /* MT：写者经 seq 取得独占（兼互斥），改值后发布，再锁外传播 */
    uint32_t s = tok_write_begin(t);
    if (t->state == TOK_NEW) {                         /* 未 form：入挂起队列（token 私有，仅写者触碰） */
        tok_pending_push(t, v, tag);
        tok_write_end(t, s);
        return;
    }
    sc_afat oldv = tok_value_excl(t);                            /* MT：base 经原子读（写者独占下） */
    sc_afat nv = t->combine ? tok_run_combine(t, oldv, v, tag) : v;
    if (!force && tok_afat_equal(nv, oldv)) { tok_write_end(t, s); return; } /* 未变更：不发布不传播（脉冲跳过） */
    tok_store_value(t, nv, tag);
    tok_write_end(t, s);
    if (t->exec) t->exec(t, t->ctx);                   /* 锁外：节点处理钩子（已发布、传播前） */
    tok_fire(t, 0);                                    /* 锁外：变更事件传播 */
}

void token_set(token *t, sc_afat v, int32_t tag) {
    tok_set_impl(t, v, tag, 0);                        /* 记忆化：同值抑制（默认） */
}

void token_pulse(token *t, sc_afat v, int32_t tag) {
    tok_set_impl(t, v, tag, 1);                        /* 脉冲：绕过相等抑制，同值也强制传播（拉取流水线/迭代） */
}

void token_form(token *t, sc_afat v, int32_t tag, void *ctx, token_exec exec) {
    if (!t) return;
    if (ctx) t->ctx = ctx;                             /* 绑定节点私有上下文（侧车）：form = 灌值 + 升格 + 挂侧车 + 挂钩子 */
    if (exec) { t->exec = exec; g_graph_epoch++; }     /* 挂节点处理钩子；新挂 exec 改变 back 调度模式 → 失效 back 缓存 */
    if (!t->mt) {                                      /* 非 MT 快路径 */
        t->value = v;
        t->tag = tag;
        t->state = TOK_READY;
        if (t->pending) {                              /* 回放 form 前挂起的 action（按 combine 合并） */
            tok_pending *q = t->pending;
            for (int i = 0; i < q->n; i++) {
                t->value = t->combine ? tok_run_combine(t, t->value, q->vals[i], q->tags[i])
                                      : q->vals[i];
                t->tag = q->tags[i];
            }
            tok_pending_drop(t);
        }
        tok_fire(t, 1);
        return;
    }
    /* MT：写者独占灌初值 + 回放挂起，发布后锁外触发就绪 */
    uint32_t s = tok_write_begin(t);
    t->state = TOK_READY;                              /* 升格 form 主（写者独占下） */
    tok_store_value(t, v, tag);                        /* 灌初值（原子发布；seq 仍奇，读者重试） */
    if (t->pending) {                                  /* 回放挂起：每步以当前值为 base 经 combine 合并 */
        tok_pending *q = t->pending;
        for (int i = 0; i < q->n; i++) {
            sc_afat nv = t->combine ? tok_run_combine(t, tok_value_excl(t), q->vals[i], q->tags[i])
                                    : q->vals[i];
            tok_store_value(t, nv, q->tags[i]);
        }
        tok_pending_drop(t);
    }
    tok_write_end(t, s);
    tok_fire(t, 1);                                    /* 锁外：就绪事件触发依赖门 */
}

/* t.ctx()：取节点私有上下文（form 绑定的侧车；未绑定=NULL）。follow/exec 体内经此取节点状态。
 * 仅读裸指针：ctx 由 form 单线程绑定（建图/启动期），运行期只读，无并发写。 */
void *token_ctx(token *t) { return t ? t->ctx : NULL; }


static void tok_depend_impl(token **ts, int ntrig, int ntot, int all, token_follow follow, void *ctx) {
    g_graph_epoch++;                                  /* 拓扑变更：注册新 dep 即失效所有 back 调度缓存 */
    tok_dep *d = (tok_dep *)sc_chunk0(sizeof(tok_dep));
    d->ts = (token **)sc_chunk((size_t)(ntot ? ntot : 1) * sizeof(token *));
    memcpy(d->ts, ts, (size_t)ntot * sizeof(token *));
    d->n = ntrig;
    d->ntot = ntot;
    d->all = all;
    d->follow = follow;
    d->ctx = ctx;
    d->armed = (unsigned char *)sc_chunk0((size_t)(ntrig ? ntrig : 1));
    d->remain = ntrig;
    for (int i = 0; i < ntot; i++)                     /* 先定门：任一成员（含 map 目标）MT 则整条 dep 走全局锁 */
        if (ts[i] && ts[i]->mt) { d->mt = 1; break; }
    for (int i = 0; i < ntot; i++) {                   /* 升格：MT dep 的所有成员（含目标）一并 MT，跨线程读写受串行化 */
        token *t = ts[i];
        if (!t) continue;
        if (d->mt) t->mt = 1;
    }
    for (int i = 0; i < ntrig; i++) {                  /* 反挂仅触发源：map 目标被 follow 写入但不触发本 dep（杜绝自环回灌） */
        token *t = ts[i];
        if (!t) continue;
        if (t->ndeps == t->capdeps) {
            t->capdeps = t->capdeps ? t->capdeps * 2 : 4;
            t->deps = (tok_dep **)sc_refit(t->deps, (size_t)t->capdeps * sizeof(tok_dep *));
        }
        t->deps[t->ndeps++] = d;
    }
    for (int i = ntrig; i < ntot; i++) {               /* 反向邻接：map 目标记下产出它的 dep（目标←dep，供 back 回溯上游） */
        token *t = ts[i];
        if (!t) continue;
        if (t->nprod == t->capprod) {
            t->capprod = t->capprod ? t->capprod * 2 : 4;
            t->producers = (tok_dep **)sc_refit(t->producers, (size_t)t->capprod * sizeof(tok_dep *));
        }
        t->producers[t->nprod++] = d;
    }
    /* 注册即就绪：触发源中已就绪者预先武装；满足门逻辑则立即触发 ready
     *（对齐 c_prototype C_Depend：注册时已 form 的依赖项即计入门逻辑）。
     * 注册本在模块 init 单线程（deps 数组建图先于任何并发 fire）；门状态读改仍走 dep 锁、
     * follow 锁外调用，与运行期 fire 同构。 */
    int fire = 0, acting = 0;
    dep_lock(d);
    if (all) {                                         /* 与门：预武装已就绪源 */
        for (int i = 0; i < ntrig; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { d->armed[i] = 1; d->remain--; }
        if (ntrig > 0 && d->remain == 0) {             /* 注册时已全部就绪 → 立即 ALL_READY */
            fire = 1; acting = TOK_ALL_READY;
            dep_reset(d);
        }
    } else {                                           /* 或门：任一已就绪 → 立即 ANY_READY */
        for (int i = 0; i < ntrig; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { fire = 1; acting = TOK_ANY_READY; break; }
    }
    dep_unlock(d);
    if (fire) {
        int next = follow ? follow(d->ts, ntot, acting, ctx) : 0;  /* 锁外回调（传全句柄数，含 map 目标） */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);
        dep_unlock(d);
    }
}

void token_depend(token **ts, int n, int all, token_follow follow, void *ctx) {
    tok_depend_impl(ts, n, n, all, follow, ctx);       /* 非 map：触发源即全部句柄 */
}

void token_depend_map(token **ts, int nsrc, int ntgt, int all, token_follow follow, void *ctx) {
    tok_depend_impl(ts, nsrc, nsrc + ntgt, all, follow, ctx);  /* map：ts = 源(nsrc) ++ 目标(ntgt) */
}

/* 烘焙：编译期对 dep…map 图算好的深度，注册时以常量写入句柄（lightmap 式预计算，运行时只读）。 */
void token_set_depth(token *t, int depth) {
    if (t) t->depth = depth;
}

/* t.depth()：读依赖图深度（源=0；O(1) 常量，无图遍历）。 */
int token_depth(token *t) {
    return t ? t->depth : 0;
}

/* 烘焙：编译期对 dep…map DAG 算好的关键路径标志 + 松弛，注册时以常量写入句柄。 */
void token_set_crit(token *t, int critical, int slack) {
    if (t) { t->critical = critical; t->slack = slack; }
}

/* t.critical()：该 token 是否在关键路径（最长链）上（O(1)；加长它即拖慢整条流水线）。 */
int token_critical(token *t) {
    return t ? t->critical : 0;
}

/* t.slack()：松弛余量（可深多少跳而不拖慢全局；0=关键点）。 */
int token_slack(token *t) {
    return t ? t->slack : 0;
}

/* 烘焙：编译期对 dep…map 图算好的扇入/扇出度，注册时以常量写入句柄（枢纽识别）。 */
void token_set_degree(token *t, int fanin, int fanout) {
    if (t) { t->fanin = fanin; t->fanout = fanout; }
}

/* t.fanin()：扇入度（被多少上游 map 依赖；O(1) 常量）。 */
int token_fanin(token *t) {
    return t ? t->fanin : 0;
}

/* t.fanout()：扇出度（驱动多少下游 map 目标；高=枢纽 / 广播源）。 */
int token_fanout(token *t) {
    return t ? t->fanout : 0;
}

/* 烘焙：编译期算好的可达下游数（脏标记影响范围），注册时以常量写入句柄。 */
void token_set_reach(token *t, int reach) {
    if (t) t->reach = reach;
}

/* t.reach()：变更本 token 后须重算的下游 token 总数（失效爆炸半径；O(1)）。 */
int token_reach(token *t) {
    return t ? t->reach : 0;
}

/* 烘焙：编译期算好的拓扑波次并行宽度，注册时以常量写入句柄（接 MT 调度）。 */
void token_set_batch(token *t, int width) {
    if (t) t->batch_width = width;
}

/* t.batch()：拓扑波次编号（= depth；同波 token 可并行触发；O(1)）。 */
int token_batch(token *t) {
    return t ? t->depth : 0;
}

/* t.batch_width()：本波次并行宽度（与本 token 同深度、可并行的 token 数）。 */
int token_batch_width(token *t) {
    return t ? t->batch_width : 0;
}

/* 烘焙：编译期支配树算好的检查点标志 + 支配子树规模，注册时以常量写入句柄（缓存边界）。 */
void token_set_dom(token *t, int checkpoint, int dom_size) {
    if (t) { t->checkpoint = checkpoint; t->dom_size = dom_size; }
}

/* t.checkpoint()：是否为支配咽喉（缓存边界——在此缓存可覆盖整个支配子树；O(1)）。 */
int token_checkpoint(token *t) {
    return t ? t->checkpoint : 0;
}

/* t.dom_size()：支配子树规模（本检查点缓存可覆盖的下游 token 数）。 */
int token_dom_size(token *t) {
    return t ? t->dom_size : 0;
}

/* dep 的目标侧深度：取其所有 map 目标（ts[n..ntot)）深度最大者；无目标退化取触发源深度。
 * 反向遍历据此排序——目标越深者越靠近输出，反拓扑序中越先行。 */
static int dep_back_rank(tok_dep *d) {
    int md = -1;
    for (int i = d->n; i < d->ntot; i++)
        if (d->ts[i] && d->ts[i]->depth > md) md = d->ts[i]->depth;
    if (md < 0)                                  /* 无 map 目标：退化用触发源深度 */
        for (int i = 0; i < d->n; i++)
            if (d->ts[i] && d->ts[i]->depth > md) md = d->ts[i]->depth;
    return md;
}

/* back 调度构建：自 sink t 沿 producers[] 反向 BFS，按图是否注册节点 exec 自动分派两种语义，
 * 把唤起序列烘进 t->back（tok_back_cache）。BFS 临时去重表（seen/work/deps）走池化短命缓冲
 * （sc_chunk/sc_refit/sc_recycle），构建末即回收——稳态训练循环只在首轮（或拓扑变更后）构建一次，
 * 此后 token_back 纯读缓存、零分配零去重零排序。
 *   mode 2（drain：图中有节点 exec）：缓存「按 depth 降序的注册 exec 节点」列表。
 *   mode 1（边反向：无任何 exec，如梯度反传）：缓存「按 dep_back_rank 降序的可达 dep」列表。 */
static tok_back_cache *tok_back_build(token *t) {
    tok_back_cache *bc = t->back;
    if (!bc) bc = (tok_back_cache *)sc_chunk0(sizeof(tok_back_cache)); /* 句柄进程生命周期 */
    int capseen = 8, nseen = 0, capwork = 8, nwork = 0, capdep = 8, ndep = 0;
    token   **seen = (token **)sc_chunk((size_t)capseen * sizeof(token *));   /* 短命：构建末回收 */
    token   **work = (token **)sc_chunk((size_t)capwork * sizeof(token *));
    tok_dep **deps = (tok_dep **)sc_chunk((size_t)capdep * sizeof(tok_dep *));
    seen[nseen++] = t;
    work[nwork++] = t;
    while (nwork) {
        token *x = work[--nwork];
        for (int i = 0; i < x->nprod; i++) {
            tok_dep *d = x->producers[i];
            int dup = 0;
            for (int k = 0; k < ndep; k++) if (deps[k] == d) { dup = 1; break; }
            if (!dup) {
                if (ndep == capdep) { capdep *= 2;
                    deps = (tok_dep **)sc_refit(deps, (size_t)capdep * sizeof(tok_dep *)); }
                deps[ndep++] = d;
            }
            for (int j = 0; j < d->n; j++) {     /* d 的触发源即上游，继续向上展开 */
                token *u = d->ts[j];
                if (!u) continue;
                int vis = 0;
                for (int k = 0; k < nseen; k++) if (seen[k] == u) { vis = 1; break; }
                if (vis) continue;
                if (nseen == capseen) { capseen *= 2;
                    seen = (token **)sc_refit(seen, (size_t)capseen * sizeof(token *)); }
                seen[nseen++] = u;
                if (nwork == capwork) { capwork *= 2;
                    work = (token **)sc_refit(work, (size_t)capwork * sizeof(token *)); }
                work[nwork++] = u;
            }
        }
    }
    int has_exec = 0;
    for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) { has_exec = 1; break; }
    if (has_exec) {                              /* mode 2：节点 drain，缓存 exec 节点（depth 降序） */
        for (int i = 1; i < nseen; i++) {        /* 插入排序按 depth 降序（规模小） */
            token *x = seen[i]; int d = x->depth; int j = i - 1;
            while (j >= 0 && seen[j]->depth < d) { seen[j + 1] = seen[j]; j--; }
            seen[j + 1] = x;
        }
        int cnt = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) cnt++;
        if (bc->cap < cnt) { bc->cap = cnt ? cnt : 1;
            bc->list = (void **)sc_refit(bc->list, (size_t)bc->cap * sizeof(void *)); }
        bc->n = 0;
        for (int i = 0; i < nseen; i++) if (seen[i] && seen[i]->exec) bc->list[bc->n++] = seen[i];
        bc->mode = 2;
    } else {                                     /* mode 1：边反向，缓存 dep（dep_back_rank 降序） */
        for (int i = 1; i < ndep; i++) {
            tok_dep *d = deps[i]; int r = dep_back_rank(d); int j = i - 1;
            while (j >= 0 && dep_back_rank(deps[j]) < r) { deps[j + 1] = deps[j]; j--; }
            deps[j + 1] = d;
        }
        if (bc->cap < ndep) { bc->cap = ndep ? ndep : 1;
            bc->list = (void **)sc_refit(bc->list, (size_t)bc->cap * sizeof(void *)); }
        bc->n = ndep;
        for (int i = 0; i < ndep; i++) bc->list[i] = deps[i];
        bc->mode = 1;
    }
    sc_recycle(seen); sc_recycle(work); sc_recycle(deps);
    bc->epoch = g_graph_epoch;                   /* 末置 epoch：缓存就绪标记（发布前最后一步） */
    return bc;
}

static int g_back_lock = 0;                      /* back 缓存构建锁：仅首构建/拓扑变更后争用 */

/* back t[, seed]：反向遍历（反向传播骨架）。自输出 token t 出发，沿 producers[] 反向邻接
 * 收集全部上游 dep（去重），按 dep_back_rank 降序（反拓扑：靠近输出者先行）依次以
 * acting=TOK_BACK 唤起其 follow——follow 体内 this->active==TOK_BACK 即走反向计算（读目标
 * 写源；梯度数学由用户体负责，多 dep 写同一源的累积经 form combine(sum) 完成）。
 * seed 非空则先灌入 t（梯度种子，如 loss.backward(1)），不触发前向级联。
 * 编译期已保证图为 DAG（环检测），反向 BFS 必终止。
 *
 * 调度缓存：唤起序列烘进 t->back，按 g_graph_epoch 命中——拓扑稳定（训练循环 / drain 运行期）
 *   下仅首轮构建，此后纯读缓存、零分配零去重零排序。构建经全局锁双检：稳态（epoch 命中）走无锁
 *   快路径，仅首构建/拓扑变更后入锁（release 发布 t->back，快路径 acquire 读，多 worker 并发 back
 *   同一 sink 安全）。拓扑变更（dep 注册 / 新挂 exec）经 g_graph_epoch++ 自动失效缓存。
 *
 * 提前中止（break）：follow/exec 返回非 0 即停止本轮反向遍历——供「drain」式协作层用：worker
 *   自 sink back，最深可认领节点的钩子认领并处理后返回非 0 中止扫描，worker 再发起下一轮
 *   back 重扫，天然实现「最近未处理优先」的拉取式流水线排空。返回 0（如反向传播）则全程遍历，
 *   行为不变（向后兼容）。 */
void token_back(token *t, sc_afat seed, int32_t tag) {
    if (!t) return;
    if (seed.p) {                                /* 种子非空：灌起点值（不触发前向 deps） */
        if (!t->mt) { t->value = seed; t->tag = tag; }
        else { uint32_t s = tok_write_begin(t); tok_store_value(t, seed, tag); tok_write_end(t, s); }
    }
    tok_back_cache *bc = (tok_back_cache *)sc_get_ord(&t->back);
    if (!bc || bc->epoch != g_graph_epoch) {     /* 缓存缺失/失效：双检入锁构建（稳态不入此） */
        for (;;) { int e = 0; if (sc_test_and_set_acq(&g_back_lock, &e, 1)) break; }
        bc = (tok_back_cache *)sc_get_ord(&t->back);
        if (!bc || bc->epoch != g_graph_epoch) {
            bc = tok_back_build(t);
            sc_set_rel(&t->back, bc);             /* release 发布：快路径 acquire 读必见已构建完整缓存 */
        }
        sc_set_rel(&g_back_lock, 0);
    }
    if (bc->mode == 2) {                          /* drain：按 depth 降序唤起注册 exec 节点 */
        for (int i = 0; i < bc->n; i++) {
            token *x = (token *)bc->list[i];
            if (x && x->exec && x->exec(x, x->ctx))
                break;                           /* 已认领并处理一节点 → 中止本轮 back 扫描 */
        }
    } else {                                      /* 边反向：按反拓扑序唤起 follow（active=TOK_BACK） */
        for (int i = 0; i < bc->n; i++) {
            tok_dep *d = (tok_dep *)bc->list[i];
            if (d->follow && d->follow(d->ts, d->ntot, TOK_BACK, d->ctx))
                break;                           /* follow 返回非 0 = 请求中止反向遍历 */
        }
    }
}

/* 烘焙：编译期 Tarjan 算好的 SCC 反馈簇划分，注册时以常量写入句柄（lightmap 式预计算）。 */
void token_set_scc(token *t, int scc_id, int scc_size) {
    if (t) { t->scc_id = scc_id; t->scc_size = scc_size; }
}

/* t.scc()：读受控反馈簇编号（O(1) 常量；非反馈/未烘焙为 0）。 */
int token_scc(token *t) {
    return t ? t->scc_id : 0;
}

/* 所属反馈簇大小（>1 或含自环 = 反馈簇；0/1 = 非反馈）。 */
int token_scc_size(token *t) {
    return t ? t->scc_size : 0;
}

/* dep loop：受控反馈环注册。与 map 不同——触发源**不反挂** deps[]（杜绝 set 自动级联导致
 * 无限环），仅登记到全局 loop dep 列表，由显式 token_loop_run 按 SCC 簇驱动迭代；目标侧仍建
 * 反向邻接 producers[]（供 back / 查询）。SCC 簇划分由编译期 Tarjan 烘焙（token_set_scc）。 */
void token_depend_loop(token **ts, int nsrc, int ntgt, int all, token_follow follow, void *ctx) {
    int ntot = nsrc + ntgt;
    g_graph_epoch++;                             /* 拓扑变更：注册 loop dep 即失效所有 back 调度缓存 */
    tok_dep *d = (tok_dep *)sc_chunk0(sizeof(tok_dep));
    d->ts = (token **)sc_chunk((size_t)(ntot ? ntot : 1) * sizeof(token *));
    memcpy(d->ts, ts, (size_t)ntot * sizeof(token *));
    d->n = nsrc;
    d->ntot = ntot;
    d->all = all;
    d->follow = follow;
    d->ctx = ctx;
    d->armed = (unsigned char *)sc_chunk0((size_t)(nsrc ? nsrc : 1));
    d->remain = nsrc;
    for (int i = nsrc; i < ntot; i++) {          /* 反向邻接：目标记下产出它的 dep（供 back / 查询） */
        token *t = ts[i];
        if (!t) continue;
        if (t->nprod == t->capprod) {
            t->capprod = t->capprod ? t->capprod * 2 : 4;
            t->producers = (tok_dep **)sc_refit(t->producers, (size_t)t->capprod * sizeof(tok_dep *));
        }
        t->producers[t->nprod++] = d;
    }
    if (g_nloop == g_caploop) {                  /* 登记全局 loop 列表（token_loop_run 据 SCC 簇筛选驱动） */
        g_caploop = g_caploop ? g_caploop * 2 : 8;
        g_loop_deps = (tok_dep **)sc_refit(g_loop_deps, (size_t)g_caploop * sizeof(tok_dep *));
    }
    g_loop_deps[g_nloop++] = d;
}

/* t.loop_run(max)：驱动 t 所在 SCC 反馈簇迭代至多 max 轮。每轮对簇内每条 loop dep 以
 * acting=TOK_LOOP 唤起 follow（环体读旧值→算新值→token_set 写回，下轮读到新值）。
 * 返回实际迭代轮数；非反馈簇（scc_size<=1）返 0。一期无 eps 收敛判据，跑满 max 轮。 */
int token_loop_run(token *t, int max) {
    if (!t || t->scc_size <= 1 || max <= 0) return 0;  /* 非反馈簇：无环可迭代 */
    int scc = t->scc_id;
    int iter = 0;
    for (; iter < max; iter++) {
        for (int i = 0; i < g_nloop; i++) {
            tok_dep *d = g_loop_deps[i];
            int in = 0;                          /* 该 dep 属 t 的反馈簇？任一成员同簇即是 */
            for (int j = 0; j < d->ntot; j++)
                if (d->ts[j] && d->ts[j]->scc_size > 1 && d->ts[j]->scc_id == scc) { in = 1; break; }
            if (in && d->follow) d->follow(d->ts, d->ntot, TOK_LOOP, d->ctx);
        }
    }
    return iter;
}
