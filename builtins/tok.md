
# 分布式 token `tok`：观念量、依赖演变与 form 形成

> 以字符串 id 唯一标识的**共享量**（distributed token / 观念 notion）。值类型擦除为 `@`
> （裸自动指针，自描述胖指针，携 `tar`/`own`/`dtor`），在「设值即广播、依赖即重算」的声明式
> 语义下跨函数、跨模块（限同进程）自动传播。

- 语法层：关键字 `tok` / `dep` / `form` + 上下文门词 `all` / `any`，类型为 `token&`（op 层声明）。
- 运行时层：`builtins/tok/tok_impl.c` 内的 `token_bind` / `token_get` / `token_set` / `token_form` /
  `token_depend`，经 op→tok 隐式依赖随工程**始终链接**（无需 `inc`）。
- 编译层：声明降解为隐藏的 combine / follow 函数 + 注册调用，注入到 `main` / 模块 `init` 序章。

---

## 0. 设计目标与边界

1. **观念即共享量**：同一个字符串 id 在任何位置 `tok x: "id"` 引用到的是**同一个** token。
   声明是「create-or-get」——首次创建、后续幂等返回（adt 哈希 dict 按字符串 id intern）。
2. **设值即传播**：`x.set(v)` 不只是写值，还会触发挂在该 token 上的全部依赖关系（`dep`）重算。
3. **两种身份**：
   - **form（形成主）**——带 combine 回调，输入要先经回调「合成」再落值（去抖、取峰、累加…）。
   - **enforce（纯从）**——无 combine，`set` 直接赋值。
4. **声明式依赖**：`dep all/any:` 声明 token 间的依赖关系，任一上游变更即唤起 `follow` 回调，
   由**与门（all）/ 或门（any）**决定触发时机；`follow` 返回值动态切换下次门逻辑。
5. **边界（v2）**：
   - **模块域静态对象**，不支持 `@` 导出（注册延迟到模块 `init` / `main` 序章）。
   - 值类型**擦除为 `@`**（`sc_afat`），读出后由调用点 `(e: T@)` 还原；裸指针/整数经 `(e: @)`
     装箱为非托管裸 `@`。`@` 携 `dtor`，故下游还原类型安全、值自描述。
   - id 为**进程内**字符串键（adt 哈希 dict，O(1) intern）；`'/'` 前缀 token 进入多线程
     模式，**细粒度无锁**同步：值用每-token **序列锁（seqlock）**（读无锁乐观重试、写者经
     `seq` 自旋独占），依赖门用每-dep **自旋锁**（仅护门计数），`follow` 回调一律在释放
     所有锁后调用——任一线程任一时刻零持锁跑用户码，根除跨 token 锁序死锁，独立 token 子图
     真并行。MT 依赖会将其全部成员一并升格 MT。非 MT token 零原子开销。
     > **约束**：`combine` 须**纯**（仅 `base`/`input`→`value`，不得 `set`/`get` 其他 token，
     > 它在写者独占下运行）；跨 token 副作用一律写在 `follow`（锁外运行）。`dep` 注册在模块
     > `init` 单线程完成（建图先于任何并发 fire）。
   - token 持值为结构拷贝（保留 `p`/`tar`/`own`/`dtor`），不额外 retain/release。

---

## 1. 语法面

### 1.1 `tok` —— 声明 token 句柄

```sc
# enforce 纯从：set 直接赋值
tok alert: "sensor.alert"

# form 形成主：紧随缩进 combine 体（取较大者 = 峰值保持 / 去抖）
tok level: "sensor.level"
    var b: i8 = (this->base: i8)    # 当前值（@ → i8）
    var i: i8 = (this->input: i8)   # 本次输入
    var m: i8 = b
    if i > b
        m = i
    return (m: @)                  # 合成结果装箱回 @
```

- 形态：`tok <句柄名>: "<字符串 id>"`，**紧随的缩进块**（无引导冒号）即 **combine 体**。
- combine 体唯一上下文形参 `this: __sctok_in&`，成员皆 `@`：
  | 成员 | 含义 |
  |------|------|
  | `this->base`   | 当前值 |
  | `this->input`  | 本次输入 |
  | `this->sender` | 输入者（恒空 `@`，预留） |
  | `this->tag`    | `set` 随附标签（`i4`，体内分流用） |
- 返回 `@`（新值）。无 combine 体 = enforce 纯从。

### 1.2 `form` —— 形成 / 灌初值

```sc
form level, (0: @)          # 初始化 form token，灌初值 0（@）并升格为 form 主
```

- 形态：`form <token 句柄>, <初值>`（语言内置语句关键字，与 `run` / `done` 同级；初值为 `@`）。
- 语义：给 form token 置初值并标记已置值，随后回放 form 前挂起的 `set`。

### 1.3 `dep` + `all` / `any` —— 依赖关系

