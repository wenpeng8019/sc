# 特性 7：ADT 容器结构体（def T: <C, I> {}）
# > 与 def T: ~ {} 对称的一套机制：
#   - ~        ：注入 _prev/_next 自链指针，配合内置 chain 提供"双向链表"能力
#   - <C, I>   ：把元素节点类型 I 整体注入为 T 的首个 synthetic 成员（offset 0），
#                由用户自定义容器 C 提供一套抽象数据类型（ADT）的集合实现
#
# 机制要点：
#   - T 是放入容器的元素类型（如 task）；I 是链接节点类型（如 tnode），被注入到 T 首位，
#     因此 T& 与 I& 可零偏移互相重解释（编译器在方法实参处自动 task&⟷tnode&）。
#   - C 是自定义容器（普通伪类结构体），必须具备必备成员函数：
#       insert: ret, item: I&, tag: ...   # 插入
#       remove: ret, item: I&             # 移除
#       find:   ret, out: I&&, ...        # 查找（出参回填）
#       first:  I&                        # 首元素
#       next:   I&, item: I&              # 后继
#     可选：last: I&   prev: I&, item: I&  # 反向导航
#   - 导航只经容器方法：t.first() / t.next(it)，返回 I&，用显式 `: T&` 下转回元素类型。
#   - ret：ADT 接口返回码（i4 别名）；ok：成功返回码（= 0）。
#   - base(&t)：跳过注入的 I，取 T 首个真实成员的地址（与链表 base 对称）。

#-------------- I：链接节点类型 ----------------------------------
def tnode: {
    next: tnode&
    prev: tnode&
}

#-------------- C：自定义容器（双向链表的 ADT 实现）-------------
def slist: {
    head: tnode&
    tail: tnode&

    init: fnc                            # 声明即构造：清空链表
        this->head = nil
        this->tail = nil

    insert: fnc: ret, item: tnode&, tag: i4
        if tag != 0                      # tag != 0：插入表头
            item->prev = nil
            item->next = this->head
            if this->head != nil
                this->head->prev = item
            else
                this->tail = item
            this->head = item
        else                             # tag == 0：追加表尾
            item->prev = this->tail
            item->next = nil
            if this->tail != nil
                this->tail->next = item
            else
                this->head = item
            this->tail = item
        return ok

    remove: fnc: ret, item: tnode&
        if item->prev != nil
            item->prev->next = item->next
        else
            this->head = item->next
        if item->next != nil
            item->next->prev = item->prev
        else
            this->tail = item->prev
        return ok

    find: fnc: ret, out: tnode&&, key: i4
        var p: tnode& = this->head
        while p != nil
            var t: task& = p: task&      # 节点零偏移重解释回元素 task
            if t->id != key
                p = p->next
            else
                *out = p                 # 出参回填（&found 已自动转 tnode&&）
                return ok
        return -1

    first: fnc: tnode&
        return this->head

    last: fnc: tnode&
        return this->tail

    next: fnc: tnode&, item: tnode&
        return item->next

    prev: fnc: tnode&, item: tnode&
        return item->prev
}

#-------------- T：元素类型（tnode 注入到首位）------------------
def task: <slist, tnode> {
    id: i4
}

fnc main: i4

    var lst: slist                       # 自动 init：head/tail = nil
    var a[4]: task
    var i: i4
    for i = 0; i < 4; i++
        a[i].id = (i + 1) * 10

    #-------------- insert：传 task&，自动转 tnode& ------------
    # 前三个追加表尾（tag 0），最后一个插表头（tag 1）
    lst.insert(&a[0], 0)                 # 10
    lst.insert(&a[1], 0)                 # 10 20
    lst.insert(&a[2], 0)                 # 10 20 30
    lst.insert(&a[3], 1)                 # 40 10 20 30

    #-------------- first/next：返回 tnode&，显式 : task& 下转 --
    var it: task& = lst.first(): task&
    ::printf("正向:")
    while it != nil
        ::printf(" %d", it->id)
        it = lst.next(it): task&         # 传 task&，自动转 tnode&
    ::printf("\n")                         # 40 10 20 30

    #-------------- last/prev：可选反向接口 --------------------
    var rit: task& = lst.last(): task&
    ::printf("反向:")
    while rit != nil
        ::printf(" %d", rit->id)
        rit = lst.prev(rit): task&
    ::printf("\n")                         # 30 20 10 40

    #-------------- find：&out 传 task&&，自动转 tnode&& -------
    var found: task&
    if lst.find(&found, 20) == ok
        ::printf("find 20 -> id=%d\n", found->id)

    #-------------- [] 下标糖：c[key] <==> find，命中得 I&，未命中 nil --
    var hit: task& = lst[30]: task&      # 命中：返回 tnode&，: task& 下转
    if hit != nil
        ::printf("lst[30] -> id=%d\n", hit->id)        # 30
    var miss: task& = lst[99]: task&     # 未命中：返回 nil
    if miss == nil
        ::printf("lst[99] -> nil\n")

    #-------------- remove：传 task&，自动转 tnode& ------------
    lst.remove(&a[1])                    # 移除 id=20
    var it2: task& = lst.first(): task&
    ::printf("remove 20 后:")
    while it2 != nil
        ::printf(" %d", it2->id)
        it2 = lst.next(it2): task&
    ::printf("\n")                         # 40 10 30

    #-------------- base：跳过注入的 tnode，取首个真实成员 id --
    var pid: i4& = base(&a[0]): i4&
    ::printf("base(&a[0]) -> id=%d\n", *pid)   # 10

    return 0
