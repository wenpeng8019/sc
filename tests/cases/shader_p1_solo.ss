tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv2:  vec2
    uv3:  vec3
}

# 单独声明采样器（不在资源块中）
var tex2d: sampler2D uniform set 0 binding 0
var tex3d: sampler3D uniform set 0 binding 1
var texcube: samplerCube uniform set 0 binding 2
var texarr: sampler2DArray uniform set 0 binding 3

frag fs_p1_tex: vec4, in: VsOut
    # sampler2D
    let c2d: vec4 = texture(tex2d, in.uv2)
    let c2d_lod: vec4 = textureLod(tex2d, in.uv2, 0.0)
    
    # sampler3D
    let c3d: vec4 = texture(tex3d, in.uv3)
    let c3d_lod: vec4 = textureLod(tex3d, in.uv3, 1.0)
    
    # samplerCube
    let ccube: vec4 = texture(texcube, in.uv3)
    let ccube_lod: vec4 = textureLod(texcube, in.uv3, 1.5)
    
    # sampler2DArray
    let carr: vec4 = texture(texarr, in.uv3)
    let carr_lod: vec4 = textureLod(texarr, in.uv3, 0.5)
    
    # texelFetch
    let cpix: vec4 = texelFetch(tex2d, ivec2(in.uv2 * 512.0), 0)
    
    # 混合
    return (c2d + c2d_lod + c3d + c3d_lod + ccube + ccube_lod + carr + carr_lod + cpix) * 0.11
