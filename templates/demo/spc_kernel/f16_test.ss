tar vulkan@450, metal@2.3

# 板验用 f16 内核：半精度算术（Float16 能力）+ f16 SSBO 存/取（SPV_KHR_16bit_storage）。
# 自包含、无需主机传 f16：核内算 h=0.5*1.5+0.25=1.0（f16），存进 f16 SSBO 再取回，
# +1.0 提升 f32 写出 → o[i] = 2.0（每元素，确定可校验）。

@def Params: {
    n: u4
} uniform set 0 binding 0

@def WBuf: {
    w[]: f2                       # f16 暂存数组（16bit storage 存/取路径）
} storage set 0 binding 1

@def OBuf: {
    o[]: f4
} storage set 0 binding 2

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_f16: in: CompIn, local 64
    if in.gid < Params.n
        var h: f2 = f2(0.5) * f2(1.5) + f2(0.25)     # 半精度算术 = 1.0
        WBuf.w[in.gid] = h                            # 存 f16（16bit storage 写）
        OBuf.o[in.gid] = float(WBuf.w[in.gid]) + 1.0  # 取 f16 → 1.0 + 1.0 = 2.0
