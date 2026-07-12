#include "ui_internal.h"

#if defined(SC_UI_ANDROID)

/* ============================================================
 * ui android 后端 —— 纯 C 经 wsi 的 JNI 反射代理驱动原生控件
 * ============================================================
 * 契约见 src/ui_internal.h。设计要点：
 *   - 不写任何 per-control 的 Java：所有 android.widget.* 的创建/配置/读回
 *     经 wsi 的 sc_jni_*（wsi_jni.h）反射完成；事件回调经 sc_jni_new_proxy
 *     生成的接口代理（Java listener → nativeInvoke → 本文件 cb → ui_emit_event）。
 *   - 根容器：wsi 提供的覆盖层 FrameLayout（sc_wsi_android_ui_root，叠在
 *     NativeActivity 渲染表面之上）。子窗口 = 嵌套 FrameLayout；控件挂到所属
 *     子窗口容器，绝对定位（setX/setY + FrameLayout.LayoutParams(w,h)）。
 *   - 线程：Android 的 View 操作必须在 UI 主线程。ui.c 的 hook 多在渲染/逻辑
 *     线程调用，故每个 hook 经 sc_jni_run_ui_sync 同步跳到 UI 主线程执行
 *     （create 须在返回前填好 backend，故取同步语义）。事件 cb 本就在 UI 线程。
 *
 * backend 字段：
 *   - sc_ui_window.backend  = and_win*  { group(ViewGroup global ref), owned }
 *   - sc_ui_control.backend = and_ctrl* { view(global ref), listener(global ref) }
 * ============================================================ */

#include "../../wsi/wsi_jni.h"

#include <stdlib.h>
#include <string.h>

typedef struct { sc_jref group; int owned; } and_win;
typedef struct { sc_jref view;  sc_jref listener; } and_ctrl;

/* ---- 一次性解析的类/方法/字段缓存（只在 UI 主线程访问，无需加锁）---- */
static struct
{
    int ready;

    /* 类 */
    sc_jref View, ViewGroup, FrameLayout, FrameLayoutLP;
    sc_jref TextView, EditText, Button, CheckBox, RadioButton, Spinner, ListView;
    sc_jref AdapterView, CompoundButton, ArrayAdapter, Integer, String;

    /* 方法 */
    sc_jmethod View_setX, View_setY, View_setZ, View_setLayoutParams, View_setOnClickListener;
    sc_jmethod ViewGroup_addView, ViewGroup_removeView;
    sc_jmethod FLP_ctor;
    sc_jmethod TextView_setText, TextView_setSingleLine, TextView_setTextColor;
    sc_jmethod CompoundButton_setChecked, CompoundButton_isChecked, CompoundButton_setOnCheckedChangeListener;
    sc_jmethod Spinner_setAdapter, ListView_setAdapter;
    sc_jmethod AdapterView_setSelection, AdapterView_getSelectedItemPosition;
    sc_jmethod AdapterView_setOnItemSelectedListener, AdapterView_setOnItemClickListener;
    sc_jmethod ArrayAdapter_ctor, ArrayAdapter_setDropDownViewResource;
    sc_jmethod Integer_intValue;

    /* android.R.layout.* 资源 id */
    int lay_list_item, lay_spinner_item, lay_spinner_dropdown;
} J;

