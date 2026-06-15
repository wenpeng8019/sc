# tests 回归测试

针对 AST / codegen 优化做**产物不变**回归：把确定性产物存为黄金快照（golden），
每次比对，差异即失败。与 `examples/`（侧重"能跑通"的冒烟）互补，这里侧重"产物没变"。

## 比对什么

确定性产物（与机器无关，不含运行时地址/绝对路径）：

| 产物 | 命令 | 黄金文件 |
|------|------|----------|
| 转译 C 源码 | `scc x.sc --emit-c` | `golden/<名>.c` |
| 规范化 sc 源码 | `scc x.sc --emit-sc` | `golden/<名>.sc` |
| 负向用例错误信息 | `scc x.sc`（预期失败，比对 stderr） | `golden/<名>.err` |

> 运行时不确定的输出（多线程顺序、`stringify` 指针地址等）不做 stdout 快照，
> 故选择 emit-c / emit-sc 这类纯函数产物作为回归基线。

## 用例来源

- `examples/feature*.sc` — 语言特性示例（复用）。
- `cases/*.sc` — 精简专项用例，针对易回归的表达式/codegen 点：
  - `cast.sc` — 强制类型转换各形态（裸转、括号转后 `->`、多级指针）。
  - `expr.sc` — `sizeof` / `offsetof` / 三目 / 成员下标。

## 运行

```sh
./build.sh test            # 构建 + examples 冒烟 + 本回归比对
./build.sh test --update   # 有意改动产物后，重新生成黄金文件
tests/run.sh               # 仅跑回归比对（需先 ./build.sh build）
tests/run.sh --update      # 仅更新黄金文件
```

新增专项用例：在 `cases/` 放 `.sc`，把相对路径加入 `run.sh` 的 `POSITIVE`
（负向用例加入 `NEGATIVE`），再 `./build.sh test --update` 生成黄金文件并提交。
