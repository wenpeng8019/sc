# 特性 6：链表结构体（def T: ~）与 prev/next 上下文关键字

# chain 为 op.sc 默认导入机制，无需 inc 即可使用

#-------------- def T: ~ —— 链表标记 --------------------------
# ~ 标记使转 C 时在成员首位自动注入 void *_prev, *_next
# 此后可通过 it->prev / it->next 上下文关键字访问前驱/后继

def task: ~ {
    id: i4
}

def node: ~ {
    id: i4
    name[8]: char
    pos: point
    score: f8
}

def point: {
    x: i4
    y: i4
}

#-------------- chain 内置双向链表 -----------------------------
# chain（inc adt.sc）：head 为首元素；首元素 _prev = 尾元素（rear），尾元素 _next = nil
# 同一 chain 只放同种结构体；不拥有元素：remove/pop/cut 不释放元素本身

fnc dump: tag: char&, l: chain&
    ::printf("%s:", tag)
    var it: task& = l->first(): task&
    while it != nil
        ::printf(" %d", it->id)
        it = it->next                   # 上下文关键字：等价 next(it) 内置函数
    ::printf("\n")

fnc main: i4

    #-------------- chain 内置操作 ----------------------------

    var l: chain
    var t[6]: task
    var i: i4
    for i = 0; i < 6; i++
        t[i].id = i

    l.append(&t[2])
    l.append(&t[3])
    l.push(&t[1])
    dump("append/push", &l)            # 1 2 3

    l.before(&t[1], &t[0])
    l.after(&t[3], &t[4])
    dump("before/after", &l)           # 0 1 2 3 4

    var f: task& = l.first(): task&
    var b: task& = l.last(): task&
    # 边界安全：head 无前驱 → prev 返回 nil；尾元素（rear）经 last() 获取
    ::printf("first=%d last=%d head_prev_nil=%d\n", f->id, b->id, f->prev == nil)

    l.remove(&t[2])
    var p: task& = l.pop(): task&
    ::printf("pop=%d\n", p->id)          # 0
    dump("remove/pop", &l)             # 1 3 4

    l.revert()
    dump("revert", &l)                 # 4 3 1

    var seg: chain
    l.cut(&t[3], &t[1], &seg)
    dump("cut-out", &seg)              # 3 1
    dump("cut-rest", &l)               # 4

    seg.append_to(&l)
    dump("append_to", &l)              # 4 3 1
    ::printf("seg empty=%d\n", seg.first() == nil)
    l.cut(&t[3], &t[1], &seg)
    seg.push_to(&l)
    dump("push_to", &l)                # 3 1 4

    # 堆元素同样可入链
    var h: task& = task()
    h->id = 9
    l.append(h)
    dump("heap", &l)                   # 3 1 4 9
    l.remove(h)
    ::free(h: void&)

    #-------------- prev/next 上下文关键字遍历 ----------------

    var l2: chain
    var n[3]: node
    for i = 0; i < 3; i++
        n[i].id = i
    l2.append(&n[0])
    l2.append(&n[1])
    l2.append(&n[2])

    var it: node& = l2.first(): node&
    ::printf("正向:")
    while it != nil                    # 等价 it->_next != nil
        ::printf(" %d", it->id)
        it = it->next                  # 上下文关键字：后移
    ::printf("\n")

    # 反向遍历（prev 上下文关键字）
    var rear: node& = l2.last(): node&
    ::printf("反向:")
    while rear != nil
        ::printf(" %d", rear->id)
        rear = rear->prev              # 上下文关键字：前移
    ::printf("\n")

    return 0
