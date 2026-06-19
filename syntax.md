
## sc 语言手册

sc 是一门基于 C 的结构化语言。它不取代 C，而是作为 C 的前台语言与 C 共生：
sc 专注更高层的语言逻辑特性，低级的平台与设备能力依然交给 C 完成（经 `inc` 互操）。
sc 语言自身的设计理念是“程序即结构”：顶层程序由 `inc` / `def` / `fnc` / `rpc` / `let` / `var` / `tls` / 这些结构对象组成，语法尽量和 C 对齐，但在类型、方法和模块上提供更高层的表达。

## 1. 语言原则

- 程序是一个由结构对象组成的树。
- 顶层对象包括：类型、变量、常量、线程局部存储、函数、远程调用、导入。
- 结构对象本身也是可递归嵌套的子节点。
- 缩进表达层级，换行表达语义边界，逗号表达同层并列项。
- 空格不参与语法。

## 2. 顶层语句

顶层目前支持：

- `def`：定义类型、枚举、结构体、联合体、类型别名。
- `var`：定义变量。
- `let`：定义常量。
- `tls`：定义线程局部变量（static 存储期，见 9.1）。
- `fnc`：定义函数、函数类型、方法。
- `rpc`：定义伪形参函数（参数/返回值展开为同名结构体，见 8.8）。
- `inc`：引入头文件或 sc 模块。
- `add`：把实现文件（`.c`）或库文件（`.a`/`.so`/`.o`）添加进工程，参与编译/链接。
- `@` 前缀：标记导出对象，供 `--emit-c` 生成头文件声明。

### 顶层示例

```sc
inc stdio.h
inc io.sc
inc adt.sc

@def Point: {
    x: i4
    y: i4
}

@var total: i4 = 0
@let limit: i4 = 100

@fnc add: i4, a:i4, b:i4
    return a + b
```

## 3. 基础类型

当前内置类型如下：

```sc
i1  -> int8_t
i2  -> int16_t
i4  -> int32_t
i8  -> int64_t
u1  -> uint8_t
u2  -> uint16_t
u4  -> uint32_t
u8  -> uint64_t
f4  -> float
f8  -> double
bool -> uint8_t
char -> char
ret  -> int32_t
```

其中 `ret` 是 `i4` 的语义别名，用作 ADT 接口返回码（`ok`=0 成功，非 0
失败）。此外还有内置字面量常量：

- `true`
- `false`
- `nil`
- `ok`（值为 `0`）

其中 `bool` 是布尔类型（u1 的语义别名），`char` 用于与 C 字符串
字面量/接口互操作（`s: char&` 即 `char *s`，区别于 i1/u1：C 中
char/signed char/unsigned char 是三个不同类型），`nil` 用于空指针/空值判断。
此外，这里没有 void 类型：函数省略返回类型即无返回值（见 §8），void 指针用
省略类型名的裸指针表示（字段 `p: &`，返回类型裸 `&`）。

## 4. 指针与数组

sc 的指针标记 `&` 写在**类型一侧**（冒号之后），数组标记 `[]`
写在**名字一侧**（冒号之前）：

```sc
name: type        # 普通对象
name: type&       # 指针
name: type&&      # 指针的指针
name: &           # 裸指针（void*，省略类型名）
name[]: type      # 数组
name[x][y]: type  # 多维数组
name[x][y]: type& # 多维指针数组
```

规则：

- `&` 写在类型名之后，可叠加表示更高层级的指针（`type&&` 即二级指针）。
- 省略类型名的裸 `&` / `&&` 即 `void*` / `void**`。
- `[]` 写在名字之后，可叠加表示多维数组。
- `name: type&` 这类写法表示“变量名 + 显式类型 + 指针元类型”。
- 强制类型转换的指针也写在类型侧：`(p: type&)`、`(p: type&&)`（见 §10）。


## 5. 导入与加入

### 导入头文件

```sc
inc stdio.h
inc "my.h"
```

对应 C 的：

- `inc stdio.h` -> `#include <stdio.h>`
- `inc "my.h"` -> `#include "my.h"`

### 导入 sc 模块

```sc
inc io.sc
```

当前采用“模块单元编译 + 链接”模型：

- 每个 `.sc` 文件独立转成 C 单元并编译为对象文件。
- `inc x.sc` 表示模块依赖，C 侧通过模块头文件连接接口，不做源码文本展开。
  同时依赖模块的 `@` 导出声明会被合并进导入方的符号表，
  使跨模块的方法调用糖、声明即构造等语法糖生效。
- scc 在运行模式下会按依赖图编译并链接多个单元。
- 仓库根下 `builtins/` 作为内置模块搜索路径；`inc x.sc` 会依次尝试
  `builtins/x.sc` 与子项目形态 `builtins/x/x.sc`（如 adt），
  也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。
- `builtins/op.sc` 是**默认导入**模块（无需 `inc`），声明基础类型上的设备
  操作数通用指令（`operand`），透传为 `platform.h` 的 `sc_<op>` 宏，详见 8.9。
- 内置模块参考手册见 `builtins/REFERENCE.md`。

### 加入 C 实现或库文件

`inc` 解决「接口声明」的引入，但对于**由 C 实现的接口**（`fnc name::` 形态，
声明在 sc、实现在 C 侧），其对应的 `.c` 文件此前没有并入工程的机制；
自定义库也只能靠构建脚本手写 `-l` / 路径链接。`add` 填补这两点：

```sc
add impl.c           # C 实现源文件：现场编译为 .o 并链接
add libfoo.a         # 静态库：直接参与链接
add libbar.so        # 动态库：直接参与链接
add prebuilt.o       # 预编译对象：直接参与链接
```

规则：

- 路径相对**声明 `add` 的 `.sc` 文件所在目录**解析（也支持绝对路径）。
- 按扩展名分流：`.c`/`.cpp`/`.cc`/`.cxx` 现场编译为对象文件后链接；
  `.o`/`.a`/`.so`/`.dylib` 直接参与链接；其它类型报错。
- 文件不存在直接报错；同一文件被多个模块或多次 `add` 时按规范化路径去重。
- `add` 是纯构建指令，**不产生 C 输出**，也**不支持 `@` 导出**。
- 凡是被依赖图（经 `inc` 链）拉入的模块，其 `add` 指令都会被收集生效——
  因此一个声明 `::` 接口的模块只要自带 `add impl.c`，使用方 `inc` 它即可
  自动带上实现，无需关心链接细节。
- 面向**自定义实现与库文件**；跨平台**系统库**仍走编译选项机制
  （`-l` / `SCC_LIBS` / 配置键 `libs`，见下）。

搭配 `fnc ::` 接口的典型用法：

```sc
# mymath.sc —— 声明 C 实现的接口 + 自带实现
add mymath_impl.c
@fnc square:: i4, x: i4
```

```c
/* mymath_impl.c */
#include <stdint.h>
int32_t square(int32_t x) { return x * x; }
```

```sc
# main.sc —— 仅 inc 即可，实现随模块自动链接
inc stdio.h
inc mymath.sc

fnc main: i4
    printf("%d\n", square(9))   # 81
    return 0
```

### 无预处理器设计与 C 头文件互操作

sc **有意不提供**预定义宏、`#define` 宏定义、`#if/#ifdef` 条件编译这类预处理能力。
设计目标是：sc 源码表面应该像脚本语言一样**平台无关**，不出现平台分支。

平台适配不是不要，而是下沉到 C 层完成：把 `#ifdef` 判断写在 C 头文件里，
sc 通过 `inc` 导入后直接使用适配后的结果：

```c
/* platform.h —— 平台适配留在 C 头文件中 */
#if defined(__APPLE__)
#define OS_NAME "macos"
#define OS_ID   1
#elif defined(_WIN32)
#define OS_NAME "windows"
#define OS_ID   2
#else
#define OS_NAME "linux"
#define OS_ID   3
#endif
#define SQUARE(x) ((x) * (x))
```

```sc
inc stdio.h
inc limits.h
inc "platform.h"

fnc main: i4
    printf("os: %s id=%d\n", OS_NAME, OS_ID)   # 宏常量直接当标识符用
    printf("int max: %d\n", INT_MAX)            # 标准头中的宏常量
    printf("square: %d\n", SQUARE(9))           # 函数式宏直接当函数调用
    return 0
```

互操作规则：

