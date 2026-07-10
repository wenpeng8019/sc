tar vulkan@450

@def VsOut: {
    clip:   vec4 builtin position
    uv:     vec2
    primID: int  flat
    edge:   vec3 noperspective
    uvC:    vec2 centroid
}

@def IntBlock: {
    itex:  isampler2D
    utex:  usampler2D
    i3d:   isampler3D
} uniform set 0 binding 0

frag fs_gap: vec4, in: VsOut
    # isampler2D → ivec4
    let iv: ivec4 = texture(IntBlock.itex, in.uv)
    let iv_lod: ivec4 = textureLod(IntBlock.itex, in.uv, 0.0)
    let iv_fetch: ivec4 = texelFetch(IntBlock.itex, ivec2(in.uv * 256.0), 0)

    # usampler2D → uvec4
    let uv: uvec4 = texture(IntBlock.utex, in.uv)
    let uv_g: uvec4 = textureGather(IntBlock.utex, in.uv, 0)

    # gl_PointCoord builtin
    # (used in separate point sprite shader)

    # flat varying — primID passed through
    let flat_val: int = in.primID

    # noperspective, centroid varyings
    let edgeW: vec3 = in.edge
    let uvC: vec2 = in.uvC

    return vec4(float(iv.x) + float(uv.x) + float(flat_val) + edgeW.x + uvC.x) * 0.0001
