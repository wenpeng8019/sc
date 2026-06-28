# llm

LLM / Transformer 模板程序：

- 单头 `scaled dot-product attention` 骨架
- 展示 `Q/K/V` 组织、causal/non-causal attention 路径
- 可继续扩展多头投影、残差、layer_norm、MLP block

运行：

```sh
./compiler/build/scc templates/dnn-framework/llm/llm.sc
```
