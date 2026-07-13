tar cpu@99

# matmul CPU 变体（design §18 M1）：C[M,N] = A[M,K] @ B[K,N]，f4 行主序。
# 每 invocation 算 C 一个元素（1D 调度 gx = M*N；无 shared/barrier——
# 内层 K 循环交编译器向量化，workgroup 间多线程分片）。
# GPU tiled 版在 matmul.ss。预编产物入库：kernels/build.sh。

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

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp mm_1d: in: CompIn, local 64
    if in.gid < Dims.m * Dims.n
        var row: u4 = in.gid / Dims.n
        var col: u4 = in.gid % Dims.n
        var acc: f4 = 0.0
        var i: u4 = 0
        while i < Dims.k
            acc = acc + ABuf.a[row * Dims.k + i] * BBuf.b[i * Dims.n + col]
            i = i + 1
        CBuf.c[in.gid] = acc
