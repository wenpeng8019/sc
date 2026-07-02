# 由 scc --emit-sc 从 AST 再生成

inc ts.sc

inc nn.sc

fnc main: i4
    rand_seed((5: i8))
    var sidx[1]: i4
    sidx[0] = 3
    var ib[3]: f4
    ib[0] = 1.0
    ib[1] = 3.0
    ib[2] = 5.0
    var idxt: tensor@1 = from_data(1, (sidx: &), (ib: &), DT_F4)
    var stg[1]: i4
    stg[0] = 1
    var tgb[1]: f4
    tgb[0] = 1.0
    var tt: tensor@1 = from_data(1, (stg: &), (tgb: &), DT_F4)
    var emb: embed@1 = nn_embedding(6, 4)
    var fc: linear@1 = nn_linear(12, 2)
    var opt: optim@1 = nn_adam(0.02)
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
        var e0: val& = emb->forward(idx)
        var seq: val& = e0->reshape(3, (s3: &))
        var seqt: val& = seq->transpose()
        var score: val& = seq->bmm(seqt)
        var ss: val& = score->scale(0.5)
        var attn: val& = ss->softmax(-1)
        var ctx: val& = attn->bmm(seq)
        var nrm: val& = ctx->rms_norm(-1, 0.00001)
        var dp: val& = nrm->dropout(0.0, 0)
        var fl: val& = dp->reshape(2, (sf: &))
        var logit: val& = fc->forward(fl)
        var loss: val& = logit->cross_entropy(tg)
        loss->backward()
        opt->step()
        opt->zero_grad()
        last = loss->item()
        if e == 0
            first = last
        nn_tape_clear()
    ::printf("attn_down=%d attn_low=%d\n", (last < first: i4), (last < 0.1: i4))
    return 0
