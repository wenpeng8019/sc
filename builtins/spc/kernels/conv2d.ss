tar vulkan@450, metal@2.0, gles@310, cpu@99

# conv2d —— spc graph 面算子内核（design §18 M2）：NCHW direct 卷积
#   x[N,Ci,H,W] ⊛ w[Co,Ci,Kh,Kw] + bias[Co] → y[N,Co,Ho,Wo]
#   Ho=(H+2*ph-Kh)/sh+1（与 ts sc_tensor_conv2d 同语义：zero-pad、bias 必传）
# 每 invocation 算一个输出元素（1D 调度 gx = N*Co*Ho*Wo）；direct 直算
# 正确性优先（tiled/winograd 优化留 M3 需求触发）。单文件四目标（无 shared）。

@def Cv: {
    n: u4
    ci: u4
    h: u4
    w: u4
    co: u4
    kh: u4
    kw: u4
    sh: u4
    sw: u4
    ph: u4
    pw: u4
    ho: u4
    wo: u4
} uniform set 0 binding 0

@def XBuf: {
    x[]: f4
} storage set 0 binding 1

@def WBuf: {
    wt[]: f4
} storage set 0 binding 2

@def BBuf: {
    bias[]: f4
} storage set 0 binding 3

@def YBuf: {
    y[]: f4
} storage set 0 binding 4

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp conv2d_direct: in: CompIn, local 64
    var total: u4 = Cv.n * Cv.co * Cv.ho * Cv.wo
    if in.gid < total
        # 展平坐标 → (n, co, oy, ox)
        var ox: u4 = in.gid % Cv.wo
        var t1: u4 = in.gid / Cv.wo
        var oy: u4 = t1 % Cv.ho
        var t2: u4 = t1 / Cv.ho
        var oc: u4 = t2 % Cv.co
        var ni: u4 = t2 / Cv.co
        var acc: f4 = BBuf.bias[oc]
        var c: u4 = 0
        while c < Cv.ci
            var ky: u4 = 0
            while ky < Cv.kh
                # 输入行 iy = oy*sh + ky - ph（u4 下溢会变大数，用 >=h 一并挡掉）
                var iy: u4 = oy * Cv.sh + ky
                if iy >= Cv.ph && iy - Cv.ph < Cv.h
                    var kx: u4 = 0
                    while kx < Cv.kw
                        var ix: u4 = ox * Cv.sw + kx
                        if ix >= Cv.pw && ix - Cv.pw < Cv.w
                            var xi: u4 = ((ni * Cv.ci + c) * Cv.h + (iy - Cv.ph)) * Cv.w + (ix - Cv.pw)
                            var wi: u4 = ((oc * Cv.ci + c) * Cv.kh + ky) * Cv.kw + kx
                            acc = acc + XBuf.x[xi] * WBuf.wt[wi]
                        kx = kx + 1
                ky = ky + 1
            c = c + 1
        YBuf.y[in.gid] = acc
