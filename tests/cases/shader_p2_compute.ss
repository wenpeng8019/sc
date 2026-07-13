tar vulkan@450, metal@2.0, gles@310

# P2：shared 共享内存 + barrier + 原子操作族
# 经典 tree reduction：每工作组 256 线程规约求和，组结果原子累加到 SSBO

@def Params: {
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def OutBuf: {
    total: u4
    hist[16]: u4
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

    # 组代表原子累加 + 原子族演示
    if in.lid == 0
        atomic_add(OutBuf.total, uint(Tile.data[0]))
        atomic_max(OutBuf.hist[0], uint(Tile.data[0]))
        atomic_min(OutBuf.hist[1], uint(Tile.data[0]))
        atomic_or(OutBuf.hist[2], 1)
        atomic_and(OutBuf.hist[3], 255)
        atomic_xor(OutBuf.hist[4], in.gid)
        atomic_sub(OutBuf.hist[5], 1)
        atomic_exchange(OutBuf.hist[6], in.gid)
        atomic_cas(OutBuf.hist[7], 0, 7)
    memory_barrier()
