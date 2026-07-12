#include "ui_internal.h"

#if defined(SC_UI_COCOA)

#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

/* ============================================================
 * ui cocoa 后端 —— 原生子视图/控件实现（MRR，无 ARC）
 * ============================================================
 * 契约见 src/ui_internal.h。约定：
 *   - sc_ui_window.backend  = SCFlippedView*（承载子视图的翻转容器）
 *   - sc_ui_control.backend = NSControl* / NSScrollView*（LIST）
 *   - backend 字段持有该对象的一次 +1（alloc），superview 另持一次；
 *     销毁时 removeFromSuperview + release 归零。
 *   - 容器采用「翻转坐标系」(左上原点)，使子对象 frame 与 UI 语义一致。
 *   - z-order 用 associated object 暂存，restack 时按 z 升序排列子视图
 *     （z 大者更靠前 = 更上层）。
 * ============================================================ */

/* ---- 翻转容器：使子视图使用左上原点坐标 ---- */
@interface SCFlippedView : NSView
@end

@implementation SCFlippedView
- (BOOL)isFlipped { return YES; }
@end

/* ---- LIST 的数据源：直接读 sc_ui_control 的 items ---- */
@interface SCListDataSource : NSObject <NSTableViewDataSource>
@property (assign) sc_ui_control* control;
@end

@implementation SCListDataSource
- (NSInteger)numberOfRowsInTableView:(NSTableView*)tableView
{
    return self.control ? self.control->itemCount : 0;
}
- (id)tableView:(NSTableView*)tableView
        objectValueForTableColumn:(NSTableColumn*)tableColumn
                              row:(NSInteger)row
{
    if (!self.control || row < 0 || row >= self.control->itemCount)
        return @"";
    const char* s = self.control->items[row];
    return s ? [NSString stringWithUTF8String:s] : @"";
}
@end

/* ============================================================
 * z-order 与 restack 辅助
 * ============================================================ */

static char g_z_key;  /* associated object key：视图的 z 值 */
static char g_ds_key; /* associated object key：LIST 的数据源 */

static void ui_cocoa_set_z(id view, int z)
{
    objc_setAssociatedObject(view, &g_z_key, @(z), OBJC_ASSOCIATION_RETAIN);
}

static int ui_cocoa_get_z(id view)
{
    NSNumber* n = objc_getAssociatedObject(view, &g_z_key);
    return n ? [n intValue] : 0;
}

static NSComparisonResult ui_cocoa_z_cmp(__kindof NSView* a, __kindof NSView* b, void* ctx)
{
    (void) ctx;
    int za = ui_cocoa_get_z(a);
    int zb = ui_cocoa_get_z(b);
    if (za < zb) return NSOrderedAscending;
    if (za > zb) return NSOrderedDescending;
    return NSOrderedSame;
}

static void ui_cocoa_restack(NSView* super)
{
    if (super)
        [super sortSubviewsUsingFunction:ui_cocoa_z_cmp context:NULL];
}

static NSString* ui_cocoa_text(sc_ui_control* control)
{
    if (control->text)
        return [NSString stringWithUTF8String:control->text];
    return @"";
}

static void ui_cocoa_combo_reload(NSComboBox* combo, sc_ui_control* control)
{
    [combo removeAllItems];
    for (int i = 0; i < control->itemCount; ++i)
    {
        const char* s = control->items[i];
        [combo addItemWithObjectValue:(s ? [NSString stringWithUTF8String:s] : @"")];
    }
    if (control->selectedIndex >= 0 && control->selectedIndex < control->itemCount)
        [combo selectItemAtIndex:control->selectedIndex];
}

