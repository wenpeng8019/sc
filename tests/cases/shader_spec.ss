# spec 特化维度（spec.md P1）：词法层单态化验证。
#   TEX_KIND —— 类型直代（维度值出现在类型位置）
#   TONEMAP —— 符号直代（维度值选择函数实现）
# 组合 = 1 × 2 = 2 实例 × 2 目标，产物带实例标签后缀，反射附 spec 字段。
tar vulkan@450
tar metal@2.0

spec TEX_KIND in [sampler2D]
spec TONEMAP in [tm_aces, tm_reinhard]

var tex: TEX_KIND uniform set 0 binding 1

@def Camera: {
    mvp: mat4
} uniform set 0 binding 0

@def VsIn: {
    pos: vec3 loc 0
    uv:  vec2 loc 1
}

@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
}

fnc tm_aces: vec3, c: vec3
    return c * (c * 2.51 + 0.03) / (c * (c * 2.43 + 0.59) + 0.14)

fnc tm_reinhard: vec3, c: vec3
    return c / (c + vec3(1.0, 1.0, 1.0))

vert vs_main: VsOut, in: VsIn
    var o: VsOut
    o.clip = Camera.mvp * vec4(in.pos, 1.0)
    o.uv = in.uv
    return o

frag fs_main: vec4, in: VsOut
    var c: vec4 = texture(tex, in.uv)
    return vec4(TONEMAP(c.xyz), 1.0)
