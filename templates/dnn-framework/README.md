# dnn-framework —— 深度神经网络框架（张量版）

把神经网络表达为一张 `tok` 依赖图，三套机制分别承担前向、反向与训练；**数据面用真张量**
（builtin [`ts`](../../builtins/ts.md)）：`tok` 负责「何时、按什么序」算，`ts` 负责「算什么」
（matmul / 逐元素 / 规约算子核），两层正交。

> **粒度说明**：本模板 `tok` = 一**层**（面向工业级规模，层内数值交给 `ts` 张量基建）。
> 若想把 `tok` = 一个**神经元**、`dep` = 一束全连接突触、`back` = 直接反向传播，看清自动微分引擎
> 本身，见姊妹模板 [dnn-design](../dnn-design/)（神经元粒度的自动微分机：utils 通用模块 + hello-dnn 示例）。

| 机制 | 语法 | 职责 |
|------|------|------|
| 前向 forward | `dep…map` DAG 边 | 输入→隐层→输出→loss，每条边 follow 体 = 该层前向算子（`ts` 张量运算） |
| 反向 backward | `back loss` | 自 loss 反向遍历（反拓扑序），逐层算梯度（链式法则） |
| 训练 train | 外层 `for` 循环 | `pulse` 驱前向 → `backward` → 优化器更新权重，重复 |

**数据面**：token 值只携带 **epoch 触发标量**（驱动 map 级联 / back 反向遍历的「时序」），
真正的张量（输入 / 权重 `W,b` / 梯度 `gGW,gGB`）存为模块级 `tensor&` 全局，follow 体直接读写。

## 目录结构

```
dnn-framework/
├── hello-dnn/      # 入门示例：单层线性回归完整闭环
│   ├── hello.sc            # tok 依赖图 + ts 张量数据面
│   └── component-demo.sc   # 复用 nn 自动微分引擎 + utils/train_utils
├── cnn/            # CNN 前向骨架（conv2d → relu → max_pool2d）
├── rnn/            # RNN 单步 recurrence 骨架（h_t = tanh(Wx·x + Wh·h + b)）
├── llm/            # 单头 attention 骨架（Q/K/V → sdpa）
└── utils/          # 纯 sc 共享模块（tensor_utils / train_utils，无 _impl.c）
```

各子目录均为**纯 sc 程序**（`inc` builtin `ts` / `nn` 与 `utils/`），可独立编译运行。

## 运行

```sh
./compiler/build/scc templates/dnn-framework/hello-dnn/hello.sc
```

## 自动微分引擎（define-by-run）

无需每次手写 `tok/dep/form` wiring：PyTorch 风格的自动微分引擎已沉淀到内置
**`builtins/nn`** 模块（nn = ts 数值反向核 + 进程内录带 tape + backward 引擎 + 模块 +
optim 的组合层；tok 负责 step/分布式宏调度）。应用 `inc nn.sc` 后：

- 用 `nn_linear(in, out)` 建层、`nn_sgd(lr)` / `nn_adam(lr)` 建优化器并 `track_linear`；
- 每轮：`nn_input(t)` 包样本 → `fc->forward(x)` / `->relu()` 等前向 → `loss->backward()`
  → `opt->step()` → `opt->zero_grad()` → `nn_tape_clear()` 回收录带。

见 `hello-dnn/component-demo.sc`（封装在 `utils/train_utils.sc:train_linear`）。


输出：训练日志（loss 单调收敛到 0.000000，`y` 逼近 `target=[5,8]`），再做一次推理打印最终 `y`。

注意：训练循环每轮喂的输入 `x` 不变。若用 `x->set(...)`，tok 的同值抑制会把后续 epoch 视为
“无新事件”，前向不再重跑。这里改用 `x->pulse((ep:@), 0)`：触发标量取 epoch 序号逐轮递增，
天然绕过相等抑制，强制每轮重跑前向（迭代/拉取语义）。

## 内置示例（单层线性回归，演示完整闭环）

```
模型：  y = W·x + b            x:[2,1]  W:[2,2]  b:[2,1]  y:[2,1]
损失：  loss = mean((y-t)²)    t:[2,1]（MSE）
前向：  x ──► y ──► loss       张量在全局间流动，token 仅级联触发
反向：  back loss              gGY = (2/N)(y-t),  gGW = gGY·xᵀ,  gGB = gGY
更新：  W -= lr·gGW ; b -= lr·gGB
```

每层算子（map 边的 follow 体）含**前向 + 反向两个分支**，以 `this->active == TOK_BACK(-4)`
区分：反向分支读下游梯度张量、写上游/权重梯度张量；前向分支读上游张量、算、写下游张量。
张量须在 `form` 前 `setup()` 分配好（`form` 激活前向链时可能级联触发 follow 体，此时全局须非 nil）。

## 扩展为通用 MLP

1. **加一层**（如隐层 `h`，带激活）：加 `tok h` + 两条 map 边（`x→h`、`h→y`）+ 该层张量全局，
   各自 follow 体写前向（`h = relu(W1·x + b1)`，用 `gH->relu_()`）与反向分支。
   `depth()`/`critical()` 自动重烘焙 = 层执行序与瓶颈层。
2. **批量 mini-batch**：列向量 `[in,1]` 换成 `[in,B]`，梯度 `gGW = gGY·Xᵀ` 经 matmul 内积自动按 batch 累加。
3. **真实优化器**：`backward_step` 里把 `W - lr·gGW` 换成 Adam/动量（再开 `tensor&` 全局存一/二阶矩）。
4. **RNN / 反馈网络**：层间有反馈环时用 `dep loop` 替 `dep map`，按时间步 `loop_run(steps)` 展开。
5. **并行**：同 `batch()` 的层两两无依赖，可并行前向。
6. **BLAS 加速**：matmul 在 `-DSCC_WITH_BLAS` 且 `DT_F4` 下走 `cblas_sgemm`（见 [`ts.md`](../../builtins/ts.md) §8）。

详见 `hello-dnn/hello.sc` 文末「扩展指南」与 `syntax.md` §21（tok 体系，含 `back` / `loop`）、`builtins/ts.md`（张量 API）。
