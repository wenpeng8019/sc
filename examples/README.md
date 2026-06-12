# examples 示例索引

两类示例，均由 `./build.sh test` 端到端验证。

## 编号系列（featureN.sc）：语言特性递进

按引入顺序编号，每个在前者基础上验证一组新增特性，全部可直接运行
（`scc examples/featureN.sc`）：

| 文件 | 覆盖特性 |
|------|----------|
| feature1.sc | 核心语法：def（枚举/结构/联合/别名）、fnc（函数类型/实现/多行参数）、var/let、指针 `&`、控制流、多行条件表达式 |
| feature2.sc | 多维数组 `name[x][y]`、`@` 导出、`inc` 头文件 |
| feature3.sc | `b` 布尔类型、`true/false/nil` 常量、伪 class 方法字段 |
| feature4.sc | sc 模块导入（`inc x.sc`，单元编译+链接）：builtins/io.sc |
| feature5.sc | `rpc` 伪形参函数（参数/返回值展开为同名结构体，`@rpc` 导出） |

## 专项验证（feature_*.sc）：编译器机制

不属于递进系列，针对单一机制：

| 文件 | 验证点 | 运行方式 |
|------|--------|----------|
| feature_forward.sc | 定义顺序无关（前置声明 + 函数原型，互递归） | 可直接运行 |
| feature_export_inc.sc | `@inc` 导出到生成头文件 | 仅 `--emit-c -o`（无 main） |
| feature_bad_value_cycle.sc | 按值互相包含的语义报错 | 负向用例，预期编译失败 |
