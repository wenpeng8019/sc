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
 *   idx >= 0        ：或门，本次变更的依赖项下标 */
#define TOK_ALL_READY   (-2)
#define TOK_ALL_CHANGED (-3)
#define TOK_ANY_READY   (-1)

enum { TOK_NEW = 0, TOK_READY = 1 };  /* 生命状态：NEW=未就绪 / READY=已就绪（form 或 enforce） */

/* MT 同步：细粒度无锁。值用每-token seqlock（t->seq），依赖门用每-dep 自旋锁（d->glock），
 * follow 一律锁外调用（见各函数）。所有锁状态字段 calloc 零值即初始（seq 偶=可读、glock 空闲），
 * 无需运行时 init；非 MT token 全程不触原子（零开销）。下方为 seqlock / 门锁的原子助手。 */

typedef struct tok_dep {
    token        **ts;      /* 依赖项句柄数组（注册时拷贝） */
    int            n;
    int            all;     /* 当前门逻辑：1=与门 / 0=或门（follow 返回值更新） */
    int            remain;  /* 与门：本轮尚未武装(arm)的依赖项数；归零即触发 */
    unsigned char *armed;   /* 每依赖项本轮是否已武装（n 字节；与门去重计数用） */
    int            mt;      /* 任一成员为 MT 则整条 dep 走自旋门锁（glock） */
    int            glock;   /* 门状态自旋锁（0=空闲 / 1=持有）：仅护 all/remain/armed */
    token_follow   follow;
    void          *ctx;
} tok_dep;

struct token {
    char         *id;        /* 字符串唯一键 */
    sc_afat       value;     /* 当前值（@ 类型擦除自描述胖指针） */
    int32_t       tag;       /* 当前值随附标签（最近一次 set/form 的 tag） */
    token_combine combine;   /* 非空=form 候选（NEW→form 才 READY）；空=enforce（bind 即 READY） */
    int           state;     /* TOK_NEW / TOK_READY */
    tok_dep     **deps;      /* 动态：引用本 token 的依赖关系（可增长数组） */
    int           ndeps, capdeps;
    sc_afat      *pendvals;  /* form 前挂起的 set 输入值（动态；form 时回放后释放） */
    int32_t      *pendtags;  /* 对应挂起输入的 tag */
    int           npending, cappending;
    int           mt;        /* MT token（id 以 '/' 开头）：值经 seqlock 同步；0=非 MT，零开销 */
    uint32_t      seq;       /* seqlock 序列号：偶=稳定可读 / 奇=写者占用中（兼写者自旋互斥） */
};

/* ---- 全局 token 表：adt 哈希（拷贝字符串键 → 裸 token*） ---- */
static dict       g_toks;          /* key_size=-1：dict 自持 id 拷贝 */
static sc_mutex_t g_toks_mtx;      /* 守护 bind 的 intern 并发 */
static int        g_toks_init = 0; /* 惰性构造（首次 bind 单线程，模块 init 阶段） */

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
    char *p = (char *)malloc(n);
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
    t = (token *)calloc(1, sizeof(token));
    t->id = tok_strdup(id);
    t->combine = combine;
    t->state = combine ? TOK_NEW : TOK_READY;         /* form 候选待 form；enforce 即就绪 */
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
        int next = d->follow ? d->follow(d->ts, d->n, acting, d->ctx) : 0;  /* 锁外回调 */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);             /* 或门→与门：重新计数 */
        dep_unlock(d);
    }
}

void token_set(token *t, sc_afat v, int32_t tag) {
    if (!t) return;
    if (!t->mt) {                                      /* 非 MT 快路径：无锁无原子 */
        if (t->combine && t->state == TOK_NEW) {       /* form 候选未 form：入挂起队列 */
            if (t->npending == t->cappending) {
                t->cappending = t->cappending ? t->cappending * 2 : 4;
                t->pendvals = (sc_afat *)realloc(t->pendvals, (size_t)t->cappending * sizeof(sc_afat));
                t->pendtags = (int32_t *)realloc(t->pendtags, (size_t)t->cappending * sizeof(int32_t));
            }
            t->pendvals[t->npending] = v;
            t->pendtags[t->npending] = tag;
            t->npending++;
            return;
        }
        t->value = t->combine ? tok_run_combine(t, t->value, v, tag) : v;
        t->tag = tag;
        tok_fire(t, 0);
        return;
    }
    /* MT：写者经 seq 取得独占（兼互斥），改值后发布，再锁外传播 */
    uint32_t s = tok_write_begin(t);
    if (t->combine && t->state == TOK_NEW) {           /* form 候选未 form：入挂起队列（token 私有，仅写者触碰） */
        if (t->npending == t->cappending) {
            t->cappending = t->cappending ? t->cappending * 2 : 4;
            t->pendvals = (sc_afat *)realloc(t->pendvals, (size_t)t->cappending * sizeof(sc_afat));
            t->pendtags = (int32_t *)realloc(t->pendtags, (size_t)t->cappending * sizeof(int32_t));
        }
        t->pendvals[t->npending] = v;
        t->pendtags[t->npending] = tag;
        t->npending++;
        tok_write_end(t, s);
        return;
    }
    sc_afat nv = t->combine ? tok_run_combine(t, tok_value_excl(t), v, tag) : v;  /* MT：base 经原子读（写者独占下） */
    tok_store_value(t, nv, tag);
    tok_write_end(t, s);
    tok_fire(t, 0);                                    /* 锁外：变更事件传播 */
}

