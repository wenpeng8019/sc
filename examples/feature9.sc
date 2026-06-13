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

fnc main: i4
    var l: chain
    var t[3]: task
    var i: i4
    for i = 0; i < 3; i++
        t[i].id = i

    # 基础操作：append / push / first / last
    l.append(&t[0])
    l.append(&t[1])
    l.push(&t[2])

    var f&: task = l.first(): task&
    var b&: task = l.last(): task&
    printf("first=%d last=%d\n", f->id, b->id)

    # remove / pop
    l.remove(&t[1])
    var p&: task = l.pop(): task&
    printf("pop=%d\n", p->id)

    return 0
