# spc_p3_demo —— subgroup 板验（Vulkan / Metal）
#
#   [SG] subgroup 三件（vote/ballot/shuffle，SPIR-V 1.3 GroupNonUniform）
#        输入全 1.0 → 结果全 3.0（subgroup 尺寸无关，见 sg_test.ss）
#
# 前置（从仓库根运行）：
#   scc templates/demo/spc_kernel/sg_test.ss -o templates/demo/spc_kernel/out/sg_test
#   GPU_BACKEND=vulkan scc templates/demo/spc_p3_demo.sc   # 或 --build -o 再跑
# 板验手册：builtins/spc/PORTING.md §6 深水区能力

inc io.sc
inc ts.sc
inc gpu.sc
inc spc.sc

add spc_kernel/out/sg_test.shader.c
add spc_kernel/out/f16_test.shader.c
add spc_kernel/out/i64_test.shader.c

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_sg_test_get::  shader_blob&, i: u8
@fnc shader_f16_test_get:: shader_blob&, i: u8
@fnc shader_i64_test_get:: shader_blob&, i: u8

# 后端 → 产物 base（条目 = base+0 reflect、base+1 comp；tar 声明序 vulkan,metal）
fnc vk_or_mtl_base: u8, backend: i4
    if backend == 1
        return 2            # metal
    return 0                # vulkan（默认）

