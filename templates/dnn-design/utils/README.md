# dnn-design/utils —— 可复用的 dnn 通用模块对象

与网络拓扑无关的通用件，以 `@def` / `@fnc` 跨模块导出，供 [hello-dnn](../hello-dnn/) 等
工程 `inc` 复用。拓扑（哪个神经元连哪个）与训练流程留在各工程自身，数值与梯度逻辑集中在此。

| 模块 | 导出 | 职责 |
|---|---|---|
| [neuron.sc](neuron.sc) | `@def neuron`、`@fnc act_fwd`、`@fnc act_bwd` | 神经元侧车结构（激活/梯度/权重/动量）+ 激活函数库（前向 + 导数） |
| [autograd.sc](autograd.sc) | `@fnc edge_fwd`、`@fnc edge_bwd` | 一束全连接突触的前向 / 反向算子——**自动微分核心**（链式法则 + 梯度汇聚） |
| [optim.sc](optim.sc) | `@fnc sgd_one` | 带动量 SGD：按累加的 `gw/gbias` 更新一个神经元 |

## 约定

- **激活种类**用整型编码：`0`=恒等、`1`=ReLU、`2`=Leaky-ReLU。命名常量（`A_IDENT` 等）因
  `let` 不跨模块，由消费端按此约定自行定义。
- **侧车与 token 一一对应**：`neuron` 经 `form t,v,&n` 绑到 token 的 ctx；dep 体内
  `t->ctx()` 取下游神经元、`this->toks[i]->ctx()` 取上游神经元。
- **权重存下游**：一条 dep ←→ 一个下游神经元，其入突触权重 `w[]`/偏置 `bias` 存于该下游侧车。

## 怎么用

工程根（带 `main`、标 `@@`）逐个 `inc` 所需模块即可：

```sc
inc ../utils/neuron.sc        # neuron 侧车 + 激活库
inc ../utils/autograd.sc      # edge_fwd / edge_bwd（自身已 inc neuron.sc）
inc ../utils/optim.sc         # sgd_one
```

`autograd.sc` 与 `optim.sc` 各自 `inc neuron.sc`（菱形依赖自动去重，`neuron` 只链接一次）。

## 扩展

- **新激活**（sigmoid/tanh）：声明 libm `fnc expf:: f4, x: f4`（macOS 自动链接 / Linux 加 `-lm`），
  在 `neuron.sc` 的 `act_fwd`/`act_bwd` 追加分支。
- **新优化器**（Adam）：在 `neuron` 侧车加一/二阶矩字段 `m[]`/`v[]`，照 `sgd_one` 写一个 `adam_one`。
- **新算子**：在 `autograd.sc` 加同形的前向/反向对（如卷积、注意力），dep 体改调即可。
