# 由 scc --emit-sc 从 AST 再生成

def view: {
    p: char&
    n: i4
}

def dev: <view> {
    data[64]: char
    fnc alloc: view&, off: i4, len: i4
    fnc free: v: view&
}

fnc dev_alloc: view&, _this: dev&, off: i4, len: i4
    var v: view& = (::malloc(sizeof(view)): view&)
    v->p = &_this->data[off]
    v->n = len
    return v

fnc dev_free: _this: dev&, v: view&
    ::free(v)

fnc main: i4
    var d: dev
    var i: i4
    for i = 0; i < 26; i++
        d.data[i] = ('a' + i)
    d.alloc = dev_alloc
    d.free = dev_free
    var s: dev[2, 5]
    s = d
    ::printf("切片:")
    var j: i4
    for j = 0; j < s._->n; j++
        ::printf(" %c", s._->p[j])
    ::printf("\n")
    s = nil
    ::printf("已解绑: %d\n", s._ == nil)
    return 0
