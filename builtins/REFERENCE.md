# builtins 内置模块参考

`builtins/` 是 scc 的内置模块搜索路径：`inc x.sc` 会依次尝试
`builtins/x.sc` 与子项目形态 `builtins/x/x.sc`。
也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。

当前内置模块：

| 模块 | 引入方式 | 说明 |
|------|----------|------|
| adt | `inc adt.sc` | 抽象数据类型：string（动态字符串）、list（动态指针数组） |
| m | `inc m.sc` | 多线程语言支持标准：run 语句、thread、mutex、cond（含 wait 方法）、pool（线程池） |
| env | `inc env.sc` | 运行环境 / 系统路径：work_dir、home_dir、download_dir、exe_file、tmp_file（跨平台，C 侧实现） |
| mem | `inc mem.sc` | 内存池：chunk/chunk0/chunk_array/chunk_aligned/refit/recycle（替代 malloc/calloc/realloc/free）、mem_usable、mem_trim（空闲页归还 OS）、mem_stat（统计快照含峰值）、mem_teardown、arena（竞技场批量分配）、shm（跨进程命名共享内存，支持只读/独占标志）；每线程 TLS 堆无锁、跨线程释放安全、线程退出堆自动回收复用 |
| op | 默认导入（无需 inc） | 语言底层（语法层面）机制：operand（设备操作数 `.` 透传为 `platform.h` 的 `sc_<op>` 宏）、chain（侵入式双向链表，C 运行时由 `op.h`/`op_impl.c` 自动提供）、stringify（类型 JSON 格式化关键字，选项类型 `stringify_t` 见 `op.h`）；platform.h 的 sc 侧入口 |

另有 `platform.h`（非模块）：面向用户的 C 跨平台基础头，默认 -I 可直接 inc，见末节。

## 子项目通用机制

子项目形态 `builtins/x/` 由三件套构成，分别面向编译流程的不同阶段：

| 文件 | 角色 | 面向谁 |
|------|------|--------|
| x.sc | **sc 侧接口声明**：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体），决定 sc 代码能怎么用 | scc 编译器 |
| x.h | **C ABI 契约**（数据布局/原型，与 x.sc 同步维护），自定义/默认实现据此编写 | C 编译器 |
| x_impl.c | **默认实现**：源实现经拼接机制并入 `x.sc` 生成的单元 `.c`（同一 TU）；预编译 `.a` 则单独链接 | C 编译器 / 链接器 |

### 本质：x.sc 真正参与编译，不是参考文档

`inc x.sc` 并非纯文档引用——scc 会**真实读取、词法+语法解析这个 `.sc` 文件**，
把其中 `@` 导出的声明提取出来标记为 `external`，挂进当前单元的 AST 参与类型检查与
代码生成。它给编译器提供的是 **sc 语言侧的"符号契约"**。

> 谁说了算：sc 代码怎么用这个类型，由 x.sc 说了算（编译器只读它）；生成的 C 与
> 底层实现怎么对齐，由 x.h 说了算（C 编译器只读它）。同一份数据布局在 x.sc 的
> `@def` 与 x.h 的 `struct` 中**各写一遍、需手工保持一致**——两者是一对必须同步的
> 声明，而不是某一方自动生成另一方。

### 流程：从 `inc x.sc` 到可执行文件

1. **路径解析**：`inc x.sc` 经 `resolveModulePath` 解析为 `builtins/x.sc` 或子项目
   `builtins/x/x.sc`，标记该 `inc` 声明 `external = true`，加入依赖列表。
2. **解析并合并声明**：scc 读取 `x.sc`、`parse(lex(...))`，把其中 `@` 导出的
   `@def`/`@fnc T::m` 声明标记 `external` 后合并进主单元 AST（`resolveUnitDeps`），
   再递归对依赖自身重复此过程（`loadUnitGraph`，含循环依赖检测）。
3. **语义检查**：`semanticCheck` 据合并后的 external 声明校验——类型可见、方法存在、
   返回类型推断、参数匹配。
4. **代码生成**：codegen **跳过所有 external 声明的代码体**（方法无函数体，"由其
   模块头提供"），仅生成对它们的引用——方法调用 `s.len()` 重整为 `string_len(&s)`，
   并依据声明驱动语法糖（`string()` 堆构造、声明即构造）。生成的 `.c` 顶部
   `#include "builtins/x/x.h"`，由 x.h 提供真正的 `struct` 定义与函数原型。
5. **并入实现**：源实现 `x_impl.c` 经**拼接机制**并入 `x.sc` 生成的单元 `.c`、
   编成同一翻译单元（与 sc 侧共享模块私有 `static`，`::` 符号就地定义）；预编译
   `x.a`（内嵌发行版释放）或 `--adt <x.o|x.a>` 指定的二进制实现无法拼接，则作为目标
   文件单独参与链接；皆无则跳过（不影响纯 sc 子项目）。拼接机制通用细节见
   [syntax.md](../syntax.md) §9「模块实现的并入：拼接机制」。

