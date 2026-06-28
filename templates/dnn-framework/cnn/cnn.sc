inc ts.sc
inc ../utils/tensor_utils.sc

# CNN 模板：conv -> relu -> pool -> flatten -> linear（前向骨架）
fnc main: i4
    var xsh[4]: i4
    xsh[0] = 1
    xsh[1] = 1
    xsh[2] = 4
    xsh[3] = 4
    var xraw[16]: f4
    for i in 16
        xraw[i] = (i: f4)
    var x: tensor& = from_data(4, xsh, (xraw: &), DT_F4)

    var wsh[4]: i4
    wsh[0] = 2
    wsh[1] = 1
    wsh[2] = 3
    wsh[3] = 3
    var w: tensor& = ones(4, wsh, DT_F4)
    var bsh[1]: i4
    bsh[0] = 2
    var b: tensor& = zeros(1, bsh, DT_F4)

    var feat: tensor& = x->conv2d(w, b, 1, 1, 0, 0)
    var act: tensor& = feat->relu()
    var pool: tensor& = act->max_pool2d(2, 2, 2, 2, 0, 0)
    printf("cnn feat=%dx%dx%dx%d pool=%dx%dx%dx%d\n",
        feat->dim(0), feat->dim(1), feat->dim(2), feat->dim(3),
        pool->dim(0), pool->dim(1), pool->dim(2), pool->dim(3))
    print_vec(pool, "cnn.pool")

    x->drop()
    w->drop()
    b->drop()
    feat->drop()
    act->drop()
    pool->drop()
    return 0
