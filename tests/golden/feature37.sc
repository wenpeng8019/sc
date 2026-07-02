# 由 scc --emit-sc 从 AST 再生成

cls Cat: {
    age: i4
    init: fnc
        this->age = 3
    SPEAK: fnc: tril, buf: char&, cap: i4
        ::snprintf(buf, cap, "喵")
        return positive
    LEGS: fnc: tril, out: i4&
        *out = 4
        return positive
}

cls Dog: {
    age: i4
    init: fnc
        this->age = 5
    SPEAK: fnc: tril, buf: char&, cap: i4
        ::snprintf(buf, cap, "汪")
        return positive
    LEGS: fnc: tril, out: i4&
        *out = 4
        return positive
}

cls Fish: {
    age: i4
    init: fnc
        this->age = 1
    LEGS: fnc: tril, out: i4&
        *out = 0
        return positive
}

cls Item: {
    obj_key: i4
    obj_name[16]: char
    init: fnc
        this->obj_key = 0
}

var gDog: Dog

@fnc main: i4
    var c: Cat
    var d: Dog
    var f: Fish
    var pets[3]: object
    pets[0] = (c: object)
    pets[1] = (d: object)
    pets[2] = (f: object)
    var i: i4 = 0
    while i < 3
        var sound[16]: char
        var r: tril = pets[i].SPEAK(&sound[0], 16)
        var legs: i4 = 0
        pets[i].LEGS(&legs)
        if r == positive
            ::printf("第%d只：%s（%d条腿）\n", i, &sound[0], legs)
        else
            ::printf("第%d只：……不出声（%d条腿）\n", i, legs)
        i = (i + 1)
    if instanceOf(pets[0], Cat)
        ::printf("pets[0] 是 Cat\n")
    if !instanceOf(pets[0], Dog)
        ::printf("pets[0] 不是 Dog\n")
    var nm[32]: char
    c.OBJ_NAME(&nm[0], 32)
    ::printf("c 默认名前缀：%c\n", nm[0])
    var x: Item
    var y: Item
    x.obj_key = 10
    ::snprintf(&x.obj_name[0], 16, "apple")
    y.obj_key = 20
    ::snprintf(&y.obj_name[0], 16, "banana")
    var rk: tril = x.RLT_KEY((y: object))
    var rn: tril = x.RLT_NAME((y: object))
    var rk2: tril = y.RLT_KEY((x: object))
    if rk == negative
        ::printf("x.key < y.key（按 obj_key 字段比对）\n")
    if rn == negative
        ::printf("x.name < y.name（按 obj_name 字段比对）\n")
    if rk2 == positive
        ::printf("y.key > x.key\n")
    var go: object = (gDog: object)
    var gsound[16]: char
    go.SPEAK(&gsound[0], 16)
    ::printf("全局 gDog 叫：%s\n", &gsound[0])
    if instanceOf(go, Dog)
        ::printf("全局 gDog 是 Dog\n")
    return 0
