# adt —— sc 内置抽象数据类型（字符串/数组/列表）
#
# 本文件是 adt 的唯一事实源：
#   @def 定义纯数据结构布局（C ABI 契约的一部分）
#   fnc name:: 方法声明（无函数体）：extern 原型，实现在 C 侧
#
# 默认实现：同目录 adt_impl.c（编译器自动编译并链接）
# 自定义实现：scc --adt <x.c|x.o|x.a>（按同目录 adt.h 契约实现）
#
# 构造/析构约定：
#   init —— 构造。值类型（如 list）函数内 `var l: list` 声明即自动调用 list_init(&l)
#           （仅限无初值、非指针、非数组的局部变量；全局变量需手动 init）。
#           堆专属 string 经 string()/string("初值") 构造（带参转发 init）。
#   drop —— 析构。需手动调用 s->drop()/l.drop()（命名保留，未来支持自动插入）

# ---------------- string：动态字符串 ----------------
# 内部保证 NUL 结尾，cstr() 可直接交给 C 接口
# 堆专属（def string&）：不存在 string 值类型，只能用 string&（普通指针）/string@（自动指针）。
#   string() 堆构造，末尾 s.drop() 释放（drop 释放缓冲 + 结构体块）。

@def string&: {
    data: char&     # 缓冲区（NUL 结尾；空串可为 nil）
    size: u4      # 字符数（不含 NUL）
    cap: u4       # 缓冲区容量

    fnc init:: s: const char&                      # 构造（s 为 nil → 空串）
    fnc drop::                                     # 释放缓冲区
    fnc len:: u8                                  # 字符数
    fnc cstr:: char&                              # C 字符串视图（始终非 nil）
    fnc clear::                                    # 置空（保留容量）
    fnc reserve:: bool, n: u8                     # 预留容量
    fnc assign:: bool, s: const char&             # 赋值为 C 字符串
    fnc append:: bool, s: const char&             # 追加 C 字符串
    fnc append_n:: bool, s: const char&, n: u8    # 追加前 n 字节
    fnc append_char:: bool, c: char               # 追加单字符
    fnc printf:: bool, fmt: const char&, ...      # 追加格式化文本（vsnprintf 探测扩容）
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

# ---------------- array：动态值数组 ----------------
# 元素为定长值块（逐字节复制、无唯一身份），以单元素字节数 elem_sz 参数化。
# 本质上 array 与 string 同构：string 即 elem_sz=1 的 char array，故接口/能力对应一致：
#   string.cstr → array.data ；string.at → array.at（返回元素指针，非值）；
#   string.append_char → array.push ；string.append_n → array.append ；
#   char 专属的 printf/strip/lower/upper 无泛型对应，省去；
#   泛型比较类（find/rfind/equals/starts_with/ends_with/sort/bsearch）需传 array_cmp 回调。
# 参考 uthash 的 utarray：内联连续存储、qsort 排序、bsearch 检索。
# 因 init 带 elem_sz 参数，不参与“声明即构造”——须显式 a.init(sizeof(T))。
# data/at/pop/front/back/bsearch 返回的是指向缓冲区内部的元素指针，下次扩容/写入后可能失效。

@fnc array_cmp: i4, a: &, b: &                          # 比较回调类型（qsort/bsearch/find 契约）

@def array: {
    buf: char&      # 连续值块缓冲区（cap 个 elem_sz 字节槽位）
    size: u4      # 元素个数
    cap: u4       # 已分配槽位
    elem_sz: u4   # 单元素字节数

    fnc init:: elem_sz: u4                           # 构造（指定单元素字节数）
    fnc drop::                                       # 释放缓冲区
    fnc len:: u8                                    # 元素个数
    fnc data:: &                                    # 缓冲区视图（≈ string.cstr；空可为 nil）
    fnc clear::                                      # 清空（保留容量）
    fnc reserve:: bool, n: u8                       # 预留槽位
    fnc resize:: bool, n: u8                        # 调整元素数（新增槽位清零）
    fnc assign:: bool, src: &, n: u8                 # 赋值为 n 个元素（≈ string.assign）
    fnc append:: bool, src: &, n: u8                 # 追加 n 个元素（≈ string.append_n）
    fnc push:: bool, value: &                        # 追加单元素（≈ string.append_char）
    fnc pop:: &                                     # 弹出并返回尾元素指针（空返回 nil）
    fnc insert:: bool, index: u8, value: &           # 指定位置插入单元素（≈ string.insert）
    fnc erase:: bool, index: u8, n: u8               # 删除自 index 起 n 个元素（≈ string.erase）
    fnc at:: &, index: u8                           # 取元素指针（≈ string.at；越界返回 nil）
    fnc set:: bool, index: u8, value: &              # 改写元素（复制）
    fnc front:: &                                   # 首元素指针（空返回 nil）
    fnc back:: &                                    # 尾元素指针（空返回 nil）
    fnc find:: i8, key: &, start: u8, cmp: array_cmp     # 线性查找（≈ string.find；未找到 -1）
    fnc rfind:: i8, key: &, cmp: array_cmp               # 反向线性查找（≈ string.rfind；未找到 -1）
    fnc equals:: bool, other: array&, cmp: array_cmp     # 逐元素比较相等（≈ string.equals）
    fnc starts_with:: bool, other: array&, cmp: array_cmp  # 前缀判断（≈ string.starts_with）
    fnc ends_with:: bool, other: array&, cmp: array_cmp    # 后缀判断（≈ string.ends_with）
    fnc slice:: bool, start: i8, stop: i8, out: array&   # 切片（≈ string.slice；负索引从尾部计）
    fnc reverse::                                    # 原地反转
    fnc clone:: bool, out: array&                   # 深拷贝到 out
    fnc sort:: cmp: array_cmp                        # qsort 排序
    fnc bsearch:: &, key: &, cmp: array_cmp          # bsearch 检索（须已排序，未找到 nil）
}

# ---------------- list：动态指针数组 ----------------
# 元素为裸指针（&），不拥有元素：drop/clear/remove 不释放元素本身

@fnc list_cmp: i4, a: &, b: &                           # sort 比较回调类型

@def list: {
    items: &&      # 元素数组
    size: u4      # 元素个数
    cap: u4       # 已分配槽位

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

