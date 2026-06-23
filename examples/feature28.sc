# 特性 28：标签联合（tagged union / sum type）与安全解构
#
# 一、定义：def T: @( ... )
#   在裸联合 ( ... ) 前加 @ 前缀即得「带标签的安全联合」。编译器为其托管一个隐藏
#   tag，强制「先判分支再取载荷」，杜绝裸 union 取错分支的经典隐患。
#     - 无载荷变体：只写变体名           Empty
#     - 标量/具名类型载荷：变体名: 类型    Circle: f4   Node: Rect
#   多字段载荷请用具名结构体（不支持内联 {} 载荷，保持纯数据布局）。
#
# 二、构造：T.Variant(载荷)
#   有载荷：Shape.Circle(2.0)      无载荷：Shape.Empty
#
# 三、解构：case 作用于标签联合值
#   分支标签即变体名；`Variant as x` 把当前变体载荷「拷贝」为只读视图 x。
#     case s:
#         Empty:            ...
#         Circle as r:      ... 用 r ...
#         Rect as box:      ... 用 box.w / box.h ...
#   穷尽性：无 default `:` 分支时必须覆盖全部变体，否则编译报错。

#-------------- 具名结构体载荷 --------------
def Rect: { w: f4, h: f4 }

#-------------- 标签联合：无载荷 / 标量载荷 / 结构体载荷 --------------
def Shape: @( Empty, Circle: f4, Rect: Rect )

# 穷尽解构：覆盖全部三个变体
fnc area: f4, s: Shape
    case s:
        Empty:
            return 0.0
        Circle as r:
            return 3.14159 * r * r
        Rect as box:
            return box.w * box.h

#-------------- Result 风格：成功值 / 错误码 --------------
def Result: @( Ok: i4, Err: i4 )

fnc safe_div: Result, a: i4, b: i4
    if b == 0
        return Result.Err(-1)
    return Result.Ok(a / b)

# 带 default 的非穷尽解构（只关心 Ok）
fnc unwrap_or: i4, r: Result, fallback: i4
    case r:
        Ok as v:
            return v
        :
            return fallback

fnc main: i4
    var a: Shape = Shape.Circle(2.0)
    var b: Shape = Shape.Empty
    var rc: Rect = { w = 3.0, h = 4.0 }
    var c: Shape = Shape.Rect(rc)
    printf("circle area = %.2f\n", area(a))
    printf("empty area  = %.2f\n", area(b))
    printf("rect area   = %.2f\n", area(c))

    var r1: Result = safe_div(10, 2)
    var r2: Result = safe_div(10, 0)
    printf("10/2 = %d\n", unwrap_or(r1, -999))
    printf("10/0 = %d (fallback)\n", unwrap_or(r2, -999))
    return 0