```sc
# 或门：温度 temp 或 烟雾 smoke 任一变更即评估，超阈值点亮 alert（行内多依赖，逗号分隔）
dep any: t:"sensor.temp", s:"sensor.smoke"
    var hot:   bool = (t->get(): i8) > 100   # t = this->toks[0]（局部名糖）
    var smoky: bool = (s->get(): i8) > 0     # s = this->toks[1]
    if hot or smoky
        alert->set((1: @), 0)
    return false                 # 返回门逻辑：false=下次仍或门，true=改与门

# 与门：温度 temp 与 湿度 humid 都更新过才一并评估（块形态，每行一项）
dep all:
    t:"sensor.temp"
    h:"sensor.humid"
    -                            # '-' 分隔依赖项列表与 follow 体
    var idx: bool = (t->get(): i8) > (h->get(): i8)
    comfort->set((idx: @), 0)
    return true                  # 维持与门：下轮仍需两者都变更
```

- 门词：`all`（与门）/ `any`（或门），为**上下文标识**（非保留字）。
- 依赖项：`<局部名>:"<id>"`，可**行内**（逗号分隔）或**块形态**（每行一项，`-` 单独成行分隔体）。
  单依赖时 `all` / `any` 等价（只有一个上游，触发时机相同）；多依赖才显出门逻辑差异：
  **或门**任一上游变更即触发，**与门**须全部上游各变更过一次才触发（之后重新计数）。
- follow 体唯一上下文形参 `this: __scdep_in&`：`this->toks`（依赖项数组 `token&&`）/
  `this->count`（个数）/ `this->active`（触发动作码）。各局部名 `a:"id"` 由编译器注入糖
  `var a: token& = this->toks[i]`，可直接以局部名引用第 `i` 个依赖项。返回 `bool` = 下次门逻辑
  （`true`=与门 / `false`=或门）。

### 1.4 取值 / 设值

token 句柄是 `token&`（指针），用**箭头** `->` 调方法：

```sc
level->set((150: @), 0)         # 设值（@ 值 + i4 标签）→ 触发依赖级联
var lv: i8 = (level->get(): i8)  # 取值（返回 @，调用点 (e: T@) / (e: i8) 还原）
```

---

## 2. 降解（desugaring）

编译器把声明式语法降解为普通函数 + 注册调用：

| 源语法 | 降解产物 |
|--------|----------|
| `tok t: "id"` + 紧随缩进体 | 隐藏 `__sctok_<id>_combine`（`tokHidden` 函数，单形参 `this: __sctok_in&`，返回 `@`）+ `var t: token&`（`isTok`，记 `tokId` / `tokFn`） |
| `tok t: "id"`（无体） | 仅 `var t: token&`（`isTok`，`tokFn` 空 = enforce） |
| `dep all/any: …` + 体 | 隐藏 `__scdep_<N>_follow`（`tokHidden`，单形参 `this: __scdep_in&`，返回 `bool`；体首注入各依赖项局部名糖 `var a: token& = this->toks[i]`）+ `DepD`（记 `depAll` / `depItems` / `tokFn`） |
| `form t, v` | `FormS` 语句 → `token_form(t, v, 0)`（`v` 为 `@`） |
| `t->set(v, tag)` / `t->get()` | 方法分派 → `token_set(t, v, tag)` / `token_get(t)` |

注册代码注入到 `main` 序章（或模块 `init` / 测试 runner）：

```c
/* tok 绑定 */
temp  = token_bind("sensor.temp",  NULL);
smoke = token_bind("sensor.smoke", NULL);
alert = token_bind("sensor.alert", NULL);
/* dep 注册（依赖项数组 + 门逻辑 all=0(或门) + follow 蹦床） */
{ token *_deps0[] = { temp, smoke }; token_depend(_deps0, 2, 0, __scdep_0_tramp, NULL); }
```

follow / combine 的 C ABI 签名以上下文结构传递；combine 直接收 `__sctok_in*`，follow 由编译器生成的
**蹦床**把运行时通用签名打包成 `__scdep_in&`：

```c
static int __scdep_0_tramp(token **_ts, int _n, int _acting, void *_ctx) {
    (void)_ctx;
    __scdep_in _self; _self.toks = _ts; _self.count = _n; _self.active = _acting;
    return (int)__scdep_0_follow(&_self);
}
```

---

## 3. 运行时（builtins/tok/tok_impl.c）

| 函数 | 作用 |
|------|------|
| `token *token_bind(const char *id, token_combine cb)` | create-or-get：按 id intern（adt 哈希 dict）；`cb` 非空则挂为 form 主，幂等补挂 |
| `sc_afat token_get(token *t)` | 取当前值（`@`） |
| `void token_set(token *t, sc_afat v, int32_t tag)` | 有 combine 则 `value = cb(this{base,input:v,sender,tag})`，否则直接赋值；触发变更依赖 |
| `void token_form(token *t, sc_afat v, int32_t tag)` | 灌初值、升格 form 主、回放挂起、触发就绪依赖 |
| `void token_depend(token **ts, int n, int all, token_follow cb, void *ctx)` | 注册依赖：拷贝依赖项数组，反挂到各 token 的 `deps[]` |