- C 宏常量（`INT_MAX`、自定义 `OS_NAME` 等）直接作为标识符使用，原样透传到生成的 C 代码。
- C 函数式宏（`SQUARE(x)`、`va_start` 等）直接按函数调用语法使用。
- `#if/#ifdef` 等条件编译只存在于 C 头文件中，sc 源码只看到适配后的最终结果。
- 运行模式下，每个模块单元编译时会自动附加源 `.sc` 文件所在目录为头文件搜索路径，
  因此 `inc "platform.h"` 能找到与源码同目录的本地头。

> 关于编译选项、链接库、`.sc` 配置文件等工具链配置
> 详见 [compiler.md](compiler.md) §4「工具链配置」。

## 6. 导出

`@` 前缀表示导出对象，`--emit-c` 时会额外生成头文件声明；
`@inc` 也生效，会把该 include 同步输出到生成的 `.h`。

```sc
@def Point: {
    x: i4
    y: i4
}

@var total: i4

@fnc add: i4, a:i4, b:i4
    return a + b

@inc stdio.h
```

导出规则：

- `@def` -> 生成类型声明。
- `@var` / `@let` -> 生成外部变量声明。
- `@fnc` -> 生成函数原型。

## 7. 类型定义

### 7.1 枚举

```sc
def color: i1
    Red = 0
    Green
    Blue
```

枚举支持显式赋值，也支持自动递增。

### 7.2 结构体

```sc
def point: {
    x: i4
    y: i4
}
```

单行写法也支持：

```sc
def rect: { lt: point, rb: point }
```

### 7.3 联合体

```sc
def value: (
    i: i4
    f: f4
)
```

### 7.4 类型别名

```sc
def byte -> u1
```

### 7.5 内联类型

变量、字段都可以直接写内联结构/联合类型：

```sc
var tmp: {
    x: i4
    y: i4
}
```

### 7.6 链表结构体

结构体名后写 `~` 标记，使该类型对象具备双向链表链接能力：

```sc
def task: ~ {
    id: i4
}
```

转 C 时编译器在成员**首部**注入两个隐藏指针成员 `_prev` / `_next`（类型为
`void*`）。它们是真实字段，可直接读写遍历，但不可在结构体内显式定义，
`--emit-sc` 也不会输出。`~` 仅支持结构体 `{}`，不支持联合体/枚举/别名。

链表结构体配合内置双向链表 `chain` 使用。`chain` 是 op.sc 的默认导入机制
（无需 `inc`，其 C 运行时由 builtins/op.h + op_impl.c 自动提供），详见
builtins/REFERENCE.md：

```sc
var l: chain
var t: task
l.append(&t)                    # 编译器自动注入 _prev 偏移
var it: task& = l->first(): task&
while it != nil
    printf("%d\n", it->id)
    it = it->next               # 尾元素 next 为 nil
```

成员访问位提供上下文关键字 `prev` / `next`：当基址是链表结构体时，
`o.prev` / `p->next` 等价 `_prev` / `_next`（转 C 时映射）。它们仅在成员
访问位生效，不是保留字：普通结构体仍可定义名为 `prev`/`next` 的字段；
链表结构体内则禁止显式定义 `prev`/`next`/`_prev`/`_next`。

约定：`head` 指向首元素；首元素的 `prev` 指向尾元素（即 rear 指针）；
尾元素的 `next` 为 `nil`。同一 `chain` 只能存放同一种结构体；chain 不拥有
元素，移除操作不释放元素内存。

内置伪函数 `base(o)`：返回对象**首个真实成员**的地址（跳过前置注入的隐藏
成员），用于在已知节点指针时取回业务数据首址。`base(o: T&)` 形式则把节点
首址直接重解释为 `T*`（零偏移）。

### 7.7 容器结构体

与 `~` 对称的另一套机制：`~` 提供内置的 prev/next 链接，`<C, I>` 则让元素
绑定到一套**自定义抽象数据类型（ADT）集合实现**。

```sc
def task: <slist, tnode> {       # T=元素类型；C=容器类型；I=链接节点类型
    id: i4
}
```

- `I`（如 `tnode`）被整体注入为 `T` 的**首个隐藏成员** `_adt`（offset 0）。
  因此 `T&` 与 `I&` 可零偏移互相重解释。`_adt` 不可显式定义，`--emit-sc` 也
  不输出。`<C, I>` 仅支持结构体 `{}`。
- `C`（如 `slist`）是用户自定义容器（普通伪类结构体），必须具备必备成员
  函数（接收者为容器自身）：

  | 成员函数 | 签名 | 说明 |
  |----------|------|------|
  | `insert` | `ret, item: I&, tag: ...` | 插入元素 |
  | `remove` | `ret, item: I&` | 移除元素 |
  | `find`   | `ret, out: I&&, ...` | 查找（出参回填节点） |
  | `first`  | `I&` | 首元素 |
  | `next`   | `I&, item: I&` | 后继 |
  | `last`   | `I&`（可选） | 尾元素 |
  | `prev`   | `I&, item: I&`（可选） | 前驱 |

  容器成员函数既可用 sc 函数体实现，也可用 `fnc m::` 交由 C 实现。

导航只经容器方法。`first` / `next` / `last` / `prev` 返回 `I&`，用显式
`: T&` 下转回元素类型（零偏移重解释）：

```sc
var lst: slist
var a[4]: task
lst.insert(&a[0], 0)             # 传 task&，编译器自动转 tnode&
var it: task& = lst.first(): task&
while it != nil
    printf("%d\n", it->id)
    it = lst.next(it): task&     # 实参 task& 自动转 tnode&，返回值 : task& 下转

var found: task&
if lst.find(&found, 20) == ok    # &found（task&&）自动转 tnode&&
    printf("%d\n", found->id)
```

**实参自动转换**：当容器成员函数形参声明为 `I&`（或出参 `I&&`），而实参是
元素 `T&`（或 `T&&`）时，编译器自动作零偏移重解释（`I` 注入在 `T` 首位）。
返回值方向则用显式 `: T&` 强转。

**`ret` 类型与 `ok` 字面量**：`ret` 是 `i4` 的语义别名，作 ADT 接口返回码；
`ok` 是值为 `0` 的成功返回码（与 `true`/`false`/`nil` 同类的内置字面量）。

**`base(&t)`**：跳过注入的 `I`，返回 `T` 首个真实成员的地址（与链表 `base`
对称）。

## 8. 函数、函数类型、函数指针和方法

### 8.1 函数类型

`fnc` 后面直接跟签名时，表示函数类型：

```sc
fnc binop_t: i4, a:i4, b:i4
```

这类类型可以用于变量、字段和常量声明。

### 8.2 普通函数

```sc
fnc add -> binop_t
    return a + b
```

或者直接定义签名和函数体：

```sc
fnc area: i4
    r: rect&
    -
    var w: i4 = r->rb.x - r->lt.x
    var h: i4 = r->rb.y - r->lt.y
    return w * h
```

说明：

- `->` 表示实现一个已命名的函数类型或别名。
- 函数体如果和参数列表分成两段，使用单独一行 `-` 分隔。
- 单行参数定义和多行参数定义都支持。
- **省略返回类型即无返回值**（void）：首项是否参数由“是否带内嵌
  冒号”区分，如 `fnc add: i4, a:i4, b:i4` 返回 i4，而
  `fnc show: a:i4, b:i4` 无返回值。无返回值且无参数时 `:` 可一并
  省略（如 `fnc tick`）。无返回值函数不能 `return 表达式`。
- void 指针返回类型写裸 `&`（如 `@fnc list::pop: &`）。

### 8.3 方法（成员函数）

成员函数直接在结构体定义内部实现：写法与函数签名字段一致，随后的缩进块即函数体。

```sc
def obj: {
    v: i4
    cb: fnc: i4, x:i4           # 无函数体, fnc 后置 → 普通函数指针字段（见 8.4）
    fnc add: i4, x:i4, y:i4     # 有函数体, fnc 前置 → 成员函数
        return this->v + x + y
}
```

这是一个 C 语言的语法糖
成员函数会被规范化成一个带接收者的 C 函数（修饰名 `obj_add`，首参
`obj *_this`），函数体内用 `this` 访问接收者，调用时自动注入：

```sc
var o: obj
o.add(1, 2)     # → obj_add(&o, 1, 2)
var p: obj& = &o
p->add(1, 2)    # → obj_add(p, 1, 2)
```

