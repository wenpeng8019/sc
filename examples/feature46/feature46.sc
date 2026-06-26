# 特性 46：mod 模块单例（声明即创建的单例对象）+ 兄弟模块互相可见（@inc）
#
# mod 关键字：声明一个「模块」——其本质是一个单例对象。`mod N:` 等价于
#   「def 类型 N_m + var 实例 N」，声明即创建（自动构造/析构，复用全局对象生命周期）。
#
#   - 成员函数：mod 体内用 `fnc name[: ret, params]` + 缩进函数体声明（接收者为 this，
#     用 this-> 访问字段/方法）。`@fnc` 逐个导出；未导出的成员函数为模块私有（static）。
#   - 构造 init / 析构 drop：类型有零参 init → 进 main 前自动构造；有 drop → main 尾声
#     自动析构。二者禁止 `@` 导出（构造/析构是单例的内部生命周期，语义上不可跨模块调用）。
#   - 导出由 @ 控制：`@mod N` → 单例类型 N_m 与实例 N 导出（extern），`inst.method()` 可
#     跨模块调用，展开为 `N_m_method(&N, ...)`；`mod N`（无 @）→ 私有模块（static），仅本
#     单元可见，且不得导出成员函数。
#
# 兄弟模块互相可见（与 @@ 根配合）：根（集成单元）以 @inc（@导出 inc）引入的各 .sc 模块
#   互为「兄弟」，其 @导出 默认彼此可见——无需兄弟之间逐一回引。普通 inc 仅为根私有依赖，
#   不参与互见。见附属模块 feature46_config.sc / feature46_logger.sc：二者经 @inc 引入，
#   logger 直接引用 config 的导出。

@@                                  # 根模块标记：本单元为集成单元（全局前奏提供者）

@inc feature46_config.sc           # 兄弟模块 1：配置单例（@inc → 参与兄弟互见）
@inc feature46_logger.sc          # 兄弟模块 2：日志单例（内部直接引用 config）

fnc main: i4
    # 跨模块调用 mod 的导出成员函数：inst.method() → Type_method(&inst)
    config.set_level(3)
    logger.emit("hello")           # logger 内部读取 config（兄弟互相可见）
    logger.emit("world")
    printf("config.level = %d, logger.count = %d\n",
           config.level(), logger.count())
    return 0