### 用途：三件套各自决定什么

| 文件 | 决定 | 举例 |
|------|------|------|
| x.sc | sc 代码**怎么用**这个类型（合法类型、方法签名、语法糖、name mangling） | `var s: string`、`s.len()` 返回 `u8` |
| x.h | 生成的 C 与实现**怎么对齐**（数据布局、函数原型） | `struct string {...}`、`string_len(...)` |
| x_impl.c | extern 符号怎么兑现（默认实现，可替换）——源实现拼接进单元 TU，二进制实现走链接 | `string_len` 的实际函数体 |

关键点：x.sc 的方法**没有函数体**，scc 因此不为其生成任何代码体，实现交给配套 C——
源实现 `x_impl.c` 拼接进同一 TU、二进制实现（`.a`/`--adt`）走链接——这正是"可替换
实现"（`--adt` 等）得以成立的基础。

## adt —— 抽象数据类型子项目

目录结构（`builtins/adt/`）：

| 文件 | 角色 |
|------|------|
| adt.sc | sc 侧接口声明：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体） |
| adt.h | C ABI 契约（数据布局/原型，与 adt.sc 同步维护），自定义实现据此编写 |
| adt_impl.c | 默认实现，经拼接机制并入 adt 单元 `.c`（同一 TU） |

### 工作机制

1. sc 源码 `inc adt.sc` 后即可使用 `string`/`list` 类型及其方法。
2. 方法声明（`fnc T::m` 无函数体）转 C 时生成 extern 原型 `T_m(T *_this, ...)`，
   实现由配套 C 兑现。
3. 单元图包含 `builtins/adt/adt.sc` 时，scc 把默认实现 `adt_impl.c` 经拼接机制并入
   adt 单元 `.c`（同一 TU）；可替换为自定义实现：

```sh
scc app.sc --adt my_adt.c      # .c 源实现拼接进 adt 单元 TU；.o/.a 直接参与链接
SCC_ADT=my_adt.o scc app.sc    # 环境变量等价；.sc 配置键 adt 亦可
```

自定义实现须完整实现 `adt.h` 中的全部函数（可基于第三方库如 sds、uthash 等封装）。

### 构造与析构

- `init`：栈对象声明即构造——函数内 `var s: string`（无初值、非指针、非数组）自动调用
  `string_init(&s)`；全局变量需手动 init。
- `T()` 堆构造糖：`var p&: string = string()` → malloc + 清零/默认值 + init，
  返回 `T&`（malloc 失败返回 nil）。
- `drop`：手动析构 `s.drop()`（命名保留，未来支持作用域自动插入）；
  堆对象 drop 后需再 `free(p)`（drop 只释放内部资源，不释放对象本身）。

### string —— 动态字符串

内部 NUL 结尾，`cstr()` 永不返回 nil，可直接交给 C 接口。
返回 `bool` 的方法：1 成功 / 0 失败（内存不足或参数越界）。
签名为空表示无返回值无参数。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造为空串 |
| drop | | 释放缓冲区 |
| len | `u8` | 字符数（不含 NUL） |
| cstr | `char&` | C 字符串视图（始终非 nil） |
| clear | | 置空（保留容量） |
| reserve | `bool, n: u8` | 预留容量 |
| assign | `bool, s&: const char` | 赋值为 C 字符串 |
| append | `bool, s&: const char` | 追加 C 字符串 |
| append_n | `bool, s&: const char, n: u8` | 追加前 n 字节 |
| append_char | `bool, c: char` | 追加单字符 |
| insert | `bool, index: u8, s&: const char` | 指定位置插入 |
| erase | `bool, index: u8, n: u8` | 删除 n 字节 |
| at | `char, index: u8` | 取字符（越界返回 0） |
| find | `i8, sub&: const char, start: u8` | 查找子串（未找到 -1） |
| rfind | `i8, sub&: const char` | 反向查找（未找到 -1） |
| equals | `bool, s&: const char` | 与 C 字符串比较相等 |
| starts_with | `bool, s&: const char` | 前缀判断 |
| ends_with | `bool, s&: const char` | 后缀判断 |
| slice | `bool, start: i8, stop: i8, out&: string` | 切片 `[start, stop)`，负索引从尾部计 |
| strip | | 去除首尾空白 |
| lower / upper | | 大小写转换（ASCII） |
| clone | `bool, out&: string` | 深拷贝到 out |

### list —— 动态指针数组

