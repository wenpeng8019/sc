# dnn-design —— 神经元级深度神经网络 / 自动微分机

用 `tok` 原语把「**自动微分引擎**」本身显式展开：**一个 `tok` = 一个神经元**、
**一条 `dep` = 一束全连接突触**、**一句 `back` = 反向传播**。每个神经元、每条突触、
每步梯度都看得见、可单步调试——面向**模型的学习、研究与设计**。

```
dnn-design/
├── README.md          # 本文：定位 + 与工业 ts 基建的差异 / 优劣
├── utils/             # 可复用的 dnn 通用模块对象（@def / @fnc 导出）
│   ├── neuron.sc      #   神经元侧车结构 neuron + 激活库 act_fwd/act_bwd
│   ├── autograd.sc    #   突触束算子 edge_fwd/edge_bwd（自动微分核心）
│   ├── optim.sc       #   带动量 SGD：sgd_one
│   └── README.md
└── hello-dnn/         # 最小可运行示例工程（inc 上面的 utils）
    ├── hello.sc       #   2-4-1 MLP：拓扑 + 训练 + 推理
    └── README.md
```

## 定位：`tok` 是「调度 / 自动微分」层，不是张量基建

这是贯穿全套设计的核心认知，也是本目录与工业级 `ts` 张量基建的分界：

> `tok` 是张量**之上**的「计算图 / 反向调度」层（≈ PyTorch 的 **autograd engine**）。
> 每个 `tok` 是 ~200B 的可调度对象、每条 `dep` 带锁与间接调用——它为**协调**而非**算数**设计。
>
> `ts`（待实现的工业张量基建）才是数值层（≈ PyTorch 的 **ATen**）：连续缓冲 + SIMD +
> dtype/rank，负责把成千上万个标量乘加压进向量指令。

**粒度决定用途**——同一套 `tok` 机制，一个 `tok` 包多大一块计算，决定它适合什么：

| | **dnn-design**（本目录） | 工业级（`ts` + [dnn-framework](../dnn-framework/)） |
|---|---|---|
| 一个 `tok` 装 | 一个**神经元**（标量激活） | 一**层**（激活张量） |
| 数值算在哪 | 神经元侧车的标量 `f4` 字段，逐元素 | 层内交给 `ts`：连续缓冲 + SIMD matmul |
| 类比 PyTorch | 纯展开的 **autograd engine** | autograd engine（层间）+ **ATen**（层内） |
| 主要价值 | **看清机制**：学习 / 研究 / 设计 | **吞吐 / 规模**：工业训练与推理 |

## 与工业 ts 基建的差异、优势、劣势

**差异（本质）**：dnn-design 把神经元塞进 `tok`，让「调度器」直接承担逐元素算数；工业路线
把神经元留在 `ts` 张量内，`tok` 只调度「层与层之间」的前向级联与反向次序。

**优势**
- **透明可调试**：每个神经元一个具名 token、每条突触一条 dep，前向/反向/梯度逐个可断点、可打印。
- **机制即代码**：链式法则的执行顺序由 `back` 的反拓扑序**免费**保证（无需手写 tape / 调度）；
  token 的持久值天然就是 `save_for_backward`。是研究「自动微分如何工作」的活教材。
- **白赚编译期烘焙**：`depth/critical/fanin/fanout/reach` 等图度量在注册期烘焙进句柄，
  直接映射为「层号 / 瓶颈神经元 / 梯度汇聚路数」，运行时 O(1) 读取（见 hello-dnn 输出）。
- **零数值依赖**：纯标量算术即可完整跑通前向、反向、优化器、训练，不依赖任何张量库。

**劣势**
- **不适合大网**：每神经元一个 ~200B `tok`、每突触束一条带锁 `dep`，协调开销在百万级元素上
  完全不划算——这是**资源/性能**意义上的取舍，**不是功能阉割**（功能完备见下）。
- **拿调度器当 ALU**：用为「协调」设计的对象做「逐元素乘加」，是层级错配；工业吞吐必须把
  层内数值交给 `ts` 的连续缓冲 + SIMD。
- **无 dtype / 无向量化**：标量 `f4`，没有 f8/i8 量化、没有 batch 维的向量并行。

**一句话**：dnn-design 与工业 `ts` 不是「玩具 vs 正品」，而是**同一引擎在两种粒度**——
本目录把 `tok` 的调度/autograd 语义放大到肉眼可见；工业路线把数值压回 `ts`，让 `tok` 回到它
真正擅长的「层间调度」。两者拼起来才是完整的 PyTorch 式架构（autograd engine + ATen）。

## 功能完备性（不是能力阉割）

神经元粒度只限制**规模**，不限制**功能**。hello-dnn 已具备真实模型所需的全部要素：

| 能力 | 实现 |
|---|---|
| 前向传播 | `dep` follow 体的 `edge_fwd`（`z = b + Σwᵢxᵢ` → 激活） |
| 反向传播（链式法则） | `back loss` + follow 的 `TOK_BACK` 分支 `edge_bwd` |
| 多种激活 | 恒等 / ReLU / Leaky-ReLU（`act_fwd`/`act_bwd`，可扩 sigmoid/tanh） |
| 损失 | MSE（loss 边前向算损失、反向播种输出梯度） |
| 优化器 | 带动量 SGD（`sgd_one`，可扩 Adam） |
| 训练 | mini-batch 逐样本：清梯度 → 前向 → `back` → 更新 |
| 梯度汇聚 | fan-out>1 时 `up->grad +=` 由反拓扑序保证时序正确 |

## 运行

```sh
./compiler/build/scc templates/dnn-design/hello-dnn/hello.sc
```

详见 [hello-dnn/README.md](hello-dnn/README.md)（含输出解读与扩展指南）、
[utils/README.md](utils/README.md)（通用模块清单）。

## 姊妹模板

- [dnn-framework](../dnn-framework/) —— **层级粒度**（`tok`=层）的工业级骨架：`tok` 管层间调度，
  层内数值交给 `ts` 张量基建。本目录是它的**神经元粒度**对照：把同一套 `tok` 机制放大到单神经元。
