# hello-dnn —— dnn-design 的最小可运行示例工程

把**单个神经元**建模为一个 `tok`、**整束全连接突触**建模为一条 `dep`、**反向传播**建模为
一次 `back`——用最少的代码把「自动微分引擎」完整跑通：前向、反向、多激活、损失、带动量
优化器、mini-batch 训练一应俱全。本工程只写**网络拓扑 + 训练流程**，通用件全部 `inc` 自
[../utils](../utils/)。

> 定位与「与工业 `ts` 基建的差异 / 优劣」见上层 [../README.md](../README.md)。

## 工程组成

```
hello-dnn/hello.sc        本工程：tok/dep 拓扑 + 数据集 + 训练循环 + main
    inc ../utils/neuron.sc    neuron 侧车 + 激活库 act_fwd/act_bwd
    inc ../utils/autograd.sc  edge_fwd / edge_bwd（自动微分核心）
    inc ../utils/optim.sc     sgd_one（带动量 SGD）
```

`hello.sc` 标 `@@`（可执行根、同目录唯一的 `main`）；三个 `inc` 把 utils 的导出件并入。

## tok 机制 → 神经网络概念

| 神经网络 | tok 原语 | 说明 |
|---|---|---|
| 神经元 | `tok t:"n.h0"` | 真实数据（act/pre/grad/权重）放**侧车** `neuron`，token 纯触发 |
| 全连接突触束 | `dep all: i0,i1 map h0` | 与门 `all` = 所有上游就绪才前向；权重存**下游神经元**侧车 |
| 前向算子 | follow `else` 分支 | 调 `edge_fwd`（`z = b + Σ wᵢ·xᵢ` → 激活）→ `pulse` 触发级联 |
| 反向算子（自动微分） | follow `TOK_BACK` 分支 | 调 `edge_bwd`：`gz = grad·act'(pre)`；`∂L/∂wᵢ += gz·xᵢ`，并把 `wᵢ·gz` 累加回上游 grad |
| `loss.backward()` | `back loss` | 按**反拓扑序**（编译期烘焙的 depth 降序）唤起各反向分支 |

**自动微分为何复用 `back` 就够**：`back` 严格反拓扑序保证「所有下游必先于上游被唤起」，于是
fan-out>1 的梯度汇聚（`up->grad +=`）时序天然正确——链式法则的执行顺序由 tok 内核**免费**保证，
无需任何手写调度或 tape。token 持久值天然就是 PyTorch 的 `save_for_backward`。

## 运行

```sh
./compiler/build/scc templates/dnn-design/hello-dnn/hello.sc
```

内置 2-4-1 MLP（输入2 → 隐层4 leaky-relu → 输出1 线性），MSE + 带动量 SGD，学习仿射目标
`y = x0 + 2·x1`。输出：

```
=== 编译期烘焙的网络结构（lightmap，O(1) 读取）===
i0    depth=0  fanout=4  reach=6          # 输入神经元：驱动 4 个隐层、波及 6 个下游
h0    depth=1  fanin=2  fanout=1  critical=1
o0    depth=2  fanin=4  batch_width=1
loss  depth=3  critical=1
=== 训练（2-4-1 MLP，目标 y = x0 + 2·x1）===
epoch    0  loss=40.63800
epoch  500  loss=0.02188
epoch  900  loss=0.00000                  # 单调收敛到 0
=== 推理 ===
x=(1.0,2.0)  pred=5.0003  target=5.0      # 精确逼近
x=(2.0,3.0)  pred=7.9995  target=8.0
```

## 白赚的编译期烘焙（lightmap）

tok 在注册期把图度量烘焙进每个句柄，运行时 O(1) 读取——直接映射为网络结构：

| 指标 | 神经网络含义 |
|---|---|
| `depth()` | 神经元所在**层号**（= 前向/反向执行序） |
| `batch()` / `batch_width()` | 同层可**并行**的神经元（同 depth 无依赖） |
| `critical()` / `slack()` | **瓶颈**神经元 / 最长依赖链 |
| `fanin()` / `fanout()` | 入 / 出突触数（`fanout` = 梯度**汇聚路数**） |
| `reach()` | 改一处波及的下游神经元数（失效爆炸半径） |

## 扩展

1. **加神经元/层**：加 `tok` + 一条 `dep` + 侧车槽；dep 体通用（调 `edge_fwd`/`edge_bwd`），
   复制即可。`depth()/batch()/critical()` 会自动重新烘焙——网络变化无需手改调度。
2. **新激活 / 新优化器 / 新算子**：改 [../utils](../utils/) 即可，本工程不动（见 utils/README）。
3. **多输出 / 分支（展示梯度汇聚）**：把隐层连到多个输出（fanout>1），反向各下游贡献经
   `up->grad +=` 自动累加——`back` 反拓扑序保证「全部下游先于上游」，汇聚时序天然正确。
4. **并行前向**：同 `batch()`（=同 depth）的神经元两两无依赖，可投递线程池
   （见 [../../workflow-graph](../../workflow-graph/)）。
5. **工业级规模**：神经元粒度只适合小网；大网请用 [../../dnn-framework](../../dnn-framework/)
   （tok=层）+ `ts` 张量基建，把「调度」与「数值」分置于各自最优的粒度。