static int resolve(void)
{
    if (J.ready) return 1;
    if (!sc_jni_available()) return 0;

    J.View          = sc_jni_find_class("android/view/View");
    J.ViewGroup     = sc_jni_find_class("android/view/ViewGroup");
    J.FrameLayout   = sc_jni_find_class("android/widget/FrameLayout");
    J.FrameLayoutLP = sc_jni_find_class("android/widget/FrameLayout$LayoutParams");
    J.TextView      = sc_jni_find_class("android/widget/TextView");
    J.EditText      = sc_jni_find_class("android/widget/EditText");
    J.Button        = sc_jni_find_class("android/widget/Button");
    J.CheckBox      = sc_jni_find_class("android/widget/CheckBox");
    J.RadioButton   = sc_jni_find_class("android/widget/RadioButton");
    J.Spinner       = sc_jni_find_class("android/widget/Spinner");
    J.ListView      = sc_jni_find_class("android/widget/ListView");
    J.AdapterView   = sc_jni_find_class("android/widget/AdapterView");
    J.CompoundButton= sc_jni_find_class("android/widget/CompoundButton");
    J.ArrayAdapter  = sc_jni_find_class("android/widget/ArrayAdapter");
    J.Integer       = sc_jni_find_class("java/lang/Integer");
    J.String        = sc_jni_find_class("java/lang/String");

    J.View_setX = sc_jni_method(J.View, "setX", "(F)V");
    J.View_setY = sc_jni_method(J.View, "setY", "(F)V");
    J.View_setZ = sc_jni_method(J.View, "setZ", "(F)V");
    J.View_setLayoutParams = sc_jni_method(J.View, "setLayoutParams", "(Landroid/view/ViewGroup$LayoutParams;)V");
    J.View_setOnClickListener = sc_jni_method(J.View, "setOnClickListener", "(Landroid/view/View$OnClickListener;)V");

    J.ViewGroup_addView    = sc_jni_method(J.ViewGroup, "addView", "(Landroid/view/View;Landroid/view/ViewGroup$LayoutParams;)V");
    J.ViewGroup_removeView = sc_jni_method(J.ViewGroup, "removeView", "(Landroid/view/View;)V");
    J.FLP_ctor = sc_jni_method(J.FrameLayoutLP, "<init>", "(II)V");

    J.TextView_setText       = sc_jni_method(J.TextView, "setText", "(Ljava/lang/CharSequence;)V");
    J.TextView_setSingleLine = sc_jni_method(J.TextView, "setSingleLine", "(Z)V");
    J.TextView_setTextColor  = sc_jni_method(J.TextView, "setTextColor", "(I)V");

    J.CompoundButton_setChecked = sc_jni_method(J.CompoundButton, "setChecked", "(Z)V");
    J.CompoundButton_isChecked  = sc_jni_method(J.CompoundButton, "isChecked", "()Z");
    J.CompoundButton_setOnCheckedChangeListener =
        sc_jni_method(J.CompoundButton, "setOnCheckedChangeListener",
                      "(Landroid/widget/CompoundButton$OnCheckedChangeListener;)V");

    J.Spinner_setAdapter  = sc_jni_method(J.Spinner, "setAdapter", "(Landroid/widget/SpinnerAdapter;)V");
    J.ListView_setAdapter = sc_jni_method(J.ListView, "setAdapter", "(Landroid/widget/ListAdapter;)V");

    J.AdapterView_setSelection            = sc_jni_method(J.AdapterView, "setSelection", "(I)V");
    J.AdapterView_getSelectedItemPosition = sc_jni_method(J.AdapterView, "getSelectedItemPosition", "()I");
    J.AdapterView_setOnItemSelectedListener =
        sc_jni_method(J.AdapterView, "setOnItemSelectedListener",
                      "(Landroid/widget/AdapterView$OnItemSelectedListener;)V");
    J.AdapterView_setOnItemClickListener =
        sc_jni_method(J.AdapterView, "setOnItemClickListener",
                      "(Landroid/widget/AdapterView$OnItemClickListener;)V");

    J.ArrayAdapter_ctor = sc_jni_method(J.ArrayAdapter, "<init>",
                                        "(Landroid/content/Context;I[Ljava/lang/Object;)V");
    J.ArrayAdapter_setDropDownViewResource =
        sc_jni_method(J.ArrayAdapter, "setDropDownViewResource", "(I)V");

    J.Integer_intValue = sc_jni_method(J.Integer, "intValue", "()I");

    sc_jref rLayout = sc_jni_find_class("android/R$layout");
    J.lay_list_item       = sc_jni_get_static_int(rLayout, sc_jni_static_field(rLayout, "simple_list_item_1", "I"));
    J.lay_spinner_item    = sc_jni_get_static_int(rLayout, sc_jni_static_field(rLayout, "simple_spinner_item", "I"));
    J.lay_spinner_dropdown= sc_jni_get_static_int(rLayout, sc_jni_static_field(rLayout, "simple_spinner_dropdown_item", "I"));

    J.ready = (J.FrameLayout && J.ViewGroup_addView && J.FLP_ctor && J.TextView_setText) ? 1 : 0;
    return J.ready;
}

