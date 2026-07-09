# shader_p0 —— syntax-s §16 P0 控制流批次回归用例（双后端黄金对照）
# 覆盖：for(init 自动声明/++/break/continue)、do-while、case(多标签/through/
# default)、swizzle 读写、强转、标量↔向量运算。
# 目标 glcore@410：默认链(SPIR-V→SPIRV-Cross 反译)与 --emit-glsl(自研文本)
# 均产纯文本,适合 stdout 黄金比对。

tar glcore@410

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
    var o: VsOut
    o.clip = v
    o.col = vec3(0.5, 0.5, 0.5)
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
    return vec4(r, r, r, 1.0)
