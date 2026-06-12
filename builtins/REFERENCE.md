# builtins 内置模块参考

`builtins/` 是 scc 的内置模块搜索路径：`inc x.sc` 会依次尝试
`builtins/x.sc` 与子项目形态 `builtins/x/x.sc`。
也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。

当前内置模块：

| 模块 | 引入方式 | 说明 |
|------|----------|------|
| adt | `inc adt.sc` | 抽象数据类型：string（动态字符串）、list（动态指针数组） |
| m | `inc m.sc` | 多线程语言支持标准：thread（线程）、mutex（互斥锁） |

另有 `platform.h`（非模块）：builtins 内各实现的跨平台基础头，见末节。

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
返回 `b` 的方法：1 成功 / 0 失败（内存不足或参数越界）。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `v` | 构造为空串 |
| drop | `v` | 释放缓冲区 |
| len | `u8` | 字符数（不含 NUL） |
| cstr | `c1&` | C 字符串视图（始终非 nil） |
| clear | `v` | 置空（保留容量） |
| reserve | `b, n: u8` | 预留容量 |
| assign | `b, s&: c1` | 赋值为 C 字符串 |
| append | `b, s&: c1` | 追加 C 字符串 |
| append_n | `b, s&: c1, n: u8` | 追加前 n 字节 |
| append_char | `b, c: c1` | 追加单字符 |
| insert | `b, index: u8, s&: c1` | 指定位置插入 |
| erase | `b, index: u8, n: u8` | 删除 n 字节 |
| at | `c1, index: u8` | 取字符（越界返回 0） |
| find | `i8, sub&: c1, start: u8` | 查找子串（未找到 -1） |
| rfind | `i8, sub&: c1` | 反向查找（未找到 -1） |
| equals | `b, s&: c1` | 与 C 字符串比较相等 |
| starts_with | `b, s&: c1` | 前缀判断 |
| ends_with | `b, s&: c1` | 后缀判断 |
| slice | `b, start: i8, stop: i8, out&: string` | 切片 `[start, stop)`，负索引从尾部计 |
| strip | `v` | 去除首尾空白 |
| lower / upper | `v` | 大小写转换（ASCII） |
| clone | `b, out&: string` | 深拷贝到 out |

### list —— 动态指针数组

元素为 `v&`（裸指针），**不拥有元素**：drop/clear/remove_at 不释放元素本身。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `v` | 构造为空列表 |
| drop | `v` | 释放槽位数组 |
| len | `u8` | 元素个数 |
| clear | `v` | 清空（保留容量） |
| reserve | `b, n: u8` | 预留槽位 |
| push | `b, value&: v` | 尾部追加 |
| pop | `v&` | 弹出尾元素（空返回 nil） |
| get | `v&, index: u8` | 取元素（越界返回 nil） |
| set | `b, index: u8, value&: v` | 改写元素 |
| insert | `b, index: u8, value&: v` | 指定位置插入 |
| remove_at | `v&, index: u8` | 删除并返回该元素 |
| index_of | `i8, value&: v` | 按指针值查找（未找到 -1） |
| reverse | `v` | 原地反转 |
| clone | `b, out&: list` | 浅拷贝到 out |
| sort | `v, cmp&: list_cmp` | 稳定排序，`list_cmp: i4, a&: v, b&: v` |

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

句柄约定：`h&: v` 为实现私有的平台句柄（实现内部分配/释放），调用方
不直接访问；结构布局因此跨平台稳定。

### thread —— 线程

线程入口为命名函数类型 `thread_fn: v, arg&: v`，用
`fnc worker -> thread_fn` 实现。

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `v` | 构造为空（声明即构造适用） |
| start | `b, f&: thread_fn, arg&: v` | 启动线程（已启动未回收返回 0） |
| join | `v` | 等待结束并回收 |
| drop | `v` | 未 join 的线程 detach 后释放 |
| sleep | `v, ms: u4` | 当前线程休眠（与接收者实例无关） |

start 后必须 join（回收）或 drop（detach 释放）二选一。

### mutex —— 互斥锁

| 方法 | 签名 | 说明 |
|------|------|------|
| init | `v` | 构造（声明即构造适用） |
| drop | `v` | 析构 |
| lock / unlock | `v` | 加锁 / 解锁 |
| try_lock | `b` | 取锁成功返回 1，已被占用返回 0 |

### 使用示例

完整示例见 `examples/feature7.sc`：

```sc
inc stdio.h
inc m.sc

def ctx: {
    mu: mutex
    n: i4
}

fnc worker -> thread_fn
    var c&: ctx = (arg: ctx&)
    c->mu.lock()
    c->n = c->n + 1
    c->mu.unlock()

fnc main: i4
    var c: ctx
    c.n = 0
    c.mu.init()        # 嵌套字段不自动构造，手动 init
    var t: thread      # 声明即构造
    t.start(worker, &c)
    t.join()
    c.mu.drop()
    printf("n=%d\n", c.n)
    return 0
```

## platform.h —— 跨平台基础头

`builtins/platform.h` 不是 sc 模块，而是 builtins 内各 C 实现
（adt_impl.c / m_impl.c ...）共用的单头文件跨平台层（参考摘取自 stdc），
随其他 builtins 资源一并内嵌/释放。实现编译时自动 `-I` builtins 根目录。

提供：平台判定宏（`P_WIN`/`P_DARWIN`/`P_BSD`/`P_LINUX`/`P_POSIX`/
`P_POSIX_LIKE`）、平台基础头引入、路径分隔符（`P_SEP`/`P_IS_SEP`）、
`TLS` 线程局部存储、字节序（`BYTE_ORDER`）、单调时钟
（`P_clock`/`P_clock_now`/`P_clock_ms`）、毫秒休眠（`P_msleep`）。

新增 builtins 实现时应统一经由本头文件做平台适配，不在实现内散落
`#ifdef` 平台分支。