布局（`:` 方法不占字段、不进结构体内存）
限制：成员函数只能在顶层 `{}` 结构体内实现（联合体 `()` 括号内
不产生缩进层级，局部 def 不支持）；不能带初值。

#### `::` —— 由 C 实现的接口

函数/方法名后紧跟 `::`（无函数体）表示**该函数/方法由 C 侧实现**：sc 只
声明接口，生成 `extern` 原型与调用糖，函数体在配套的 C 文件里提供。
这是 sc 对接手写 C 实现的通道——结构体本身保持纯数据

```sc
@def string: {
    data: char&                   # 纯数据字段
    size: u8
    cap: u8
    fnc len:: u8                  # C 实现：uint64_t string_len(string*)
    fnc append:: bool, s: char&   # C 实现：bool string_append(string*, char*)
    fnc clear::                   # C 实现：无返回、无额外参数
}
```
布局（`::` 方法不占字段、不进结构体内存）

要点：
- `::` 方法**不能有函数体**（有体即报错）；如需 sc 实现请用普通
  `fnc name: 签名` + 缩进体形态。
- 调用糖与 sc 实现成员一致：`s.len()` → `string_len(&s)`，
  `p->append("x")` → `string_append(p, "x")`，首参自动注入接收者。

#### 构造与析构：init / drop

两个保留方法名承担构造/析构语义：

- `init`（无参）：**声明即构造**。函数内 `var x: T` 且无显式初值、
  非指针非数组时，转 C 会自动插入 `T_init(&x)`。
  全局变量不自动构造（C 静态初始化限制），需手动调用。
- `drop`：析构，当前需**手动调用** `x.drop()`（命名保留，
  未来支持作用域结束自动插入）。

```sc
fnc main: i4
    var s: string      # 自动 string_init(&s)
    s.append("hi")
    s.drop()           # 手动释放
    return 0
```

#### 堆构造：T() 类型伪调用

堆对象的创建无法从数据流追踪（malloc 只是普通调用），所以用显式语法
让“创建”成为语法事件：结构体类型名作无参调用 `T()`，返回 `T&`：

```sc
var p: string& = string()   # malloc + 字段默认值/清零 + string_init(p)
p->append("heap")
p->drop()                   # 手动析构
free(p)                     # 再释放内存（drop 不含 free）
```

转 C 时生成 `static inline T *T__new(void)` 辅助函数：malloc 失败返回
nil；有字段默认值用默认值初始化，否则清零；类型有无参 `init` 则调用。
同名局部/全局变量会遮蔽该糖（按普通调用处理）。

### 8.4 函数指针字段

结构体字段可以声明为普通函数指针（函数签名字段不带函数体）：

```sc
def obj: {
    cb: fnc: i4, x:i4, y:i4
    v: i4
}
```

- `cb: fnc: ...` 是普通函数指针，不隐式注入接收者；需要接收者
  时显式声明参数（如 `cb: fnc: i4, o: obj&, x:i4`）并显式传入。
- 函数指针字段默认为 nil，支持与 `nil` 比较。

### 8.5 匿名函数字面量（伪闭包）

可以在表达式位置写一个匿名函数，把它赋给函数指针变量/字段，或作为
实参传入。语法是 `fnc: 返回类型, 参数...` 后接缩进函数体（与具名
函数一样可用单独的 `-` 行分隔签名与函数体）：

```sc
var cb: fnc: i4, x: i4
cb = fnc: i4, x: i4
    return x * x
printf("cb(7)=%d\n", cb(7))      # 49

o.func2 = fnc: i4, a: i4, b: i4
    return a + b
```

要点：

- 这是**不捕获外层变量**的“伪闭包”：函数体内不能引用定义处的局部
  变量（只能用参数、全局符号）。
- 转 C 时提升为顶层 `static` 函数（生成名形如 `sc__lambda_N`），
  字面量表达式处替换为该函数名 —— 即一个普通函数指针，零运行时开销。
- 签名需与目标函数指针类型一致；返回类型可省略（无返回值）。

### 8.6 调用约定

```sc
o.cb(&o, 1)  # 普通函数指针调用：接收者（如需）显式传入
o.add(1, 2)  # 成员函数：自动注入 _this 指针（传入 &o）
p->add(1, 2) # 成员函数：自动注入 _this 指针（传入 p）
```

#### 默认补 0 机制

调用实参少于形参个数时，剩余参数自动以“零值”补全（类似 C++
默认参数，但仅支持零值）：指针/数组/函数指针补 `nil`，按值
聚合补 `(T){0}`，标量补 `0`。适用于普通函数、成员函数、函数
指针字段/变量、rpc 及 run 语句：

```sc
fnc add3: i4, a:i4, b:i4, c:i4
    return a + b + c

add3(7)        # → add3(7, 0, 0)
o.add(1)       # → obj_add(&o, 1, 0)
run work(&c)   # rpc 剩余参数清零
```

实参个数超出形参个数仍报错（可变参数函数除外）。

### 8.7 可变参数函数

在参数列表末尾加 `...` 声明可变参数函数：

```sc
fnc my_printf: fmt: u1&, ...
    var ap: va_list
    va_start(ap, fmt)
    vprintf(fmt, ap)
    va_end(ap)

# 可变参数函数类型
fnc printer_t: fmt: u1&, ...

# 可变参数函数指针字段
def handler: {
    log: fnc: fmt: u1&, ...
}
```

约束：

- `...` 只能出现在参数列表末尾，且前面至少有一个具名参数。
- `va_list`、`va_start`、`va_arg`、`va_end` 直接透传 C，按 C 惯用法使用。
- `stdarg.h` 已纳入默认包含头，无需 `inc stdarg.h`。

### 8.8 伪形参函数 rpc

`rpc` 是与 `fnc` 同级的语法糖：形式与 `fnc` 完全一致，但内核（转 C）会把
参数和返回值展开为一个**同名结构体**，实际函数则是以该结构体为唯一参数的
`void name_rpc(struct name*)`。这使得调用参数天然可打包、可转发——适合消息
派发、跨进程/跨机 RPC 等场景，而调用侧写法与普通函数没有任何区别。

```sc
rpc add: i4, a: i4, b: i4
    return a + b

fnc main: i4
    var r: i4 = add(3, 4)    # 调用形式与 fnc 完全一致
    return 0
```

展开为 C 的“三件套”：

```c
struct add {            /* 1. 同名参数结构体 */
    int32_t _;          /*    返回槽：首个默认成员，C 侧用 _ 访问 */
    int32_t a;
    int32_t b;
};
void add_rpc(struct add *_p);            /* 2. 实际函数 */
static inline int32_t add(int32_t a, int32_t b) {  /* 3. 调用包装 */
    struct add _p = {0};
    _p.a = a; _p.b = b;
    add_rpc(&_p);                        /* 装填 → 执行 → 取返回槽 */
    return _p._;
}
```

函数体内对参数的引用自动改写为结构体成员访问（`a` → `_p->a`），
`return 表达式` 改写为写入返回槽（`_p->_ = 表达式; return;`）。

结构体仅用 C 的 struct tag（不 typedef）：tag 与函数名在 C 中分属不同命名
空间，二者可同名共存。

与 `fnc` 一样，只写签名不写函数体即为**声明**：生成结构体、实际函数原型与
调用包装，而实际函数 `name_rpc` 由外部（通常是 C 侧的传输/派发层）实现：

```sc
rpc mul: i4, a: i4, b: i4    # 仅声明：无函数体
```

```c
/* C 侧实现实际函数：可在此序列化、转发、远程执行后回填返回槽 */
void mul_rpc(struct mul *p) { p->_ = p->a * p->b; }
```

**数组参数**：数组形参不能整体由参数赋值，故映射为结构体内 `<T*, size>`
两个字段——指针（数组退化）与字节数（`sizeof`）。调用包装自动传数组地址
与其 `sizeof`，便于 C 传输/派发层据 `size` 序列化整段数组；函数体内按声明
维度正常索引（多维亦可）。维度写在名字侧（`a[3]: i4`）：

```sc
rpc dot: i4, a[3]: i4, b[3]: i4
    var s: i4 = 0
    var i: i4 = 0
    for i = 0; i < 3; i++
        s = s + a[i] * b[i]
    return s
```

