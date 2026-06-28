# hello-dnn

最小可运行示例工程：

- hello.sc
  - 原始层级 tok 图线性回归骨架（手写 dep/map/back）
- component-demo.sc
  - 使用 nn 自动微分引擎（val/linear/optim，define-by-run）训练单层 Linear（免手写 wiring）

运行：

```sh
./compiler/build/scc templates/dnn-framework/hello-dnn/hello.sc
./compiler/build/scc templates/dnn-framework/hello-dnn/component-demo.sc
```
