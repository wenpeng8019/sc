tar vulkan@450

@def TexBlock: {
    tex: sampler2D
} uniform set 0 binding 0

comp cs_tex_proj
    let p: vec3 = vec3(0.5, 0.5, 1.0)
    let c: vec4 = textureProj(TexBlock.tex, p)
    let d: vec4 = c * 0.5