元素为裸指针（`&`，即 void 指针），**不拥有元素**：drop/clear/remove_at 不释放元素本身。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造为空列表 |
| drop | | 释放槽位数组 |
| len | `u8` | 元素个数 |
| clear | | 清空（保留容量） |
| reserve | `bool, n: u8` | 预留槽位 |
| push | `bool, value&:` | 尾部追加 |
| pop | `&` | 弹出尾元素（空返回 nil） |
| get | `&, index: u8` | 取元素（越界返回 nil） |
| set | `bool, index: u8, value&:` | 改写元素 |
| insert | `bool, index: u8, value&:` | 指定位置插入 |
| remove_at | `&, index: u8` | 删除并返回该元素 |
| index_of | `i8, value&:` | 按指针值查找（未找到 -1） |
| reverse | | 原地反转 |
| clone | `bool, out&: list` | 浅拷贝到 out |
| sort | `cmp&: list_cmp` | 稳定排序，`list_cmp: i4, a&:, b&:` |

### 使用示例

完整示例见 `examples/feature6.sc`：

```sc
inc stdio.h
inc adt.sc

fnc main: i4
    var s: string              # 声明即构造
    s.append("hello")
    printf("%s len=%llu\n", s.cstr(), s.len())
    s.drop()                   # 手动析构
    return 0
```

## op —— 语法机制模块（默认导入）

`builtins/op.sc` 与 `platform.h` 一样属**默认导入**，无需 `inc`。它汇集编译器
需要感知的「语言底层（语法层面）机制」声明，据此识别方法调用糖、字段访问、
链表注入等语法糖。它同样作为正式单元进入模块图、生成 `op.c`，其 C 运行时
`op_impl.c` 经拼接机制并入同一 TU（无需 inc）：

| 文件 | 角色 |
|------|------|
| op.sc | sc 侧机制声明（operand、chain；后续切片/容器/COM 等） |
| op.h | C ABI 契约（chain 结构体/原型），随 `platform.h` 默认带入每个生成单元 |
| op_impl.c | 机制运行时默认实现，经拼接机制并入 op 单元 `.c`（同一 TU） |

### operand —— 设备操作数通用指令

`operand` 伪结构体的每个成员函数声明一条作用于**基础/任意类型操作数**的通用
指令。语法上对基础类型（`i4`、`type&` 等）扩展 `.` 操作调用：`v.get()` /
`p->set(x)`。scc 不生成 `operand_xxx`，而是透传为 `platform.h` 的同名
`sc_<op>` 宏（接收者以指针传入，值接收者自动取址）；指令类型无关
（C 侧 `__typeof__` 推导）。新增指令时在 op.sc 加一行 `fnc`、并在 platform.h
加同名 `sc_<op>` 宏即可。

| 指令 | 调用 | 透传 | 说明 |
|------|------|------|------|
| get | `v.get()` | `sc_get(&v)` | 原子读（relaxed） |
| set | `v.set(x)` | `sc_set(&v, x)` | 原子写（relaxed） |
| get_acq | `v.get_acq()` | `sc_get_acq(&v)` | 原子读（acquire） |
| set_rel | `v.set_rel(x)` | `sc_set_rel(&v, x)` | 原子写（release） |

### chain —— 侵入式双向链表

配合链表结构体 `def T: ~ {}` 使用（编译器在 T 成员首位注入 `void *_prev` /
`void *_next`）。`chain` 为 op.sc 默认导入机制，**无需 `inc`**；C 结构体与运行时
由 `op.h` / `op_impl.c` 自动提供。机制约定：

- `head` 指向首元素；首元素的 `_prev` 指向尾元素（即 rear），尾元素的
  `_next` 为 `nil`——取队尾 O(1)，正向遍历以 `nil` 结尾。
- 同一 `chain` 只能存放**同一种**结构体；`_prev`/`_next` 固定在元素首部。
- **不拥有元素**：pop/remove/cut 只摘除链接，不释放元素内存。
- 元素入链前无需初始化 `_prev`/`_next`；同一元素不可同时挂在两条链上。

| 方法 | 签名 | 说明 |
|------|------|------|
| append | `it&:` | 添加到队尾 |
| push | `it&:` | 添加到队首 |
| pop | `&` | 移除并返回首元素（空返回 nil） |
| before | `pos&:, it&:` | 插入到 pos 前面 |
| after | `pos&:, it&:` | 插入到 pos 后面 |
| remove | `it&:` | 摘除指定元素 |
| first | `&` | 首元素（空返回 nil） |
| last | `&` | 尾元素（即 first 的 `_prev`） |
| revert | | 首尾翻转 |
| append_to | `dst&: chain` | 整链接到 dst 尾部，自身清空 |
| push_to | `dst&: chain` | 整链接到 dst 头部，自身清空 |
| cut | `from&:, to&:, out&: chain` | 截取 `[from..to]` 段为新链 out（out 被覆盖） |

