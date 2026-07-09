# spc_demo —— 多维空间并行计算演示:kernel / graph / model 三入口验证
#
#   kernel 面:scc .ss comp(saxpy)→ Metal compute,GPU 结果 vs ts CPU
#   graph 面 :MPSGraph matmul,GPU 结果 vs ts CPU matmul
#   model 面 :CoreML tiny 模型推理(倾向 ANE),MLComputePlan 查证调度
#
# 前置(从仓库根目录运行；gpu/spc 源码动态编译，平台框架链接由编译器自动注入，零 SCC_LDFLAGS):
#   ./compiler/build/scc templates/demo/spc_kernel/saxpy.ss -o templates/demo/spc_kernel/out/saxpy
#   python3 templates/demo/spc_model/gen.py        # 生成 tiny.mlmodelc(需 coremltools)
#   ./compiler/build/scc templates/demo/spc_demo.sc

inc io.sc
inc ts.sc
inc gpu.sc
inc spc.sc

# 整文件读入 malloc 缓冲(尾部补 NUL)。失败返回 nil。
# 内核资源：scc 默认产物 saxpy.shader.c（单目标 metal：条目 0=reflect、1=cs_saxpy）。
add spc_kernel/out/saxpy.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_saxpy_get:: shader_blob&, i: u8    # 绑 sc_shader_saxpy_get

fnc main: i4
    # ---- 环境:gpu(headless)+ spc ----
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    if gpu_init(&gd) == 0
        print "gpu_init 失败\n"
        return 1
    var sdc: ::sc_spc_desc
    ::memset(&sdc, 0, sizeof(::sc_spc_desc))
    if spc_init(&sdc) == 0
        print "spc_init 失败\n"
        gpu_shutdown()
        return 1
    print "spc 就绪(kernel=Metal, graph=MPSGraph, model=CoreML)\n"

    var fails: i4 = 0

    # ============ 1. kernel 面:saxpy(y = 2x + 1) ============
    var brj: shader_blob& = shader_saxpy_get(0)      # reflect.json
    var bms: shader_blob& = shader_saxpy_get(1)      # cs_saxpy.metal

    var x: tensor& = arange(0.0, 1024.0, 1.0, DT_F4)
    var y: tensor& = ones_like(x)
    var xb: u4 = spc_buffer_from_tensor(x)
    var yb: u4 = spc_buffer_from_tensor(y)

    var kd: ::sc_spc_kernel_desc
    ::memset(&kd, 0, sizeof(::sc_spc_kernel_desc))
    kd.code.ptr  = (bms->data: &)
    kd.code.size = bms->size
    kd.entry     = "cs_saxpy"
    kd.reflect_json = (brj->data: const char&)
    var krn: u4 = spc_make_kernel(&kd)
    if krn == 0
        print "make_kernel 失败\n"
        return 1

    # uniform 块 {a: f4, n: u4}(与 .ss 的 Params 布局一致)
    var pb: & = ::malloc(8)
    var pf: f4& = (pb: f4&)
    pf[0] = 2.0
    var pu: u4& = (pb: u4&)
    pu[1] = 1024

    var bnd: ::sc_spc_bindings
    ::memset(&bnd, 0, sizeof(::sc_spc_bindings))
    bnd.uniforms[0].ptr  = pb
    bnd.uniforms[0].size = 8
    bnd.buffers[1] = xb
    bnd.buffers[2] = yb

    if spc_dispatch(krn, 1024, 1, 1, &bnd) == 0
        print "dispatch 失败\n"
        return 1
    spc_finish()
    spc_buffer_to_tensor(yb, y)

    # CPU 对照:2x + 1
    var e1: tensor& = x->mul_scalar(2.0)
    var expect: tensor& = e1->add_scalar(1.0)
    if y->allclose(expect, 0.00001, 0.000001)
        print "[kernel] saxpy GPU vs CPU:通过(n=1024)\n"
    else
        print "[kernel] saxpy 结果不符!\n"
        fails = fails + 1
    e1->drop()
    expect->drop()
    ::free(pb)

    # ============ 2. graph 面:matmul(MPSGraph) ============
    var msh[2]: i4
    msh[0] = 128
    msh[1] = 128
    rand_seed(42)
    var a: tensor& = rand_uniform(2, msh, -1.0, 1.0, DT_F4)
    var b: tensor& = rand_uniform(2, msh, -1.0, 1.0, DT_F4)
    var c_gpu: tensor& = zeros(2, msh, DT_F4)
    if spc_mm(a, b, c_gpu) == 0
        print "[graph] spc_mm 失败\n"
        fails = fails + 1
    else
        var c_cpu: tensor& = a->matmul(b)
        if c_gpu->allclose(c_cpu, 0.001, 0.0001)
            print "[graph] matmul 128x128 GPU vs CPU:通过\n"
        else
            print "[graph] matmul 结果不符!\n"
            fails = fails + 1
        c_cpu->drop()

    # ============ 3. model 面:CoreML 推理(倾向 ANE) ============
    var mdl: u4 = spc_model_load("templates/demo/spc_model/tiny.mlmodelc", 3)   # CPU_ANE
    if mdl == 0
        print "[model] 模型加载失败(先跑 spc_model/gen.py)\n"
        fails = fails + 1
    else
        # 输入 x[1,3,128,128] = arange(49152)/49152（与 gen.py 参考一致）
        var i0: tensor& = arange(0.0, 49152.0, 1.0, DT_F4)
        var i1: tensor& = i0->div_scalar(49152.0)
        var ish[4]: i4
        ish[0] = 1
        ish[1] = 3
        ish[2] = 128
        ish[3] = 128
        var min_: tensor& = i1->reshape(4, ish)
        var osh[1]: i4
        osh[0] = 8
        var mout: tensor& = zeros(1, osh, DT_F4)
        if spc_model_run1(mdl, min_, mout) == 0
            print "[model] 推理失败\n"
            fails = fails + 1
        else
            var ref: tensor& = ts_load("templates/demo/spc_model/tiny_ref.npy")
            if ref != nil && mout->allclose(ref, 0.02, 0.02)   # fp16 容差
                print "[model] tiny 模型推理数值:通过\n"
            else
                print "[model] 推理数值不符!\n"
                fails = fails + 1
            if ref != nil
                ref->drop()
        var ane: i4 = spc_model_ane_ratio(mdl)
        print "[model] ANE 调度占比: ", ane, "%(MLComputePlan;-1=不可查)\n"
        min_->drop()
        i1->drop()
        i0->drop()
        mout->drop()
        spc_destroy_model(mdl)

    # ---- 清理 ----
    spc_destroy_kernel(krn)
    spc_destroy_buffer(xb)
    spc_destroy_buffer(yb)
    x->drop()
    y->drop()
    a->drop()
    b->drop()
    c_gpu->drop()
    spc_shutdown()
    gpu_shutdown()

    if fails == 0
        print "spc 三入口全部验证通过\n"
    else
        print "验证失败项: ", fails, "\n"
    return fails
