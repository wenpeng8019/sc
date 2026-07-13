tar vulkan@450, metal@2.0, gles@310

# matmul —— spc graph 面算子内核（design §18 M1）：C[M,N] = A[M,K] @ B[K,N]
#
# tiled 实现（经典 16×16 shared 分块）：每工作组算 C 的一个 16×16 tile，
# K 维按 16 步进装载 A/B 子块到共享内存，barrier 同步后累加。
# f4、行主序（ts C-连续布局同构）；边界用守卫（M/N/K 任意，无需对齐）。
#
# 2D 调度：dispatch(gx=ceil(N/16)*16, gy=ceil(M/16)*16, 1)——gid.x 列、gid.y 行。
# CPU 变体在 matmul_cpu.ss（相位分裂限 1D 且无 uvec3，见 §17.5）。
# 条目布局（每目标两连 [reflect, mm_tiled]）：声明序 vulkan(0)/metal(2)/gles(4)。
# 预编产物入库：kernels/build.sh（改本文件后重跑）。

@def Dims: {
    m: u4
    n: u4
    k: u4
} uniform set 0 binding 0

@def ABuf: {
    a[]: f4
} storage set 0 binding 1

@def BBuf: {
    b[]: f4
} storage set 0 binding 2

@def CBuf: {
    c[]: f4
} storage set 0 binding 3

@def TileA: {
    t[256]: f4          # 16×16 A 子块
} shared

@def TileB: {
    t[256]: f4          # 16×16 B 子块
} shared

@def CompIn: {
    gid: uvec3 builtin global_invocation_id
    lid: uvec3 builtin local_invocation_id
}

# 2D tiled 路径（GPU 后端：Metal/Vulkan/GLES）
comp mm_tiled: in: CompIn, local 16 16
    var col: u4 = in.gid.x            # C 列（N 维）
    var row: u4 = in.gid.y            # C 行（M 维）
    var lx: u4 = in.lid.x
    var ly: u4 = in.lid.y
    var acc: f4 = 0.0
    var t0: u4 = 0
    var ntiles: u4 = (Dims.k + 15) / 16
    while t0 < ntiles
        # 装载 A[row, t0*16+lx] 与 B[t0*16+ly, col]（越界补 0）
        var ak: u4 = t0 * 16 + lx
        if row < Dims.m && ak < Dims.k
            TileA.t[ly * 16 + lx] = ABuf.a[row * Dims.k + ak]
        else
            TileA.t[ly * 16 + lx] = 0.0
        var bk: u4 = t0 * 16 + ly
        if bk < Dims.k && col < Dims.n
            TileB.t[ly * 16 + lx] = BBuf.b[bk * Dims.n + col]
        else
            TileB.t[ly * 16 + lx] = 0.0
        barrier()
        var i: u4 = 0
        while i < 16
            acc = acc + TileA.t[ly * 16 + i] * TileB.t[i * 16 + lx]
            i = i + 1
        barrier()
        t0 = t0 + 1
    if row < Dims.m && col < Dims.n
        CBuf.c[row * Dims.n + col] = acc
