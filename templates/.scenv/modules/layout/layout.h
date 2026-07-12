#ifndef SC_LAYOUT_H
#define SC_LAYOUT_H

#include "../../../../builtins/platform.h"
#include "../ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LAYOUT_SHARED
 #define LAYOUT_SHARED 0
#endif
#ifndef LAYOUT_EXPORTS
 #define LAYOUT_EXPORTS 0
#endif

#define LAYOUT_API SC_API(LAYOUT)

/* ============================================================
 * layout —— 平台无关的逻辑视图树 / 布局驱动层
 * ============================================================
 * 定位：
 *   layout 是纯逻辑层，与任何平台、任何具体 UI 后端无关。它只维护
 *   一棵「视图节点树」，每个节点持有几何(pos/size)与 z-order，并可
 *   计算/传播布局结果，去「驱动」外部的可摆放对象（如 ui 的原生
 *   窗口/控件）的位置与层叠顺序。
 *
 * 为什么独立成层：
 *   逻辑布局与视图树是平台无关的事。个别平台（如 Android）内置了
 *   自己的 layout/tree 机制，但各平台互不相同、很多桌面平台根本没有，
 *   无法跨平台。因此把「逻辑树 + 布局」抽成独立模块，反过来驱动各
 *   平台的原生控件位置，而不是依赖平台自带能力。
 *
 * 驱动契约（sink）：
 *   layout 不认识「控件」或「窗口」，只认识「能被摆放的东西」——即
 *   实现了 sc_ui_sink（set_frame / set_z）的对象。注意：该接口标准由
 *   ui 模块定义并实现，layout 作为驱动器引用它来操作 ui。
 *   任何暴露了 pos/size/z-order 的对象，提供一个 sink 即可被 layout 驱动。
 *
 * 分层关系：
 *   wsi     ：root window 与事件循环（平台相关）
 *   layout  ：逻辑视图树 + 布局驱动（本模块，平台无关）
 *   ui      ：各平台原生控件/子窗口封装（平台相关，被 layout 驱动）
 *   surface ：从 native handle 创建渲染 surface（平台相关）
 * ============================================================ */

/* ============================================================
 * 类型
 * ============================================================ */

/* 不透明句柄：具体布局见 src/layout.c。 */
typedef struct sc_layout_ctx sc_layout_ctx;   /* 一棵视图树的上下文 */
typedef struct sc_layout_node sc_layout_node; /* 视图树节点（逻辑，非原生） */

/* ============================================================
 * 上下文（ctx）：生命周期
 * ============================================================ */

/* 创建布局上下文，自动建立一个根节点。失败返回 NULL。 */
LAYOUT_API sc_layout_ctx* sc_layout_create(void);

/* 销毁上下文：释放整棵节点树与上下文自身（不触碰被驱动的外部对象）。 */
LAYOUT_API void sc_layout_destroy(sc_layout_ctx* ctx);

/* 返回根节点（整棵树的顶层）。 */
LAYOUT_API sc_layout_node* sc_layout_get_root(sc_layout_ctx* ctx);

/* ============================================================
 * 节点（node）：树结构
 * ============================================================ */

/* 在 parent 下创建子节点（parent 为 NULL 时挂到根节点）。失败返回 NULL。 */
LAYOUT_API sc_layout_node* sc_layout_node_create(sc_layout_ctx* ctx, sc_layout_node* parent);

/* 销毁节点及其整棵子树；根节点不可销毁（静默忽略）。 */
LAYOUT_API void sc_layout_node_destroy(sc_layout_node* node);

/* 遍历：父节点 / 首个子节点 / 下一个兄弟节点，无则返回 NULL。 */
LAYOUT_API sc_layout_node* sc_layout_node_parent(sc_layout_node* node);
LAYOUT_API sc_layout_node* sc_layout_node_first_child(sc_layout_node* node);
LAYOUT_API sc_layout_node* sc_layout_node_next_sibling(sc_layout_node* node);

/* ============================================================
 * 节点：几何与层叠（pos/size/z-order）
 * ============================================================ */

/* 几何（相对父节点坐标）：读取/设置 x/y/width/height。
 * get 的输出指针允许为 NULL（按需取值）。 */
LAYOUT_API void sc_layout_node_get_frame(sc_layout_node* node, int* x, int* y, int* width, int* height);
LAYOUT_API void sc_layout_node_set_frame(sc_layout_node* node, int x, int y, int width, int height);

/* z-order（层叠顺序，值大者在上）：读取/设置。 */
LAYOUT_API int sc_layout_node_get_z(sc_layout_node* node);
LAYOUT_API void sc_layout_node_set_z(sc_layout_node* node, int z);

/* 布局标志位（预留：未来承载布局策略提示，如拉伸/对齐）：读取/设置。 */
LAYOUT_API int sc_layout_node_get_flags(sc_layout_node* node);
LAYOUT_API void sc_layout_node_set_flags(sc_layout_node* node, int flags);

/* ============================================================
 * 节点：驱动绑定（★ 与 ui 等原生层的衔接点）
 * ============================================================ */

/* 将节点绑定到一个可摆放对象：sink 提供 set_frame/set_z 回调（由 ui
 * 模块定义的 sc_ui_sink 标准），target 为该对象指针。sc_layout_apply
 * 时会把节点几何推送给它。sink 需在节点生命周期内保持有效。 */
LAYOUT_API void sc_layout_node_bind(sc_layout_node* node, const sc_ui_sink* sink, void* target);

/* 解除绑定（此后该节点不再驱动任何对象）。 */
LAYOUT_API void sc_layout_node_unbind(sc_layout_node* node);

/* ============================================================
 * 布局应用
 * ============================================================ */

/* 遍历整棵树，将每个已绑定节点的几何与 z-order 推送到其 target。
 *
 * 说明（当前阶段）：仅传播各节点已设置的 frame/z，尚未实现自动布局
 * 算法（如盒模型/弹性/网格）。自动布局求解将在后续版本补充，届时
 * 本函数在推送前先根据 flags 与子节点约束计算各节点 frame。 */
LAYOUT_API void sc_layout_apply(sc_layout_ctx* ctx);

#ifdef __cplusplus
}
#endif

#endif
