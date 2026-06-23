# sc 语法速查（Cheatsheet）

这一份把当前已经实现的写法压缩成一组可直接照着写的模板。完整语义见 [`syntax.md`](syntax.md)。

## 1. 顶层对象

```sc
inc io.sc

def Name: base_type
    EnumItem = 0

def Name: {
    field: type
}

def Name: (
    field: type
)

def Alias -> target_type

fnc FuncType: ret_type, a:type, b:type

fnc func -> FuncType
    return a + b

fnc func:
    a:type
    -
    return a + 1

var name: type = init
let name: type = init
tls name: type = init
```

## 2. 函数体语句

```sc
return expr
if cond
    stmt
else
    stmt

while cond
    stmt

for init; cond; step
    stmt

var a: i4 = 1, b: i4 = 2
let c: i4 = 3

case abc:
    v1:
        ...
    v2:
        ...
    :
        ...

```

### 2.1 `case` 的严格缩进层级

`case` 采用三层结构，必须严格对齐：

- 第 1 层：`case expr:`
- 第 2 层：分支标签 `value:` 或空标签 `:`（default）
- 第 3 层：该分支语句体
- 支持多标签同体：`v1, v2, v3:` 表示这些标签共享同一个分支语句体

正确示例：

```sc
case code:
    1, 2:
        handle_a()
    3:
        handle_b()
        through
    4, 5:
        handle_c()
    :
        handle_default()
```

多标签同体会在代码生成时展开为多个 C 的 `case` 标签并共享同一语句块，例如 `1, 2:` 等价于 `case 1: case 2:`。

错误示例（标签和语句体错位）：

```sc
case code:
  1:
        handle_a()   # 错：标签不是 4 空格缩进
    2:
      handle_b()     # 错：语句体和同层语句未对齐
```

## 3. 成员函数与函数指针字段

```sc
def Obj: {
    value: i4
    cb: fnc: i4, x:i4, y:i4          # 无函数体 → 普通函数指针字段
    add: fnc: i4, x:i4, y:i4         # 有函数体 → 成员函数
        return this->value + x + y
}

o.cb(1, 2)      # 函数指针调用
o.add(1, 2)     # 自动注入 &o
p->add(1, 2)    # 自动注入 p
```

## 4. 类型限定符 const / volatile / restrict

限定符写在“类型一侧”（冒号之后）：`const` / `volatile` 在类型名之前，限定被指对象；
`restrict` 跟在指针 `&` 之后；用 `let` 声明则让指针本身为 const。

```sc
var a: volatile i4                 # volatile int32_t a
var reg: volatile u4&              # volatile uint32_t *reg
var p: const node&                 # const node *p          （指向 const 对象）
let q: node&                       # node *const q          （const 指针）
let r: const node&                 # const node *const r    （都 const）
let n: i4                          # const int32_t n        （只读标量）

fnc copy: dst: i4& restrict, src: const i4& restrict
    # int32_t *restrict dst, const int32_t *restrict src
    return
```

## 5. 初始化列表

```sc
var arr[3]: i4 = [10, 20, 30]      # 数组：方括号
var pt: point = {1, 2}             # 结构体：花括号（顺序）
var pt2: point = {x = 9, y = 11}   # 结构体：花括号（指定成员）
```

## 6. 常用表达式

```sc
a = b + c * d
obj.field = value
ptr->field = value
arr[i] = 1
f(a, b, c)
ok = p == nil ? false : true
var arr[3]: i4 = [10, 20, 30]     # 数组初始化列表（方括号）
var mask: u4 = 0xFF00              # 十六进制
var big: u8 = 100UL                # 字面量后缀
(p + 1: node&)->next               # 强转后取成员：需括号
var t: i4 = x: i4                  # 值转换：右值位置免括号
```
