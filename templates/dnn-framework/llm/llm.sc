inc ts.sc
inc ../utils/tensor_utils.sc

# LLM 模板：单头 attention block 骨架（Q/K/V -> sdpa -> out）
fnc main: i4
    var sh[3]: i4
    sh[0] = 1
    sh[1] = 2
    sh[2] = 2

    var qb[4]: f4
    qb[0] = 1.0
    qb[1] = 0.0
    qb[2] = 0.0
    qb[3] = 1.0
    var kb[4]: f4
    kb[0] = 1.0
    kb[1] = 0.0
    kb[2] = 0.0
    kb[3] = 1.0
    var vb[4]: f4
    vb[0] = 10.0
    vb[1] = 1.0
    vb[2] = 20.0
    vb[3] = 2.0

    var q: tensor& = from_data(3, sh, (qb: &), DT_F4)
    var k: tensor& = from_data(3, sh, (kb: &), DT_F4)
    var v: tensor& = from_data(3, sh, (vb: &), DT_F4)

    var attn: tensor& = q->sdpa(k, v, false)
    var attn_causal: tensor& = q->sdpa(k, v, true)
    printf("llm attn at0=%.4f at1=%.4f causal0=%.4f causal1=%.4f\n", attn->at(0), attn->at(1), attn_causal->at(0), attn_causal->at(1))
    print_vec(attn, "llm.attn")

    q->drop()
    k->drop()
    v->drop()
    attn->drop()
    attn_causal->drop()
    return 0
