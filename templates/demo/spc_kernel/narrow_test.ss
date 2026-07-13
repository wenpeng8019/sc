tar vulkan@450, metal@2.3

# 板验用窄整数内核（i1=int8 / i2=int16，名字即字节数）。
# int8 算术 100+20=120（∈[-128,127]），存 int8 SSBO（8bit storage）再取回；
# int16 算术 300+100=400，存 int16 SSBO（16bit storage）再取回；
# o[i] = float(int8) + float(int16) = 120 + 400 = 520（每元素，确定可校验）。

@def Params: {
    n: u4
} uniform set 0 binding 0

@def BBuf: {
    b[]: i1                       # int8 暂存（SPV_KHR_8bit_storage）
} storage set 0 binding 1

@def SBuf: {
    s[]: i2                       # int16 暂存（SPV_KHR_16bit_storage）
} storage set 0 binding 2

@def OBuf: {
    o[]: f4
} storage set 0 binding 3

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_narrow: in: CompIn, local 64
    if in.gid < Params.n
        var c: i1 = i1(100) + i1(20)      # int8 算术 = 120
        BBuf.b[in.gid] = c                # 存 int8（8bit storage 写）
        var h: i2 = i2(300) + i2(100)     # int16 算术 = 400
        SBuf.s[in.gid] = h                # 存 int16（16bit storage 写）
        OBuf.o[in.gid] = float(BBuf.b[in.gid]) + float(SBuf.s[in.gid])  # 120+400=520
