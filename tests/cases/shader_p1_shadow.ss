tar vulkan@450

@def VsOut: {
    clip:      vec4 builtin position
    uv:        vec2
    depth_ref: f4
    uv3:       vec3
}

@def ShadowBlock: {
    shd2d:   sampler2DShadow
    shdcube: samplerCubeShadow
    shdarr:  sampler2DArrayShadow
} uniform set 0 binding 0

frag fs_shadow: f4, in: VsOut
    # sampler2DShadow: vec3(s, t, ref) → float
    let d0: f4 = texture(ShadowBlock.shd2d, vec3(in.uv, in.depth_ref))
    let d1: f4 = textureLod(ShadowBlock.shd2d, vec3(in.uv, in.depth_ref), 0.0)

    # samplerCubeShadow: vec4(dir.xyz, ref) → float
    let d2: f4 = texture(ShadowBlock.shdcube, vec4(in.uv3, in.depth_ref))
    let d3: f4 = textureLod(ShadowBlock.shdcube, vec4(in.uv3, in.depth_ref), 0.0)

    # sampler2DArrayShadow: vec4(s, t, layer, ref) → float
    let d4: f4 = texture(ShadowBlock.shdarr, vec4(in.uv, 0.0, in.depth_ref))

    return (d0 + d1 + d2 + d3 + d4) * 0.2
