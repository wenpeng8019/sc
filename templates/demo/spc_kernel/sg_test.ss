tar vulkan@450, metal@2.1

# 板验用 subgroup 内核（vote + ballot + shuffle，SPIR-V 1.3 GroupNonUniform 系）。
# 设计为「subgroup 尺寸无关、结果确定」以便逐元素校验：
#   输入 x[i] = 1.0（全一致）：
#     subgroup_all(v>0.5)  → 每个 subgroup 恒 true → flag=1.0（vote）
#     subgroup_ballot(v>0.5).x % 2 → 0 号通道恒在且谓词 true → bit0=1 → =1.0（ballot）
#     subgroup_shuffle(v,0) → 0 号通道值=1.0（uniform 输入）→ head=1.0（shuffle）
#   结果 = 1.0 + 1.0 + 1.0 = 3.0（每元素），与 subgroup_size 无关。

@def XBuf: {
    x[]: f4
} storage set 0 binding 0

@def CompIn: {
    gid: u4 builtin global_invocation_id
}

comp cs_sgtest: in: CompIn, local 64
    var v: f4 = XBuf.x[in.gid]
    var flag: f4 = 0.0
    if subgroup_all(v > 0.5)
        flag = 1.0
    var bits: uvec4 = subgroup_ballot(v > 0.5)
    var head: f4 = subgroup_shuffle(v, 0)
    XBuf.x[in.gid] = flag + head + float(bits.x % 2)