上下文结构（C ABI，见 `tok.h`）：`__sctok_in { sc_afat sender, base, input; int32_t tag; }`、
`__scdep_in { token **toks; int32_t count; int32_t active; }`。

### 3.1 门逻辑与触发

`token_set` / `token_form` → `tok_fire`：遍历该 token 反挂的每条依赖关系（`tok_dep`），按门逻辑处理：

- **与门（all）**：每个依赖项本轮首次事件即「武装(arm)」并令 `remain--`；`remain` 归零（全部
  依赖项各到位一次）才以 `acting=TOK_ALL_CHANGED`（set）/ `TOK_ALL_READY`（form）唤起 `follow`，
  触发后重置 `armed` / `remain` 进入下一轮。
- **或门（any）**：任一事件即唤起 `follow`（`acting=` 变更项下标，或 form 时 `TOK_ANY_READY`）。
- `follow` 返回值更新该关系的门逻辑（`true`→与门 / `false`→或门），动态切换；切到与门时重置计数。

`token_depend` 注册时即结算一次：已就绪（form / enforce）的依赖项预先武装，满足门逻辑则立即以
`TOK_ALL_READY` / `TOK_ANY_READY` 触发 follow（对齐"注册即就绪"语义）。

动作码：`TOK_ALL_READY (-2)`（与门 form 就绪）、`TOK_ALL_CHANGED (-3)`（与门 set 变更）、
`TOK_ANY_READY (-1)`（或门就绪 / 退化）、`idx >= 0`（或门变更项下标）。依赖表 `deps` 与挂起队列
`pending` 均为动态增长数组（无定长上限）。

> 后续待扩：关系索引访问、引用计数 / 生命周期回收、句柄与依赖记录的回收（当前为进程生命周期
> 静态对象）。v2 已含 adt 哈希 dict token 查找、form 就绪（`TOK_ALL_READY` / `TOK_ANY_READY`）
> 触发、`'/'` 前缀多线程细粒度无锁（每-token seqlock 读 + 每-dep 自旋门 + follow 锁外）。

---

## 4. 完整示例

见 `examples/feature47.sc`：`level`（form / 峰值保持）+ `alert`（enforce）+ `dep any`
（超阈值点亮 alert）。运行输出：

```
after 50:  level=50 alert=0
after 150: level=150 alert=1     # 超阈值，依赖触发
after 30:  level=150             # 30 < 峰值 150，combine 取较大者 → 仍 150
```

---

## 5. 实现落点

| 文件 | 内容 |
|------|------|
| `compiler/src/lexer.{h,cpp}` | 关键字 `tok` / `dep` / `form`（`KwTok` / `KwDep` / `KwForm`） |
| `compiler/src/ast.h` | `Stmt::FormS`、`Decl::DepD`、`Decl` 的 `isTok` / `tokId` / `tokFn` / `tokHidden` / `depAll` / `depItems` |
| `compiler/src/parser.cpp` | `parseTok` / `parseDep` / `FormS`；id 去引号、combine / follow 合成 |
| `compiler/src/semantic.cpp` | `FormS` 类型检查（expr 为 token&、初值任意） |
| `compiler/src/codegen_c.cpp` | 收集 `tokBinds` / `depRegs`，注入注册、生成蹦床与前向声明 |
| `compiler/src/codegen_sc.cpp` | `tok` / `dep` / `form` 反生成；跳过 `tokHidden` 函数 |
| `compiler/src/ast_json.cpp` | `FormS` / `DepD` / `tok` 节点 |
| `builtins/op.sc` | `@def token`（句柄类型 + `get` / `set` 方法协议，默认导入，不生成代码） |
| `builtins/op.h` | `#include "tok/tok.h"`（令 `token` / `token_*` 随 op.h 默认带入每个 C 单元） |
| `builtins/tok/tok.sc` | tok 运行时载体模块（经 op→tok 隐式依赖携带 `tok_impl.c`） |
| `builtins/tok/tok.h` | `token` C 类型与 `token_*` 原型（C ABI 契约） |
| `builtins/tok/tok_impl.c` | `token_*` 运行时（经拼接机制始终链接） |
| `compiler/src/main.cpp` | op→tok 隐式依赖（使 tok 单元恒入图、`tok_impl.c` 恒链接） |

---

## 6. 状态与后续

**已跑通（v2）**：声明 / form / 依赖触发、combine（form 主）/ enforce（纯从）、and / or 门、
follow 动态切门、form 就绪触发（`TOK_ALL_READY` / `TOK_ANY_READY`）、form 前挂起回放、
`'/'` 前缀多线程**细粒度无锁**（每-token seqlock 读 + 每-dep 自旋门 + follow 锁外，
TSan 干净）、adt 哈希 dict intern、`@` 值贯通（`this` 上下文结构 + 标签）、
emit-c / emit-sc 双向往返、回归快照。

**未实现（后续）**：关系索引访问、跨进程 id 空间、引用计数 / 生命周期回收、句柄与依赖记录回收。