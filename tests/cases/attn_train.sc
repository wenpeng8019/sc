# nn 自注意力 Transformer block 端到端训练回归用例（define-by-run）：
#   - embed 模块查表 → reshape 成序列 [T,D]；
#   - 自注意力：score = seq·seqᵀ（bmm + transpose）→ scale → softmax → ctx = attn·seq；
#   - rms_norm 归一 + dropout(推理关) + 残差思路（此处直接前馈）；
#   - reshape 展平 → linear → cross_entropy；Adam 优化；
#   - 验证多轮训练后损失明显下降，覆盖 embedding/bmm/transpose/scale/softmax/rms_norm/dropout 反向。
inc ts.sc
inc nn.sc

fnc main: i4
    rand_seed((5: i8))

    # token 序列 idx[3] = {1,3,5}
    var sidx[1]: i4
    sidx[0] = 3
    var ib[3]: f4
    ib[0] = 1.0
    ib[1] = 3.0
    ib[2] = 5.0
    var idxt: tensor& = from_data(1, (sidx: &), (ib: &), DT_F4)

    # 目标类标 [1] = {1}
    var stg[1]: i4
    stg[0] = 1
    var tgb[1]: f4
    tgb[0] = 1.0
    var tt: tensor& = from_data(1, (stg: &), (tgb: &), DT_F4)

    var emb: embed& = nn_embedding(6, 4)     # 词表 6，维度 4
    var fc: linear& = nn_linear(12, 2)       # 3*4=12 → 2
    var opt: optim& = nn_adam(0.02)
    opt->track_embedding(emb)
    opt->track_linear(fc)

    var s3[3]: i4
    s3[0] = 1
    s3[1] = 3
    s3[2] = 4
    var sf[2]: i4
    sf[0] = 1
    sf[1] = 12

    var first: f8 = 0.0
    var last: f8 = 0.0
    for e in 150
        var idx: val& = nn_input(idxt)
        var tg: val& = nn_input(tt)
        var e0: val& = emb->forward(idx)            # [3,4]
        var seq: val& = e0->reshape(3, (s3: &))     # [1,3,4]
        var seqt: val& = seq->transpose()           # [1,4,3]
        var score: val& = seq->bmm(seqt)            # [1,3,3]
        var ss: val& = score->scale(0.5)            # 1/sqrt(D≈4)≈0.5
        var attn: val& = ss->softmax(-1)            # [1,3,3]
        var ctx: val& = attn->bmm(seq)              # [1,3,4]
        var nrm: val& = ctx->rms_norm(-1, 0.00001)
        var dp: val& = nrm->dropout(0.0, 0)         # 推理关闭
        var fl: val& = dp->reshape(2, (sf: &))      # [1,12]
        var logit: val& = fc->forward(fl)
        var loss: val& = logit->cross_entropy(tg)
        loss->backward()
        opt->step()
        opt->zero_grad()
        last = loss->item()
        if e == 0
            first = last
        nn_tape_clear()
    printf("attn_down=%d attn_low=%d\n", (last < first: i4), (last < 0.1: i4))

    emb->drop()
    fc->drop()
    opt->drop()
    idxt->drop()
    tt->drop()
    return 0
