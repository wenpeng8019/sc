# saxpy —— spc kernel 面验证:y[i] = a * x[i] + y[i]
#
# 编译(四目标:资源条目 0/1=vulkan reflect/.spv、2/3=metal、4/5=gles、6=cpu reflect;
#      cpu 内核源码另落散文件 out/saxpy.cpu.c,宿主 add 编入后构造器自注册):
#   ./compiler/build/scc templates/demo/spc_kernel/saxpy.ss -o templates/demo/spc_kernel/out/saxpy
#
# 说明:dispatch 用精确全局线程数(Metal non-uniform threadgroups),
# n 越界守卫仍保留(通用做法,兼容需要对齐线程组的后端)。
# 板端最小化验证首选本内核(无 shared/atomic,见 spc/PORTING.md §7)。

tar vulkan@450, metal@2.0, gles@310, cpu@99

@def Params: {
    a: f4
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def YBuf: {
    y[]: f4
} storage set 0 binding 2

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_saxpy: in: CompIn
    if in.gid < Params.n
        YBuf.y[in.gid] = Params.a * XBuf.x[in.gid] + YBuf.y[in.gid]