void token_form(token *t, sc_afat v, int32_t tag) {
    if (!t) return;
    if (!t->mt) {                                      /* 非 MT 快路径 */
        t->value = v;
        t->tag = tag;
        t->state = TOK_READY;
        if (t->npending) {                             /* 回放 form 前挂起的 action（按 combine 合并） */
            for (int i = 0; i < t->npending; i++) {
                t->value = t->combine ? tok_run_combine(t, t->value, t->pendvals[i], t->pendtags[i])
                                      : t->pendvals[i];
                t->tag = t->pendtags[i];
            }
            free(t->pendvals); free(t->pendtags);
            t->pendvals = NULL; t->pendtags = NULL;
            t->npending = t->cappending = 0;
        }
        tok_fire(t, 1);
        return;
    }
    /* MT：写者独占灌初值 + 回放挂起，发布后锁外触发就绪 */
    uint32_t s = tok_write_begin(t);
    t->state = TOK_READY;                              /* 升格 form 主（写者独占下） */
    tok_store_value(t, v, tag);                        /* 灌初值（原子发布；seq 仍奇，读者重试） */
    if (t->npending) {                                 /* 回放挂起：每步以当前值为 base 经 combine 合并 */
        for (int i = 0; i < t->npending; i++) {
            sc_afat nv = t->combine ? tok_run_combine(t, tok_value_excl(t), t->pendvals[i], t->pendtags[i])
                                    : t->pendvals[i];
            tok_store_value(t, nv, t->pendtags[i]);
        }
        free(t->pendvals); free(t->pendtags);
        t->pendvals = NULL; t->pendtags = NULL;
        t->npending = t->cappending = 0;
    }
    tok_write_end(t, s);
    tok_fire(t, 1);                                    /* 锁外：就绪事件触发依赖门 */
}

void token_depend(token **ts, int n, int all, token_follow follow, void *ctx) {
    tok_dep *d = (tok_dep *)calloc(1, sizeof(tok_dep));
    d->ts = (token **)malloc((size_t)n * sizeof(token *));
    memcpy(d->ts, ts, (size_t)n * sizeof(token *));
    d->n = n;
    d->all = all;
    d->follow = follow;
    d->ctx = ctx;
    d->armed = (unsigned char *)calloc((size_t)(n ? n : 1), 1);
    d->remain = n;
    for (int i = 0; i < n; i++)                         /* 先定门：任一成员 MT 则整条 dep 走全局锁 */
        if (ts[i] && ts[i]->mt) { d->mt = 1; break; }
    for (int i = 0; i < n; i++) {                       /* 反挂到各依赖项的动态 deps 数组 */
        token *t = ts[i];
        if (!t) continue;
        if (d->mt) t->mt = 1;                           /* MT dep：所有成员一并升格 MT，使其值访问亦受全局锁串行化（杜绝非 MT 成员被 follow 跨线程读写的竞争） */
        if (t->ndeps == t->capdeps) {
            t->capdeps = t->capdeps ? t->capdeps * 2 : 4;
            t->deps = (tok_dep **)realloc(t->deps, (size_t)t->capdeps * sizeof(tok_dep *));
        }
        t->deps[t->ndeps++] = d;
    }
    /* 注册即就绪：依赖项中已就绪者预先武装；满足门逻辑则立即触发 ready
     *（对齐 c_prototype C_Depend：注册时已 form 的依赖项即计入门逻辑）。
     * 注册本在模块 init 单线程（deps 数组建图先于任何并发 fire）；门状态读改仍走 dep 锁、
     * follow 锁外调用，与运行期 fire 同构。 */
    int fire = 0, acting = 0;
    dep_lock(d);
    if (all) {                                         /* 与门：预武装已就绪项 */
        for (int i = 0; i < n; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { d->armed[i] = 1; d->remain--; }
        if (n > 0 && d->remain == 0) {                 /* 注册时已全部就绪 → 立即 ALL_READY */
            fire = 1; acting = TOK_ALL_READY;
            dep_reset(d);
        }
    } else {                                           /* 或门：任一已就绪 → 立即 ANY_READY */
        for (int i = 0; i < n; i++)
            if (ts[i] && ts[i]->state == TOK_READY) { fire = 1; acting = TOK_ANY_READY; break; }
    }
    dep_unlock(d);
    if (fire) {
        int next = follow ? follow(ts, n, acting, ctx) : 0;  /* 锁外回调 */
        dep_lock(d);
        int prev = d->all;
        d->all = next ? 1 : 0;
        if (!prev && d->all) dep_reset(d);
        dep_unlock(d);
    }
}
