# examples 示例索引

两类示例，均由 `./build.sh test` 冒烟运行（编译 + 执行，校验不崩溃）。
面向"产物不变"的回归验证（`--emit-c`/`--emit-sc` 黄金快照）见 [../tests](../tests)。

## 编号系列（featureN.sc）：语言特性递进

按引入顺序编号，每个在前者基础上验证一组新增特性，全部可直接运行
（`scc examples/featureN.sc`）：

| 文件 | 覆盖特性 |
|------|----------|
| feature1.sc | 核心语法：def（枚举/结构/联合/别名）、fnc（函数类型/实现/多行参数）、var/let、指针 `&`、控制流、多行条件表达式 |
| feature2.sc | 多维数组 `name[x][y]`、`@` 导出、`inc` 头文件 |
| feature3.sc | `bool` 布尔类型、`true/false/nil` 常量、伪 class 方法字段 |
| feature4.sc | sc 模块导入（`inc x.sc`，单元编译+链接）：builtins/adt/adt.sc |
| feature5.sc | `rpc` 伪形参函数（参数/返回值展开为同名结构体，`@rpc` 导出） |
| feature6.sc | 内置 ADT（string/list）与方法语法：`fnc T::m` 定义/声明、init 声明即构造、drop 手动析构、调用糖 |
| feature7.sc | 内置多线程（run + m）：`run rpc调用[, &t|pool]` 创建线程/入池，joinable/detach、mutex 保护计数、P_usleep（platform.h）、`c.wait(&mu[, nsec, sec])` 条件等待、pool 线程池、tls 线程局部变量 |
| feature8.sc | 语法糖三件套：右值强转免括号 `expr: type&`、调用缺参默认补 0/nil/{0}、结构体内成员函数实现 |
| feature9.sc | 链表结构体 `def T: ~ {}`（注入 `_prev`/`_next`）与内置 `chain` 双向链表：append/push/pop/before/after/remove/first/last/revert/append_to/push_to/cut；`prev`/`next` 上下文关键字（边界安全逻辑前驱：head 无前驱→nil） |
| feature10.sc | ADT 容器结构体 `def T: <C, I> {}`（把链接节点 `I` 注入为元素首位 `_adt`）：自定义容器 `C` 实现 insert/remove/find/first/next/last/prev；导航经容器方法 `t.next(it): T&`，实参 `T&`⟷`I&` 自动重解释；`ret` 返回码 / `ok` 字面量；`base(&t)` 跳过注入成员 |

## 专项验证（feature_*.sc）：编译器机制

不属于递进系列，针对单一机制：

| 文件 | 验证点 | 运行方式 |
|------|--------|----------|
| feature_forward.sc | 定义顺序无关（前置声明 + 函数原型，互递归） | 可直接运行 |
| feature_export_inc.sc | `@inc` 导出到生成头文件 | 仅 `--emit-c -o`（无 main） |
| feature_bad_value_cycle.sc | 按值互相包含的语义报错 | 负向用例，预期编译失败 |
| test_demo.sc | 单元测试框架 `tst` / `assert` / `tst.skip`（含值回显与软失败） | 仅 `--test`（无 main，含故意失败用例，退出码非零） |

## 维护引用清单：增删 / 重排 / 改名 feature 时要连带改的地方

feature 的**文件名**与**编号顺序**被多处硬编码引用。调整时按下表逐一同步，
否则回归会失败或文档失准：

| 牵连位置 | 内容 | 何时要改 |
|----------|------|----------|
| [tests/golden/](../tests/golden) | 每个 feature 一对同名快照 `featureN.c` + `featureN.sc`（负向用例为 `feature_bad_value_cycle.err`） | 改名→`git mv` 重命名；改内容/重排→ `./tests/run.sh --update` 重生成 |
| [tests/run.sh](../tests/run.sh) | POSITIVE 列表逐行硬编码 `examples/featureN.sc` 路径与顺序，末尾负向单列 | 增删 / 重排 / 改名 |
| [build.sh](../build.sh) | e2e 冒烟 `for f in feature1 ... feature_forward` 编号列表；另有特例硬编码 `feature1`（--emit-c 演示）、`feature_export_inc`（仅头文件）、`feature_bad_value_cycle`（预期报错） | 增删 / 重排；动到这三个特例的名字/编号时 |
| 附属模块（`featureN_*.sc`） | 某些 feature 带「附属/消费单元」（如 [feature4_lib.sc](feature4_lib.sc) 演示跨模块静态全局生命周期、[feature30/feature30_mod.sc](feature30/feature30_mod.sc) 演示根模块导出注入的消费侧）。被主 feature `inc`，随主 feature 编译运行。**消费单元若引用主模块导出，不可单列进 `tests/run.sh` POSITIVE**（独立 `--emit-c` 会因缺主模块上下文而失败）；自包含的附属模块（如 feature4_lib）才可单列。**根模块导出注入示例（`@@` 标记）须放在独立子目录**（如 [feature30/](feature30/)）：根发现按目录扫描，同目录多个独立程序会被同一 `@@` 根波及 | 增删主 feature 时连带处理其附属模块 |
| 本文件 [README.md](README.md) | 上方两张特性索引表（编号系列强调"按引入顺序"，重排即需重写描述） | 增删 / 重排 / 改内容 |
| feature 间交叉引用 | feature 源码注释里互相提编号，如 [feature15.sc](feature15.sc) 注释"对比 feature12" | 被引用方改号时 |
| [builtins/REFERENCE.md](../builtins/REFERENCE.md) | "完整示例见 `examples/feature6.sc` / `feature7.sc`"等编号引用 | 被引用方改号时 |
| 根 [README.md](../README.md) | 命令示例用 `feature1.sc` | 仅 feature1 被动到时 |
| [compiler.md](../compiler.md) | 示意报错文本 `feature1.sc:31`（影响很小） | 仅 feature1 被动到时 |

**不用动**：`.git/*`（历史提交信息）、构建产物（`compiler/build*`、`CMakeFiles/*`、`vendor/*`）。

**推荐顺序**：① 改/移动 `examples/featureN.sc` 本体 → ② 同步 `tests/run.sh`、`build.sh` 编号清单
→ ③ 改交叉引用注释、本表所列文档编号 → ④ `git mv` 或删除对应 `tests/golden/featureN.*`
→ ⑤ `./tests/run.sh --update` 重生成 golden → ⑥ `./build.sh test` 跑通双后端回归。
