tar vulkan@450, metal@2.0

# P2：特化常量（let ... spec N → OpSpecConstant/constant_id；管线创建期可覆写）

let TILE: u4 = 64 spec 0
let GAIN: f4 = 1.5 spec 1
let PLAIN: f4 = 2.0

@def XBuf: {
    x[]: f4
} storage set 0 binding 0

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_spec: in: CompIn
    if in.gid < TILE
        XBuf.x[in.gid] = XBuf.x[in.gid] * GAIN * PLAIN
