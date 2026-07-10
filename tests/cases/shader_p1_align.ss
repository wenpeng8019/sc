tar vulkan@450

@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
    ref:  f4
    uv3:  vec3
}

# 采样器资源块
@def TexBlock: {
    tex2d:  sampler2D
    tex3d:  sampler3D
    shd2d:  sampler2DShadow
} uniform set 0 binding 0

frag fs_p1_align: vec4, in: VsOut
    # --- 1. texture with bias (Bias = 0x1) ---
    let c_bias: vec4 = texture(TexBlock.tex2d, in.uv, 0.5)

    # --- 2. shadow textureProj (OpImageSampleProjDrefImplicitLod) ---
    # vec4 P = (s*w, t*w, ref*w, w)
    let proj4: vec4 = vec4(in.uv * 2.0, in.ref * 2.0, 2.0)
    let d_proj: f4 = textureProj(TexBlock.shd2d, proj4)

    # --- 3. shadow textureProjLod (ProjDrefExplicitLod + Lod) ---
    let d_projlod: f4 = textureProjLod(TexBlock.shd2d, proj4, 0.0)

    # --- 4. shadow textureProjGrad (ProjDrefExplicitLod + Grad) ---
    let dx2: vec2 = dFdx(in.uv)
    let dy2: vec2 = dFdy(in.uv)
    let d_projgrad: f4 = textureProjGrad(TexBlock.shd2d, proj4, dx2, dy2)

    # --- 5. textureGather regular (OpImageGather, comp = 0) ---
    let g_r: vec4 = textureGather(TexBlock.tex2d, in.uv, 0)
    let g_g: vec4 = textureGather(TexBlock.tex2d, in.uv, 1)

    # --- 6. textureGather shadow (OpImageDrefGather) ---
    # shadow textureGather: coord = vec2, ref = separate float
    let g_shd: vec4 = textureGather(TexBlock.shd2d, in.uv, in.ref)

    # --- 7. textureOffset (ConstOffset = 0x8, frag → ImplicitLod) ---
    let c_off: vec4 = textureOffset(TexBlock.tex2d, in.uv, ivec2(1, 0))

    # --- 8. textureLodOffset (Lod | ConstOffset = 0xA) ---
    let c_lodoff: vec4 = textureLodOffset(TexBlock.tex2d, in.uv, 0.0, ivec2(0, 1))

    # --- 9. textureProjOffset ---
    let proj3: vec3 = vec3(in.uv, 2.0)
    let c_projoff: vec4 = textureProjOffset(TexBlock.tex2d, proj3, ivec2(1, 1))

    # --- 10. textureGradOffset (Grad | ConstOffset = 0xC) ---
    let c_gradoff: vec4 = textureGradOffset(TexBlock.tex2d, in.uv, dx2, dy2, ivec2(-1, 0))

    # --- 11. textureQueryLod → vec2 ---
    let qlod: vec2 = textureQueryLod(TexBlock.tex2d, in.uv)

    # --- 12. textureQueryLevels → int ---
    let qlevels: int = textureQueryLevels(TexBlock.tex2d)

    # 汇总（避免 dead-code 优化消除指令）
    let shadow_sum: f4 = d_proj + d_projlod + d_projgrad
    let color_sum: vec4 = c_bias + c_off + c_lodoff + c_projoff + c_gradoff + g_r + g_g + g_shd
    return color_sum + vec4(shadow_sum + qlod.x + float(qlevels), 0.0, 0.0, 0.0)
