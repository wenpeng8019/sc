# spc_p2_demo —— P2/P3 计算深化端到端验证（跨后端：Metal / Vulkan / GLES3.1）
#
#   内核 1（reduce.ss → cs_reduce）：shared 树形规约 + barrier + atomic_add，
#          local 256 经反射清单驱动线程组尺寸
#   内核 2（scale_spec.ss → cs_scale）：特化常量 SCALE（constant_id=0）运行时
#          传值覆写（仅 Metal/Vulkan；GL 后端跳过本项）
#   验证：x = arange(0..n)，GPU 原子累加整数和 vs CPU 闭式和 n(n-1)/2；
#         spec 传 3.0 后 x*3 vs CPU
#
# 跨后端条目选择：产物条目布局 = 每目标两连 [reflect, comp]，声明序 =
#   reduce.ss:     base 0 = vulkan、2 = metal、4 = gles
#   scale_spec.ss: base 0 = vulkan、2 = metal
# 运行时按 gpu_query_backend()（1=Metal 2=GL 3=Vulkan）选 base。
#
# 前置(从仓库根目录运行；平台框架链接由编译器自动注入,零 SCC_LDFLAGS):
#   ./compiler/build/scc templates/demo/spc_kernel/reduce.ss -o templates/demo/spc_kernel/out/reduce
#   ./compiler/build/scc templates/demo/spc_kernel/scale_spec.ss -o templates/demo/spc_kernel/out/scale_spec
#   ./compiler/build/scc templates/demo/spc_p2_demo.sc
# 板端（Vulkan/GLES）验证手册：builtins/spc/PORTING.md

inc io.sc
inc ts.sc
inc gpu.sc
inc spc.sc

add spc_kernel/out/reduce.shader.c
add spc_kernel/out/scale_spec.shader.c
add spc_kernel/out/saxpy.shader.c
add spc_kernel/out/saxpy.cpu.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_reduce_get:: shader_blob&, i: u8       # 绑 sc_shader_reduce_get
@fnc shader_scale_spec_get:: shader_blob&, i: u8   # 绑 sc_shader_scale_spec_get
@fnc shader_saxpy_get:: shader_blob&, i: u8        # 绑 sc_shader_saxpy_get（CPU 对拍用）

# 后端 → reduce.ss 产物 base（条目 = base+0 reflect、base+1 comp）
fnc reduce_base: u8, backend: i4
    if backend == 3
        return 0            # vulkan
    if backend == 1
        return 2            # metal
    return 4                # gl(gles)

# 后端 → scale_spec.ss 产物 base（无 gles 目标）
fnc scale_base: u8, backend: i4
    if backend == 3
        return 0            # vulkan
    return 2                # metal