/* ============================================================
 * 子窗口 hook
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win)
{
    if (!win)
        return;

    /* 父视图：非 root 取 parent 的容器；root 取 wsi 内容视图（sc_ui_create 已置入 nativeWindow）。 */
    NSView* super = win->parent ? (NSView*) win->parent->backend
                                : (NSView*) win->nativeWindow;
    if (!super)
        return;

    NSRect frame = NSMakeRect(win->x, win->y, win->width, win->height);
    SCFlippedView* view = [[SCFlippedView alloc] initWithFrame:frame];

    if (!win->parent)
        [view setAutoresizingMask:(NSViewWidthSizable | NSViewHeightSizable)];

    [super addSubview:view];
    win->backend = view; /* 持有 alloc 的 +1 */

    if (win->parent)
    {
        /* 子窗口自身即可作为 surface backing。 */
        win->nativeWindow = view;
        win->platform = SC_PLATFORM_COCOA;
    }

    ui_cocoa_set_z(view, win->z);
    ui_cocoa_restack(super);
}

void ui_backend_window_destroy(sc_ui_window* win)
{
    if (!win)
        return;

    NSView* view = (NSView*) win->backend;
    if (view)
    {
        [view removeFromSuperview];
        [view release];
        win->backend = NULL;
    }
}

void ui_backend_window_set_frame(sc_ui_window* win)
{
    if (!win)
        return;

    NSView* view = (NSView*) win->backend;
    if (view)
        [view setFrame:NSMakeRect(win->x, win->y, win->width, win->height)];
}

void ui_backend_window_set_z(sc_ui_window* win)
{
    if (!win)
        return;

    NSView* view = (NSView*) win->backend;
    if (!view)
        return;

    ui_cocoa_set_z(view, win->z);
    ui_cocoa_restack([view superview]);
}

/* ============================================================
 * 控件 hook
 * ============================================================ */

void ui_backend_control_create(sc_ui_control* control)
{
    if (!control)
        return;

    NSView* super = control->window ? (NSView*) control->window->backend : nil;
    if (!super)
        return;

    NSRect frame = NSMakeRect(control->x, control->y, control->width, control->height);
    NSString* text = ui_cocoa_text(control);
    NSView* obj = nil;

    switch (control->kind)
    {
        case SC_UI_LABEL:
        {
            NSTextField* tf = [[NSTextField alloc] initWithFrame:frame];
            [tf setStringValue:text];
            [tf setBezeled:NO];
            [tf setDrawsBackground:NO];
            [tf setEditable:NO];
            [tf setSelectable:NO];
            obj = tf;
            break;
        }
        case SC_UI_EDIT:
        {
            NSTextField* tf = [[NSTextField alloc] initWithFrame:frame];
            [tf setStringValue:text];
            [tf setBezeled:YES];
            [tf setEditable:YES];
            obj = tf;
            break;
        }
        case SC_UI_TEXT:
        {
            NSTextField* tf = [[NSTextField alloc] initWithFrame:frame];
            [tf setStringValue:text];
            [tf setBezeled:YES];
            [tf setEditable:YES];
            [[tf cell] setWraps:YES];
            obj = tf;
            break;
        }
        case SC_UI_BUTTON:
        {
            NSButton* b = [[NSButton alloc] initWithFrame:frame];
            [b setTitle:text];
            [b setButtonType:NSButtonTypeMomentaryPushIn];
            [b setBezelStyle:NSBezelStyleRounded];
            obj = b;
            break;
        }
        case SC_UI_CHECKBOX:
        {
            NSButton* b = [[NSButton alloc] initWithFrame:frame];
            [b setTitle:text];
            [b setButtonType:NSButtonTypeSwitch];
            [b setState:(control->checked ? NSControlStateValueOn : NSControlStateValueOff)];
            obj = b;
            break;
        }
        case SC_UI_RADIOBOX:
        {
            NSButton* b = [[NSButton alloc] initWithFrame:frame];
            [b setTitle:text];
            [b setButtonType:NSButtonTypeRadio];
            [b setState:(control->checked ? NSControlStateValueOn : NSControlStateValueOff)];
            obj = b;
            break;
        }
        case SC_UI_COMBO:
        {
            NSComboBox* cb = [[NSComboBox alloc] initWithFrame:frame];
            [cb setUsesDataSource:NO];
            ui_cocoa_combo_reload(cb, control);
            obj = cb;
            break;
        }
        case SC_UI_LIST:
        {
            NSScrollView* sv = [[NSScrollView alloc] initWithFrame:frame];
            [sv setHasVerticalScroller:YES];
            [sv setBorderType:NSBezelBorder];

            NSTableView* tv = [[NSTableView alloc] initWithFrame:[[sv contentView] bounds]];
            NSTableColumn* col = [[NSTableColumn alloc] initWithIdentifier:@"col"];
            [col setWidth:frame.size.width];
            [tv addTableColumn:col];
            [col release];
            [tv setHeaderView:nil];

            SCListDataSource* ds = [[SCListDataSource alloc] init];
            ds.control = control;
            [tv setDataSource:ds];
            /* dataSource 不被 tableView 持有，用 associated object 保活。 */
            objc_setAssociatedObject(sv, &g_ds_key, ds, OBJC_ASSOCIATION_RETAIN);
            [ds release];

            [sv setDocumentView:tv];
            [tv release];
            [tv reloadData];
            obj = sv;
            break;
        }
        default:
            return;
    }

    if (!obj)
        return;

    [super addSubview:obj];
    control->backend = obj; /* 持有 alloc 的 +1 */

    ui_cocoa_set_z(obj, control->z);
    ui_cocoa_restack(super);
}

