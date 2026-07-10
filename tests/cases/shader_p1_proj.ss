tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv:  vec2
}

@def TexBlock: {
    tex: sampler2D
} uniform set 0 binding 0

frag fs_tex_proj: vec4, in: VsOut
    let c1: vec4 = textureProj(TexBlock.tex, vec4(in.uv, 1.0, 1.0))
    return c1