fnc main: i4
    # ---- 环境:gpu(headless)+ spc ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    # GPU_BACKEND=vulkan 切 Vulkan 后端；=gl 切 GL；缺省=平台首选(mac=Metal,win/linux=GL)
    var envb: char& = (::getenv("GPU_BACKEND"): char&)
    if envb != nil && envb[0] == (118: char)        # 'v'
        gd.backend = 3                              # SC_GPU_BACKEND_VULKAN
    if envb != nil && envb[0] == (103: char)        # 'g'
        gd.backend = 2                              # SC_GPU_BACKEND_GL
    if gpu_init(&gd) == 0
        print "gpu_init 失败\n", .
        return 1
    var sdc: ::sc_spc_desc
    ::memset(&sdc, 0, sizeof(::sc_spc_desc))
    if spc_init(&sdc) == 0
        print "spc_init 失败\n", .
        gpu_shutdown()
        return 1
    var bk: i4 = gpu_query_backend()
    print "spc 就绪(P2/P3 计算深化;gpu 后端 ", bk, ": 1=Metal 2=GL 3=Vulkan)\n", .

    # ============ 1. shared+barrier+atomic 规约 ============
    var rbase: u8 = reduce_base(bk)
    var brj: shader_blob& = shader_reduce_get(rbase)          # reflect.json
    var bkr: shader_blob& = shader_reduce_get(rbase + 1)      # cs_reduce 产物

    # x = arange(0..4096):和 = 4096*4095/2 = 8386560(f4 整数值域内精确)
    var n: i4 = 4096
    var x: tensor& = arange(0.0, 4096.0, 1.0, DT_F4)
    var xb: u4 = spc_buffer_from_tensor(x)

    # OutBuf.total(u4)原始缓冲,清零初值
    var zero: u4 = 0
    var ob: ::sc_spc_buffer_desc
    ::memset(&ob, 0, sizeof(::sc_spc_buffer_desc))
    ob.size = 4
    ob.data = (&zero: const &)
    var tb: u4 = spc_make_buffer(&ob)

    var kd: ::sc_spc_kernel_desc
    ::memset(&kd, 0, sizeof(::sc_spc_kernel_desc))
    kd.code.ptr  = (bkr->data: &)
    kd.code.size = bkr->size
    kd.entry     = "cs_reduce"
    kd.reflect_json = (brj->data: const char&)
    var krn: u4 = spc_make_kernel(&kd)
    if krn == 0
        print "make_kernel 失败\n", .
        return 1

    # uniform 块 {n: u4}
    var pn: u4 = 4096
    var bnd: ::sc_spc_bindings
    ::memset(&bnd, 0, sizeof(::sc_spc_bindings))
    bnd.uniforms[0].ptr  = (&pn: &)
    bnd.uniforms[0].size = 4
    bnd.buffers[1] = xb
    bnd.buffers[2] = tb

    if spc_dispatch(krn, n, 1, 1, &bnd) == 0
        print "dispatch 失败\n", .
        return 1
    spc_finish()

    # 读回 total 与 CPU 闭式和对照
    var total: u4 = 0
    if spc_buffer_read(tb, (&total: &), 4, 0) == 0
        print "buffer_read 失败\n", .
        return 1
    var expect: u4 = 8386560            # 4096*4095/2
    var fails: i4 = 0
    if total == expect
        print "[P2] shared+barrier+atomic 规约:通过(total=", total, ", 16 组 x 256 线程)\n", .
    else
        print "[P2] 规约结果不符!total=", total, " 期望=", expect, "\n", .
        fails = fails + 1

    # ============ 2. 特化常量运行时传值（仅 Metal/Vulkan） ============
    if bk == 1 || bk == 3
        var sbase: u8 = scale_base(bk)
        var srj: shader_blob& = shader_scale_spec_get(sbase)
        var bsc: shader_blob& = shader_scale_spec_get(sbase + 1)
        var sv: ::sc_spc_spec_value
        ::memset(&sv, 0, sizeof(::sc_spc_spec_value))
        sv.id = 0
        var fv: f4 = 3.0
        var fp: u4& = (&fv: u4&)
        sv.value = fp[0]                    # f4 位模式传入
        var kd2: ::sc_spc_kernel_desc
        ::memset(&kd2, 0, sizeof(::sc_spc_kernel_desc))
        kd2.code.ptr  = (bsc->data: &)
        kd2.code.size = bsc->size
        kd2.entry     = "cs_scale"
        kd2.reflect_json = (srj->data: const char&)
        kd2.spec_values = &sv
        kd2.spec_count  = 1
        var krn2: u4 = spc_make_kernel(&kd2)
        if krn2 == 0
            print "make_kernel(spec) 失败\n", .
            return 1
        var x2: tensor& = arange(0.0, 4096.0, 1.0, DT_F4)
        var xb2: u4 = spc_buffer_from_tensor(x2)
        var bnd2: ::sc_spc_bindings
        ::memset(&bnd2, 0, sizeof(::sc_spc_bindings))
        bnd2.uniforms[0].ptr  = (&pn: &)
        bnd2.uniforms[0].size = 4
        bnd2.buffers[1] = xb2
        if spc_dispatch(krn2, n, 1, 1, &bnd2) == 0
            print "dispatch(spec) 失败\n", .
            return 1
        spc_finish()
        spc_buffer_to_tensor(xb2, x2)
        var e2: tensor& = arange(0.0, 4096.0, 1.0, DT_F4)
        var e3: tensor& = e2->mul_scalar(3.0)
        if x2->allclose(e3, 0.00001, 0.000001)
            print "[P3] spec 特化常量传值:通过(SCALE 默认 1.0 → 运行时 3.0)\n", .
        else
            print "[P3] spec 传值结果不符!\n", .
            fails = fails + 1
        e2->drop()
        e3->drop()
        spc_destroy_kernel(krn2)
        spc_destroy_buffer(xb2)
        x2->drop()
    else
        print "[P3] spec 特化常量:GL 后端无此机制,跳过(见 PORTING.md)\n", .

    spc_destroy_kernel(krn)
    spc_destroy_buffer(xb)
    spc_destroy_buffer(tb)
    x->drop()
    spc_shutdown()

    # ============ 3. CPU SPMD 后端对拍（§17 M1：saxpy y=2x+1） ============
    # 重开 spc，强制 kernel_backend=CPU（tar cpu@99 产物 saxpy.cpu.c 已 add 编入，
    # 构造器自注册；code 字段 CPU 后端不消费，填反射指针满足非空校验）
    var sdc2: ::sc_spc_desc
    ::memset(&sdc2, 0, sizeof(::sc_spc_desc))
    sdc2.kernel_backend = 1             # SC_SPC_KERNEL_CPU
    if spc_init(&sdc2) == 0
        print "spc_init(CPU) 失败\n", .
        return 1
    var crj: shader_blob& = shader_saxpy_get(6)     # cpu99 reflect 条目
    var kd3: ::sc_spc_kernel_desc
    ::memset(&kd3, 0, sizeof(::sc_spc_kernel_desc))
    kd3.code.ptr  = (crj->data: &)
    kd3.code.size = crj->size
    kd3.entry     = "cs_saxpy"
    kd3.reflect_json = (crj->data: const char&)
    var krn3: u4 = spc_make_kernel(&kd3)
    if krn3 == 0
        print "make_kernel(cpu) 失败\n", .
        return 1
    var x3: tensor& = arange(0.0, 4096.0, 1.0, DT_F4)
    var y3: tensor& = ones_like(x3)
    var xb3: u4 = spc_buffer_from_tensor(x3)
    var yb3: u4 = spc_buffer_from_tensor(y3)
    var prm[2]: u4
    var pf: f4& = (&prm[0]: f4&)
    pf[0] = 2.0                          # a = 2.0
    prm[1] = 4096                        # n
    var bnd3: ::sc_spc_bindings
    ::memset(&bnd3, 0, sizeof(::sc_spc_bindings))
    bnd3.uniforms[0].ptr  = (&prm[0]: &)
    bnd3.uniforms[0].size = 8
    bnd3.buffers[1] = xb3
    bnd3.buffers[2] = yb3
    if spc_dispatch(krn3, n, 1, 1, &bnd3) == 0
        print "dispatch(cpu) 失败\n", .
        return 1
    spc_finish()
    spc_buffer_to_tensor(yb3, y3)
    var e4: tensor& = x3->mul_scalar(2.0)
    var e5: tensor& = e4->add_scalar(1.0)
    if y3->allclose(e5, 0.00001, 0.000001)
        print "[CPU] SPMD 后端 saxpy 对拍:通过(y=2x+1, n=4096, 向量化交目标编译器)\n", .
    else
        print "[CPU] saxpy 对拍不符!\n", .
        fails = fails + 1
    e4->drop()
    e5->drop()
    spc_destroy_kernel(krn3)
    spc_destroy_buffer(xb3)
    spc_destroy_buffer(yb3)
    x3->drop()
    y3->drop()
    spc_shutdown()
    gpu_shutdown()
    if fails == 0
        print "spc_p2_demo 全部通过\n", .
    return fails
