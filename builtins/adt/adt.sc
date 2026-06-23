# adt —— sc 内置抽象数据类型（字符串/列表）
#
# 本文件是 adt 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧
#
# 默认实现：同目录 adt_impl.c（编译器自动编译并链接）
# 自定义实现：scc --adt <x.c|x.o|x.a>（按同目录 adt.h 契约实现）
#
# 构造/析构约定：
#   init —— 构造。函数内 `var s: string` 声明即自动调用 string_init(&s)
#           （仅限无初值、非指针、非数组的局部变量；全局变量需手动 init）
#   drop —— 析构。需手动调用 s.drop()（命名保留，未来支持自动插入）

# ---------------- string：动态字符串 ----------------
# 内部保证 NUL 结尾，cstr() 可直接交给 C 接口

@def string: {
    data: char&     # 缓冲区（NUL 结尾；空串可为 nil）
    size: u8      # 字符数（不含 NUL）
    cap: u8       # 缓冲区容量

    fnc init::                                     # 构造为空串
    fnc drop::                                     # 释放缓冲区
    fnc len:: u8                                  # 字符数
    fnc cstr:: char&                              # C 字符串视图（始终非 nil）
    fnc clear::                                    # 置空（保留容量）
    fnc reserve:: bool, n: u8                     # 预留容量
    fnc assign:: bool, s: const char&             # 赋值为 C 字符串
    fnc append:: bool, s: const char&             # 追加 C 字符串
    fnc append_n:: bool, s: const char&, n: u8    # 追加前 n 字节
    fnc append_char:: bool, c: char               # 追加单字符
    fnc insert:: bool, index: u8, s: const char&  # 指定位置插入
    fnc erase:: bool, index: u8, n: u8            # 删除 n 字节
    fnc at:: char, index: u8                      # 取字符（越界返回 0）
    fnc find:: i8, sub: const char&, start: u8    # 查找子串（未找到 -1）
    fnc rfind:: i8, sub: const char&              # 反向查找（未找到 -1）
    fnc equals:: bool, s: const char&             # 与 C 字符串比较相等
    fnc starts_with:: bool, s: const char&        # 前缀判断
    fnc ends_with:: bool, s: const char&          # 后缀判断
    fnc slice:: bool, start: i8, stop: i8, out: string&  # 切片（负索引从尾部计）
    fnc strip::                                    # 去除首尾空白
    fnc lower::                                    # 转小写（ASCII）
    fnc upper::                                    # 转大写（ASCII）
    fnc clone:: bool, out: string&                # 深拷贝到 out
}

# ---------------- list：动态指针数组 ----------------
# 元素为裸指针（&），不拥有元素：drop/clear/remove 不释放元素本身

@fnc list_cmp: i4, a: &, b: &                           # sort 比较回调类型

@def list: {
    items: &&      # 元素数组
    size: u8      # 元素个数
    cap: u8       # 已分配槽位

    fnc init::                                       # 构造为空列表
    fnc drop::                                       # 释放槽位数组（不释放元素）
    fnc len:: u8                                    # 元素个数
    fnc clear::                                      # 清空（保留容量）
    fnc reserve:: bool, n: u8                       # 预留槽位
    fnc push:: bool, value: &                        # 尾部追加
    fnc pop:: &                                     # 弹出尾元素（空返回 nil）
    fnc get:: &, index: u8                          # 取元素（越界返回 nil）
    fnc set:: bool, index: u8, value: &              # 改写元素
    fnc insert:: bool, index: u8, value: &           # 指定位置插入
    fnc remove_at:: &, index: u8                    # 删除并返回该元素
    fnc index_of:: i8, value: &                      # 查找元素位置（未找到 -1）
    fnc reverse::                                    # 原地反转
    fnc clone:: bool, out: list&                    # 浅拷贝到 out
    fnc sort:: cmp: list_cmp                         # 按比较回调排序
}

