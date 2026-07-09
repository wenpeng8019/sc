# gpu_demo 三角形着色器（syntax-s）——gpu 模块 Metal 后端首光 demo。
# 无顶点缓冲、无 uniform：三个顶点的位置与颜色写在常量数组里，
# 由内建 vertex_id 索引。运行时 host 只需空 draw(3)。
#
# 编译（多目标产物带目标标签：vs_main.metal20.metal / vs_main.glcore410.vert
#       / gpu_tri.metal20.reflect.json / gpu_tri.glcore410.reflect.json）：
#   ./compiler/build/scc templates/demo/gpu_shader/gpu_tri.ss -o templates/demo/gpu_shader/out

tar metal@2.0
tar glcore@410
tar vulkan@450

@def VsIn: {
    vid: i4 builtin vertex_id
}

@def VsOut: {
    clip:  vec4 builtin position
    color: vec3
}

vert vs_main: VsOut, in: VsIn
    let pos[3]: vec2 = {vec2(0.0, -0.5), vec2(0.5, 0.5), vec2(-0.5, 0.5)}
    let col[3]: vec3 = {vec3(1.0, 0.0, 0.0), vec3(0.0, 1.0, 0.0), vec3(0.0, 0.0, 1.0)}
    var o: VsOut
    o.clip = vec4(pos[in.vid], 0.0, 1.0)
    o.color = col[in.vid]
    return o

frag fs_main: vec4, in: VsOut
    return vec4(in.color, 1.0)
