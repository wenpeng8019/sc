# 由 scc --emit-sc 从 AST 再生成

inc adt.sc

fnc int_cmp -> array_cmp
    return (a: i4&)[0] - (b: i4&)[0]

fnc dump: a: array&
    var i: u8 = 0
    for i = 0; i < a->len(); i++
        ::printf(" %d", (a->at(i): i4&)[0])
    ::printf("\n")

fnc main: i4
    var a: array
    a.init(4)
    var v: i4 = 30
    a.push(&v)
    v = 10
    a.push(&v)
    v = 20
    a.push(&v)
    v = 10
    a.push(&v)
    ::printf("len=%llu\n", a.len())
    dump(&a)
    a.sort(int_cmp)
    ::printf("sorted:")
    dump(&a)
    var key: i4 = 20
    var found: i4& = a.bsearch(&key, int_cmp)
    ::printf("bsearch(20)=%d\n", (found != nil) ? found[0] : (0 - 1))
    key = 10
    ::printf("find(10)=%lld rfind(10)=%lld\n", a.find(&key, 0, int_cmp), a.rfind(&key, int_cmp))
    var b: array
    a.clone(&b)
    ::printf("clone equals=%d\n", a.equals(&b, int_cmp))
    b.reverse()
    ::printf("reversed:")
    dump(&b)
    var part: array
    a.slice(1, 3, &part)
    ::printf("slice(1,3):")
    dump(&part)
    v = 99
    a.set(0, &v)
    a.insert(0, &key)
    ::printf("after set/insert:")
    dump(&a)
    a.erase(0, 2)
    ::printf("after erase:")
    dump(&a)
    a.drop()
    b.drop()
    part.drop()
    return 0
