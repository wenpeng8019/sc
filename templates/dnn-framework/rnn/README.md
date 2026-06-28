# rnn

RNN 模板程序：

- 单步隐状态更新 `h_t = tanh(Wx x_t + Wh h_{t-1} + b)`
- 展示如何组织循环状态张量与递推骨架
- 后续若要做时间展开，可结合 tok 的 `dep loop` / `loop_run`

运行：

```sh
./compiler/build/scc templates/dnn-framework/rnn/rnn.sc
```
