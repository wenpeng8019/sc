# builtins 内置模块参考

`builtins/` 是 scc 的内置模块搜索路径：`inc x.sc` 会依次尝试
`builtins/x.sc` 与子项目形态 `builtins/x/x.sc`。
也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。

当前内置模块：

| 模块 | 引入方式 | 说明 |
|------|----------|------|
| adt | `inc adt.sc` | 抽象数据类型：string（动态字符串）、list（动态指针数组） |
| m | `inc m.sc` | 多线程语言支持标准：run/wait 语句、thread、mutex、cond、pool（线程池） |

另有 `platform.h`（非模块）：面向用户的 C 跨平台基础头，默认 -I 可直接 inc，见末节。

## 子项目通用机制

子项目形态 `builtins/x/` 由三件套构成：

| 文件 | 角色 |
|------|------|
| x.sc | 唯一事实源：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体） |
| x.h | C ABI 契约（与 x.sc 同步维护），自定义/默认实现据此编写 |
| x_impl.c | 默认实现，编译器自动编译并链接（`-I` 自身目录与 builtins 根） |

单元图包含 `<目录>/x/x.sc` 且同目录存在 `x_impl.c`（或内嵌发行版释放的
预编译 `x.a`）时，实现自动参与链接；两者皆无则跳过（不影响纯 sc 子项目）。

## adt —— 抽象数据类型子项目

目录结构（`builtins/adt/`）：

| 文件 | 角色 |
|------|------|
| adt.sc | 唯一事实源：`@def` 数据布局 + `@fnc T::m` 方法声明（无函数体） |
| adt.h | C ABI 契约（与 adt.sc 同步维护），自定义实现据此编写 |
| adt_impl.c | 默认实现，编译器自动编译并链接 |

### 工作机制

1. sc 源码 `inc adt.sc` 后即可使用 `string`/`list` 类型及其方法。
2. 方法声明（`fnc T::m` 无函数体）转 C 时生成 extern 原型 `T_m(T *_this, ...)`，
   实现由链接期注入。
3. 单元图包含 `builtins/adt/adt.sc` 时，scc 自动编译并链接默认实现
   `adt_impl.c`；可替换为自定义实现：

```sh
scc app.sc --adt my_adt.c      # .c 自动编译；.o/.a 直接参与链接
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
| assign | `bool, s&: char` | 赋值为 C 字符串 |
| append | `bool, s&: char` | 追加 C 字符串 |
| append_n | `bool, s&: char, n: u8` | 追加前 n 字节 |
| append_char | `bool, c: char` | 追加单字符 |
| insert | `bool, index: u8, s&: char` | 指定位置插入 |
| erase | `bool, index: u8, n: u8` | 删除 n 字节 |
| at | `char, index: u8` | 取字符（越界返回 0） |
| find | `i8, sub&: char, start: u8` | 查找子串（未找到 -1） |
| rfind | `i8, sub&: char` | 反向查找（未找到 -1） |
| equals | `bool, s&: char` | 与 C 字符串比较相等 |
| starts_with | `bool, s&: char` | 前缀判断 |
| ends_with | `bool, s&: char` | 后缀判断 |
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

## m —— 多线程语言支持标准

多线程将逐步成为 sc 语言特性的一部分，本模块是其支持标准；后续按
语言特性需要扩展（条件变量/原子操作/线程局部存储等）。

目录结构（`builtins/m/`）：m.sc（事实源）、m.h（C ABI 契约）、
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

另提供 rpc 仅声明 `msleep: ms: u4`：当前线程休眠毫秒（C 侧实现）。

thread 不可手工构造（无 init）；`run` 是唯一创建途径。

### mutex —— 互斥锁

| 方法 | 签名 | 说明 |
|------|------|------|
| init | | 构造（声明即构造适用） |
| drop | | 析构 |
| lock / unlock | | 加锁 / 解锁 |
| try_lock | `bool` | 取锁成功返回 1，已被占用返回 0 |

### cond 与 wait 语句 —— 条件变量

条件等待由 `wait` 语句完成（语言特性，编译器生成 `cond_wait` 调用）：

```sc
wait c, mu            # 无限等待（调用前须已持有 mu）
wait c, mu, nsec, sec # 超时等待（nsec/sec 全 0 等价于无限等待）
```

c/mu 可为对象或指针，对象自动取地址；被虚假唤醒需循环复查条件。
Windows 下超时精度为毫秒（纳秒向上取整）。

| 方法 | 说明 |
|------|------|
| init / drop | 构造 / 析构 |
| one | 唤醒一个等待者 |
| all | 唤醒全部等待者 |

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

刻意不提供 future/cancel/动态扩容：任务级同步用 cond + wait 语句，
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
    msleep(50)                 # 等 detach 线程结束
    c.mu.drop()
    printf("n=%d\n", c.n)
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
