#include "../layout.h"

#include <stdlib.h>

/* ============================================================
 * layout 实现 —— 接口语义与参数说明见 layout.h（本文件仅做实现与区段划分）。
 * ============================================================ */

/* ============================================================
 * 内部数据结构
 * ============================================================ */

struct sc_layout_node
{
    struct sc_layout_ctx* ctx;
    sc_layout_node* parent;
    sc_layout_node* firstChild;
    sc_layout_node* lastChild;
    sc_layout_node* nextSibling;

    int x;
    int y;
    int width;
    int height;
    int z;
    int flags;

    const sc_ui_sink* sink;
    void* target;
};

struct sc_layout_ctx
{
    sc_layout_node* root;
};

/* ============================================================
 * 内部辅助
 * ============================================================ */

static sc_layout_node* layout_alloc_node(sc_layout_ctx* ctx, sc_layout_node* parent)
{
    sc_layout_node* node = (sc_layout_node*) calloc(1, sizeof(sc_layout_node));
    if (!node)
        return NULL;

    node->ctx = ctx;
    node->parent = parent;
    return node;
}

static void layout_destroy_tree(sc_layout_node* node)
{
    if (!node)
        return;

    sc_layout_node* child = node->firstChild;
    while (child)
    {
        sc_layout_node* next = child->nextSibling;
        layout_destroy_tree(child);
        child = next;
    }

    free(node);
}

static void layout_apply_tree(sc_layout_node* node)
{
    if (!node)
        return;

    if (node->sink)
    {
        if (node->sink->set_frame)
            node->sink->set_frame(node->target, node->x, node->y, node->width, node->height);
        if (node->sink->set_z)
            node->sink->set_z(node->target, node->z);
    }

    sc_layout_node* child = node->firstChild;
    while (child)
    {
        layout_apply_tree(child);
        child = child->nextSibling;
    }
}

/* ============================================================
 * 上下文（ctx）：生命周期
 * ============================================================ */

LAYOUT_API sc_layout_ctx* sc_layout_create(void)
{
    sc_layout_ctx* ctx = (sc_layout_ctx*) calloc(1, sizeof(sc_layout_ctx));
    if (!ctx)
        return NULL;

    ctx->root = layout_alloc_node(ctx, NULL);
    if (!ctx->root)
    {
        free(ctx);
        return NULL;
    }

    return ctx;
}

LAYOUT_API void sc_layout_destroy(sc_layout_ctx* ctx)
{
    if (!ctx)
        return;

    layout_destroy_tree(ctx->root);
    free(ctx);
}

LAYOUT_API sc_layout_node* sc_layout_get_root(sc_layout_ctx* ctx)
{
    return ctx ? ctx->root : NULL;
}

/* ============================================================
 * 节点（node）：树结构
 * ============================================================ */

LAYOUT_API sc_layout_node* sc_layout_node_create(sc_layout_ctx* ctx, sc_layout_node* parent)
{
    if (!ctx)
        return NULL;

    if (!parent)
        parent = ctx->root;

    sc_layout_node* node = layout_alloc_node(ctx, parent);
    if (!node)
        return NULL;

    if (!parent->firstChild)
        parent->firstChild = node;
    else
        parent->lastChild->nextSibling = node;

    parent->lastChild = node;
    return node;
}

LAYOUT_API void sc_layout_node_destroy(sc_layout_node* node)
{
    if (!node || !node->ctx || node == node->ctx->root)
        return;

    sc_layout_node* parent = node->parent;
    if (parent)
    {
        sc_layout_node* prev = NULL;
        sc_layout_node* it = parent->firstChild;
        while (it)
        {
            if (it == node)
            {
                if (prev)
                    prev->nextSibling = it->nextSibling;
                else
                    parent->firstChild = it->nextSibling;

                if (parent->lastChild == it)
                    parent->lastChild = prev;

                break;
            }
            prev = it;
            it = it->nextSibling;
        }
    }

    layout_destroy_tree(node);
}

LAYOUT_API sc_layout_node* sc_layout_node_parent(sc_layout_node* node)
{
    return node ? node->parent : NULL;
}

LAYOUT_API sc_layout_node* sc_layout_node_first_child(sc_layout_node* node)
{
    return node ? node->firstChild : NULL;
}

LAYOUT_API sc_layout_node* sc_layout_node_next_sibling(sc_layout_node* node)
{
    return node ? node->nextSibling : NULL;
}

/* ============================================================
 * 节点：几何与层叠（pos/size/z-order）
 * ============================================================ */

LAYOUT_API void sc_layout_node_get_frame(sc_layout_node* node, int* x, int* y, int* width, int* height)
{
    if (!node)
        return;

    if (x) *x = node->x;
    if (y) *y = node->y;
    if (width) *width = node->width;
    if (height) *height = node->height;
}

LAYOUT_API void sc_layout_node_set_frame(sc_layout_node* node, int x, int y, int width, int height)
{
    if (!node)
        return;

    node->x = x;
    node->y = y;
    node->width = width;
    node->height = height;
}

LAYOUT_API int sc_layout_node_get_z(sc_layout_node* node)
{
    return node ? node->z : 0;
}

LAYOUT_API void sc_layout_node_set_z(sc_layout_node* node, int z)
{
    if (!node)
        return;

    node->z = z;
}

LAYOUT_API int sc_layout_node_get_flags(sc_layout_node* node)
{
    return node ? node->flags : 0;
}

LAYOUT_API void sc_layout_node_set_flags(sc_layout_node* node, int flags)
{
    if (!node)
        return;

    node->flags = flags;
}

/* ============================================================
 * 节点：驱动绑定
 * ============================================================ */

LAYOUT_API void sc_layout_node_bind(sc_layout_node* node, const sc_ui_sink* sink, void* target)
{
    if (!node)
        return;

    node->sink = sink;
    node->target = target;
}

LAYOUT_API void sc_layout_node_unbind(sc_layout_node* node)
{
    if (!node)
        return;

    node->sink = NULL;
    node->target = NULL;
}

/* ============================================================
 * 布局应用
 * ============================================================ */

LAYOUT_API void sc_layout_apply(sc_layout_ctx* ctx)
{
    if (!ctx)
        return;

    layout_apply_tree(ctx->root);
}
