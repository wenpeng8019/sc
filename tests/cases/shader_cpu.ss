tar cpu@99

# CPU SPMD 后端（§17 M1）：标量 kernel → SPMD 循环化 C（restrict + 向量化提示）
# 覆盖：uniform 标量成员 / storage 运行时数组 + 定长数组 / 窄宽标量 /
#      控制流 / 数学内建 / 辅助函数 / local_invocation_index·workgroup_id 映射

@def Params: {
    a: f4
    n: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def YBuf: {
    scale: f2
    big: i8
    lut[4]: f4
    y[]: f4
} storage set 0 binding 2

@def CompIn: {
    gid: u4 builtin global_invocation_id
    lid: u4 builtin local_invocation_index
    wkid: u4 builtin workgroup_id
}

fnc lerp1: f4, a: f4, b: f4, t: f4
    return a + (b - a) * t

comp cs_cpu: in: CompIn, local 128
    if in.gid < Params.n
        var v: f4 = XBuf.x[in.gid]
        v = lerp1(v, YBuf.lut[in.lid % 4], 0.5)
        var k: u4 = 0
        while k < 4
            v = v + sqrt(abs(v)) * 0.001
            k = k + 1
        if in.wkid == 0
            v = v * float(YBuf.scale)
        YBuf.y[in.gid] = clamp(v * Params.a, -100.0, 100.0)
        if in.gid == 0
            YBuf.big = i8(in.gid) + 42
