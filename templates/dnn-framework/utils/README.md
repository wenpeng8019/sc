# dnn-framework/utils

共享公共模块（全部为纯 sc 实现，无 _impl.c；张量底层能力由 builtins/ts 提供）：

- tensor_utils.sc
  - make_col_f4(n, data): 由 f4 缓冲构造列向量 [n,1]
  - vec_argmax(t): 向量 argmax
  - print_vec(t, label): 打印向量

- train_utils.sc
  - train_linear(fc, opt, x, target, epochs): 基于 nn 自动微分引擎（val/tape/backward + optim）跑单层 Linear 训练循环，返回末轮 loss

这些模块定位为模板工程共用胶水层：帮助各示例减少样板代码，不替代 ts/tok 本身。
