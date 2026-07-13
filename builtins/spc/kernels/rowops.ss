tar vulkan@450, metal@2.0, gles@310, cpu@99

# rowops —— spc graph 面行归约算子（design §18 M2）：softmax / layer_norm
#   输入视作 [rows, cols]（末轴连续；spc 派发层负责把任意 axis 归一成末轴形态，
#   非末轴/非连续时回退 ts CPU）。每 invocation 算一行（行间并行；行内串行，
#   数值语义与 ts CPU 一致：softmax 减 max 稳定化、layer_norm 有偏方差）。

@def Ro: {
    rows: u4
    cols: u4
    eps: f4          # layer_norm 用；softmax 忽略
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def YBuf: {
    y[]: f4
} storage set 0 binding 2

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp softmax_rows: in: CompIn, local 64
    if in.gid < Ro.rows
        var base: u4 = in.gid * Ro.cols
        # 1) 行 max（数值稳定）
        var mx: f4 = XBuf.x[base]
        var i: u4 = 1
        while i < Ro.cols
            mx = max(mx, XBuf.x[base + i])
            i = i + 1
        # 2) exp 求和
        var sum: f4 = 0.0
        i = 0
        while i < Ro.cols
            var e: f4 = exp(XBuf.x[base + i] - mx)
            YBuf.y[base + i] = e
            sum = sum + e
            i = i + 1
        # 3) 归一
        i = 0
        while i < Ro.cols
            YBuf.y[base + i] = YBuf.y[base + i] / sum
            i = i + 1

comp layernorm_rows: in: CompIn, local 64
    if in.gid < Ro.rows
        var base: u4 = in.gid * Ro.cols
        var fn2: f4 = float(Ro.cols)
        # mean
        var mean: f4 = 0.0
        var i: u4 = 0
        while i < Ro.cols
            mean = mean + XBuf.x[base + i]
            i = i + 1
        mean = mean / fn2
        # 有偏方差（÷N，与 ts layer_norm 一致）
        var vr: f4 = 0.0
        i = 0
        while i < Ro.cols
            var d: f4 = XBuf.x[base + i] - mean
            vr = vr + d * d
            i = i + 1
        vr = vr / fn2
        var inv: f4 = inversesqrt(vr + Ro.eps)
        i = 0
        while i < Ro.cols
            YBuf.y[base + i] = (XBuf.x[base + i] - mean) * inv
            i = i + 1