```sc
def task: ~ { id: i4 }

var l: chain                  # chain 默认可用，无需 inc
var t[3]: task
l.append(&t[0])
l.append(&t[1])
l.push(&t[2])              # 2 0 1
var it&: task = l.first(): task&
while it != nil
    printf("%d ", it->id)
    it = it->_next
```

完整示例见 `examples/feature6.sc`。

### stringify(...) —— 类型 JSON 格式化（关键字）

`stringify` 是 JSON 格式化关键字（区别于类型 `string` 与堆构造
`string()`）：编译器按实参的静态类型合成格式化函数。选项类型 `stringify_t`
属语言底层机制（默认导入，C ABI 见 `op.h`，无需 `inc`）；格式化器返回
`string`，故仍需 `inc adt.sc`。转 C 时按类型生成的
静态内联格式化器写入独立的 `stringify.h`，由生成的 `.c` 在类型定义之后 `#include`。
两种形态按实参个数静态派发：

```sc
var s: string = stringify(t)       # 默认多行美化，返回 string，调用方负责 drop
print("t=%s", s.cstr())
s.drop()

var b[256]: char
print("t=%s", stringify<compact:1>(t, b, 256)) # 紧凑 + 缓存：截断保证 NUL 结尾，
                                   # 返回 char&（即缓存首址），无需 drop
```

选项块 `stringify<key:val, ...>(值)`：编译器据此构造 `(stringify_t){...}` 传入格式化器，
值限整数字面量。当前支持 `compact`：`compact:1` 紧凑单行 `{"x":3,"y":4}`；默认
（无选项 / `compact:0`）多行美化，对象/数组逐层 2 空格缩进、嵌套换行。

格式化规则（输出合法 JSON，对象键加双引号）：

| 实参类型 | 输出 |
| --- | --- |
| 整数 / 浮点 / 枚举 | 数字（`%g` 浮点） |
| bool | `true` / `false` |
| char | `'a'` |
| char&（C 字符串）/ char 一维数组 | `"文本"`，nil → `nil` |
| 结构体指针（成员） | `"类型名@0x地址"`（不深递归），nil → `nil` |
| 标量指针（成员） | `"&值"`（取值输出），nil → `nil` |
| 其他指针（`void&` / 多级） | `"0x` 十六进制地址"，nil → `nil` |
| 结构体 / 联合体（值） | `{"字段": 值, ...}` JSON 对象；子成员为结构体（值）递归展开；函数指针字段与合成字段（`_prev` 等）跳过 |
| 结构体 / 联合体一级指针（顶层实参） | 自动解引用展开，nil → `"nil"` |
| 一维数组 | `[v, v, ...]` |
| string | `"内容"` |

限制：暂不支持多维数组（编译报错）；联合体按全部字段展开（按值语义读取）。

## m —— 多线程语言支持标准

多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；后续按
语言特性需要扩展（条件变量/原子操作/线程局部存储等）。

目录结构（`builtins/m/`）：m.sc（sc 侧接口声明）、m.h（C ABI 契约）、
m_impl.c（默认实现，跨平台经由 `platform.h`：POSIX pthread / Windows 线程）。
Linux 等平台链接时自动追加 `-lpthread`。

句柄约定：`h&:` 为实现私有指针（void 指针），调用方不直接访问；结构布局因此
跨平台稳定。

### run 语句与 thread —— 线程

线程由 `run` 语句创建（语言特性），目标必须是 **rpc 调用**：rpc 参数
天然可打包，正好作线程上下文，无需额外定义入口函数类型。
第二参数决定执行形态：

```sc
run work(a, b)        # detach：独立线程，结束后自释放
run work(a, b), &t    # joinable：t&: thread，须 t->join() 等待并回收
run work(a, b), p     # 入池：p 为 pool（对象或指针），任务排队执行
```

实现机制：run 内部单次分配 `sizeof(thread) + sizeof(rpc参数) + 实现私有区`
的联合实体，rpc 参数紧随 thread 对象之后（`p + sizeof(thread)` 即参数），
线程实体与参数同生命周期。语法层面能拿到的 thread 必为 joinable，
所以 thread 对象非常简洁：

| 成员/方法 | 类型/签名 | 说明 |
|------|------|------|
| id | `u8` | 跨平台统一线程 id（线程启动后由其自身填写） |
| join | | 等待结束并回收（含 thread 对象本身，之后指针失效） |

线程休眠用 `platform.h` 的 `P_usleep(us)`（默认 -I，`inc "platform.h"` 即用）。

thread 不可手工构造（无 init）；`run` 是唯一创建途径。

### mutex —— 互斥锁

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造（声明即构造适用） |
| drop | | 析构 |
| lock / unlock | | 加锁 / 解锁 |
| try_lock | `bool` | 取锁成功返回 1，已被占用返回 0 |

### cond —— 条件变量

