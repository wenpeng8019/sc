# 类型限定符回归：const / volatile / restrict 与 let/var 的正交组合。
# 规则：类型侧 const/volatile 限定「指向对象/对象本身」；let/var 决定「指针本身」是否 const；
#       restrict 尾置（& 之后），约束指针无别名。仅做 emit-c / emit-sc 双快照。
inc stdint.h

@def node: {
    v: i4
}

# restrict 形参：互不别名的源/目的指针（src 指向只读）
@fnc copy: dst: i4& restrict, src: const i4& restrict
    *dst = *src

@fnc main: i4
    var a: volatile i4 = 5            # volatile int32_t a
    var reg: volatile u4& = nil       # volatile uint32_t *reg（MMIO 寄存器）
    var x: const volatile u4& = nil   # const volatile uint32_t *x
    var p: const node& = nil          # const node *p（指针可改，指向只读）
    let q: node& = nil                # node *const q（指针只读，指向可改）
    let r: const node& = nil          # const node *const r（皆只读）
    let n: i4 = 7                     # const int32_t n（标量常量）
    var src: i4 = 11
    var dst: i4 = 0
    copy(&dst, &src)
    return dst + n