/* ============================================================
 * 低层 View 操作辅助（均在 UI 主线程执行）
 * ============================================================ */

static sc_jref new_widget(sc_jref cls, sc_jref ctx)
{
    sc_jmethod ctor = sc_jni_method(cls, "<init>", "(Landroid/content/Context;)V");
    sc_jval a = sc_jv_o(ctx);
    return sc_jni_new(cls, ctor, &a, 1);
}

static void view_setpos(sc_jref v, int x, int y)
{
    sc_jval fx = sc_jv_f((float)x); sc_jni_call_void(v, J.View_setX, &fx, 1);
    sc_jval fy = sc_jv_f((float)y); sc_jni_call_void(v, J.View_setY, &fy, 1);
}

static void view_setsize(sc_jref v, int w, int h)
{
    sc_jval lpa[2] = { sc_jv_i(w), sc_jv_i(h) };
    sc_jref lp = sc_jni_new(J.FrameLayoutLP, J.FLP_ctor, lpa, 2);
    sc_jval a = sc_jv_o(lp);
    sc_jni_call_void(v, J.View_setLayoutParams, &a, 1);
    sc_jni_release(lp);
}

static void view_add(sc_jref parent, sc_jref view, int x, int y, int w, int h)
{
    sc_jval lpa[2] = { sc_jv_i(w), sc_jv_i(h) };
    sc_jref lp = sc_jni_new(J.FrameLayoutLP, J.FLP_ctor, lpa, 2);
    sc_jval av[2] = { sc_jv_o(view), sc_jv_o(lp) };
    sc_jni_call_void(parent, J.ViewGroup_addView, av, 2);
    sc_jni_release(lp);
    view_setpos(view, x, y);
}

static void view_remove(sc_jref parent, sc_jref view)
{
    if (!parent || !view) return;
    sc_jval a = sc_jv_o(view);
    sc_jni_call_void(parent, J.ViewGroup_removeView, &a, 1);
}

static void view_settext(sc_jref v, const char* t)
{
    sc_jval a = sc_jv_s(t ? t : "");   /* String 即 CharSequence */
    sc_jni_call_void(v, J.TextView_setText, &a, 1);
}

/* 透明背景的文字控件（label/checkbox/radio）叠在 gpu SurfaceView 之上，系统默认
 * 文字色随主题可能为深色而在深色画面上不可见——显式给一个高对比浅色兜底可读性。
 * 有自身底框的控件（button/edit/text）不动，保留主题默认深字。 */
static void view_settextcolor(sc_jref v, int argb)
{
    if (!v || !J.TextView_setTextColor) return;
    sc_jval a = sc_jv_i(argb);
    sc_jni_call_void(v, J.TextView_setTextColor, &a, 1);
}

static void view_setchecked(sc_jref v, int on)
{
    sc_jval a = sc_jv_z(on);
    sc_jni_call_void(v, J.CompoundButton_setChecked, &a, 1);
}

static void view_singleline(sc_jref v, int on)
{
    sc_jval a = sc_jv_z(on);
    sc_jni_call_void(v, J.TextView_setSingleLine, &a, 1);
}

/* 用 control 的 items 造一个 ArrayAdapter<String>。 */
static sc_jref make_adapter(sc_ui_control* c, int layoutId)
{
    sc_jref ctx = sc_jni_activity();
    sc_jref arr = sc_jni_new_object_array(J.String, c->itemCount);
    for (int i = 0; i < c->itemCount; ++i)
    {
        sc_jref s = sc_jni_new_string(c->items[i] ? c->items[i] : "");
        sc_jni_set_object_array(arr, i, s);
        sc_jni_release(s);
    }
    sc_jval aa[3] = { sc_jv_o(ctx), sc_jv_i(layoutId), sc_jv_o(arr) };
    sc_jref ad = sc_jni_new(J.ArrayAdapter, J.ArrayAdapter_ctor, aa, 3);
    sc_jni_release(arr);
    return ad;
}