```c
struct dot {
    int32_t _;
    int32_t *a;  size_t a_size;     /* 数组 → 指针 + 字节数（首维退化）*/
    int32_t *b;  size_t b_size;
};
void dot_rpc(struct dot *_p);
static inline int32_t dot(int32_t a[3], int32_t b[3]) {
    struct dot _p = {0};
    _p.a = a; _p.a_size = sizeof(int32_t[3]);   /* 传地址 + sizeof */
    _p.b = b; _p.b_size = sizeof(int32_t[3]);
    dot_rpc(&_p);
    return _p._;
}
```

多维数组 `m[2][3]: i4` 仅首维退化：字段为 `int32_t (*m)[3]` 与 `size_t m_size`
（`m_size = sizeof(int32_t[2][3])`），体内 `m[i][j]` 照常索引。

约束：

- 返回类型规则与 fnc 一致：省略即无返回值，此时结构体不含 `_` 成员。
- 支持 `@` 导出：头文件中生成结构体、实际函数原型与调用包装，跨模块/纯 C
  均可直接调用。
- 数组参数映射为 `<T*, size>` 两字段（见上），按声明维度索引。
- 不支持的形态
  - 方法（成员函数）形态（`:`、`::`）
  - 预定义函数类型（`->`）、
  - 可变参数 `...`

### 8.9 operand 设备操作数通用指令（`.` 透传）

`builtins/op.sc` 是**默认导入**模块（与 `platform.h` 一样，无需 `inc`），
是 `platform.h` 的 sc 侧入口。它用一个 `operand` 伪结构体声明一组对
**基础/任意类型**操作数的通用硬件指令扩展（如原子读写）：

```sc
# builtins/op.sc
def operand: {
    fnc get::            # 原子读（relaxed）
    fnc set::            # 原子写（relaxed）
    fnc get_acq::        # 原子读（acquire）
    fnc set_rel::        # 原子写（release）
}
```

`operand` 的成员均为 `fnc m::` 声明形态（实现在 C 侧），用于在语法上为
基础类型扩展 `.` 操作。当接收者**没有同名成员函数**时，`.`（或 `->`）
调用会透传为 `platform.h` 中的同名 `sc_<op>` 宏：

```sc
var x: i4 = 0
x.set(42)               # → sc_set(&x, 42)      值接收者自动取址
var y: i4 = x.get()     # → sc_get(&x)

fnc bump: void, p: i4&
    var c: i4 = p->get_acq()   # → sc_get_acq(p)   指针接收者原样传入
    p->set_rel(c + 1)          # → sc_set_rel(p, ...)
```

规则与约定：

- **接收者一律以指针传入**：值接收者 `v.op()` 自动取址为 `&v`；指针接收者
  `p->op()` 原样传入。这契合 `platform.h` 中 `sc_<op>`/`P_<op>` 宏首参为指针
  的约定。
- **指令类型无关**：`sc_<op>` 宏借 `__typeof__` 推导操作数类型，故忽略入参与
  返回值类型，可适配任意 C 类型（`i4`/`f8`/自定义类型等）。
- **优先级**：聚合类型自身的同名成员函数优先；`operand` 透传仅在接收者无同名
  方法时作为回退生效。
- **扩展方式**：新增一条指令时，在 `op.sc` 的 `operand` 内加一行 `fnc` 声明，
  并在 `platform.h` 加同名 `sc_<op>` 宏即可。

## 9. 变量与常量

```sc
var a: i4 = 12
let b: i4 = 34
```

支持的写法还包括：

```sc
var msg: = "hello sc"   # 由初始化值推断类型
var p: obj& = nil        # 指针初值为空
var count: i4            # 显式类型，无初始化
```

如果类型省略，默认类型按以下规则确定：

- **有初值时按字面量推断**（`var x: = 初值`）：
  - 字符串字面量 `"..."` → `char&`（即 C 的 `char*`）。
  - 字符字面量 `'...'` → `char`。
  - 浮点字面量 `3.14` → `f8`（double，避免精度损失）；带 `f`/`F` 后缀 → `f4`。
  - 整数字面量 `42` → `i4`（32 位）；数值超出 32 位有符号范围或带 `l`/`L` 后缀 → `i8`；
    带 `u`/`U` 后缀取无符号 `u4`，超出 32 位无符号范围或带 `l`/`L` → `u8`。
  - 前缀正负号会被穿透（`var n: = -5` 推断为 `i4`）。
  - 初值非上述字面量（如表达式、函数调用结果）时，回退到下面的无初值默认规则。
- **无初值或非字面量初值时**：普通对象默认 `char&`（`char*`），指针默认 `void&`（`void*`）。

### 9.1 tls 线程局部变量

`tls` 与 `var` 同形，定义线程局部存储变量：每个线程持有独立实例，
互不可见。与 C 规范一致，tls 变量始终是 static 存储期——
既可在顶层定义，也可在函数内部定义（跨调用保持，不可 `@` 导出）：

```sc
tls hits: i4 = 0           # 顶层：每线程独立的计数器

fnc next_id: i4
    tls id: i4 = 100       # 函数内：static 存储期，跨调用保持
    id++
    return id
```

生成的 C 为 `static TLS T name`（`TLS` 宏由 platform.h 提供，
按编译器适配为 `_Thread_local` / `__thread` / `__declspec(thread)`）。
注意：static 存储期要求初始值为常量表达式；tls 不执行
字段默认值 `__default()` 与声明即构造的 `init()`（无初值时零初始化）。

## 10. 表达式

当前表达式支持：

- 字面量：整数、浮点、字符串、字符、`true`、`false`、`nil`
- 相邻字符串字面量拼接（C 风格）：`"aa" "bb"` 等价 `"aabb"`；括号内可跨行拼接
- 十六进制整数：`0xFF`、`0x1A2B`
- 字面量后缀（与 C 对齐）：整数 `u/U/l/L` 可组合（`1u`、`100UL`、`7LL`），浮点 `f/F/l/L`（`3.14f`）
- 标识符
- 函数调用：`f(a, b)`
- 成员访问：`a.b`、`a->b`
- 下标：`a[i]`
- 前缀一元：`! ~ - + * & ++ --`（`&` 为取址，`*` 为解引用）
- 后缀一元：`++ --`
- 二元运算：算术、比较、逻辑、位运算、赋值和复合赋值
- 三元条件：`cond ? a : b`
- 括号分组：`(expr)`
- 异步前缀：`async rpc(...)`（登记到事件循环，返回 `future&`）、`await expr`（挂起待 future 就绪，见 11.4）
- 强制类型转换：`expr: type`、`(expr: type&)`（右值位置免括号，见 10.1）
- 初始化列表：`{1, 2, 3}`，可嵌套（见 10.2）
- `sizeof(expr)` 或 `sizeof(type)`：返回字节大小（映射到 C `sizeof`）
- `offsetof(Type, field)`：返回字段偏移字节数（映射到 C `offsetof`）

示例：

```sc
a = b + c * d
ptr->field = arr[i]
ok = p == nil ? false : true
var n: u8 = sizeof(point)
var off: u8 = offsetof(point, y)
var mask: u4 = 0xFF00
var big: u8 = 100UL
let pi: f4 = 3.14f
```

### 10.1 强制类型转换

sc 的强转写在表达式之后，用 `:` 引出目标类型：

```sc
expr: type      # 值转换，等价 C 的 (type)(expr)
expr: type&     # 指针转换，等价 C 的 (type*)(expr)
expr: type&&    # 二级指针转换
```

指针层数 `&` 加在目标类型名之后，与声明中的类型侧指针写法一致（见 §4）。

**仅作右值时免括号**：赋值右侧、实参、return 表达式等右值位置
可直接写裸形态；需对强转结果继续操作（如 `->` 取成员）或处于
三目分支内时，仍需括号：

```sc
var t: i4 = x: i4                # 裸形态：赋值右值
free(buf: void&)                 # 裸形态：实参位置
return strcmp(a: char&, b: char&)

# C:  ((node*)(p + 1))->next     两层括号，不好写
# sc: (p + 1: node&)->next       后续取成员 → 需括号
printf("%d\n", (p: node&)->val)  # void* 转结构体指针后取成员
ok = cond ? (a: i4) : b          # 三目分支内 → 需括号（避免与 : 歧义）
```

### 10.2 初始化列表

