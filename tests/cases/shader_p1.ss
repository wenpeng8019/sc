tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv0:  vec2
    uv3:  vec3
}

@def Tex2DBlock: {
    tex:    sampler2D
    tex3d:  sampler3D
    texcube: samplerCube
    texarr: sampler2DArray
} uniform set 0 binding 0

frag fs_p1: vec4, in: VsOut
    let c2d: vec4 = texture(Tex2DBlock.tex, in.uv0)
    let c2d_lod: vec4 = textureLod(Tex2DBlock.tex, in.uv0, 0.0)
    let c3d: vec4 = texture(Tex2DBlock.tex3d, in.uv3)
    let c3d_lod: vec4 = textureLod(Tex2DBlock.tex3d, in.uv3, 1.0)
    let ccube: vec4 = texture(Tex2DBlock.texcube, in.uv3)
    let ccube_lod: vec4 = textureLod(Tex2DBlock.texcube, in.uv3, 1.5)
    let carr: vec4 = texture(Tex2DBlock.texarr, in.uv3)
    let carr_lod: vec4 = textureLod(Tex2DBlock.texarr, in.uv3, 0.5)
    let cpix: vec4 = texelFetch(Tex2DBlock.tex, ivec2(in.uv0 * 512.0), 0)
    return (c2d + c2d_lod + c3d + c3d_lod + ccube + ccube_lod + carr + carr_lod + cpix) * 0.11