static void combo_reload(sc_jref spinner, sc_ui_control* c)
{
    sc_jref ad = make_adapter(c, J.lay_spinner_item);
    if (!ad) return;
    sc_jval dv = sc_jv_i(J.lay_spinner_dropdown);
    sc_jni_call_void(ad, J.ArrayAdapter_setDropDownViewResource, &dv, 1);
    sc_jval a = sc_jv_o(ad);
    sc_jni_call_void(spinner, J.Spinner_setAdapter, &a, 1);
    sc_jni_release(ad);
}

static void list_reload(sc_jref list, sc_ui_control* c)
{
    sc_jref ad = make_adapter(c, J.lay_list_item);
    if (!ad) return;
    sc_jval a = sc_jv_o(ad);
    sc_jni_call_void(list, J.ListView_setAdapter, &a, 1);
    sc_jni_release(ad);
}

static void view_select(sc_jref v, int idx)
{
    if (idx < 0) return;
    sc_jval a = sc_jv_i(idx);
    sc_jni_call_void(v, J.AdapterView_setSelection, &a, 1);
}

/* ============================================================
 * 事件回调（在 UI 主线程被 Bridge.nativeInvoke 触发）→ ui_emit_event
 * ============================================================ */

static void on_click_cb(void* user, const char* m, const sc_jval* a, int n, sc_jval* ret)
{
    (void)m; (void)a; (void)n; (void)ret;
    ui_emit_event((sc_ui_control*)user, SC_UI_EVENT_CLICK);
}

static void on_checked_cb(void* user, const char* m, const sc_jval* a, int n, sc_jval* ret)
{
    (void)m; (void)a; (void)n; (void)ret;
    sc_ui_control* c = (sc_ui_control*)user;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (b && b->view)
        c->checked = sc_jni_call_bool(b->view, J.CompoundButton_isChecked, NULL, 0);
    ui_emit_event(c, SC_UI_EVENT_TOGGLE);
}

static void on_select_cb(void* user, const char* m, const sc_jval* a, int n, sc_jval* ret)
{
    (void)a; (void)n; (void)ret;
    sc_ui_control* c = (sc_ui_control*)user;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (m && strcmp(m, "onNothingSelected") == 0)
        c->selectedIndex = -1;
    else if (b && b->view)
        c->selectedIndex = sc_jni_call_int(b->view, J.AdapterView_getSelectedItemPosition, NULL, 0);
    ui_emit_event(c, SC_UI_EVENT_SELECT);
}

static void on_itemclick_cb(void* user, const char* m, const sc_jval* a, int n, sc_jval* ret)
{
    (void)m; (void)ret;
    sc_ui_control* c = (sc_ui_control*)user;
    /* onItemClick(AdapterView parent, View view, int position, long id)：position 在下标 2（自动装箱 Integer）。 */
    if (n >= 3 && a[2].tag == SC_JV_OBJ && a[2].v.o)
        c->selectedIndex = sc_jni_call_int(a[2].v.o, J.Integer_intValue, NULL, 0);
    ui_emit_event(c, SC_UI_EVENT_SELECT);
}

static sc_jref bind_listener(sc_jref target, sc_jmethod setter, const char* iface,
                             sc_jni_invoke_cb cb, sc_ui_control* c)
{
    const char* ifaces[1] = { iface };
    sc_jref l = sc_jni_new_proxy(ifaces, 1, cb, c);
    if (l)
    {
        sc_jval a = sc_jv_o(l);
        sc_jni_call_void(target, setter, &a, 1);
    }
    return l;
}

/* ============================================================
 * 子窗口 hook（UI 线程内实体）
 * ============================================================ */