与 C 对齐，用于数组/结构体初始化，可嵌套、允许尾逗号，括号内可换行：

```sc
var arr[3]: i4 = {10, 20, 30}
var m[2][2]: i4 = {{1, 2}, {3, 4}}
var pt: point = {1, 2}
var tab[2][3]: i4 = {
    {1, 2, 3},
    {4, 5, 6},
}
```

## 11. 语句与控制流

当前已经实现的语句包括：

- `return`
- `if / else if / else`
- `while`
- `do ... while`
- `for`
- `case`（替代 C 的 `switch/case/default`）
- `through`（仅用于 case 分支末尾，表示贯穿）
- `goto`
- 标签语句（`label:`）
- `break`
- `continue`
- `run`（以 rpc 调用创建线程，见 11.2）
- `done`（标记 future 就绪并唤醒等待者，见 11.4）
- 变量/常量声明
- 表达式语句

示例：

```sc
# if / else if / else
if p.x > 1
    printf("single cond\n")

if p.x > 1
    && p.y < 10
    -
    printf("multi-line cond\n")
else
    printf("cond fail\n")

if x == 1
    printf("one\n")
else if x == 2
    printf("two\n")
else
    printf("other\n")

# while
while counter > 0
    counter--
    if counter == 1
        break

# do ... while
var i: i4 = 0
do
    i++
while i < 5

# for（三段表达式）
for i = 0; i < 3; i++
    counter += i

# for（省略某段）
for ; running; 
    step()

# case（替代 switch，默认自动 break）
case code:
    1, 2:
        printf("A\n")
    3:
        printf("B\n")
        through
    4:
        printf("C\n")
    :
        printf("default\n")

# goto 与标签
start:
    do
        i++
    while i < 2
    if i < 4
        goto start

# return
fnc add: i4, a:i4, b:i4
    return a + b

fnc noop
    return

# 变量/常量声明
var a: i4 = 1, b: i4 = 2
let max: i4 = 100

# 多行声明
var x: i4 = 0
    y: i4 = 0
    z: i4 = 0

# continue
for i = 0; i < 10; i++
    if i == 5
        continue
    printf("%d\n", i)
```

### 11.1 run 语句（多线程）

`run` 以 **rpc 调用**创建线程：rpc 的参数天然可打包（见 8.8），正好作
线程上下文，无需额外定义入口函数类型。需要 `inc m.sc`（thread 类型与
线程原语实现）。第二参数决定执行形态（按类型静态分派）：

```sc
inc m.sc

rpc work: c: ctx&, rounds: i4    # rpc 即线程体（省略返回类型）
    ...

fnc main: i4
    var t: thread& = nil
    run work(&c, 10000), &t    # joinable：t 接收 thread 指针
    run work(&c, 10000)        # detach：线程结束后自释放
    t->join()                  # 等待并回收（含 thread 对象），之后 t 失效

    var p: pool                # 线程池：同一个 run 动词
    p.init(4)                  # 4 个工作线程；0 → CPU 逻辑核数
    run work(&c, 10000), p     # 入池：任务排队，由工作线程执行
    p.join()                   # 屏障：等全部已提交任务完成
    p.drop()                   # 析构：等任务完成后停池回收
    return 0
```

- 第二参数（可选）按类型分派：`&t`（t 为 `thread&`）→ joinable
  独立线程，必须 `join()` 等待并回收；pool 对象或指针 → 任务入池
  （对象自动取地址，与方法调用糖一致）；省略 → 线程内部 detach，
  结束后自释放。
- thread 对象由 run 内部联合分配：单次
  `alloc(sizeof(thread) + sizeof(rpc参数) + 实现私有区)`，rpc 参数紧随
  thread 之后（`p + sizeof(thread)` 即参数），线程实体与参数同生命周期。
- 语法层面能拿到的 thread 必为 joinable，所以 thread 成员很简洁：
  `id`（跨平台统一线程 id）与 `join` 方法。不可手工构造。
- 转 C：装填 rpc 参数结构体后按第二参类型改发线程原语——独立线程
  `thread_run(入口, &参数, sizeof(参数), 出参|NULL, stack, prio)`，入池
  `pool_run(&池, 入口, &参数, sizeof(参数))`（均 m_impl 提供，
  POSIX pthread / Windows 线程；C 侧是两个普通函数，无运行时多态）。

#### 线程属性选项 `run<...>`

`run` 关键字后可紧跟 `<key:val, ...>` 选项块，设置线程属性，透传给 C 由
m 模块的 `thread_run` 具体实现（语法与 `stringify<...>` 一致，值限非负整数
字面量）。当前支持两个键（均可选、顺序无关）：

| 键 | 类型 | 含义 | 取 0 / 省略 |
|---|---|---|---|
| `stack` | u4 | 线程栈字节数 | 平台默认栈 |
| `prio` | u1 | 调度优先级（1..255 最佳努力映射） | 平台默认优先级 |

```sc
run<stack:262144> work(&c, 10000), &t          # 仅设栈大小（256 KiB）
run<stack:131072, prio:10> work(&c, 10000), &t # 栈 + 优先级
run<prio:5> note(7)                            # 仅设优先级（detach 线程）
```

- 选项仅适用于**独立线程**（joinable/detach）；用于 pool 目标会报错
  （池工作线程已预创建，逐任务的栈/优先级无意义）。
- 优先级为最佳努力：多数平台默认调度策略（SCHED_OTHER）不支持线程优先级，
  设置失败即忽略；需要实时优先级通常要相应权限。
- 转 C：选项作为末两个实参传入
  `thread_run(..., (uint32_t)stack, (uint8_t)prio)`，省略的键传 0
  （表示由 C 取平台默认）。

### 11.2 异步（async / await / done）

与 `run`（抢占式、多线程）相对的**第二套并发模型**：单线程协作式异步——
一个事件循环 + 把含 `await` 的 `rpc` 编译为状态机（stackless coroutine）。
三个语言关键字 + 一个内置类型 `future`，均属 **op.sc 默认导入机制**（无需
`inc`，C ABI 见 builtins/op.h，默认带入每个 C 单元）；其默认运行时实现
（libuv）在 async 模块，须 `inc async.sc` 触发链接（含叶子原语 `delay`）。

```sc
inc async.sc                 # 链接 libuv 运行时 + 引入叶子原语 delay

rpc greet: char&, name: char&, ms: u4
    await delay(ms)          # 挂起 ms 毫秒（不占线程），就绪后恢复
    return name

fnc main: i4
    async_init()                          # 建立当前线程事件循环
    var fa: future& = async greet("A", 80)  # 发起但不等待，立即得 future&
    var fb: future& = async greet("B", 30)
    async_loop()                          # 驱动事件循环至全部完成
    printf("%s %s\n", fa->get(): char&, fb->get(): char&)  # A B
    async_final()                         # 销毁事件循环
    return 0
```

**`future`（内置类型）**：异步结果句柄（类型擦除，`result` 为 `void*`）。

| 用法 | 含义 |
|---|---|
| `future()` | 伪类构造（见 8.3）：造一个未就绪 future，登记到当前事件循环 |
| `f.ready()` | 是否已就绪（`bool`） |
| `f.get(): T&` | 取结果（须已就绪；类型擦除，调用点用 `: T&` 强转还原） |

注意 `async` 返回的是 `future&`（指针），其方法调用用 `->`（如 `fa->get()`）。

**三个关键字：**

- `await E`（**仅 rpc 体内**）：`E` 是产生 `future` 的表达式（叶子异步原语
  调用，或对另一个 rpc 的调用）。`await` 挂起当前 rpc，待 `E` 就绪后恢复，
  整个 `await` 表达式求值为 `E` 的结果（类型 = 被调 rpc 的返回类型）。含
  `await` 的 rpc 被编译为状态机：跨 await 存活的局部/参数/返回槽提升到帧，
  函数体按 await 切段，运行时按状态 `goto` 恢复。
- `async E`（任意 fnc/rpc 体内）：把 rpc 调用 `E` 登记进当前线程事件循环，
  **立即返回 `future&`**（不阻塞），即"发起但不等待"。
- `done future [, result]`（语句）：标记 `future` 就绪并唤醒其等待者，等价
  运行时 `future_done`。`result` 可省略（默认空）；给定时**自动类型擦除**
  为 `void*`（指针类直转，标量经 `intptr_t` 往返，与 `f.get(): T&` 还原对应），
  无需手写 `: void&` 强转。

