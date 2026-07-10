tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv: vec2
}

@def TexBlock: {
    tex: sampler2D
} uniform set 0 binding 0

frag fs_tex_projlod: vec4, in: VsOut
    let p: vec3 = vec3(in.uv, 1.0)
    let c0: vec4 = textureProj(TexBlock.tex, p)
    let c1: vec4 = textureProjLod(TexBlock.tex, p, 1.0)
    return c0 * 0.5 + c1 * 0.5
