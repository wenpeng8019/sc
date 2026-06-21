# 由 scc --emit-sc 从 AST 再生成

inc stdio.h

def Rect: {
    w: f4
    h: f4
}

def Shape: @(
    Empty
    Circle: f4
    Rect: Rect
)

fnc area: f4, s: Shape
    case s:
        Empty:
            return 0.0
        Circle as r:
            return (3.14159 * r) * r
        Rect as box:
            return box.w * box.h

def Result: @(
    Ok: i4
    Err: i4
)

fnc safe_div: Result, a: i4, b: i4
    if b == 0
        return Result.Err(-1)
    return Result.Ok(a / b)

fnc unwrap_or: i4, r: Result, fallback: i4
    case r:
        Ok as v:
            return v
        :
            return fallback

fnc main: i4
    var a: Shape = Shape.Circle(2.0)
    var b: Shape = Shape.Empty
    var rc: Rect = {w = 3.0, h = 4.0}
    var c: Shape = Shape.Rect(rc)
    printf("circle area = %.2f\n", area(a))
    printf("empty area  = %.2f\n", area(b))
    printf("rect area   = %.2f\n", area(c))
    var r1: Result = safe_div(10, 2)
    var r2: Result = safe_div(10, 0)
    printf("10/2 = %d\n", unwrap_or(r1, -999))
    printf("10/0 = %d (fallback)\n", unwrap_or(r2, -999))
    return 0
