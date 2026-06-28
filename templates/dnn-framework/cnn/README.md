# cnn

CNN 模板程序（前向骨架）：

- `x[N,C,H,W] -> conv2d -> relu -> max_pool2d`
- 用于快速起一个卷积网络项目骨架
- 当前演示关注层结构与张量流，不含完整训练图

运行：

```sh
./compiler/build/scc templates/dnn-framework/cnn/cnn.sc
```
