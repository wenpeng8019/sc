tar vulkan@450, metal@2.3

# P3：窄/宽标量（名字即字节数）——f2=f16、i8/u8=int64、i1/u1=int8、i2/u2=int16
# f16 权重缓冲 + 半精度算术 + 64 位累加器（嵌入式带宽/ML 量化场景）

let HALF_SCALE: f2 = 0.5
let BIG: i8 = 1099511627776      # 2^40（64 位字面量）

@def Params: {
    n: u4
} uniform set 0 binding 0

@def WBuf: {
    w[]: f2                       # f16 数组（SPV_KHR_16bit_storage）
} storage set 0 binding 1

@def OBuf: {
    acc: i8                       # 64 位累加器
    o[]: f4
} storage set 0 binding 2

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_f16: in: CompIn, local 64
    if in.gid < Params.n
        # f16 载入 → 半精度算术 → 提升 f32 写出
        var h: f2 = WBuf.w[in.gid]
        h = h * HALF_SCALE + f2(0.25)
        OBuf.o[in.gid] = float(h)
        # 64 位整数算术
        if in.gid == 0
            OBuf.acc = BIG + i8(in.gid)
