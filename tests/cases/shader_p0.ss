# shader_p0 —— syntax-s §16 P0 控制流批次回归用例（双后端黄金对照）
# 覆盖：辅助函数(OpFunctionCall)、discard(OpKill)、for/do-while/break/
# continue、case(多标签/through/default)、swizzle 读写、++ --、强转。

tar glcore@410

fnc clamp01: f4, x: f4
    return clamp(x, 0.0, 1.0)

fnc lerp3: vec3, a: vec3, b: vec3, t: f4
    return a + (b - a) * t

@def VsIn: {
    vid: i4 builtin vertex_id
}

@def VsOut: {
    clip: vec4 builtin position
    col:  vec3
}

vert vs_main: VsOut, in: VsIn
    var acc: f4 = 0.0
    for i = 0; i < 8; i++
        if i == 3
            continue
        if i > 6
            break
        acc += (i: f4)
    var s: f4 = 0.0
    var k: i4 = 4
    do
        s += (k: f4)
        k--
    while k > 0
    var v: vec4 = vec4(0.0, 0.0, 0.0, 1.0)
    v.xy = vec2(s * 0.01, acc * 0.01)
    let pos[3]: vec2 = {vec2(-0.5, -0.5), vec2(0.5, -0.5), vec2(0.0, 0.5)}
    var o: VsOut
    o.clip = vec4(pos[in.vid], 0.0, 1.0)
    o.col = lerp3(vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), clamp01((in.vid: f4) * 0.5))
    return o

frag fs_main: vec4, in: VsOut
    var m: i4 = (in.col.x * 8.0: i4)
    var r: f4 = 0.0
    case m:
        1, 2:
            r = 0.25
        3:
            r = 0.5
            through
        4:
            r = r + 0.25
        :
            r = 1.0
    var a: f4 = clamp01(in.col.x)
    if a < 0.01
        discard
    return vec4(in.col * r, a)
