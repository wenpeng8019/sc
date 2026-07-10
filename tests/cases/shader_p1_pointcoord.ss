tar vulkan@450

# gl_PointCoord：片元着色器内建（点精灵）
@def FragIn: {
    pc: vec2 builtin point_coord
}

@def TexBlock: {
    tex: sampler2D
} uniform set 0 binding 0

frag fs_point: vec4, in: FragIn
    let c: vec4 = texture(TexBlock.tex, in.pc)
    return c