**事件循环生命周期**（op.sc 声明，async 模块 libuv 实现）：

```sc
async_init()       # 建立当前线程事件循环
async_loop()       # 驱动事件循环，推进所有挂起 rpc 直到全部完成
async_final()      # 销毁事件循环
```

**可 await 契约**：`await` 只认 `future`，libuv 只是默认实现（可替换/自定义）。
任何异步源（线程回调、硬件中断、外部库）只要在"完成时"用 `done` 兑现，即可
接入 `await`。下例把后台线程（`run`）桥接进事件循环，证明 `delay` 并非特例：

```sc
inc m.sc                                 # 后台线程（run）
inc async.sc

rpc square_worker: f: future&, n: i4
    done f, n * n                        # 跨线程兑现：置就绪 + 唤醒 waiter（自动擦除）

fnc bg_square: future&, n: i4
    var f: future& = future()            # 造未就绪 future（伪类构造）
    run square_worker(f, n)              # 后台线程去算（detach）
    return f                             # 立即返回 future&，可被 await

rpc compute: i4, n: i4
    var a: i4 = await bg_square(n)        # 真异步：让出，待线程算完才恢复
    return await bg_square(a)             # 串联依赖
```

**线程安全**：`done`（`future_done`）可被任意线程调用（如后台 `run` 线程）。
运行时在锁内置位并把唤醒投递回事件循环线程，所有状态机 `resume` 只在循环线程
串行发生，故编译器生成的状态机代码天然单线程、无需加锁。

说明：用到 `async`/`await`/`done`/`future`/`async_init` 等却未 `inc async.sc`
会在链接期报错（异步运行时是可选模块，按需引入）。

### 11.3 stringify(...) 文本格式化语句

`stringify(值)` 是 JSON 字符串格式化关键字（区别于类型 `string` 与堆构造
`string()`）：按实参的静态类型生成格式化代码，返回内置 `string`（需 `inc adt.sc`
与 `inc io.sc`，调用方负责 `drop`）；三参形态 `stringify(值, 缓存, 大小)` 在给定缓存内
构建（截断保证 NUL 结尾），返回 `char&` 即缓存首址，无需 drop。转 C 时，按类型生成的
静态内联格式化器写入独立的 `stringify.h`，由生成的 `.c` 在类型定义之后 `#include`：

```sc
var s: string = stringify(t)        # 默认多行美化（2 空格逐层缩进）
print "t=", s.cstr()                # 拼接糖：s.cstr() 返回 char& → %s
s.drop()

var b[256]: char
print "t=", stringify<compact:1>(t, b, 256)   # 缓存 + 紧凑：{"id":1,"name":"AB"}
```

选项块 `stringify<key:val, ...>(值)`：以 `(stringify_t){...}` 传入格式化器，键值限整数
字面量（来源 io 的 `stringify_t`，故需 `inc io.sc`）。当前支持 `compact`：

- `compact:1` → 紧凑单行 `{"x":3,"y":4}`；
- 默认（无选项 / `compact:0`）→ 多行美化，对象/数组逐层 2 空格缩进、嵌套换行。

格式化规则（输出合法 JSON，对象键加双引号）：整数/浮点→数字（枚举按整数）；
`bool`→true/false；`char`→'a'；`char&` / char 一维数组→"文本"；`string`→"内容"；
结构/联合→`{"字段": 值, ...}` JSON 对象（函数指针/合成字段跳过），其中
子成员为结构体（值）递归展开，成员为**结构体指针**→`"类型名@0x地址"`（不深递归），
成员为**标量指针**→`"&值"`（nil→nil），其它指针（`void&`/多级）→`"0x地址"`；
一维数组→`[v, v, ...]`；顶层实参为结构体一级指针时自动解引用展开内容（nil→"nil"）。
暂不支持多维数组。

`stringify(...)` 是上下文关键字：若作用域内存在同名函数/变量，
按普通标识符解析。

## 11.4 print 日志输出（io 内置函数）

`print` 是日志输出函数（非语言语句），由内置 io 子模块实现（需 `inc io.sc`）。
它有两种写法：**无括号拼接糖**与**有括号 C printf 兼容模式**：

```sc
inc io.sc

# 拼接糖（无括号）：字符串字面量=纯文本，非字面量按静态类型自动补说明符
var n: i4 = 42
var s: char& = “hello”
print “n=”, n, “ s=”, s             # → n=42 s=hello（i4→%d, char&→%s）
print “默认浮点=”, pi, “ 定点=”, (pi: “%.2f”)   # (expr: “%fmt”) 显式格式覆盖
print<7> “通道 7 日志”               # <chn> 指定 u1 日志通道（默认 0），透传给 C print

# C printf 兼容模式（有括号）：首参为格式串，实参原样传递
print(“n=%d s=%s”, 42, “hello”)     # 默认 D 级别
print(“E: 错误 code=%d”, -1)        # 格式串前缀 “X:” 设级别
```

- **拼接糖**：字符串字面量直接作为文本（`%` 自动转义）；其余实参按静态类型自动选择
  printf 说明符并追加到可变参数。类型→说明符映射：
  `i1/i2/i4`→`%d`、`i8`→`%`PRId64、`u1/u2/u4`→`%u`、`u8`→`%`PRIu64、
  `f4/f8`→`%f`、`char`→`%c`、`bool`→`%d`、`char&`/`char[]`→`%s`、
  `string` 值/指针→`%s`（经 `string_cstr`）、枚举→`%d`、其余指针/数组→`%p`。
  无法推断时用 `(expr: “%fmt”)` 显式指定该实参格式。
- **兼容模式**：等价于直接调用 C `print`，格式串与实参原样传递（沿用旧的 printf 用法）。
- 两种写法均支持 `<chn>` 通道块与文本前缀 `F:`/`E:`/`W:`/`I:`/`D:`/`V:` 设日志级别
  （致命/错误/警告/信息/调试/详细），无前缀默认 `D`。
- 输出格式 `HH:MM:SS.mmm L| 文本`（`chn!=0` 时加通道标记），自动换行，单次 fprintf
  保证多线程不串行。
- 运行时环境变量 `SC_LOG=F/E/W/I/D/V` 设过滤级别（默认 D，V 不输出）。

`print` 是上下文关键字：若作用域内存在同名函数/变量，按普通标识符解析。

## 12. I/O通讯与流操作符

sc 在语言层提供一套**设备通讯**基础能力：内置类型 `com`（通讯端点）配合
两个流操作符 `<<`（发）/ `>>`（收），把"把一组值发到设备 / 从设备收一组值"
表达为与 C++ 流类似的链式写法。`com` 属 **op.sc 默认导入机制**（无需 `inc`，
C ABI 见 builtins/op.h，通用运行时见 op_impl.c，始终随工程链接）；具体设备
io（`read`/`write` 等）由用户或可选模块实现（如 `inc io.sc` 的 `file` 文件设备）。

> 完整机制说明（编译器展开细节、运行时流程）见 `builtins/op.sc` 顶部的
> `# com 通讯机制` 注释块（形态 【A】–【F】）；可运行示例见
> `examples/feature16.sc`（同步值）、`feature17.sc`（异步值）、
> `feature18.sc`（有界读句柄）、`feature19.sc`（rpc 序列化收发）。

### 12.1 com 端点与流操作符

`com` 是设备通讯端点（机制框架）：具体 io 依赖设备，由每对象方法指针
（`fnc read::` / `fnc write::` 形态，绑定到对象字段）实现。语言层提供 `<<` / `>>`
两个流操作符：

```sc
com << v        # 发送：write(&com, &v, &sizeof(v))
com >> v        # 接收：read (&com, &v, &sizeof(v))
```

- **接收者自动取址**：左操作数为 `com` 值时自动取 `&com`，为 `com&` 指针时
  原样传入（与方法调用糖一致）。
- **链式左结合**：`com << a << b`、`com >> x >> y` 逐个执行，最左操作数须为
  `com` 端点。
- `size` 为 in/out（`u4&`）：传入期望字节数，回写实际收发字节数；返回码
  （见 `io` 枚举：`<0` 错误 / `0` 成功 / `again` / `eof`）由调用方按需检查。

