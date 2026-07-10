tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv: vec2
    uv3: vec3
}

@def TexBlock: {
    tex2d: sampler2D
    tex3d: sampler3D
    texcube: samplerCube
    texarr: sampler2DArray
    texcube_arr: samplerCubeArray
} uniform set 0 binding 0

frag fs_tex_size: vec4, in: VsOut
    let s2d: ivec2 = textureSize(TexBlock.tex2d, 0)
    let s3d: ivec3 = textureSize(TexBlock.tex3d, 0)
    let scube: ivec2 = textureSize(TexBlock.texcube, 0)
    let sarr: ivec3 = textureSize(TexBlock.texarr, 0)
    let sca: ivec3 = textureSize(TexBlock.texcube_arr, 0)

    # 把尺寸转成颜色，确保结果被使用
    let fx: f4 = f4(s2d.x + s3d.x + scube.x + sarr.x + sca.x)
    return vec4(fx * 0.0001, 0.0, 0.0, 1.0)
