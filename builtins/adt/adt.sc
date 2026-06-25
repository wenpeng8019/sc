# adt —— sc 内置抽象数据类型（字符串/数组/环形队列/列表/字典/二叉搜索树/堆/前缀树/LRU 缓存）
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

# ---------------- ring：SPSC 无锁循环队列（kfifo 风格） ----------------
# 单生产者单消费者无锁有界队列。元素为定长值块（elem_sz 字节，逐字节复制），容量为 2 的幂。
# head（消费者独写）/ tail（生产者独写）为自由递增计数器，槽位下标 = idx & mask，
#   元素数 = tail - head（无符号回绕）。生产者 release 发布 tail、消费者 acquire 观测，
#   保证数据写入先于索引可见；空 = (tail==head)，满 = (tail-head > mask)。
# 约束：仅 SPSC 安全——单生产者线程只调 push、单消费者线程只调 pop/peek；
#   多生产者/多消费者须外部加锁。init/drop/clear 仅在无并发访问时调用。
#   head/tail 同 cache 行存在伪共享，极端吞吐场景可自行加 padding。
# 因 init 带参数，不参与「声明即构造」——须显式 r.init(sizeof(T), capacity)。

@def ring: {
    buf: char&      # 连续值块缓冲区（cap = mask+1 个 elem_sz 字节槽）
    head: u4        # 消费者索引（自由递增；& mask 取槽）
    tail: u4        # 生产者索引（自由递增）
    mask: u4        # cap - 1（cap 为 2 的幂）
    elem_sz: u4     # 单元素字节数

    fnc init:: bool, elem_sz: u4, capacity: u4       # 构造（capacity 向上取 2 幂；分配失败返回 false）
    fnc drop::                                       # 释放缓冲区
    fnc cap:: u8                                    # 容量（2 的幂；未初始化 0）
    fnc len:: u8                                    # 当前元素数快照（tail - head）
    fnc is_empty:: bool                             # 是否空
    fnc is_full:: bool                              # 是否满
    fnc clear::                                      # 复位 head/tail（仅无并发时安全）
    fnc push:: bool, value: &                        # 生产者入队一个元素（满返回 false）
    fnc pop:: bool, out: &                           # 消费者出队到 out（空返回 false）
    fnc peek:: &                                    # 消费者借用队首元素指针（空返回 nil）
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

# ---------------- bst：AVL/红黑 融合的有序映射 ----------------
# 单棵树以 red_depth 区分 AVL(0) 与红黑(1)——本质是「容忍不平衡的深度」不同（AVL 最多差 1 层，
#   红黑多容忍 1 层）。value 为裸自动指针 @（sc_afat），bst 拥有每节点一份 retain；key 三态同 dict：
#   key_size  > 0：定长数值/POD，内联拷贝；默认按宽度（1/2/4/8）做有符号整数比较，其余宽度退化字节序；
#                  浮点/无符号/复合键须传 cmp 自定义比较器；
#   key_size == 0：引用字符串，仅存 const char& 借用指针（bst 不拥有）；strcmp 比较；
#   key_size == -1：拷贝字符串，put 时 chunk 复制、remove/clear/drop 时 recycle；strcmp 比较。
# cmp 非空时一律走自定义比较（返回 sign(a-b)，a/b 为逻辑键：数值模式为键字节指针、字符串模式为 char&）。
# 对齐安全：节点用内部父指针自然对齐设计，无 pack(1) 外置检索栈，数值比较经 memcpy 装载，杜绝非对齐 UB。
# 取出语义同 dict「取用分离」：get 借用（返回句柄不改计数）；remove 删除并 release（返回 bool）。
# 因 init 带参数，不参与「声明即构造」——须显式 t.init(red_depth, key_size, cmp, ctx)。

@fnc bst_cmp_fn: i4, a: const &, b: const &, ctx: &      # 自定义比较器（返回 sign(a-b)；nil=内置）
@fnc bst_each_fn: bool, key: const &, value: @, ctx: &   # 中序遍历回调（返回 false 提前终止）

@def bst: {
    root: &          # 根节点（不透明 bst_node*；内部）
    head: &          # 中序首节点
    rear: &          # 中序末节点
    cmp: &           # 自定义比较器指针（nil = 内置）
    cmp_ctx: &       # 比较器上下文
    size: u8         # 元素数
    key_size: i4     # >0 定长数值 / 0 引用字符串 / -1 拷贝字符串
    red_depth: u1    # 0 = AVL / 1 = 红黑（>1 预留，按红黑处理）

    fnc init:: red_depth: u1, key_size: i4, cmp: bst_cmp_fn, ctx: &   # 构造（红黑/AVL + key 模式 + 比较器）
    fnc drop::                                       # 释放全部 retain + 回收全部节点
    fnc len:: u8                                    # 元素个数
    fnc has:: bool, key: const &                     # 是否含 key
    fnc get:: @, key: const &                        # 借用 value 句柄（未命中返回空句柄；不改计数）
    fnc put:: bool, key: const &, value: @           # 插入/替换（retain 新、替换 release 旧）
    fnc remove:: bool, key: const &                  # 删除并 release value（未命中返回 false）
    fnc clear::                                      # 清空并 release 全部 value
    fnc each:: fn: bst_each_fn, ctx: &              # 中序（升序）遍历（回调返 false 即停）

    # 游标双向遍历（游标 = 不透明节点 token；空集/越界返回 0）。中序升序，O(1) 取前驱后继。
    # 游标在只读操作期间稳定；put/remove 可能回收节点使其失效，遍历期间勿增删。
    fnc first:: i8                                  # 中序首节点游标（空集 0）
    fnc last:: i8                                   # 中序末节点游标（空集 0）
    fnc next:: i8, cur: i8                          # 后继游标（无则 0）
    fnc prev:: i8, cur: i8                          # 前驱游标（无则 0）
    fnc key_at:: const &, cur: i8                   # 游标处 key（无效返回 nil）
    fnc value_at:: @, cur: i8                       # 游标处 value 借用（无效返回空句柄）
    fnc index_of:: i8, key: const &                 # key 的 0 基中序序号（未命中 -1）
    fnc at:: i8, index: u8                          # 0 基中序序号处的游标（越界 0）
    fnc most:: i8, key: const &                     # <= key 的最接近项游标（前驱或等于；无 0）
    fnc least:: i8, key: const &                    # >= key 的最接近项游标（后继或等于；无 0）
}


# ---------------- heap：数组背二叉堆 / 优先队列 ----------------
# (key, value) 对：key 决定优先级，value 为裸自动指针 @（sc_afat），heap 拥有每元素一份 retain。
# 数组背完全二叉树（隐式编码：父 (i-1)/2、左右子 2i+1/2i+2），槽连续 [@ value][key]，value 在前保 8 对齐。
# push 末尾追加后上滤、pop 末尾补根后下滤，均 O(log n)；扩容几何增长。
# min 决定堆向：min=1 小键在顶（最小堆，典型用于 Dijkstra/定时器）、min=0 大键在顶（最大堆）。
# key 三态/比较器语义同 bst：key_size >0 定长数值（按宽度有符号）/ ==0 引用字符串 / ==-1 拷贝字符串；
#   cmp 非空时一律走自定义比较（返回 sign(a-b)，a/b 为逻辑键指针）。
# 取出语义同 dict「取用分离」：peek 借用堆顶（不改计数），pop 删除并 release（返回 bool）。
# 不提供遍历游标——堆数组非优先序。优先级变更用「推新+pop 时跳过陈旧」惰性删除。
# 因 init 带参数，不参与「声明即构造」——须显式 h.init(min, key_size, cmp, ctx)。

@fnc heap_cmp_fn: i4, a: const &, b: const &, ctx: &     # 自定义比较器（返回 sign(a-b)；nil=内置）

@def heap: {
    slots: char&     # 连续槽数组（cap * stride，每槽 [@ value][key]；内部）
    cmp: &           # 自定义比较器指针（nil = 内置）
    cmp_ctx: &       # 比较器上下文
    stride: u4       # 单槽字节 = align8(sizeof(@) + keylen)
    size: u4         # 元素数
    cap: u4          # 槽容量
    key_size: i4     # >0 定长数值 / 0 引用字符串 / -1 拷贝字符串
    min: u1          # 1 = 最小堆（小键在顶）/ 0 = 最大堆

    fnc init:: min: u1, key_size: i4, cmp: heap_cmp_fn, ctx: &   # 构造（堆向 + key 模式 + 比较器）
    fnc drop::                                       # 释放全部 retain + 回收槽数组
    fnc len:: u8                                    # 元素个数
    fnc is_empty:: bool                             # 是否空
    fnc clear::                                      # 清空并 release 全部 value（保留容量）
    fnc reserve:: bool, n: u8                       # 预留至少 n 槽
    fnc push:: bool, key: const &, value: @          # 入堆（retain value；上滤）
    fnc pop:: bool                                   # 弹出堆顶并 release（空返回 false；下滤）
    fnc peek:: @                                     # 借用堆顶 value 句柄（空返回空句柄；不改计数）
    fnc peek_key:: const &                           # 借用堆顶 key（空返回 nil）
}


# ---------------- trie：前缀树（字符串键 → 裸 @ 映射） ----------------
# 键恒为 NUL 结尾字符串，逐字节分解进树路径（路径即键，不另存键串）；value 为裸自动指针 @，
#   trie 拥有每键一份 retain（put retain、替换/remove/clear/drop release）。
# 节点采「首子/次兄」有序链 + 父指针：每节点存一个边字节，兄弟按字节升序——故遍历天然字典序；
#   节点 subkeys 记子树内键数，使 count_prefix O(prefix)。
# 取出语义同 dict「取用分离」：get 借用（不改计数），remove 删除并 release（返回 bool）。
# 前缀能力：has_prefix/count_prefix/each_prefix（自动补全）/longest_prefix（路由/最长匹配）。
# 不提供整数游标——键串须沿路径重建，each/each_prefix 用回调在 DFS 中增量拼键，O(总字符数)。
# init 无参——参与「声明即构造」：var t: trie 自动 trie_init(&t)。

@fnc trie_each_fn: bool, key: const char&, value: @, ctx: &   # 遍历回调（key 为完整键串；返回 false 提前终止）

@def trie: {
    root: &          # 根节点（不透明 trie_node*；空树为 nil）
    size: u8         # 键数

    fnc init::                                       # 构造（空树；无参→声明即构造）
    fnc drop::                                       # 释放全部 retain + 回收全部节点
    fnc len:: u8                                    # 键个数
    fnc has:: bool, key: const char&                 # 是否含精确键
    fnc get:: @, key: const char&                    # 借用键对应 value（未命中空句柄；不改计数）
    fnc put:: bool, key: const char&, value: @       # 插入/替换（retain 新、替换 release 旧）
    fnc remove:: bool, key: const char&              # 删除并 release（未命中 false；剪枝空节点）
    fnc clear::                                      # 清空并 release 全部 value
    fnc has_prefix:: bool, prefix: const char&       # 是否存在以 prefix 开头的键
    fnc count_prefix:: u8, prefix: const char&       # 以 prefix 开头的键数（O(prefix)）
    fnc each:: fn: trie_each_fn, ctx: &             # 按字典序遍历全部键（回调返 false 即停）
    fnc each_prefix:: prefix: const char&, fn: trie_each_fn, ctx: &  # 按字典序遍历 prefix 开头的键（自动补全）
    fnc longest_prefix:: i8, text: const char&       # text 的最长「键前缀」长度（无则 -1；空串键返 0）
}


# ---------------- lru：LRU 缓存（有界容量 + 最近最少使用淘汰） ----------------
# 组合容器：内嵌 dict（key → 节点指针，O(1) 查找） + 侵入双向链表（head=最近使用 MRU、tail=最久未用 LRU）。
# value 为裸自动指针 @，lru 拥有每键一份 retain（put 插入/替换 retain、remove/淘汰/clear/drop release）。
# 访问语义：get 命中则「触顶」（移到 MRU）并借用 value；peek 不改最近度；has 不触顶。
# cap=0 表无界（退化为可保插入顺序的 dict）；cap>0 时 put 新键超容即淘汰队尾（LRU）；set_cap 缩容立即淘汰至达标。
# key 三态同 dict：key_size >0 定长数值 / ==0 引用字符串（候误差由 value 自持）/ ==-1 拷贝字符串。
# 取出语义同 dict「取用分离」：get/peek 借用（返句柄），remove 删除并 release（返 bool）。
# each 按 MRU→LRU 顺序遍历。因 init 带参，不参与「声明即构造」——须显式 c.init(key_size, cap)。

@fnc lru_each_fn: bool, key: const &, value: @, ctx: &    # 遍历回调（MRU→LRU；返 false 提前终止）

@def lru: {
    map: dict        # 内嵌字典：key → 节点指针（借用句柄、不计数）
    head: &          # MRU 端节点（不透明 lru_node*；空为 nil）
    tail: &          # LRU 端节点（下一个被淘汰）
    cap: u8          # 容量上限（0 = 无界）
    key_size: i4     # 原始三态（>0 定长 / 0 引用串 / -1 拷贝串）

    fnc init:: key_size: i4, cap: u8                 # 构造（key 模式 + 容量；cap=0 无界）
    fnc drop::                                       # 释放全部 retain + 回收全部节点/字典
    fnc len:: u8                                    # 当前键数
    fnc is_empty:: bool                             # 是否空
    fnc cap:: u8                                    # 当前容量上限（0 = 无界）
    fnc set_cap:: cap: u8                           # 调整容量（缩容立即淘汰 LRU 至 len<=cap）
    fnc has:: bool, key: const &                     # 是否含键（不触顶）
    fnc get:: @, key: const &                        # 取值并触顶（移至 MRU；未命中空句柄；借用）
    fnc peek:: @, key: const &                       # 取值不触顶（未命中空句柄；借用）
    fnc put:: bool, key: const &, value: @           # 插入/替换 + 触顶（retain 新、替换 release 旧；超容淘汰）
    fnc remove:: bool, key: const &                  # 删除并 release（未命中 false）
    fnc clear::                                      # 清空并 release 全部 value（保留容量）
    fnc mru_key:: const &                            # 最近使用键（空返 nil）
    fnc lru_key:: const &                            # 最久未用键（下一被淘汰；空返 nil）
    fnc each:: fn: lru_each_fn, ctx: &              # 按 MRU→LRU 遍历（回调返 false 即停）
}



