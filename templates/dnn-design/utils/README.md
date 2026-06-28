# dnn-design/utils —— 通用件已升格为内置 `neuron` 模块

本目录原先的三件套（`neuron.sc` / `autograd.sc` / `optim.sc`）已**升格为可复用的独立
神经元模块 `neuron`**（独立神经元基元，与工业张量库 `nn` 成对），
无需再随模板复制。直接：

```sc
inc neuron.sc
```

即可获得：

| 来源（原 utils） | 现内置 `neuron` 导出 |
|---|---|
| `neuron.sc` | **神经元对象 `@def neuron`**：数据侧车（含 SGD 动量 + Adam 一/二阶矩字段）+ 方法 `forward`/`backward`/`sgd`/`adam`/`seed_mse`（均返回 `this` 可链式）；激活枚举 `act_kind`（裸名常量 `AK_IDENT`/`AK_RELU`/`AK_LEAKY`/`AK_SIGMOID`/`AK_TANH`）、纯标量数学 `act_fwd`/`act_bwd`（自由函数） |
| `autograd.sc` | 前向/反向已收为 neuron 方法（`n->forward`/`n->backward`）；**`edge_step`**（dep 体统一调度：收成一行 `edge_step(this, 下游)` + `return false`） |
| `optim.sc` | neuron 方法 `n->sgd(lr, momentum)`（带动量 SGD）、`n->adam(lr, b1, b2, eps, t)`（含偏差校正） |
| —（新增） | 损失：`mse_loss`（标量）+ `n->seed_mse`（输出层播种）、`xent_seed`（softmax 交叉熵，多分类） |

> `neuron` 与工业级张量库 `nn` 成对：`nn` = 张量/层粒度 define-by-run tape（吞吐/规模）；
> `neuron` = 神经元粒度、把自动微分【引擎 = tok 的 `back`】的算子层显式展开（学习/研究/设计）。
> 自动微分【引擎】本身就是 tok 的 `back`/`TOK_BACK`（反拓扑序免费保证链式法则次序）。

## 约定（不变）

- **侧车与 token 一一对应**：`neuron` 经 `form t,v,&n` 绑到 token 的 ctx；dep 体内
  `down->ctx()` 取下游神经元、`this->toks[i]->ctx()` 取上游神经元。
- **权重存下游**：一条 dep ←→ 一个下游神经元，其入突触权重 `w[]`/偏置 `bias` 存于该下游侧车。
- **激活种类**用枚举裸名常量 `AK_*`（跨模块可引用，无需再各自 `let` 定义）。

## 阅读引擎源码

`neuron` 是纯 sc 实现——`builtins/neuron/neuron.sc` 全程可读：每个激活、每条突触前向/反向、每步优化器
都是裸标量算术。设计路线的「透明可调试」价值不因升格为内置而损失：**拓扑（tok/dep/back）
仍在你自己的 .sc 里显式写出**，本库只承载与拓扑无关的标量算子。

## 扩展

- **新激活**：在 `neuron.sc` 的 `act_kind` 枚举加一项、`act_fwd`/`act_bwd` 各加一分支。
- **新优化器**：照 `neuron.adam` 方法再写一个（侧车已留 `vw/sw`、`vbias/sbias` 两组矩字段）。
- **新算子/损失**：在 `neuron.sc` 加 neuron 方法或损失函数。