static void do_window_create(void* p)
{
    sc_ui_window* win = (sc_ui_window*)p;
    if (!resolve()) return;

    and_win* b = (and_win*)calloc(1, sizeof(and_win));
    if (!b) return;

    if (!win->parent)
    {
        /* root：直接用 wsi 的覆盖层根容器（借用，不 owned）。 */
        b->group = sc_wsi_android_ui_root();
        b->owned = 0;
    }
    else
    {
        and_win* pb = (and_win*)win->parent->backend;
        sc_jref parent = pb ? pb->group : 0;
        if (!parent) { free(b); return; }
        sc_jref fl = new_widget(J.FrameLayout, sc_jni_activity());
        if (!fl) { free(b); return; }
        view_add(parent, fl, win->x, win->y, win->width, win->height);
        b->group = fl;
        b->owned = 1;
    }
    win->backend = b;
}

static void do_window_destroy(void* p)
{
    sc_ui_window* win = (sc_ui_window*)p;
    and_win* b = (and_win*)win->backend;
    if (!b) return;
    if (b->owned && b->group)
    {
        and_win* pb = win->parent ? (and_win*)win->parent->backend : NULL;
        view_remove(pb ? pb->group : 0, b->group);
        sc_jni_release(b->group);
    }
    free(b);
    win->backend = NULL;
}

static void do_window_set_frame(void* p)
{
    sc_ui_window* win = (sc_ui_window*)p;
    and_win* b = (and_win*)win->backend;
    if (!b || !b->owned || !b->group) return;
    view_setsize(b->group, win->width, win->height);
    view_setpos(b->group, win->x, win->y);
}

static void do_window_set_z(void* p)
{
    sc_ui_window* win = (sc_ui_window*)p;
    and_win* b = (and_win*)win->backend;
    if (!b || !b->owned || !b->group) return;
    sc_jval a = sc_jv_f((float)win->z);
    sc_jni_call_void(b->group, J.View_setZ, &a, 1);
}

/* ============================================================
 * 控件 hook（UI 线程内实体）
 * ============================================================ */

static void do_control_create(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    if (!resolve()) return;

    and_win* wb = c->window ? (and_win*)c->window->backend : NULL;
    sc_jref parent = wb ? wb->group : 0;
    if (!parent) return;

    sc_jref ctx = sc_jni_activity();
    const char* text = c->text ? c->text : "";
    sc_jref view = 0, listener = 0;

    switch (c->kind)
    {
        case SC_UI_LABEL:
            view = new_widget(J.TextView, ctx);
            view_settext(view, text);
            view_settextcolor(view, (int)0xFFECECEC);
            break;
        case SC_UI_EDIT:
            view = new_widget(J.EditText, ctx);
            view_settext(view, text);
            view_singleline(view, 1);
            break;
        case SC_UI_TEXT:
            view = new_widget(J.EditText, ctx);
            view_settext(view, text);
            view_singleline(view, 0);
            break;
        case SC_UI_BUTTON:
            view = new_widget(J.Button, ctx);
            view_settext(view, text);
            listener = bind_listener(view, J.View_setOnClickListener,
                                     "android/view/View$OnClickListener", on_click_cb, c);
            break;
        case SC_UI_CHECKBOX:
            view = new_widget(J.CheckBox, ctx);
            view_settext(view, text);
            view_settextcolor(view, (int)0xFFECECEC);
            view_setchecked(view, c->checked);
            listener = bind_listener(view, J.CompoundButton_setOnCheckedChangeListener,
                                     "android/widget/CompoundButton$OnCheckedChangeListener", on_checked_cb, c);
            break;
        case SC_UI_RADIOBOX:
            view = new_widget(J.RadioButton, ctx);
            view_settext(view, text);
            view_settextcolor(view, (int)0xFFECECEC);
            view_setchecked(view, c->checked);
            listener = bind_listener(view, J.CompoundButton_setOnCheckedChangeListener,
                                     "android/widget/CompoundButton$OnCheckedChangeListener", on_checked_cb, c);
            break;
        case SC_UI_COMBO:
            view = new_widget(J.Spinner, ctx);
            combo_reload(view, c);
            listener = bind_listener(view, J.AdapterView_setOnItemSelectedListener,
                                     "android/widget/AdapterView$OnItemSelectedListener", on_select_cb, c);
            view_select(view, c->selectedIndex);
            break;
        case SC_UI_LIST:
            view = new_widget(J.ListView, ctx);
            list_reload(view, c);
            listener = bind_listener(view, J.AdapterView_setOnItemClickListener,
                                     "android/widget/AdapterView$OnItemClickListener", on_itemclick_cb, c);
            break;
        default:
            return;
    }

    if (!view) return;
    view_add(parent, view, c->x, c->y, c->width, c->height);

    and_ctrl* b = (and_ctrl*)calloc(1, sizeof(and_ctrl));
    if (!b) { view_remove(parent, view); sc_jni_release(view); if (listener) sc_jni_release(listener); return; }
    b->view = view;
    b->listener = listener;
    c->backend = b;
}

