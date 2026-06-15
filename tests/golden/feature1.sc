# 由 scc --emit-sc 从 AST 再生成

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

def value: (
    i: i4
    f: f4
)

def byte -> u1

fnc add_f: i4, a: i4, b: i4

fnc add1 -> add_f
    return a + b

fnc add2 -> add_f
    return a + (2 * b)

fnc dec_f: v: i4

fnc dec -> dec_f
    --v

fnc clamp: i4, v: i4, lo: i4, hi: i4
    if v < lo
        return lo
    if v > hi
        return hi
    return v

fnc area: i4, r&: rect
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h

let MAX: i4 = 100

var counter: i4 = 0

fnc main: i4
    var p: point
    p.x = 3
    p.y = 4
    var r: rect
    (r.lt.x = 0) , (r.lt.y = 0)
    (r.rb.x = 10) , (r.rb.y = 5)
    var r_ptr&: rect = &r
    var msg: = "hello sc"
    printf("%s\n", msg)
    printf("add1(3,4) = %d\n", add1(p.x, p.y))
    printf("add2(3,4) = %d\n", add2(p.x, p.y))
    printf("area = %d\n", area(&r))
    printf("clamp(42,0,10) = %d\n", clamp(42, 0, 10))
    if (p.x > 1) && (p.y < 10)
        printf("cond ok\n")
    else
        printf("cond fail\n")
    var i: i4
    for i = 0; i < 3; i++
        counter += i
    printf("counter = %d\n", counter)
    while counter > 0
        counter--
        if counter == 1
            break
    var c: color = Green
    printf("color = %d, MAX = %d\n", c, MAX)
    return 0
