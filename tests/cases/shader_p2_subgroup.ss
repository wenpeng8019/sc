tar vulkan@450, metal@2.1

# P2：subgroup 基础三件（vote / ballot / shuffle，SPIR-V 1.3 GroupNonUniform 系；
# metal 侧 vote/ballot 需 MSL 2.1，shuffle 2.0 即可）

@def XBuf: {
    x[]: f4
} storage set 0 binding 0

@def CompIn: {
    gid: u4 builtin global_invocation_id
    sid: u4 builtin subgroup_invocation_id
    sgsize: u4 builtin subgroup_size
}

comp cs_sg: in: CompIn, local 64
    var v: f4 = XBuf.x[in.gid]
    # vote：全组是否都为正
    if subgroup_all(v > 0.0)
        v = v * 2.0
    # ballot：谓词分布位图（uvec4）
    var bits: uvec4 = subgroup_ballot(v > 1.0)
    # shuffle：取 0 号通道的值
    var head: f4 = subgroup_shuffle(v, 0)
    XBuf.x[in.gid] = v + head + float(bits.x % 2) + float(in.sgsize - in.sid)
