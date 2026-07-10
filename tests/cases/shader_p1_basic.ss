tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv:  vec2
}

@def TexBlock: {
    tex: sampler2D
} uniform set 0 binding 0

frag fs_tex_basic: vec4, in: VsOut
    let c1: vec4 = texture(TexBlock.tex, in.uv)
    let c2: vec4 = textureLod(TexBlock.tex, in.uv, 0.0)
    return c1 * 0.5 + c2 * 0.5
