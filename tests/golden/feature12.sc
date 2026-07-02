# 由 scc --emit-sc 从 AST 再生成

def view: {
    p: char&
    n: i4
    capacity: fnc: i4
        return sizeof(self->data)
}

def buffer: <view> {
    data[256]: char
    alloc: fnc: view&, off: i4, len: i4
        var v: view& = (::malloc(sizeof(view)): view&)
        v->p = &this->data[off]
        v->n = len
        return v
    free: fnc: v: view&
        ::free(v)
}

fnc main: i4
    var b: buffer
    var i: i4
    for i = 0; i < 26; i++
        b.data[i] = ('a' + i)
    var s: buffer[2, 5]
    s = b
    ::printf("切片:")
    var j: i4
    for j = 0; j < s._->n; j++
        ::printf(" %c", s._->p[j])
    ::printf("\n")
    ::printf("本体容量: %d\n", s._->capacity())
    s = nil
    ::printf("已解绑: %d\n", s._ == nil)
    return 0
