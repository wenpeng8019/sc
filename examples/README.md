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
| feature7.sc | 内置多线程（run/wait + m）：`run rpc调用[, &t|pool]` 创建线程/入池，joinable/detach、mutex 保护计数、P_usleep（platform.h）、`wait cond, mutex[, nsec[, sec]]` 条件等待、pool 线程池、tls 线程局部变量 |
| feature8.sc | 语法糖三件套：右值强转免括号 `expr: type&`、调用缺参默认补 0/nil/{0}、结构体内成员函数实现 |
| feature9.sc | 链表结构体 `def T: ~ {}`（注入 `_prev`/`_next`）与内置 `chain` 双向链表：append/push/pop/before/after/remove/first/last/revert/append_to/push_to/cut；`prev`/`next` 上下文关键字（边界安全逻辑前驱：head 无前驱→nil） |

## 专项验证（feature_*.sc）：编译器机制

不属于递进系列，针对单一机制：

| 文件 | 验证点 | 运行方式 |
|------|--------|----------|
| feature_forward.sc | 定义顺序无关（前置声明 + 函数原型，互递归） | 可直接运行 |
| feature_export_inc.sc | `@inc` 导出到生成头文件 | 仅 `--emit-c -o`（无 main） |
| feature_bad_value_cycle.sc | 按值互相包含的语义报错 | 负向用例，预期编译失败 |