条件等待由 cond 的 `wait` 方法完成（普通方法调用，映射到 C 侧 `cond_wait`）：

```sc
c.wait(&mu)            # 无限等待（调用前须已持有 mu）
c.wait(&mu, nsec, sec) # 超时等待（nsec/sec 省略或全 0 等价于无限等待）
```

接收者为 cond（对象自动取地址）；首参为 mutex 指针（对象须显式 `&mu`）。
返回 i4：0 被唤醒 / 1 超时 / -1 错误；被虚假唤醒需循环复查条件。
Windows 下超时精度为毫秒（纳秒向上取整）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init / drop | | 构造 / 析构 |
| one | | 唤醒一个等待者 |
| all | | 唤醒全部等待者 |
| wait | `i4, m: mutex&, nsec: u8, sec: u8` | 条件等待，返回 0 唤醒 / 1 超时 / -1 错误 |

### pool —— 线程池

pool 是 `run` 语句的另一种执行目标：任务提交复用 run 语句，没有
新增提交方法——“线程”与“线程池”在语言层面是同一个动词：

```sc
var p: pool
p.init(4)                  # 4 个工作线程；0 → CPU 逻辑核数
run work(&c, 1000), p      # 入池排队（与独立线程同一语句）
p.join()                   # 屏障：等全部已提交任务完成（之后仍可提交）
p.drop()                   # 析构：等任务完成 → 停工作线程 → 回收
```

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `n: u4` | n 个工作线程；0 → CPU 逻辑核数 |
| join | | 屏障：等待全部已提交任务完成（可反复使用） |
| drop | | 析构：等已提交任务全部完成后停池回收（不丢任务） |

按类型静态分派机制：编译器在生成 run 语句时推断第二参类型——
pool 对象或指针 → 生成 `pool_run(&p, fn, &参数, sizeof)` 入池；其余
（`&t` 形态）→ 生成 `thread_run` 创建独立线程。C 侧是两个普通函数，
没有运行时多态。任务节点延续联合分配哲学：`[节点][rpc 参数]`单块
分配，参数拷贝入节点，调用点无需保活。

刻意不提供 future/cancel/动态扩容：任务级同步用 cond + wait 方法，
需要隔离时建多个 pool。

### 使用示例

完整示例见 `examples/feature7.sc`：

```sc
inc stdio.h
inc m.sc

def ctx: {
    mu: mutex
    n: i4
}

rpc work: c&: ctx, rounds: i4      # rpc 即线程体，参数即线程上下文
    var i: i4 = 0
    for i = 0; i < rounds; i++
        c->mu.lock()
        c->n = c->n + 1
        c->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()        # 嵌套字段不自动构造，手动 init
    var t&: thread = nil
    run work(&c, 10000), &t    # joinable
    run work(&c, 10000)        # detach（仅示意，结束后自释放）
    t->join()                  # 等待并回收，之后 t 失效
    P_usleep(50000)            # 等 detach 线程结束（platform.h）
    c.mu.drop()
    printf("n=%d\n", c.n)
    return 0
```

## io —— 输入输出子项目

`inc io.sc` 引入。io.sc 本身只含文档注释（无类型/函数声明），引入它的作用
是把 `io_impl.c` 拉入编译链接，为 `print` 关键字提供运行时实现。

### print —— C 风格日志输出（关键字）

`print(fmt, ...)` 由编译器直接生成对 `print`（io_impl.c，C ABI 见 io.h）
的调用：

```sc
print("n=%d s=%s", 42, "hello")    # 默认 D 级别
print("E: 错误 code=%d", -1)        # 前缀 "X:" 设级别
```

- 格式串与 printf 完全一致（vsnprintf 实现，参考 stdc 的简化移植）。
- 级别前缀：`F:` 致命 / `E:` 错误 / `W:` 警告 / `I:` 信息 / `D:` 调试 /
  `V:` 详细；无前缀默认 `D`；前缀后的一个空格自动跳过。
- 输出格式：`HH:MM:SS.mmm L| 文本`，自动补换行；整行一次 fprintf 写 stdout，
  多线程不串行。单行上限 2048 字节，超出截断。
- 过滤：环境变量 `SC_LOG=F/E/W/I/D/V`（启动后首次 print 时读取一次），
  默认 `D`（V 级不输出）。
- `print` 是上下文关键字：作用域内存在同名函数/变量时按普通标识符解析；
  未 `inc io.sc` 而使用会编译报错。
- 相比 stdc 完整日志系统，省略了 tag/UDP 上报/缓冲模式等机制，保留接口
  风格以便后续扩展。

## env —— 运行环境 / 系统路径子项目

