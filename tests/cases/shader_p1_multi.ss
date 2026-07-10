tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv2:  vec2
    uv3:  vec3
}

@def TexBlock: {
    tex2d: sampler2D
    tex3d: sampler3D
    texcube: samplerCube
} uniform set 0 binding 0

frag fs_tex_multi: vec4, in: VsOut
    let c2d: vec4 = texture(TexBlock.tex2d, in.uv2)
    let c3d: vec4 = texture(TexBlock.tex3d, in.uv3)
    let ccube: vec4 = texture(TexBlock.texcube, in.uv3)
    return c2d * 0.33 + c3d * 0.33 + ccube * 0.34
