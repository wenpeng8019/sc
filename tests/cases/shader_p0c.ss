tar vulkan@450

# ---- 编译期常量 ----
let PI: f4 = 3.14159265
let HALF: f4 = 0.5

# ---- 辅助函数参数/返回结构体 ----
def Color: {
    r: f4
    g: f4
    b: f4
    a: f4
}

def Pair: {
    lo: vec2
    hi: vec2
}

fnc makePair: Pair, a: vec2, b: vec2
    return Pair(a, b)

fnc colorFromPair: Color, p: Pair, alpha: f4
    return Color(p.lo.x, p.lo.y, p.hi.x, alpha)

# ---- I/O ----
@def VsIn: {
    vid:  i4 builtin vertex_id
    iid:  i4 builtin instance_id
}

@def VsOut: {
    clip:   vec4 builtin position
    psize:  f4   builtin point_size
    col:    vec3
    uv:     vec2
}

@def Params: {
    scale: f4
    tint:  Color
} uniform set 0 binding 0

# ---- vert ----
vert vs_main: VsOut, in: VsIn
    # 编译期常量使用
    var angle: f4 = PI * (in.vid: f4) * HALF
    let base[3]: vec2 = {vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.0, 0.5)}
    # 结构体值类型构造/传参/成员访问
    var p: Pair = makePair(base[in.vid] * Params.scale, vec2(sin(angle), cos(angle)))
    var c: Color = colorFromPair(p, 1.0)
    # 多维数组
    var grid[2][3]: f4
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            grid[i][j] = (i: f4) + (j: f4) * 0.1
    var o: VsOut
    o.clip  = vec4(p.lo, 0.0, 1.0)
    o.psize = 1.0
    o.col   = vec3(c.r, c.g, c.b)
    o.uv    = p.hi
    return o

# ---- frag ----
frag fs_main: vec4, in: VsOut
    # front_facing
    var flip: f4 = 1.0
    if dFdx(in.uv.x) < 0.0
        flip = -1.0
    var col: vec3 = normalize(in.col * Params.tint.r)
    # matrix ops: mat × vec
    var m: mat3 = mat3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0))
    col = m * col
    return vec4(col * flip, in.uv.x + Params.tint.a)
