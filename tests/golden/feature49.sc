# 由 scc --emit-sc 从 AST 再生成

tok x: "nn.x"

tok y: "nn.y"

tok loss: "nn.loss"

tok gx: "nn.gx"

tok gy: "nn.gy"

dep any: a:"nn.x" map b:"nn.y"
    if this->active == (0 - 4)
        var g: i8 = (gy->get(): i8)
        gx->set((g * 2: @), 0)
        return false
    var v: i8 = (a->get(): i8)
    b->set((v * 2: @), 0)
    return false

dep any: c:"nn.y" map o:"nn.loss"
    if this->active == (0 - 4)
        var gloss: i8 = (o->get(): i8)
        gy->set((gloss: @), 0)
        return false
    var v: i8 = (c->get(): i8)
    o->set((v + 5: @), 0)
    return false

fnc main: i4
    form gx, (0: @)
    form gy, (0: @)
    form loss, (0: @)
    form y, (0: @)
    form x, (0: @)
    x->set((4: @), 0)
    ::printf("forward: x=%lld y=%lld loss=%lld\n", (x->get(): i8), (y->get(): i8), (loss->get(): i8))
    back loss, (1: @)
    ::printf("backward(seed=1): gy=%lld gx=%lld\n", (gy->get(): i8), (gx->get(): i8))
    return 0