static void do_control_destroy(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b) return;
    and_win* wb = c->window ? (and_win*)c->window->backend : NULL;
    if (b->view)
    {
        view_remove(wb ? wb->group : 0, b->view);
        sc_jni_release(b->view);
    }
    if (b->listener) sc_jni_release(b->listener);
    free(b);
    c->backend = NULL;
}

static void do_control_set_frame(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    view_setsize(b->view, c->width, c->height);
    view_setpos(b->view, c->x, c->y);
}

static void do_control_set_z(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    sc_jval a = sc_jv_f((float)c->z);
    sc_jni_call_void(b->view, J.View_setZ, &a, 1);
}

static void do_control_set_text(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    switch (c->kind)
    {
        case SC_UI_LABEL: case SC_UI_EDIT: case SC_UI_TEXT:
        case SC_UI_BUTTON: case SC_UI_CHECKBOX: case SC_UI_RADIOBOX:
            view_settext(b->view, c->text);   /* 均为 TextView 子类 */
            break;
        default: break;
    }
}

static void do_control_set_checked(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    if (c->kind == SC_UI_CHECKBOX || c->kind == SC_UI_RADIOBOX)
        view_setchecked(b->view, c->checked);
}

static void do_control_set_items(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    if (c->kind == SC_UI_COMBO) combo_reload(b->view, c);
    else if (c->kind == SC_UI_LIST) list_reload(b->view, c);
}

static void do_control_set_selected_index(void* p)
{
    sc_ui_control* c = (sc_ui_control*)p;
    and_ctrl* b = (and_ctrl*)c->backend;
    if (!b || !b->view) return;
    if (c->kind == SC_UI_COMBO || c->kind == SC_UI_LIST)
        view_select(b->view, c->selectedIndex);
}

/* ============================================================
 * ui_backend_* 契约实现：统一同步跳到 UI 主线程执行
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win)
{ if (win && sc_jni_available()) sc_jni_run_ui_sync(do_window_create, win); }
void ui_backend_window_destroy(sc_ui_window* win)
{ if (win && win->backend) sc_jni_run_ui_sync(do_window_destroy, win); }
void ui_backend_window_set_frame(sc_ui_window* win)
{ if (win && win->backend) sc_jni_run_ui_sync(do_window_set_frame, win); }
void ui_backend_window_set_z(sc_ui_window* win)
{ if (win && win->backend) sc_jni_run_ui_sync(do_window_set_z, win); }

void ui_backend_control_create(sc_ui_control* c)
{ if (c && sc_jni_available()) sc_jni_run_ui_sync(do_control_create, c); }
void ui_backend_control_destroy(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_destroy, c); }
void ui_backend_control_set_frame(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_frame, c); }
void ui_backend_control_set_z(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_z, c); }
void ui_backend_control_set_text(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_text, c); }
void ui_backend_control_set_checked(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_checked, c); }
void ui_backend_control_set_items(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_items, c); }
void ui_backend_control_set_selected_index(sc_ui_control* c)
{ if (c && c->backend) sc_jni_run_ui_sync(do_control_set_selected_index, c); }

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    /* Android 用系统原生控件，CJK 由系统字体渲染，无需自行加载。 */
    (void)ctx; (void)path; (void)size;
    return 0;
}

#endif /* SC_UI_ANDROID */
