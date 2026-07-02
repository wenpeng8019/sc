# 特性 37：类机制（cls / dim / object / instanceOf）—— 个人创新的「类」
#
# 不是传统 vtable + 继承，而是「单一分派器 + 全局维度选择子 + 三态应答 + 类型擦除引用」四件套：
#   1. 每个 cls 对象首部内嵌「一个」分派函数指针 _class（synthetic，复用 def 结构体全部机制）；
#   2. 所有「方法」即维度 dim，折叠进该类「唯一」分派器 T_hyper_impl 的 switch(dim_id)；
#   3. 维度名是「全局选择子」——同名 dim 跨类即同一消息 → 天然多态；
#   4. 维度恒返回三态 tril（positive 应答 / unknown 不应答 / negative 否定），
#      真正的输出经「指针出参」回填。
# object 是类型擦除引用（指向任意对象的 _class 槽），既是身份又是分派入口；
# instanceOf 为 O(1) 身份判定（比较分派器指针）。

cls Cat: {
    age: i4
    init: fnc
        this->age = 3
    # SPEAK 维度：把叫声写入出参 buf；返回 positive 表示「应答」
    dim SPEAK: tril, buf: char&, cap: i4
        ::snprintf(buf, cap, "喵")
        return positive
    dim LEGS: tril, out: i4&
        *out = 4
        return positive
}

cls Dog: {
    age: i4
    init: fnc
        this->age = 5
    # 与 Cat 同名的 SPEAK —— 同一全局选择子 SC_DIM_SPEAK，多态分派
    dim SPEAK: tril, buf: char&, cap: i4
        ::snprintf(buf, cap, "汪")
        return positive
    dim LEGS: tril, out: i4&
        *out = 4
        return positive
}

cls Fish: {
    age: i4
    init: fnc
        this->age = 1
    # Fish 不实现 SPEAK → 分派落 default 返回 unknown（不应答），仍可被探测
    dim LEGS: tril, out: i4&
        *out = 0
        return positive
}

# 含 obj_key / obj_name 字段的类：OBJ_KEY/OBJ_NAME 默认实现自动取字段值（无需手写 dim 覆盖）；
# RLT_KEY / RLT_NAME 为保留维度，默认实现「与另一同类对象比大小」，返回三态。
cls Item: {
    obj_key: i4
    obj_name[16]: char
    init: fnc
        this->obj_key = 0
}

# 全局（非函数内）cls 实例：_class 分派器指针在模块初始化序言安装（早于全局 init），
# 故全局对象同样可经 object 擦除做动态多态分派与 instanceOf。
var gDog: Dog

@fnc main: i4
    var c: Cat
    var d: Dog
    var f: Fish

    # 类型擦除：三个不同类的实例装进同一 object 数组，运行时按对象自身分派器多态分派
    var pets[3]: object
    pets[0] = (c: object)
    pets[1] = (d: object)
    pets[2] = (f: object)

    var i: i4 = 0
    while i < 3
        var sound[16]: char
        var r: tril = pets[i].SPEAK(&sound[0], 16)   # 动态接收者：(*ob)(ob, SC_DIM_SPEAK, ...)
        var legs: i4 = 0
        pets[i].LEGS(&legs)
        if r == positive
            ::printf("第%d只：%s（%d条腿）\n", i, &sound[0], legs)
        else
            ::printf("第%d只：……不出声（%d条腿）\n", i, legs)
        i = i + 1

    # instanceOf：O(1) 身份判定（*o == TypeName_hyper_impl）
    if instanceOf(pets[0], Cat)
        ::printf("pets[0] 是 Cat\n")
    if !instanceOf(pets[0], Dog)
        ::printf("pets[0] 不是 Dog\n")

    # 静态接收者直呼维度：c.OBJ_NAME(...) → Cat_hyper_impl(&c._class, SC_DIM_OBJ_NAME, ...)
    # OBJ_NAME 为保留维度，默认实现 snprintf "<类名>@<地址>"
    var nm[32]: char
    c.OBJ_NAME(&nm[0], 32)
    ::printf("c 默认名前缀：%c\n", nm[0])              # 'C'（来自 "Cat@0x...")

    # RLT_KEY / RLT_NAME：与另一同类对象比对 key/name（默认实现「直接比大小」，返回三态）。
    # Item 含 obj_key/obj_name 字段 → OBJ_KEY/OBJ_NAME 默认实现自动取字段值。
    var x: Item
    var y: Item
    x.obj_key = 10
    ::snprintf(&x.obj_name[0], 16, "apple")
    y.obj_key = 20
    ::snprintf(&y.obj_name[0], 16, "banana")
    var rk: tril = x.RLT_KEY((y: object))            # 10 < 20 → negative
    var rn: tril = x.RLT_NAME((y: object))           # "apple" < "banana" → negative
    var rk2: tril = y.RLT_KEY((x: object))           # 20 > 10 → positive
    if rk == negative
        ::printf("x.key < y.key（按 obj_key 字段比对）\n")
    if rn == negative
        ::printf("x.name < y.name（按 obj_name 字段比对）\n")
    if rk2 == positive
        ::printf("y.key > x.key\n")

    # 全局 cls 实例：经 object 擦除动态分派（验证 _class 在模块初始化已安装）
    var go: object = (gDog: object)
    var gsound[16]: char
    go.SPEAK(&gsound[0], 16)
    ::printf("全局 gDog 叫：%s\n", &gsound[0])
    if instanceOf(go, Dog)
        ::printf("全局 gDog 是 Dog\n")
    return 0
