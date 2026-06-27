# templates —— 项目脚手架（scaffolds）

与 `examples/`（演示单个语言特性的 demo，读完即弃）不同，`templates/` 收录**可直接复制改名
起一个真实项目**的框架骨架。每个子目录是一类程序的最小可运行起点，附 README 说明架构与扩展方式。

## 现有模板

| 模板 | 类型 | 核心机制 | 起点文件 |
|------|------|----------|----------|
| [workflow-graph/](workflow-graph/) | 工作流计算图框架（类 OpenVX，**拉取式**） | `tok` 节点 + `dep…map` DAG 边 + `back`/`exec` 拉取 + 线程池缓冲 | `workflow.sc` |
| [push-reactive/](push-reactive/) | 推送式反应数据流（类响应式电子表格，**推送式**） | `tok` 格 + `dep…map` 同步级联 + `exec` 节点观察 + 变更检测记忆化 | `reactive.sc` |
| [dnn-framework/](dnn-framework/) | 深度神经网络框架 | `dep…map` 前向 + `back` 反向求导 + 训练循环（`pulse` 驱迭代；`dep loop` 适配 RNN） | `dnn.sc` |

## 使用方式

```sh
# 直接运行某个模板（类解释器）
./compiler/build/scc templates/workflow-graph/workflow.sc
./compiler/build/scc templates/push-reactive/reactive.sc
./compiler/build/scc templates/dnn-framework/dnn.sc

# 复制一份起你自己的项目，改名后扩展
cp -r templates/workflow-graph myproj && cd myproj
# 编辑 workflow.sc：加节点 = 加一个 tok + 一条 dep…map 边 + 写 kernel（拉取式经 form 第4参挂 exec，推送式写 combine）
```

## 设计约定

- 每个模板**自包含、可直接编译运行**，不依赖其它模板。
- 节点/层一律走 tok 依赖图：**声明式连边**（`dep…map`），**编译期烘焙**结构度量
  （depth / critical / batch / checkpoint…），**运行时 O(1) 查表**驱动调度——零图遍历。
- 模板聚焦**框架骨架与扩展点**，业务/数学留作 `TODO` 注释，复制后填充。
