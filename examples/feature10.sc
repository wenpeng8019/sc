# 特性 10：内置 ADT
# > builtins/adt 模块：实现
#
#   - 成员函数：结构体内直接实现（签名字段 + 缩进函数体）
#   - 方法声明：fnc T::m 仅声明形态（实现在 C 侧，见 adt.sc）
#   - 栈构造：var x: T 声明即自动调用 T 的 init（局部、无初值、非指针）
#   - 堆构造：T() 类型伪调用 → malloc + 字段默认值/清零 + init
#   - drop 析构：手动调用（命名保留，未来支持自动插入）；堆对象再 free
#   - 调用糖：值接收者 o.m(...) / 指针接收者 p->m(...)

inc adt.sc

# 成员函数：结构体内直接实现，函数体内用 this 访问接收者
def counter: {
    n: i4
    init: fnc
        this->n = 100
    add: fnc: i4, k: i4
        this->n = this->n + k
        return this->n
}

fnc str_cmp -> list_cmp
    return strcmp(a: char&, b: char&)    # 裸强转：实参位置免括号

fnc main: i4
    # 声明即构造：自动调用 counter_init/string_init/list_init
    var c: counter
    printf("counter: init=%d add(5)=%d\n", c.n, c.add(5))

    # string：动态字符串（堆专属：string() 构造，drop 释放缓冲+块体）
    var s: string& = string()
    s->append("Hello")
    s->append(", sc!")
    printf("s=%s len=%llu\n", s->cstr(), s->len())
    printf("find \"sc\"=%lld starts_with(Hello)=%d\n",
           s->find("sc", 0), s->starts_with("Hello"))
    var part: string& = string()
    s->slice(-3, -1, part)              # 负索引切片
    printf("slice(-3,-1)=%s\n", part->cstr())
    s->upper()
    printf("upper=%s\n", s->cstr())

    # list：动态指针数组（元素 v&，不拥有元素）
    var l: list
    l.push("banana")
    l.push("apple")
    l.push("cherry")
    l.sort(str_cmp)
    var i: u8 = 0
    for i = 0; i < l.len(); i++
        printf("list[%llu]=%s\n", i, l.get(i): char&)    # 裸强转作实参

    # 析构：手动 drop（指针接收者用 ->；string 堆专属，drop 连块体一并释放）
    var lp: list& = &l
    lp->drop()
    part->drop()
    s->drop()

    # 堆构造：string() 伪调用 → sc_alloc + init，drop 释放缓冲并 sc_free 块体
    var hs: string& = string()
    hs->append("on the heap")
    printf("heap: %s\n", hs->cstr())
    hs->drop()
    return 0
