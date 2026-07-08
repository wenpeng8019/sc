#!/usr/bin/env python3
# gen.py —— 生成 spc model 面（ANE 验证）用的 tiny CoreML 模型
#
# 结构（卷积网络——ANE 亲和；算量太小会被 CoreML 成本模型调去 CPU）：
#   x[1,3,64,64] → conv3x3(3→32) → relu → [conv3x3(32→32) → relu]×3
#                → conv3x3(32→8) → relu → 全局平均池化 → y[1,8]
# 用 coremltools MIL Builder 从头构建（不依赖 torch/tf），转 mlprogram
# （默认 fp16 计算精度——ANE 友好），再经 coremlcompiler 编译为 .mlmodelc。
#
# 用法：python3 gen.py   （产出 ./tiny.mlmodelc/ 与 tiny_ref.npy）

import shutil, subprocess, os
import numpy as np
import coremltools as ct
from coremltools.converters.mil import Builder as mb
from numpy.lib.stride_tricks import sliding_window_view

np.random.seed(7)
CH = 64
W1 = (np.random.randn(CH, 3, 3, 3) * 0.2).astype(np.float32)
B1 = (np.random.randn(CH) * 0.1).astype(np.float32)
Wm = [(np.random.randn(CH, CH, 3, 3) * 0.05).astype(np.float32) for _ in range(4)]
Bm = [(np.random.randn(CH) * 0.1).astype(np.float32) for _ in range(4)]
W3 = (np.random.randn(8, CH, 3, 3) * 0.1).astype(np.float32)
B3 = (np.random.randn(8) * 0.1).astype(np.float32)

@mb.program(input_specs=[mb.TensorSpec(shape=(1, 3, 128, 128))])
def prog(x):
    h = mb.conv(x=x, weight=W1, bias=B1, strides=[1, 1], pad_type="same")
    h = mb.relu(x=h)
    for i in range(4):
        h = mb.conv(x=h, weight=Wm[i], bias=Bm[i], strides=[1, 1], pad_type="same")
        h = mb.relu(x=h)
    h = mb.conv(x=h, weight=W3, bias=B3, strides=[1, 1], pad_type="same")
    h = mb.relu(x=h)
    y = mb.reduce_mean(x=h, axes=[2, 3], keep_dims=False)   # [1,8]
    return y

model = ct.convert(
    prog,
    convert_to="mlprogram",
    compute_units=ct.ComputeUnit.ALL,
    minimum_deployment_target=ct.target.macOS14,
)

here = os.path.dirname(os.path.abspath(__file__))
pkg = os.path.join(here, "tiny.mlpackage")
out = os.path.join(here, "tiny.mlmodelc")
if os.path.exists(pkg): shutil.rmtree(pkg)
if os.path.exists(out): shutil.rmtree(out)
model.save(pkg)

# 参考输出（demo 数值校验用）：x = arange(49152)/49152
def conv2d_same(x, W, B):
    KH, KW = W.shape[2], W.shape[3]
    xp = np.pad(x, ((0, 0), (0, 0), (1, 1), (1, 1)))
    win = sliding_window_view(xp[0], (KH, KW), axis=(1, 2))   # [C_in,H,W,KH,KW]
    y = np.einsum('chwij,ocij->ohw', win, W) + B[:, None, None]
    return y[None].astype(np.float32)

x = (np.arange(49152, dtype=np.float32) / 49152.0).reshape(1, 3, 128, 128)
h = np.maximum(conv2d_same(x, W1, B1), 0)
for i in range(4):
    h = np.maximum(conv2d_same(h, Wm[i], Bm[i]), 0)
h = np.maximum(conv2d_same(h, W3, B3), 0)
ref = h.mean(axis=(2, 3))[0]
np.save(os.path.join(here, "tiny_ref.npy"), ref.astype(np.float32))
print("参考输出:", ref)

subprocess.run(["xcrun", "coremlcompiler", "compile", pkg, here], check=True)
shutil.rmtree(pkg)
print("已生成", out)
