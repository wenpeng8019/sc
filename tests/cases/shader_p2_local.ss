tar vulkan@450, metal@2.0, glcore@430

# P2：local 工作组尺寸签名尾语法（local X [Y [Z]]，缺省补 1；未声明 = 64×1×1）

@def Params: {
    a: f4
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_scale: in: CompIn, local 128
    if in.gid < Params.n
        XBuf.x[in.gid] = Params.a * XBuf.x[in.gid]

comp cs_tile: in: CompIn, local 8 8
    if in.gid < Params.n
        XBuf.x[in.gid] = XBuf.x[in.gid] + 1.0

comp cs_default: in: CompIn
    if in.gid < Params.n
        XBuf.x[in.gid] = XBuf.x[in.gid] * 2.0
