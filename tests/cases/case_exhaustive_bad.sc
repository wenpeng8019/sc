def Shape: @( Empty, Circle: f4, Rect: f4 )

@fnc area: f4, s: Shape
    case s:
        Empty:
            return 0.0
        Circle as r:
            return 3.14 * r * r
    return 0.0