fnc main: i4
    var gd: ::sc_gpu_desc
    ::memset(&gd, 0, sizeof(::sc_gpu_desc))
    var envb: char& = (::getenv("GPU_BACKEND"): char&)
    if envb != nil && envb[0] == (118: char)        # 'v'
        gd.backend = 3
    if envb != nil && envb[0] == (103: char)        # 'g'
        gd.backend = 2
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
    print "spc P3 板验(gpu 后端 ", bk, ": 1=Metal 2=GL 3=Vulkan)\n", .
    if bk == 2
        print "[跳过] GL 后端无 compute subgroup 板验路径（见 PORTING.md）\n", .
        spc_shutdown()
        gpu_shutdown()
        return 0
    var fails: i4 = 0

    # ============ [SG] subgroup vote + ballot + shuffle ============
    var sgb: u8 = vk_or_mtl_base(bk)
    var srj: shader_blob& = shader_sg_test_get(sgb)
    var scs: shader_blob& = shader_sg_test_get(sgb + 1)
    var kd: ::sc_spc_kernel_desc
    ::memset(&kd, 0, sizeof(::sc_spc_kernel_desc))
    kd.code.ptr  = (scs->data: &)
    kd.code.size = scs->size
    kd.entry     = "cs_sgtest"
    kd.reflect_json = (srj->data: const char&)
    var krn: u4 = spc_make_kernel(&kd)
    if krn == 0
        print "[SG] make_kernel 失败\n", .
        return 1
    var n: i4 = 1024
    var shp: i4 = 1024
    var x: tensor& = ones(1, &shp, DT_F4)             # 输入全 1.0
    var xb: u4 = spc_buffer_from_tensor(x)
    var bnd: ::sc_spc_bindings
    ::memset(&bnd, 0, sizeof(::sc_spc_bindings))
    bnd.buffers[0] = xb
    if spc_dispatch(krn, n, 1, 1, &bnd) == 0
        print "[SG] dispatch 失败\n", .
        return 1
    spc_finish()
    spc_buffer_to_tensor(xb, x)
    var exp3: tensor& = full(1, &shp, 3.0, DT_F4)      # 期望全 3.0
    if x->allclose(exp3, 0.00001, 0.000001)
        print "[SG] subgroup vote+ballot+shuffle:通过(全元素=3.0)\n", .
    else
        print "[SG] 结果不符!x[0]=", x->at(0), " x[1]=", x->at(1), "\n", .
        fails = fails + 1
    exp3->drop()
    spc_destroy_kernel(krn)
    spc_destroy_buffer(xb)
    x->drop()

    # ============ [F16] f16 存储 + 半精度算术 ============
    var fb: u8 = vk_or_mtl_base(bk)
    var frj: shader_blob& = shader_f16_test_get(fb)
    var fcs: shader_blob& = shader_f16_test_get(fb + 1)
    var kdf: ::sc_spc_kernel_desc
    ::memset(&kdf, 0, sizeof(::sc_spc_kernel_desc))
    kdf.code.ptr  = (fcs->data: &)
    kdf.code.size = fcs->size
    kdf.entry     = "cs_f16"
    kdf.reflect_json = (frj->data: const char&)
    var krnf: u4 = spc_make_kernel(&kdf)
    if krnf == 0
        print "[F16] make_kernel 失败\n", .
        fails = fails + 1
    else
        var fshp: i4 = 1024
        # WBuf: f16 暂存（1024*2B，核内先写后读，无需初值）
        var wbd: ::sc_spc_buffer_desc
        ::memset(&wbd, 0, sizeof(::sc_spc_buffer_desc))
        wbd.size = (1024 * 2: u8)
        var wb: u4 = spc_make_buffer(&wbd)
        # OBuf: f32 输出（1024*4B）
        var obd: ::sc_spc_buffer_desc
        ::memset(&obd, 0, sizeof(::sc_spc_buffer_desc))
        obd.size = (1024 * 4: u8)
        var obf: u4 = spc_make_buffer(&obd)
        var pn: u4 = 1024
        var bndf: ::sc_spc_bindings
        ::memset(&bndf, 0, sizeof(::sc_spc_bindings))
        bndf.uniforms[0].ptr  = (&pn: &)
        bndf.uniforms[0].size = 4
        bndf.buffers[1] = wb
        bndf.buffers[2] = obf
        if spc_dispatch(krnf, n, 1, 1, &bndf) == 0
            print "[F16] dispatch 失败\n", .
            fails = fails + 1
        else
            spc_finish()
            var oshp: i4 = 1024
            var o: tensor& = zeros(1, &oshp, DT_F4)
            spc_buffer_to_tensor(obf, o)
            var exp2: tensor& = full(1, &oshp, 2.0, DT_F4)
            if o->allclose(exp2, 0.001, 0.001)
                print "[F16] f16 存储+半精度算术:通过(全元素=2.0)\n", .
            else
                print "[F16] 结果不符!o[0]=", o->at(0), " o[1]=", o->at(1), "\n", .
                fails = fails + 1
            exp2->drop()
            o->drop()
        spc_destroy_kernel(krnf)
        spc_destroy_buffer(wb)
        spc_destroy_buffer(obf)

    # ============ [I64] 64 位整数算术 ============
    var ib: u8 = vk_or_mtl_base(bk)
    var irj: shader_blob& = shader_i64_test_get(ib)
    var ics: shader_blob& = shader_i64_test_get(ib + 1)
    var kdi: ::sc_spc_kernel_desc
    ::memset(&kdi, 0, sizeof(::sc_spc_kernel_desc))
    kdi.code.ptr  = (ics->data: &)
    kdi.code.size = ics->size
    kdi.entry     = "cs_i64"
    kdi.reflect_json = (irj->data: const char&)
    var krni: u4 = spc_make_kernel(&kdi)
    if krni == 0
        print "[I64] make_kernel 失败\n", .
        fails = fails + 1
    else
        var ishp: i4 = 1024
        var iod: ::sc_spc_buffer_desc
        ::memset(&iod, 0, sizeof(::sc_spc_buffer_desc))
        iod.size = (1024 * 4: u8)
        var iobf: u4 = spc_make_buffer(&iod)
        var pni: u4 = 1024
        var bndi: ::sc_spc_bindings
        ::memset(&bndi, 0, sizeof(::sc_spc_bindings))
        bndi.uniforms[0].ptr  = (&pni: &)
        bndi.uniforms[0].size = 4
        bndi.buffers[1] = iobf
        if spc_dispatch(krni, n, 1, 1, &bndi) == 0
            print "[I64] dispatch 失败\n", .
            fails = fails + 1
        else
            spc_finish()
            var io: tensor& = zeros(1, &ishp, DT_F4)
            spc_buffer_to_tensor(iobf, io)
            var expi: tensor& = arange(0.0, 1024.0, 1.0, DT_F4)   # 期望 o[i]=gid
            if io->allclose(expi, 0.00001, 0.000001)
                print "[I64] 64 位整数算术:通过(2^40+gid-2^40=gid)\n", .
            else
                print "[I64] 结果不符!o[1]=", io->at(1), " o[1000]=", io->at(1000), "\n", .
                fails = fails + 1
            expi->drop()
            io->drop()
        spc_destroy_kernel(krni)
        spc_destroy_buffer(iobf)

    spc_shutdown()
    gpu_shutdown()
    if fails == 0
        print "spc_p3_demo 全部通过\n", .
    return fails
