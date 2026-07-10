tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
    ref:  f4
    uv3:  vec3
}

@def TexGradBlock: {
    tex2d:  sampler2D
    tex3d:  sampler3D
    texcube: samplerCube
    shd2d:  sampler2DShadow
} uniform set 0 binding 0

frag fs_grad: vec4, in: VsOut
    # 从 frag 导数计算梯度
    let dx2: vec2 = dFdx(in.uv)
    let dy2: vec2 = dFdy(in.uv)
    let dx3: vec3 = dFdx(in.uv3)
    let dy3: vec3 = dFdy(in.uv3)

    # textureGrad: sampler2D
    let c0: vec4 = textureGrad(TexGradBlock.tex2d, in.uv, dx2, dy2)

    # textureGrad: sampler3D（梯度须与坐标同维）
    let c1: vec4 = textureGrad(TexGradBlock.tex3d, in.uv3, dx3, dy3)

    # textureGrad: samplerCube
    let c2: vec4 = textureGrad(TexGradBlock.texcube, in.uv3, dx3, dy3)

    # textureGrad + shadow sampler：coord = vec3(s,t,ref)，梯度为 vec2
    let shadow_coord: vec3 = vec3(in.uv, in.ref)
    let d0: f4 = textureGrad(TexGradBlock.shd2d, shadow_coord, dx2, dy2)

    # textureProjGrad: sampler2D，投影坐标 vec3(s,t,q)，梯度 vec2
    let proj: vec3 = vec3(in.uv, 1.0)
    let c3: vec4 = textureProjGrad(TexGradBlock.tex2d, proj, dx2, dy2)

    return c0 + c1 + c2 + vec4(d0) + c3
