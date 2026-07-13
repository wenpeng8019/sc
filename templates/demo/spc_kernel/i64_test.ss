tar vulkan@450, metal@2.3

# 板验用 int64 内核（Int64 能力 / shaderInt64 特性）。
# big=2^40（超 32 位）：v = big + gid，再 v - big = gid。若按 32 位运算 2^40 会截断，
# 故结果 = gid 即证明真 64 位算术。o[i] = float(gid)，与 arange(0..n) 对照。

@def Params: {
    n: u4
} uniform set 0 binding 0

@def OBuf: {
    o[]: f4
} storage set 0 binding 1

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_i64: in: CompIn, local 64
    if in.gid < Params.n
        var big: i8 = 1099511627776       # 2^40（64 位字面量）
        var v: i8 = big + i8(in.gid)
        OBuf.o[in.gid] = float(v - big)   # = gid（经 64 位算术回收）