```sc
inc stdio.h

fnc main: i4
    var c: com
    c.read  = dev_read              # 绑定每对象 io 实现（用户提供）
    c.write = dev_write

    var msg[4]: char = {'H', 'i', '!', 0}
    c << msg                        # write(&c, &msg, sizeof msg = 4)

    var buf[6]: char
    c >> buf                        # read(&c, &buf, sizeof buf = 6)

    var p: com& = &c
    p >> buf >> msg                 # 指针接收者 + 连续链：依次两次接收
    return 0
```

右操作数有三种形态，编译器据其静态形态选择展开：① 普通变量（收发
`sizeof` 字节）；② `com[...]` 有界读句柄（仅 `>>`，框架确定读流程）；
③ rpc 调用/名（按参数序列化收发）。

### 12.2 同步与异步：按所在函数形态分形

同一套 `<<` / `>>` 写法，按**所在函数是否异步**自动分形——这是 com 通讯
的核心设计：业务代码无需区分阻塞与非阻塞，由上下文决定收发语义。

- **同步形态（`fnc` 内）**：直接调用设备 `write`/`read`，立即完成收发。
- **异步形态（`rpc` 内）**：`rpc` 体内出现 `<<` / `>>` 即把该 rpc 标记为异步
  （等同含 `await`，见 §11.2），编译为状态机。每个收发点是一个 `await` 切点：
  发起 `com_read_async` / `com_write_async`（产出 `future`）→ 登记本帧为
  waiter → 让出；io 就绪后由事件循环恢复状态机续跑。跨收发点存活的局部与
  参数自动提升到帧结构体。

```sc
inc stdio.h
inc async.sc                        # 异步形态需事件循环运行时

# rpc 内：com >> / << 自动套用 await 状态机
rpc handler: ret, c: com&
    var buf[4]: char
    c >> buf                        # 异步接收（await com_read_async）
    var msg[3]: char = {'O', 'K', 0}
    c << msg                        # 异步发送（await com_write_async）
    return 0

fnc main: i4
    async_init()
    var c: com
    c.read  = dev_read
    c.write = dev_write
    var f: future& = async handler(&c)   # 挂起式启动 rpc，立即返回 future
    async_loop(nil)                      # 驱动事件循环至完成
    async_final()
    return 0
```

异步驱动由语言自有异步内核负责：把一次 com io 登记为事件循环活动请求，按
设备能力探测就绪（`readable`/`writable` 给出可监听句柄 → 注册进
epoll/kqueue/IOCP/poll；否则轮询返回值），就绪后执行收发并 `future_done`
兑现，全程不阻塞循环线程。

### 12.3 com[...] 有界读句柄（框架确定读流程）

读"边界不定"的协议（如读一整行、读到分隔符）需要一套确定的读循环。
`com[size, ending]` 声明一个**有界读视图句柄**（com 的分身/切片，见 §7.6
的分身机制），`com >> s` 把读流程交给框架内核 `limit_read`，截止策略全在
用户的 `data()` / `ending` 实现里：

```sc
var s: com[256, http_ending]        # size=256（每次最大 chunk），ending=动态截止判定
s = c                               # 分身构造：c.alloc(&c, 256, http_ending)
c >> s                              # 框架读流程：limit_read(&c, s._)
printf("收到 %u 字节\n", s._->len)
s = nil                             # 分身析构：c.free(&c, s._)
```

- 句柄是「有界读视图」，**仅用于 `>>` 读流程**；`com << s`（句柄写）编译报错。
- 框架读循环（`limit_read`）反复调 `com.read` 写入 `data()+len`，每段后回调
  `ending` 判截止（返回 `>=0` 命中并停止 / `<0` 继续）；`ending` 为 `nil` 则按
  `size` 定长读满。框架只跑这套不变循环，边界规格不写死在语言里。
- 异步形态（rpc 内）下，读循环遇 `com.read` 返回 `again` 即挂起 await、设备
  就绪后续读，直至命中——由运行时 `com_limit_read_async` 驱动，语义一致。

### 12.4 与 rpc 联动：参数序列化收发

`rpc` 的参数天然可打包（见 §8.8），com 借此把 rpc 的**参数列表**当作一组
待收发的协议字段，按声明顺序逐字段过 com（跳过返回槽 `_`）。发收两端字段
顺序一致，构成一对对称的「序列化协议」：

```sc
rpc do_some: i4, a: i4, b: i4       # 序列化收发的一对 rpc（参数即协议字段）
    return 0
rpc on_some: i4, a: i4, b: i4
    return 0

fnc call: com&
    com << do_some(a, b)            # 发：序列化 do_some 的参数（不触发本地调用）
    com >> on_some                  # 收：读出参数 → 触发本地 on_some_rpc(&参数)
    return
```

- **发** `com << rpc(实参...)`（带括号实参）：按本地调用同样装填 rpc 参数
  结构体，再逐字段 `write`；仅写参数字节，**不触发本地 rpc 调用**（纯发送方）。
- **收** `com >> rpc`（裸 rpc 名，无实参）：逐字段从 com `read` 进参数结构体，
  读齐后调用 `rpc_rpc(&参数)` 触发本地处理（不取返回值，仅触发）。
- **参数类型**：标量/指针按 `sizeof` 收发；数组按其字节数收发（收端栈/堆开
  等长后备缓冲）；`com[...]` 句柄参数收端以"本 com"为本体绑定句柄、走 §12.3
  的框架读流程从同一 com 读入。
- 同样按上下文分形：`fnc` 内同步逐字段收发；`rpc` 内每字段一个 await 让出点。
- 约束：可变参数 rpc、异步 rpc 暂不支持 com 序列化收发（编译报错）；
  `com[...]` 句柄参数不能用于 `<<` 发送（须由裸字节流另行承载）。

## 13. 内置函数

以下函数是编译器内置的伪函数（语言内核），不生成调用、不参与链接，编译器直接
展开为指针重解释或 C 同名运算。它们属于 op.sc 默认导入机制，无需 `inc`；
若作用域内存在同名普通函数/变量，让位于用户符号。

### base(o) —— 取首个真实成员地址

跳过编译器注入的隐藏成员（链表 `~` 的 `_prev`/`_next`，或容器 `<C,I>` 的
Item 节点 `_adt`），返回结构体首个用户字段的地址：

```sc
def task: ~ {                     # 链表结构体：注入 _prev/_next
    name: char&
    prio: i4
}
var t: task
var p: task& = &t

base(t)                           # → &t.name（char&&，跳过 _prev/_next）
base(p)                           # → &p->name（char&，指针接收者不解引用）
base(p: task&)                    # → p（显式指目标类型，直接以 p 为基址重解释）
base((task*)p)                    # → (task*)p（C 风格显式强转为 task*）
```

- 对非链表/非容器普通结构体：`base` 即取对象首地址（零偏移）。
- 显式类型标注 `base(o: T&)` 直接把 `o` 的首址重解释为 `T*`（不推导）。
- `base(o)` 按 `o` 的静态类型推导真实成员偏移；值接收者自动取址。

### prev(o) / next(o) —— 链表前驱/后继

仅对链表结构体（`def T: ~ {}`）生效，作为上下文关键字在成员访问位使用：

```sc
var it: task& = ...               # task 带 ~ 标记
it.next                           # → 取注入的 _next 成员（等价 it->_next）
p->prev                           # → 取注入的 _prev 成员（等价 p->_prev）
```

- `next(o)` 直接取注入的 `_next` 字段。
- `prev(o)` 为"边界安全前驱"：链首节点的 prev 返回 nil（运行时经
  `op.h` 的 `chain_prev` 契约判定，不显式访问注入字段）。
- 导航结果类型为 `void&`，用显式 `: T&` 下转回元素类型：`t.next(it): task&`。
- 仅在成员访问位生效，不是保留字；普通结构体仍可定义名为 prev/next 的字段。

### sizeof(expr | type) —— 编译期取字节大小

```sc
var s: u8 = sizeof(point)         # 类型：sizeof(point)
var n: u8 = sizeof(arr)           # 表达式：sizeof(arr)
```

- 结果类型 `u8`（`size_t`），直接转译为 C 的 `sizeof`。
- 支持表达式与类型名两种写法，语义与 C 一致。

### offsetof(Type, field) —— 编译期取字段偏移

```sc
var off: u8 = offsetof(point, y)  # y 字段在 point 内的字节偏移
```

