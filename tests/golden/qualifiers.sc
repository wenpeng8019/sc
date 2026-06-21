# 由 scc --emit-sc 从 AST 再生成

inc stdint.h

@def node: {
    v: i4
}

@fnc copy: dst: i4& restrict, src: const i4& restrict
    *dst = *src

@fnc main: i4
    var a: volatile i4 = 5
    var reg: volatile u4& = nil
    var x: const volatile u4& = nil
    var p: const node& = nil
    let q: node& = nil
    let r: const node& = nil
    let n: i4 = 7
    var src: i4 = 11
    var dst: i4 = 0
    copy(&dst, &src)
    return dst + n
