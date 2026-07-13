tar vulkan@450, metal@2.0, gles@310, cpu@99

# elementwise —— spc graph 面逐元素算子（design §18 M2）
#   ew_unary ：y = f(x)   op: 0=relu 1=gelu(tanh 近似) 2=sigmoid 3=tanh 4=silu
#   ew_binary：y = a ⊕ b  op: 0=add 1=mul 2=sub
# op 走 uniform（四后端统一；elementwise 带宽受限，分支开销可忽略）。
# gelu 常数与 ts sc_tensor_gelu 逐字一致（0.7978845608028654 / 0.044715），
# 保证 f4 对拍不飘。

@def Ew: {
    n: u4
    op: u4
} uniform set 0 binding 0

@def ABuf: {
    a[]: f4
} storage set 0 binding 1

@def BBuf: {
    b[]: f4
} storage set 0 binding 2

@def YBuf: {
    y[]: f4
} storage set 0 binding 3

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp ew_unary: in: CompIn, local 64
    if in.gid < Ew.n
        var x: f4 = ABuf.a[in.gid]
        var y: f4 = 0.0
        if Ew.op == 0
            y = max(x, 0.0)                       # relu
        else
            if Ew.op == 1                          # gelu（tanh 近似，常数同 ts）
                var c3: f4 = x * x * x
                y = 0.5 * x * (1.0 + tanh(0.7978845608028654 * (x + 0.044715 * c3)))
            else
                if Ew.op == 2                      # sigmoid
                    y = 1.0 / (1.0 + exp(0.0 - x))
                else
                    if Ew.op == 3                  # tanh
                        y = tanh(x)
                    else                           # 4 silu = x*sigmoid(x)
                        y = x / (1.0 + exp(0.0 - x))
        YBuf.y[in.gid] = y

comp ew_binary: in: CompIn, local 64
    if in.gid < Ew.n
        var av: f4 = ABuf.a[in.gid]
        var bv: f4 = BBuf.b[in.gid]
        var y: f4 = 0.0
        if Ew.op == 0
            y = av + bv
        else
            if Ew.op == 1
                y = av * bv
            else
                y = av - bv
        YBuf.y[in.gid] = y
