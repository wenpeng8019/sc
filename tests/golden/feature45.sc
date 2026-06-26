# 由 scc --emit-sc 从 AST 再生成

inc mt.sc

var g_sess: session& = nil

var g_arg: i4 = 0

rpc serve: i4, x: i4
    var s: session& = async
    g_sess = s
    g_arg = x
    return 0

rpc server: qq: queue&
    qq->pull(-1)
    done g_sess, g_arg * 10

fnc main: i4
    var sq: queue& = default_queue(nil)
    var st: thread& = nil
    run server(sq), &st
    var r: i4 = sync<sq> serve(7)
    printf("delayed response: r=%d\n", r)
    st->join()
    sq->drop()
    return 0