void ui_backend_control_destroy(sc_ui_control* control)
{
    if (!control)
        return;

    NSView* obj = (NSView*) control->backend;
    if (obj)
    {
        [obj removeFromSuperview];
        [obj release];
        control->backend = NULL;
    }
}

void ui_backend_control_set_frame(sc_ui_control* control)
{
    if (!control)
        return;

    NSView* obj = (NSView*) control->backend;
    if (obj)
        [obj setFrame:NSMakeRect(control->x, control->y, control->width, control->height)];
}

void ui_backend_control_set_z(sc_ui_control* control)
{
    if (!control)
        return;

    NSView* obj = (NSView*) control->backend;
    if (!obj)
        return;

    ui_cocoa_set_z(obj, control->z);
    ui_cocoa_restack([obj superview]);
}

void ui_backend_control_set_text(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    NSString* text = ui_cocoa_text(control);

    switch (control->kind)
    {
        case SC_UI_LABEL:
        case SC_UI_EDIT:
        case SC_UI_TEXT:
            [(NSTextField*) control->backend setStringValue:text];
            break;
        case SC_UI_BUTTON:
        case SC_UI_CHECKBOX:
        case SC_UI_RADIOBOX:
            [(NSButton*) control->backend setTitle:text];
            break;
        case SC_UI_COMBO:
            [(NSComboBox*) control->backend setStringValue:text];
            break;
        default:
            break;
    }
}

void ui_backend_control_set_checked(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    if (control->kind == SC_UI_CHECKBOX || control->kind == SC_UI_RADIOBOX)
    {
        [(NSButton*) control->backend
            setState:(control->checked ? NSControlStateValueOn : NSControlStateValueOff)];
    }
}

void ui_backend_control_set_items(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    if (control->kind == SC_UI_COMBO)
    {
        ui_cocoa_combo_reload((NSComboBox*) control->backend, control);
    }
    else if (control->kind == SC_UI_LIST)
    {
        NSTableView* tv = (NSTableView*) [(NSScrollView*) control->backend documentView];
        [tv reloadData];
    }
}

void ui_backend_control_set_selected_index(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    int idx = control->selectedIndex;

    if (control->kind == SC_UI_COMBO)
    {
        NSComboBox* cb = (NSComboBox*) control->backend;
        if (idx >= 0 && idx < control->itemCount)
            [cb selectItemAtIndex:idx];
        else
            [cb deselectItemAtIndex:[cb indexOfSelectedItem]];
    }
    else if (control->kind == SC_UI_LIST)
    {
        NSTableView* tv = (NSTableView*) [(NSScrollView*) control->backend documentView];
        if (idx >= 0 && idx < control->itemCount)
            [tv selectRowIndexes:[NSIndexSet indexSetWithIndex:idx] byExtendingSelection:NO];
        else
            [tv deselectAll:nil];
    }
}

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    /* Cocoa 后端使用系统原生控件，已支持 CJK，无需自行加载字体。 */
    (void) ctx; (void) path; (void) size;
    return 0;
}

#endif /* SC_UI_COCOA */
