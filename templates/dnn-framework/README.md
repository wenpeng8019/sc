# dnn-framework —— 深度神经网络框架

把神经网络表达为一张 `tok` 依赖图，三套机制分别承担前向、反向与训练：

| 机制 | 语法 | 职责 |
|------|------|------|
| 前向 forward | `dep…map` DAG 边 | 输入→隐层→输出→loss，每条边 follow 体 = 该层前向算子 |
| 反向 backward | `back loss` | 自 loss 反向遍历（反拓扑序），逐层算梯度（链式法则） |
| 训练 train | 外层 `for` 循环 | `pulse` 驱前向 → `backward` → 优化器更新权重，重复 |

权重 `w` 是**参数 token**：不在 map 图里（不被级联），各层 follow 按句柄读它、反向算出
梯度 `gw`、优化器据 `gw` 做梯度下降。

## 运行

```sh
./compiler/build/scc templates/dnn-framework/dnn.sc
```

输出：12 个 epoch 的训练日志（loss 从 -40 收敛到 -3，`w` 从 10 逼近 `target=50`，第 11 轮到 47），
再做一次推理。

注意：训练循环里输入 `x` 每轮都喂同一个值 `1`。若仍用 `x->set((1:@), 0)`，tok 的同值抑制会把后续 epoch
视为“无新事件”，前向不再重跑，`y/loss` 卡死而 `w` 继续漂移。这里改用 `x->pulse((1:@), 0)`：保留默认
`set` 的记忆化语义，同时给迭代驱动一个显式“同值也要传播”的逃生门。

## 内置示例（单权重线性回归，演示完整闭环）

```
前向：  x ──► y ──► loss        y = w·x ,  loss = y - target
        (map)  (map)
反向：  back loss               gy = (y-target) ,  gw = gy·x
更新：  w := w - lr·gw          MSE 梯度下降，lr=1/4
```

每层算子（map 边的 follow 体）含**前向 + 反向两个分支**，以 `this->active == TOK_BACK(-4)`
区分：反向分支读下游梯度、写上游/权重梯度；前向分支读上游、算、写下游。前向链与梯度量在训练前也需
先 `form` 激活，否则 `set/pulse` 只会落待决值、不传播。

## 扩展为通用 MLP

1. **加一层**（如隐层 `h`）：加 `tok h` + 两条 map 边（`x→h`、`h→y`），各自 follow 体写
   前向（`h = relu(w1·x)`）与反向分支。`depth()`/`critical()` 自动重烘焙 = 层执行序与瓶颈层。
2. **张量权重 / 批量**：标量换堆专属缓冲 `def tensor&: { n: i4, data: [1024]f4 }`，
   层 follow 体里做矩阵运算。
3. **真实优化器**：`backward_step` 里把 `w - g/LR_DIV` 换成 Adam/动量（再开状态 token 存矩）。
4. **RNN / 反馈网络**：层间有反馈环时用 `dep loop` 替 `dep map`，按时间步 `loop_run(steps)` 展开。
5. **并行**：同 `batch()` 的层两两无依赖，可并行前向。

详见 `dnn.sc` 文末「扩展指南」与 `syntax.md` §21（tok 体系，含 `back` / `loop`）。
