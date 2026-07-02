inc ts.sc

# ============================================================
# utils/tensor_utils.sc —— 共享张量工具（纯 sc 实现）
# ============================================================
# 与 dnn-design/utils 一致：模板内的复用件全部用纯 sc 写函数体，
# 不引入任何 _impl.c（张量底层能力由 builtins/ts 提供）。
#   · make_col_f4 —— 由 f4 缓冲构造 [n,1] 列向量。
#   · vec_argmax  —— 逻辑扁平 argmax（越界安全）。
#   · print_vec   —— 逐元素打印一维向量。
# ============================================================

# 由 f4 缓冲构造 [n,1] 列向量。
@fnc make_col_f4: tensor&, n: i4, data: &
    var sh[2]: i4
    sh[0] = n
    sh[1] = 1
    return from_data(2, sh, data, DT_F4)

# 逻辑扁平 argmax；空张量返回 0（at 越界返回 0，安全）。
@fnc vec_argmax: i4, t: tensor&
    var best: i4 = 0
    var bestv: f8 = t->at(0)
    var n: i8 = t->numel
    for i in n
        var v: f8 = t->at(i)
        if v > bestv
            bestv = v
            best = (i: i4)
    return best

# 打印一维向量（逻辑扁平遍历）。
@fnc print_vec: t: tensor&, label: const char&
    ::printf("%s [", label)
    var n: i8 = t->numel
    for i in n
        ::printf("%s%.4f", i ? ", " : "", t->at(i))
    print "]"
    return
