# layout —— 平台无关的逻辑视图树 / 布局驱动层
#
# 定位：纯逻辑层，维护一棵视图节点树（pos/size/z-order），反过来
#       驱动各平台原生控件/窗口的位置与层叠，不依赖任何平台能力。
#
# 驱动契约：layout 只认识实现了 sc_ui_sink（set_frame/set_z）的可摆放对象。
#           该接口标准由 ui 模块定义；layout 作为驱动器引用它来操作 ui。
#
# 说明：各接口函数的功能与参数注释见 layout.h（本文件仅按功能分区罗列绑定）。

inc layout.h
add liblayout.a

# === 上下文（ctx）：生命周期 ===
@fnc layout_create:: ::sc_layout_ctx&
@fnc layout_destroy:: ctx: ::sc_layout_ctx&
@fnc layout_get_root:: ::sc_layout_node&, ctx: ::sc_layout_ctx&

# === 节点（node）：树结构 ===
@fnc layout_node_create:: ::sc_layout_node&, ctx: ::sc_layout_ctx&, parent: ::sc_layout_node&
@fnc layout_node_destroy:: node: ::sc_layout_node&
@fnc layout_node_parent:: ::sc_layout_node&, node: ::sc_layout_node&
@fnc layout_node_first_child:: ::sc_layout_node&, node: ::sc_layout_node&
@fnc layout_node_next_sibling:: ::sc_layout_node&, node: ::sc_layout_node&

# === 节点：几何与层叠（pos/size/z-order） ===
@fnc layout_node_get_frame:: node: ::sc_layout_node&, x: i4&, y: i4&, width: i4&, height: i4&
@fnc layout_node_set_frame:: node: ::sc_layout_node&, x: i4, y: i4, width: i4, height: i4
@fnc layout_node_get_z:: i4, node: ::sc_layout_node&
@fnc layout_node_set_z:: node: ::sc_layout_node&, z: i4
@fnc layout_node_get_flags:: i4, node: ::sc_layout_node&
@fnc layout_node_set_flags:: node: ::sc_layout_node&, flags: i4

# === 节点：驱动绑定（★ 与 ui 等原生层的衔接点） ===
@fnc layout_node_bind:: node: ::sc_layout_node&, sink: ::sc_ui_sink&, target: &
@fnc layout_node_unbind:: node: ::sc_layout_node&

# === 布局应用 ===
@fnc layout_apply:: ctx: ::sc_layout_ctx&
