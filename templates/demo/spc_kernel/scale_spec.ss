tar vulkan@450, metal@2.0

# 特化常量内核：x[i] *= SCALE（SCALE 由管线创建期传值覆写，constant_id=0）。
# GL 后端无特化常量机制（能力表门控 gles=-1），故单独成文件（reduce.ss 才带 gles）。
# 条目布局每目标两连 [reflect, cs_scale]：base 0 = vulkan、base 2 = metal。
# 消费方：templates/demo/spc_p2_demo.sc

let SCALE: f4 = 1.0 spec 0      # 运行时可覆写（默认 1.0）

@def Params: {
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_scale: in: CompIn, local 64
    if in.gid < Params.n
        XBuf.x[in.gid] = XBuf.x[in.gid] * SCALE