`inc env.sc` 引入。提供跨平台的系统路径查询，全部为 **C 侧实现的自由函数**
（`@fnc name::` 无函数体，链接期由 `env_impl.c` 注入）。平台适配统一经由
`platform.h`（`P_WIN`/`P_DARWIN`/`P_BSD`/`P_LINUX`），实现内不散落平台分支。

目录结构（`builtins/env/`）：env.sc（sc 侧声明）、env.h（C ABI 契约 +
返回码）、env_impl.c（默认实现）。

### 调用约定

所有函数把结果路径写入调用方提供的 `buf`（NUL 结尾），`size` 为字节容量，
返回 `ret`（即 i4 语义别名）：

| 返回码 | 含义 |
|--------|------|
| `0` | 成功 |
| `-1` | 系统调用失败（`ENV_ERR`） |
| `-2` | `buf` 容量不足（`ENV_ERR_CAPACITY`） |

建议 `buf` 至少 `PATH_MAX`（4096）字节；`download_dir`/`exe_file` 在部分平台
要求 `buf >= PATH_MAX`，过小返回 `-2`。

| 函数 | 签名 | 说明 |
|------|------|------|
| env_work_dir | `ret, buf: char&, size: u4` | 当前工作目录（cwd） |
| env_home_dir | `ret, buf: char&, size: u4` | 用户 home：POSIX 优先 `$HOME`，回退 `getpwuid_r`；Windows 取用户配置目录 |
| env_download_dir | `ret, buf: char&, size: u4` | 下载目录：Win 已知文件夹 / macOS sysdir / Linux `$XDG_DOWNLOAD_DIR` 回退 `~/Downloads` |
| env_exe_file | `ret, buf: char&, size: u4` | 当前可执行文件的规范化绝对路径 |
| env_tmp_file | `ret, buf: char&, size: u4` | 在系统临时目录**创建**一个唯一空临时文件并返回路径（调用方负责删除） |

实现移植自 stdc 的 `P_*` 同名函数，并修正若干健壮性问题：work_dir 在
Windows 补全缓冲不足判定；home_dir 改用线程安全的 `getpwuid_r` 并优先
`$HOME`；download_dir 在 macOS 用 `PATH_MAX` 局部缓冲承接 `sysdir` 写入以
避免越界；exe_file 未知平台优雅返回错误（不再 `#error`）；tmp_file 两端
统一「真实创建唯一空文件」语义（Windows 用 `GetTempFileNameA`）。

> Windows 链接：`SHGetKnownFolderPath` / `CoTaskMemFree` 依赖 `shell32` 与
> `ole32`。MSVC 已由 `#pragma comment(lib, ...)` 自动链接；MinGW/clang 需
> 自行追加 `-lshell32 -lole32`。

### 使用示例

```sc
inc stdio.h
inc env.sc

fnc main: i4
    var b[4096]: char
    if env_work_dir(b, 4096) == 0
        printf("cwd = %s\n", b)
    if env_exe_file(b, 4096) == 0
        printf("exe = %s\n", b)
    return 0
```

## mem —— 内存池子项目

`inc mem.sc` 引入。提供池化内存管理，作为 `malloc`/`calloc`/`realloc`/`free`
的高性能替代：小对象走每线程 TLS 堆（无锁），大对象（>64KiB）直走系统分配；
跨线程释放经无锁 MPSC 队列回收到属主线程。另提供 `arena` 竞技场用于批量同
生命周期分配，以及 `shm` 跨进程命名共享内存。核心为 **C 侧自由函数**（`@fnc
name::` 无函数体，链接期由 `mem_impl.c` 注入），平台适配经由 `platform.h`
（`TLS` + `sc_*` 原子）。

目录结构（`builtins/mem/`）：mem.sc（sc 侧声明）、mem.h（C ABI 契约）、
mem_impl.c（默认实现）。

### 自由函数

| 函数 | 签名 | 对应 | 说明 |
|------|------|------|------|
| chunk | `&, size: u8` | malloc | 申请至少 `size` 字节；`size==0` 当 1 处理，永不返回 nil（除非内存耗尽） |
| chunk0 | `&, size: u8` | calloc | 同 chunk 但内容清零 |
| chunk_array | `&, count: u8, size: u8` | calloc(n,size) | 分配并清零 `count*size` 字节；`count*size` 溢出（超过 `SIZE_MAX`）返回 nil |
| chunk_aligned | `&, size: u8, align: u8` | aligned_alloc | 起始地址对齐到 `align`（须 2 的幂）；`align<=16` 退化为 chunk，非 2 的幂返回 nil。块可 recycle/refit/mem_usable。用于 SIMD（32/64）、缓存行（64）、页对齐 |
| refit | `&, p: &, size: u8` | realloc | 扩缩到 `size` 并保留旧内容；`p==nil` 等价 chunk；`size==0` 等价 recycle 并返回 nil；超对齐块 refit 保持原对齐 |
| recycle | `p: &` | free | 归还内存到池；`p==nil` 安全；**可由任意线程调用**（跨线程释放安全） |
| mem_usable | `u8, p: &` | malloc_usable_size | 返回该块实际可用字节数（≥ 申请值，因按尺寸档对齐） |
| mem_trim | `u8` | malloc_trim | 归还**当前线程**空闲堆页回 OS，返回释放字节数；仅当本线程无存活分配（`count==0`）时生效，否则保守不动。适合工作线程闲暇时压缩驻留 |
| mem_stat | `out: mem_stat_t&` | mallinfo | 填充统计快照（见下）；`out==nil` 安全空操作 |
| mem_teardown | （无） | — | 归还所有池化页与线程堆，并清零全部统计；**仅在所有线程静止时调用**（通常进程退出前） |

