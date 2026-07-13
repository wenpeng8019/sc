tar vulkan@450, metal@2.0, gles@310

# P2 计算深化端到端内核：shared 共享内存树形规约 + barrier + atomic_add。
# 三目标产物（条目布局每目标两连 [reflect, cs_reduce]，声明序 = base）：
#   base 0 = vulkan（.spv 二进制，spc Vulkan 后端）
#   base 2 = metal （MSL 文本，spc Metal 后端）
#   base 4 = gles  （GLSL ES 3.1 文本，spc GL 后端）
# 消费方：templates/demo/spc_p2_demo.sc（按 gpu_query_backend 选 base）

@def Params: {
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def OutBuf: {
    total: u4
} storage set 0 binding 2

@def Tile: {
    data[256]: f4
} shared

@def CompIn: {
    gid: u4 builtin global_invocation_id
    lid: u4 builtin local_invocation_index
}

comp cs_reduce: in: CompIn, local 256
    # 装载到共享内存（越界补 0）
    if in.gid < Params.n
        Tile.data[in.lid] = XBuf.x[in.gid]
    else
        Tile.data[in.lid] = 0.0
    barrier()

    # 树形规约
    var stride: u4 = 128
    while stride > 0
        if in.lid < stride
            Tile.data[in.lid] = Tile.data[in.lid] + Tile.data[in.lid + stride]
        barrier()
        stride = stride / 2

    # 组代表把部分和（整数值域内精确）原子累加
    if in.lid == 0
        atomic_add(OutBuf.total, uint(Tile.data[0]))
