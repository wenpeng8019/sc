# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

inc stdlib.h

def color: i1
    Red = 0
    Green
    Blue

def point: {
    x: i4
    y: i4
}

def rect: {
    lt: point
    rb: point
}

def obj: {
    id: i4
    meta: { tag: i4, flag: bool }
}

def value: (
    i: i4
    f: f4
)

def byte -> u1

let MAX: i4 = 100

var counter: i4 = 0

let grid[2][3]: i4

fnc main: i4
    var a: i4 = 42
    printf("a=%d\n", a)
    var b: i4
    printf("b=%d\n", b)
    var n: = 100
    var pi: = 3.14
    printf("n=%d pi=%.2f\n", n, pi)
    var msg:
    msg = "hello"
    printf("%s\n", msg)
    var i: i4 = 0, j: i4 = 0
    var tmp: { x: i4, y: i4 }
    (tmp.x = 1) , (tmp.y = 2)
    printf("inline var: x=%d y=%d\n", tmp.x, tmp.y)
    var pt: point = {5, 6}
    printf("pt: x=%d y=%d\n", pt.x, pt.y)
    var o: obj
    o.id = 1
    o.meta.tag = 10
    o.meta.flag = true
    printf("obj: id=%d meta.tag=%d meta.flag=%d\n", o.id, o.meta.tag, o.meta.flag)
    var np: i4& = nil
    if np == nil
        printf("np is nil\n")
    var vp: & = nil
    printf("vp=%p\n", vp)
    var pt2: point
    (pt2.x = 7) , (pt2.y = 8)
    var px: point& = &pt2
    printf("px->x=%d\n", px->x)
    var pp: point&& = &px
    printf("pp=%p\n", pp)
    var arr[3]: i4 = {10, 20, 30}
    printf("arr: %d %d %d\n", arr[0], arr[1], arr[2])
    var tab[2][3]: i4 = {{1, 2, 3}, {4, 5, 6}}
    printf("tab[0][1]=%d tab[1][2]=%d\n", tab[0][1], tab[1][2])
    var m[2][3]: i4
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            m[i][j] = ((i * 3) + j)
    var s: i4 = 0
    for i = 0; i < 2; i++
        for j = 0; j < 3; j++
            s += m[i][j]
    printf("sum = %d\n", s)
    var name[8][16]: char
    strcpy(name[0], "hi")
    printf("name0 = %s\n", name[0])
    var ok: bool = true
    var no: bool = false
    printf("ok=%d no=%d\n", ok, no)
    var mask: u4 = 0xFF00
    printf("mask=0x%x\n", mask)
    var big: u8 = 100UL
    let pi_f: f4 = 3.14f
    printf("big=%llu pi_f=%.2f\n", big, pi_f)
    var big2: i8 = 300
    var small: i4 = (big2: i4)
    printf("cast: %d\n", small)
    var buf: char& = (malloc(8): char&)
    free((buf: void&))
    var f: f8 = 3.75
    printf("cast expr: %d\n", (small + f: i4))
    var pv: void& = &tmp
    printf("paren cast: %d\n", (pv: point&)->x)
    printf("sizeof(point)=%lu\n", sizeof(point))
    printf("offsetof(point,y)=%lu\n", offsetof(point, y))
    if tmp.x == 1
        printf("one\n")
    else if tmp.x == 2
        printf("two\n")
    else
        printf("other\n")
    if (tmp.x > 0) && (tmp.y < 10)
        printf("cond ok\n")
    else
        printf("cond fail\n")
    counter = 0
    for i = 0; i < 3; i++
        counter += i
    printf("counter = %d\n", counter)
    for ; counter < 10; counter++
        printf("counter at %d\n", counter)
    while counter > 3
        counter--
        if counter == 5
            break
    i = 0
    do
        i++
    while i < 3
    printf("do-while: i=%d\n", i)
    for i = 0; i < 5; i++
        if i == 2
            continue
        printf("  i=%d\n", i)
    var code: i4 = 2
    case code:
        1, 2:
            printf("case 1 or 2\n")
        3:
            printf("case 3\n")
            through
        4:
            printf("case 3 through to 4\n")
        :
            printf("default\n")
    var cnt: i4 = 0
    again:
        cnt++
        if cnt < 2
            goto again
    printf("goto: cnt=%d\n", cnt)
    return 0