### 内存统计 mem_stat

`mem_stat(&s)` 填充一个 `mem_stat_t` 快照：

| 字段（`u8`） | 含义 |
|------|------|
| reserved | 向 OS 申请并仍持有的总字节（池化页 + 活跃大对象，含对象头），近似内存占用 |
| live | 当前分配给用户的可用字节（usable 口径，小对象按尺寸档计） |
| peak_live | `live` 历史峰值（最高水位）。**单线程精确**；多线程下为各线程峰值之和的上界 |
| count | 当前活跃（未归还）分配块数 |
| allocs | 累计成功分配次数 |
| frees | 累计成功归还次数 |

- 恒等式 `count == allocs - frees`（含在途的跨线程归还）；`reserved >= live`。
- **快照口径**：小对象遍历各线程 TLS 堆累加（无锁），大对象走全局原子计数。
  并发分配/释放时各线程堆计数为近似值；需精确一致请在所有线程静止时调用。
- **跨线程归还延迟计入**：A 线程分配、B 线程 `recycle` 的块，在物主线程 A
  下次分配并回之前仍计入 `live`/`count`，尚未计入 `frees`。
- `arena` 走独立分配器，**不计入** `mem_stat`。
- `mem_teardown` 会把全部统计清零（含累计 allocs/frees）。

### arena 竞技场

`arena` 用于批量、同生命周期的分配：逐块分配但整体释放，无需对每块调用
`recycle`。适合「一次请求/一帧/一轮解析」内大量小对象的场景。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `cap: u8` | 初始化，`cap` 为每块底层页的建议容量（`0` 取默认 64KiB） |
| chunk | `&, size: u8` | 从竞技场线性分配 `size` 字节（16 对齐）；容量不足时自动追加新页 |
| reset | （无） | 重置游标复用已有页，**不释放内存**（快速重用） |
| drop | （无） | 释放竞技场全部页，置 `h=nil` |

### 设计与多线程语义

- **分尺寸档空闲链**：1..65536 字节映射到 44 个 16 对齐尺寸档，回收即入对应
  空闲链，再分配 O(1) 命中。对象头 16 字节（owner + info，按「已分配 / 空闲 /
  跨线程」状态分时复用）。
- **每线程 TLS 堆**：小对象分配/本线程释放全程无锁。唯一共享状态是堆注册表
  与每堆的跨线程释放 MPSC 队列，均用 `sc_*` 原子（CAS 入队 + 单消费者
  exchange 排空，ABA 安全）。已通过 TSan / ASan 200 万次跨线程分配/释放压测。
- **线程退出堆回收**：线程结束时其私有堆被标记为「废弃」，新线程优先抢占复用
  （CAS 标志 1→0，注册表只增不删故无 ABA），避免死线程堆永驻、其上未并回的
  跨线程释放永不消费。POSIX 经 `pthread_key` 析构器、Windows 经 FLS 回调自动触发。
- **保留语义与主动压缩**：底层页一经取得默认不归还 OS（换取分配热路径零 `mmap`）；
  需要「用完即还」时调用 `mem_trim()`（当前线程无存活分配时释放其全部空闲页），
  或对批量场景用 `arena` + `drop`，进程退出/测试用 `mem_teardown`。
- **超对齐对象**：`chunk_aligned` 经独立哨兵路径（不进尺寸档），过量分配后回退
  指针保存原始基址与对齐，故仍可 `recycle`/`refit`/`mem_usable`。
- **大对象旁路**：>64KiB 直接 `malloc`/`free`，不进尺寸档。
- **调试构建**：以 `-DMEM_DEBUG` 编译 `mem_impl.c` 时，对象头增设魔数守卫，
  `recycle`/`refit` 校验，捕获双重释放、释放非本池/已损坏指针（best-effort，
  缓冲区越界检测由编译器 `--check=mem` 负责）。默认关闭，零开销。
- **ABI**：sc `u8` ↔ C `uint64_t`（精确匹配，内部转 `size_t`）；返回指针
  恒 16 字节对齐（`max_align_t`）。