- 结果类型 `u8`，直接转译为 C 的 `offsetof`。
- 第一个参数为类型名，第二个为字段名（须为该类型的直接字段）。

### 其他内置

以下机制虽也属语言内核（op.sc 默认导入），但因涉及完整语法体系，在各自章节独立说明：

| 机制 | 章节 |
|------|------|
| `print` 日志输出函数 | §17 |
| `stringify(...)` JSON 格式化 | §11.1 |
| `chain` 双向链表方法（append / push / pop / first / last 等） | §7.6 |
| `operand` 设备操作数指令（get / set / inc / CAS 等） | §8.9 |
| `future` 异步结果句柄（init / ready / get / ctx） | §11.4 |
| `thread` 线程实体（join） | §11.2 |

## 14. 布局规则

sc 的布局规则是“缩进 + 换行”为主：

- 缩进表示层级。
- 同层项目可以用换行分隔，也可以在单行中用逗号并列。
- 多行条件、参数列表、字段列表都可以写成竖排。
- 当一个声明的头部和主体需要明显分隔时，可以用单独一行 `-`。

### 14.1 严格缩进格式（强约束）

- 一级缩进固定为 4 个空格。
- 不允许使用 Tab。
- 不允许混用 Tab 和空格。
- 子块相对父块只能增加 1 级缩进，不允许跳级。
- 同一语法层级的行必须完全对齐。
- 缩进只表达结构，不参与表达式拼写。

“4 空格 + 严格对齐”是编译器强制规则，不满足将直接报错。

示例：

```sc
var a:i1, b: i1&, c[32]:i1

var a:i1
    b: i1&
    c[32]:i1
```

## 15. 目前已经跑通的组合能力

当前可用的语法组合已经覆盖：

- 结构体、联合体、枚举、类型别名
- 普通函数、函数类型、成员函数（结构体内实现）
- 普通函数指针字段（无函数体的签名字段）
- `inc` 系统头文件与 sc 模块导入
- 多模块单元编译与链接运行（每个 `.sc` 独立生成 C 单元，不做源码展开）
- `@` 导出到头文件；未导出顶层符号保持文件内可见（对应 C `static`）
- `true / false / nil / b`
- `.` / `->` 方法调用自动注入接收者
- 调用实参不足时默认补 0/nil/{0}（函数/成员函数/函数指针/rpc/run）
- 结构体字段默认值 + 未指定字段零初始化
- 可变参数函数（`...`）+ `va_list` 透传
- `def Alias -> Type` 提供类型别名（等价 typedef）
- `do ... while`、`goto`、标签语句
- `sizeof(expr)`、`sizeof(type)`、`offsetof(Type, field)` 内置表达式
- 强制类型转换 `expr: type&`（右值位置免括号），初始化列表 `{1, 2, 3}`，十六进制/字面量后缀，相邻字符串拼接
- 基础语义检查：`nil` 赋值边界、解引用/下标越界、返回/赋值类型兼容、局部地址逃逸
- 聚合类型循环检测：结构/联合按值递归包含会在 AST 语义阶段报错（需改为指针字段 `&`）
- 详细诊断输出：出错行源代码展示、编译上下文、文件位置追踪
- 调试符号生成：行号映射注释、-g 编译标志支持源码级调试（GDB/LLDB）
- 预定义 ADT 接口（`inc adt.sc`）：`string` / `list` / `dict` / `dim` / `json`（高覆盖 Python 常用能力，具体实现由插件注入）
- 链表结构体 `def T: ~ {}`：末尾注入 `_prev`/`_next` 自链指针，配合内置 `chain` 双向链表（append/push 时编译器自动注入偏移）；`chain` 属 op.sc 默认导入机制，无需 inc；成员访问位提供 `prev`/`next` 上下文关键字
- io 内置关键字：`stringify(值[, 缓存, 大小])` 按静态类型 JSON 格式化（返回 `string` 或缓存 `char&`，需 `inc adt.sc`，转 C 时生成独立 `stringify.h`）；`print` 日志输出函数（拼接糖 + C printf 兼容模式，F/E/W/I/D/V 级别、SC_LOG 过滤，需 `inc io.sc`，详见 §17）
- 异步机制（op.sc 默认导入，async 运行时需 `inc async.sc`）：内置类型 `future` + 关键字 `async`（发起返回 future&）/`await`（rpc 内挂起，编译为状态机）/`done`（标记就绪并唤醒，跨线程安全）；`async_init`/`async_loop`/`async_final` 事件循环生命周期；libuv 默认实现含叶子原语 `delay`，可 await 契约支持自定义异步源
- VS Code 工具链增强：实时诊断、悬停提示、跳转定义、文档符号、`Format Document`（基于 `scc --emit-sc`）
- 定义顺序无关：生成 C 时默认自动输出结构/联合前置声明与函数原型，支持先使用后定义（含递归/互递归函数）

## 16. 语言演进路线图

### 16.1 近期优先

**类型检查与安全性：**
- 扩展类型检查深度：数据流分析、别名关系、生命周期边界验证
- 内存安全防护：更强的 `nil`、悬空指针、越界访问检测
- 生命周期语义：明确的所有权模型、局部地址逃逸防护

**诊断与工具链：**
- 多文件编译时的错误链完整展示
- 常见错误自动修复建议（typos、类型不匹配、缺失操作符等）
- 源码级调试的完善：更丰富的调试符号与映射信息

**模块系统稳定性：**
- 模块名到文件路径的稳定规范映射
- 命名空间与符号冲突诊断
- 跨模块编译的增量构建支持

### 16.2 中期建设

**语言特性：**
- `const` / `volatile` / `restrict` 限定符
- `defer` 或等价的作用域退出钩子
- 更强的资源管理保障：数组切片、字符串生命周期、动态分配约定

**工程支持：**
- 依赖图缓存与增量编译优化
- 包描述文件与版本管理（保持最终可转 C 链接模型）
- 标准库分层：基础类型、字符串、数组、IO、文件、时间、容器
- 单元测试框架、格式化器、代码风格检查

**开发工具：**
- 语言服务器协议（LSP）基础实现
- IDE 集成：hover 提示、自动完成等

### 16.3 长期愿景

**高级类型系统：**
- 泛型或模板机制，支撑通用容器与算法库
- 高级别名分析与类型推导

**工程级成熟度：**
- 完整的包管理与版本依赖系统
- 命名空间与模块隔离机制
- LSP 全功能支持与 IDE 深度集成

**成为真实生产工具：**
- 让 sc 能直接承担工程级项目，而不只是转译原型
- 生态化：社区包、最佳实践、学习资源

## 17. 参考语法模板

这一节把当前已经实现的写法压缩成一组可直接照着写的模板。

### 17.1 顶层对象

```sc
inc stdio.h
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
```

### 17.2 函数体语句

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

### 17.2.1 `case` 的严格缩进层级

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

### 17.3 成员函数与函数指针字段

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

### 17.4 常用表达式

```sc
a = b + c * d
obj.field = value
ptr->field = value
arr[i] = 1
f(a, b, c)
ok = p == nil ? false : true
var arr[3]: i4 = {10, 20, 30}     # 初始化列表
var mask: u4 = 0xFF00              # 十六进制
var big: u8 = 100UL                # 字面量后缀
(p + 1: node&)->next               # 强转后取成员：需括号
var t: i4 = x: i4                  # 值转换：右值位置免括号
```

## 18. 当前限制

下面这些是目前文档里应该明确标出的语言边界，也是真实项目里最容易踩坑的地方。

- 嵌套 `fnc` 目前不支持，函数只能在顶层定义。
- `inc *.sc` 已支持单元编译与链接，但命名空间/包版本管理仍未完成。
- 还没有独立的包管理和版本依赖描述。
- 还没有完整的泛型/模板能力。
- 还没有显式的 `const`/`volatile`/`restrict` 限定符。
- 还没有成熟的资源管理机制，例如 `defer` 或析构钩子。
- 初始化列表 `{1, 2, 3}` 已支持；指定成员初始化（C 的 `.field = x`）仍未支持。
- 预处理能力（宏定义、条件编译）是**有意不提供**的设计决策而非缺失，见第 5 节“无预处理器设计”。
- 目前的工程化能力主要集中在转译与 AST，标准库和工具链仍偏薄。

这些限制不是”缺文档”，而是”语言还在演进中”的真实状态，应该在后续实现时逐条收敛。

