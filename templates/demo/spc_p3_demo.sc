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

def shader_blob: {
    entry:  const char&
    stage:  const char&
    target: const char&
    ext:    const char&
    data:   const u1&
    size:   u8
}
@fnc shader_sg_test_get:: shader_blob&, i: u8

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

    spc_shutdown()
    gpu_shutdown()
    if fails == 0
        print "spc_p3_demo 全部通过\n", .
    return fails