### 使用示例

```sc
inc stdio.h
inc mem.sc

fnc main: i4
    var p: & = chunk(100)
    var s: char& = p: char&
    sprintf(s, "pooled-%d", 42)
    printf("usable=%llu text=%s\n", mem_usable(p), s)
    p = refit(p, 5000)          # 扩容保留内容
    recycle(p)

    var a: arena                # 竞技场批量分配
    a.init(0)
    var i: i4 = 0
    for i = 0; i < 1000; i++
        var q: & = a.chunk(48)
    a.drop()                    # 整体释放

    mem_teardown()              # 进程退出前归还池
    return 0
```

### shm 跨进程命名共享内存

一块由 `name` 标识的命名内存区，可被多个进程映射到各自地址空间共享读写。
跨平台：POSIX 用 `shm_open` + `mmap`；Windows 用 `CreateFileMapping` + `MapViewOfFile`。

| 接口 | 类型/签名 | 说明 |
|------|---------|------|
| shm.make | `bool, name: const char&, size: u8, flags: u4` | 创建或附着命名区，映射 `size` 字节（0 为 1，向上取整到页）；已存在则附着（要求容量足够，`size()` 回报真实容量）。`flags`：0 默认读写、1 只读（`SHM_RDONLY`，仅附着不创建）、2 独占创建（`SHM_EXCL`，已存在则失败），可按位或。成功 1 / 失败 0 |
| shm.data | `&` | 本进程映射首地址；未映射返回 nil |
| shm.size | `u8` | 实际映射字节数（附着已存在区时为其底层真实容量）；未映射返回 0 |
| shm.drop | （无） | 解除映射 + 关闭句柄（不删除命名）；可重复调用 |
| shm_remove | `bool, name: const char&`（自由函数） | 删除命名区（POSIX `shm_unlink`）；Windows 无需删除返回 1 |

- **标志**：`SHM_RDONLY`(1) 以只读保护映射（POSIX `PROT_READ` / Windows `FILE_MAP_READ`），
  单独使用时仅附着已存在区、不创建；`SHM_EXCL`(2) 独占创建，区已存在则 `make` 失败
  （POSIX `O_EXCL` / Windows `ERROR_ALREADY_EXISTS`），用于「确保自己是创建者」。

- **命名约定**：用简单标记（字母/数字/下划线），勿含路径分隔符；POSIX 实现内部自动加前导 `/`。
- **生命周期**：POSIX 命名区一经创建即持久，所有进程 `drop` 后仍需 `shm_remove(name)` 才真正销毁；
  Windows 为内核对象引用计数，最后句柄关闭即销毁（`shm_remove` 空操作）。
- **并发**：共享页内存一致（同一物理页），区内并发读写同步由调用方负责（可将 m 模块原语或 `sc_*` 原子置于区内）。
- **链接**：较老 glibc 的 `shm_open`/`shm_unlink` 在 librt（需 `-lrt`）；glibc >= 2.34 已并入 libc；macOS/BSD 在 libc 内。

```sc
inc stdio.h
inc mem.sc

fnc main: i4
    var nm: char& = "my_region"
    var s: shm
    if s.make(nm, 4096, 0)
        var d: char& = s.data(): char&
        sprintf(d, "hello from pid")   # 其他进程 make(nm, ..., 0) 后可读到
        s.drop()                       # 解除本进程映射
    shm_remove(nm)                      # 销毁命名区（POSIX）
    return 0
```

## platform.h —— 跨平台基础头

`builtins/platform.h` 不是 sc 模块，而是面向用户的单头文件 C 跨平台层
（参考摘取自 stdc），builtins 内各 C 实现（adt_impl.c / m_impl.c ...）
也统一经由它做平台适配。builtins 根目录默认加入编译 `-I`，因此
用户代码中 `inc "platform.h"` 与 `inc stdint.h` 一样开箱即用；
随其他 builtins 资源一并内嵌/释放。

提供：平台判定宏（`P_WIN`/`P_DARWIN`/`P_BSD`/`P_LINUX`/`P_POSIX`/
`P_POSIX_LIKE`）、平台基础头引入、路径分隔符（`P_SEP`/`P_IS_SEP`）、
`TLS` 线程局部存储、字节序（`BYTE_ORDER`）、时钟（`P_clock` 类型，
`P_time_now` 墙钟 / `P_clock_now` 单调 / `P_cost_now` CPU 耗时，
`clock_s/ms/us` 取值与 `*_diff` 差值等宏族，`P_tick_s/ms/us` 快捷计时）、
微秒休眠（`P_usleep`，Windows 毫秒精度向上取整）、
CPU 逻辑核数（`P_ncpu`）。

新增 builtins 实现时应统一经由本头文件做平台适配，不在实现内散落
`#ifdef` 平台分支。
