# spec P2+P3（spec.md）：有体 spec（分支标签即取值）+ use 白名单 + 类型直代。
# 维度：TEX_KIND(1) × TONE(2) × BLEND(2) 全积 = 4；use 白名单收敛为 2 实例：
#   sampler2D.tm_a.ADD / sampler2D.tm_b.MUL
tar vulkan@450

spec TEX_KIND in [sampler2D]
spec TONE in [tm_a, tm_b]

# 有体 spec：算法变体按分支分岔（标签 ADD/MUL 即维度取值集合）
spec BLEND:
    ADD:
        fnc blend: vec3, a: vec3, b: vec3
            return a + b
    MUL:
        fnc blend: vec3, a: vec3, b: vec3
            return a * b

use TONE, BLEND
    tm_a, ADD
    tm_b, MUL

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

fnc tm_a: vec3, c: vec3
    return c * (c * 2.51 + 0.03) / (c * (c * 2.43 + 0.59) + 0.14)

fnc tm_b: vec3, c: vec3
    return c / (c + vec3(1.0, 1.0, 1.0))

vert vs_main: VsOut, in: VsIn
    var o: VsOut
    o.clip = Camera.mvp * vec4(in.pos, 1.0)
    o.uv = in.uv
    return o

frag fs_main: vec4, in: VsOut
    var c: vec4 = texture(tex, in.uv)
    return vec4(TONE(blend(c.xyz, c.xyz)), 1.0)
