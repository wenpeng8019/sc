# builtins 内置模块参考

`builtins/` 是 scc 的内置模块搜索路径：`inc x.sc` 会依次尝试
`builtins/x.sc` 与子项目形态 `builtins/x/x.sc`。
也可用环境变量 `SCC_BUILTINS` 指定额外搜索目录。

当前内置模块：

| 模块 | 引入方式 | 说明 |
|------|----------|------|
| adt | `inc adt.sc` | 抽象数据类型：string（动态字符串）、list（动态指针数组） |

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
