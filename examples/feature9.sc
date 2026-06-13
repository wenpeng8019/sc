# 特性 9：链表结构体（def T: ~）与内置 chain 双向链表
#   - def T: ~ {}：链表标记，转 C 在成员首位注入 void *_prev, *_next
#   - chain（inc adt.sc）：head 为首元素；首元素 _prev = 尾元素（rear），尾元素 _next = nil
#   - base(o)、prev(o)、next(o)：内置导航函数，返回 void*；支持强制类型转换获得类型化结果
#   - 不拥有元素：remove/pop/cut 不释放元素本身
inc stdio.h
inc stdlib.h
inc adt.sc

def task: ~ {
    id: i4
}

# 顺序打印整条链（正向遍历：尾元素 next() 为 nil）
fnc dump: tag&: char, l&: chain
    printf("%s:", tag)
    var it&: task = l->first(): task&
    while it != nil
        printf(" %d", it->id)
        it = (next(it): task&)
    printf("\n")

fnc main: i4
    var l: chain
    var t[6]: task
    var i: i4
    for i = 0; i < 6; i++
        t[i].id = i

    # append 队尾 / push 队首
    l.append(&t[2])
    l.append(&t[3])
    l.push(&t[1])
    dump("append/push", &l)            # 1 2 3

    # before / after 定点插入
    l.before(&t[1], &t[0])
    l.after(&t[3], &t[4])
    dump("before/after", &l)           # 0 1 2 3 4

    # first / last（首元素 prev() 即 rear）
    var f&: task = l.first(): task&
    var b&: task = l.last(): task&
    var r&: task = (prev(f): task&)
    printf("first=%d last=%d rear=%d\n", f->id, b->id, r->id)

    # remove 中间元素 / pop 队首
    l.remove(&t[2])
    var p&: task = l.pop(): task&
    printf("pop=%d\n", p->id)          # 0
    dump("remove/pop", &l)             # 1 3 4

    # revert 首尾翻转
    l.revert()
    dump("revert", &l)                 # 4 3 1

    # cut 截取 [3..1] 段为新链
    var seg: chain
    l.cut(&t[3], &t[1], &seg)
    dump("cut-out", &seg)              # 3 1
    dump("cut-rest", &l)               # 4

    # append_to / push_to 整链拼接（自身清空）
    seg.append_to(&l)
    dump("append_to", &l)              # 4 3 1
    printf("seg empty=%d\n", seg.first() == nil)
    l.cut(&t[3], &t[1], &seg)
    seg.push_to(&l)
    dump("push_to", &l)                # 3 1 4

    # 堆元素同样可入链
    var h&: task = task()
    h->id = 9
    l.append(h)
    dump("heap", &l)                   # 3 1 4 9
    l.remove(h)
    free(h: void&)
    return 0
