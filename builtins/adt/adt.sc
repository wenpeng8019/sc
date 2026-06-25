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

# ---------------- list：段式裸 @ 自动指针容器 ----------------
# 元素为裸自动指针 @（sc_afat），list 拥有元素（每元素一份 retain）。
# 段式存储：元素 i 住第 i/LIST_SEG 段的第 i%LIST_SEG 槽；段索引表与各段内存均来自 mem
#   chunk（不受全局 -DSC_POOL 影响）。核心接口与 array 一致，区别仅在元素是 ref 句柄而非值块。
# 取出语义「取用分离」：get 借用（返回句柄、不改计数）；pop/remove_at 仅删除并 release（返回
#   bool）。要取并保留：先 get 借用 → (x: T@) 还原绑定（retain）→ 再 pop（release）。
# push retain（目标 in++）、pop/remove_at/set/clear/drop release（in--，触零自析构，dtor 随句柄）。

@fnc list_cmp: i4, a: &, b: &                           # sort 比较回调类型（实参为元素 .p 实体基址）

@def list: {
    segs: &&      # 段索引表（sc_afat**；内部，sc 侧不直接访问）
    nsegs: u4     # 已分配段数
    size: u4      # 元素个数
    cap: u4       # 总槽位（nsegs * LIST_SEG）

    fnc init::                                       # 构造为空列表
    fnc drop::                                       # 释放全部 retain + 回收段内存
    fnc len:: u8                                    # 元素个数
    fnc clear::                                      # 清空并 release 全部元素（保留段容量）
    fnc reserve:: bool, n: u8                       # 预留槽位
    fnc push:: bool, value: @                         # 尾部追加（retain 元素）
    fnc pop:: bool                                   # 删除并 release 尾元素（空返回 false）
    fnc get:: @, index: u8                           # 借用元素句柄（越界返回空句柄；不改计数）
    fnc set:: bool, index: u8, value: @               # 改写元素（retain 新、release 旧）
    fnc insert:: bool, index: u8, value: @            # 指定位置插入（retain 元素）
    fnc remove_at:: bool, index: u8                  # 删除并 release 该元素（越界返回 false）
    fnc index_of:: i8, value: @                       # 按 .p 实体基址查找（未找到 -1）
    fnc reverse::                                    # 原地反转
    fnc clone:: bool, out: list&                    # 逐元素 retain 到 out
    fnc sort:: cmp: list_cmp                         # 按比较回调排序
}

# ---------------- dict：开放寻址裸 @ 自动指针映射 ----------------
# value 为裸自动指针 @（sc_afat），dict 拥有 value（每条一份 retain）。
# key 由 init 的 key_size 三态决定，接口的 key 均为 const & 裸指针，按 key_size 解读：
#   key_size  > 0：定长数值/POD，内联拷贝 key_size 字节，memcmp 比较（浮点键不安全，限整数/指针类）；
#   key_size == 0：引用字符串，仅存 const char& 借用指针——dict 不拥有，字符串本体由 value 对象自持；
#   key_size == -1：拷贝字符串，put 时 chunk 复制、remove/clear/drop 时 recycle。
# 开放寻址：全部 item 内联住一整块桶数组，无 per-item 分配；整张表仅 ctrl + slots 两块（mem chunk），
#   resize 整体 rehash 重建。每桶 [@ value][key]，value 在前保 8 对齐。
# 取出语义同 list「取用分离」：get 借用（返回句柄不改计数）；remove 删除并 release（返回 bool）。
# 因 init 带 key_size 参数，不参与「声明即构造」——须显式 d.init(key_size)。

@fnc dict_each_fn: bool, key: const &, value: @, ctx: &  # 遍历回调（返回 false 提前终止）

@def dict: {
    ctrl: u1&       # 控制字节数组（空/墓碑/占用）
    slots: char&    # 桶数据（nbuckets * stride，每桶 [@ value][key]）
    key_size: i4    # >0 定长 / 0 引用字符串 / -1 拷贝字符串
    stride: u4      # 单桶字节 = align8(sizeof(@) + keylen)
    size: u4        # 元素数
    used: u4        # 占用 + 墓碑（rehash 阈值用）
    nbuckets: u4    # 桶数（2 的幂；0 = 未分配）

    fnc init:: key_size: i4                          # 构造（指定 key 模式）
    fnc drop::                                       # 释放全部 retain + 回收桶/控制块
    fnc len:: u8                                    # 元素个数
    fnc has:: bool, key: const &                     # 是否含 key
    fnc get:: @, key: const &                        # 借用 value 句柄（未命中返回空句柄；不改计数）
    fnc put:: bool, key: const &, value: @           # 插入/替换（retain 新、替换 release 旧）
    fnc remove:: bool, key: const &                  # 删除并 release value（未命中返回 false）
    fnc clear::                                      # 清空并 release 全部 value（保留桶容量）
    fnc each:: fn: dict_each_fn, ctx: &              # 无序遍历占用桶（回调返 false 即停）

    # 整数游标双向遍历（游标 = 桶下标；空集/越界返回 -1）。each 与 next 走同一桶序。
    # 游标在 get/has/each 期间稳定；put/remove 可能 rehash 使其失效，遍历期间勿增删。
    fnc first:: i8                                  # 首个占用桶游标（空集 -1）
    fnc last:: i8                                   # 末个占用桶游标（空集 -1）
    fnc next:: i8, cur: i8                          # cur 之后的占用桶（无则 -1）
    fnc prev:: i8, cur: i8                          # cur 之前的占用桶（无则 -1）
    fnc key_at:: const &, cur: i8                   # 游标处 key（无效返回 nil）
    fnc value_at:: @, cur: i8                       # 游标处 value 借用（无效返回空句柄）
}



