#include "ui_internal.h"

#if defined(SC_UI_UIKIT)

#import <UIKit/UIKit.h>
#import <objc/runtime.h>

/* ============================================================
 * ui uikit 后端 —— iOS 原生子视图/控件实现（MRR，无 ARC）
 * ============================================================
 * 契约见 src/ui_internal.h。与 cocoa 后端对称（NSView→UIView、
 * NSControl→UIControl），差异：
 *   - UIView 坐标系本就左上原点，无需翻转容器。
 *   - z-order：UIView 无 sortSubviewsUsingFunction，改用 associated object
 *     暂存 z，restack 时按 z 升序逐个 bringSubviewToFront（大者置顶）。
 *   - 控件映射：LABEL=UILabel / EDIT=UITextField / TEXT=UITextView /
 *     BUTTON=UIButton / CHECKBOX·RADIOBOX=UISwitch（UIKit 无原生复选/单选）/
 *     COMBO=UIPickerView / LIST=UITableView。
 *   - backend 持有该对象一次 +1，superview 另持一次；销毁时
 *     removeFromSuperview + release 归零（与 cocoa 一致）。
 * ============================================================ */

/* ---- LIST 的表数据源 + 委托：读 items、选中转 SELECT 事件 ---- */
@interface SCTableSource : NSObject <UITableViewDataSource, UITableViewDelegate>
@property (assign) sc_ui_control* control;
@end

@implementation SCTableSource
- (NSInteger)tableView:(UITableView*)tableView numberOfRowsInSection:(NSInteger)section
{
    return self.control ? self.control->itemCount : 0;
}
- (UITableViewCell*)tableView:(UITableView*)tableView cellForRowAtIndexPath:(NSIndexPath*)indexPath
{
    UITableViewCell* cell = [tableView dequeueReusableCellWithIdentifier:@"c"];
    if (!cell)
        cell = [[[UITableViewCell alloc] initWithStyle:UITableViewCellStyleDefault
                                       reuseIdentifier:@"c"] autorelease];
    const char* s = (self.control && indexPath.row < self.control->itemCount)
                        ? self.control->items[indexPath.row] : "";
    cell.textLabel.text = s ? [NSString stringWithUTF8String:s] : @"";
    return cell;
}
- (void)tableView:(UITableView*)tableView didSelectRowAtIndexPath:(NSIndexPath*)indexPath
{
    if (!self.control) return;
    self.control->selectedIndex = (int) indexPath.row;
    ui_emit_event(self.control, SC_UI_EVENT_SELECT);
}
@end

/* ---- COMBO 的选择器数据源/委托 ---- */
@interface SCPickerSource : NSObject <UIPickerViewDataSource, UIPickerViewDelegate>
@property (assign) sc_ui_control* control;
@end

@implementation SCPickerSource
- (NSInteger)numberOfComponentsInPickerView:(UIPickerView*)pickerView { return 1; }
- (NSInteger)pickerView:(UIPickerView*)pickerView numberOfRowsInComponent:(NSInteger)component
{
    return self.control ? self.control->itemCount : 0;
}
- (NSString*)pickerView:(UIPickerView*)pickerView
            titleForRow:(NSInteger)row
           forComponent:(NSInteger)component
{
    if (!self.control || row < 0 || row >= self.control->itemCount) return @"";
    const char* s = self.control->items[row];
    return s ? [NSString stringWithUTF8String:s] : @"";
}
- (void)pickerView:(UIPickerView*)pickerView
      didSelectRow:(NSInteger)row
       inComponent:(NSInteger)component
{
    if (!self.control) return;
    self.control->selectedIndex = (int) row;
    ui_emit_event(self.control, SC_UI_EVENT_SELECT);
}
@end

/* ---- 控件事件目标：UIButton 点击 / UISwitch 切换 → ui_emit_event ---- */
@interface SCTarget : NSObject
@property (assign) sc_ui_control* control;
- (void)onButton:(id)sender;
- (void)onSwitch:(id)sender;
@end

@implementation SCTarget
- (void)onButton:(id)sender
{
    (void) sender;
    if (self.control)
        ui_emit_event(self.control, SC_UI_EVENT_CLICK);
}
- (void)onSwitch:(id)sender
{
    if (!self.control) return;
    /* 读回开关态同步 control->checked，令 get_checked 拿到最新值。 */
    self.control->checked = [(UISwitch*) sender isOn] ? 1 : 0;
    ui_emit_event(self.control, SC_UI_EVENT_TOGGLE);
}
@end

/* ============================================================
 * z-order 与 restack 辅助
 * ============================================================ */

static char g_z_key;  /* associated object key：视图的 z 值 */
static char g_ds_key; /* associated object key：LIST/COMBO 的数据源 */
static char g_tgt_key; /* associated object key：BUTTON/SWITCH 的事件目标 */

static void ui_uikit_set_z(id view, int z)
{
    objc_setAssociatedObject(view, &g_z_key, @(z), OBJC_ASSOCIATION_RETAIN);
}

static int ui_uikit_get_z(id view)
{
    NSNumber* n = objc_getAssociatedObject(view, &g_z_key);
    return n ? [n intValue] : 0;
}

static void ui_uikit_restack(UIView* super)
{
    if (!super)
        return;
    NSArray* subs = [[super subviews] sortedArrayUsingComparator:^NSComparisonResult(UIView* a, UIView* b) {
        int za = ui_uikit_get_z(a);
        int zb = ui_uikit_get_z(b);
        if (za < zb) return NSOrderedAscending;
        if (za > zb) return NSOrderedDescending;
        return NSOrderedSame;
    }];
    for (UIView* v in subs)
        [super bringSubviewToFront:v];
}

static NSString* ui_uikit_text(sc_ui_control* control)
{
    if (control->text)
        return [NSString stringWithUTF8String:control->text];
    return @"";
}

/* ============================================================
 * 子窗口 hook
 * ============================================================ */

void ui_backend_window_create(sc_ui_window* win)
{
    if (!win)
        return;

    UIView* super = win->parent ? (UIView*) win->parent->backend
                                : (UIView*) win->nativeWindow;
    if (!super)
        return;

    CGRect frame = CGRectMake(win->x, win->y, win->width, win->height);
    UIView* view = [[UIView alloc] initWithFrame:frame];

    if (!win->parent)
        [view setAutoresizingMask:(UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight)];

    [super addSubview:view];
    win->backend = view; /* 持有 alloc 的 +1 */

    if (win->parent)
    {
        win->nativeWindow = view;
        win->platform = SC_PLATFORM_IOS;
    }

    ui_uikit_set_z(view, win->z);
    ui_uikit_restack(super);
}

void ui_backend_window_destroy(sc_ui_window* win)
{
    if (!win)
        return;

    UIView* view = (UIView*) win->backend;
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

    UIView* view = (UIView*) win->backend;
    if (view)
        [view setFrame:CGRectMake(win->x, win->y, win->width, win->height)];
}

void ui_backend_window_set_z(sc_ui_window* win)
{
    if (!win)
        return;

    UIView* view = (UIView*) win->backend;
    if (!view)
        return;

    ui_uikit_set_z(view, win->z);
    ui_uikit_restack([view superview]);
}

/* ============================================================
 * 控件 hook
 * ============================================================ */

void ui_backend_control_create(sc_ui_control* control)
{
    if (!control)
        return;

    UIView* super = control->window ? (UIView*) control->window->backend : nil;
    if (!super)
        return;

    CGRect frame = CGRectMake(control->x, control->y, control->width, control->height);
    NSString* text = ui_uikit_text(control);
    UIView* obj = nil;

    switch (control->kind)
    {
        case SC_UI_LABEL:
        {
            UILabel* l = [[UILabel alloc] initWithFrame:frame];
            [l setText:text];
            /* 透明底标签叠在深色 gpu 画面上——默认 labelColor 在浅色外观下为黑，
             * 会不可见；显式浅色确保可读（与 android setTextColor 对齐）。 */
            [l setTextColor:[UIColor colorWithWhite:0.925 alpha:1.0]];
            obj = l;
            break;
        }
        case SC_UI_EDIT:
        {
            UITextField* tf = [[UITextField alloc] initWithFrame:frame];
            [tf setText:text];
            [tf setBorderStyle:UITextBorderStyleRoundedRect];
            obj = tf;
            break;
        }
        case SC_UI_TEXT:
        {
            UITextView* tv = [[UITextView alloc] initWithFrame:frame];
            [tv setText:text];
            [tv setEditable:YES];
            obj = tv;
            break;
        }
        case SC_UI_BUTTON:
        {
            UIButton* b = [[UIButton buttonWithType:UIButtonTypeSystem] retain];
            [b setFrame:frame];
            [b setTitle:text forState:UIControlStateNormal];
            SCTarget* tgt = [[SCTarget alloc] init];
            tgt.control = control;
            [b addTarget:tgt action:@selector(onButton:)
                forControlEvents:UIControlEventTouchUpInside];
            objc_setAssociatedObject(b, &g_tgt_key, tgt, OBJC_ASSOCIATION_RETAIN);
            [tgt release];
            obj = b;
            break;
        }
        case SC_UI_CHECKBOX:
        case SC_UI_RADIOBOX:
        {
            /* UIKit 无原生复选/单选，用 UISwitch 表达开关态（frame 尺寸被忽略）。 */
            UISwitch* sw = [[UISwitch alloc] initWithFrame:frame];
            [sw setOn:(control->checked ? YES : NO)];
            SCTarget* tgt = [[SCTarget alloc] init];
            tgt.control = control;
            [sw addTarget:tgt action:@selector(onSwitch:)
                 forControlEvents:UIControlEventValueChanged];
            objc_setAssociatedObject(sw, &g_tgt_key, tgt, OBJC_ASSOCIATION_RETAIN);
            [tgt release];
            obj = sw;
            break;
        }
        case SC_UI_COMBO:
        {
            UIPickerView* pk = [[UIPickerView alloc] initWithFrame:frame];
            SCPickerSource* ds = [[SCPickerSource alloc] init];
            ds.control = control;
            [pk setDataSource:ds];
            [pk setDelegate:ds];
            objc_setAssociatedObject(pk, &g_ds_key, ds, OBJC_ASSOCIATION_RETAIN);
            [ds release];
            if (control->selectedIndex >= 0 && control->selectedIndex < control->itemCount)
                [pk selectRow:control->selectedIndex inComponent:0 animated:NO];
            obj = pk;
            break;
        }
        case SC_UI_LIST:
        {
            UITableView* tv = [[UITableView alloc] initWithFrame:frame style:UITableViewStylePlain];
            SCTableSource* ds = [[SCTableSource alloc] init];
            ds.control = control;
            [tv setDataSource:ds];
            [tv setDelegate:ds];
            objc_setAssociatedObject(tv, &g_ds_key, ds, OBJC_ASSOCIATION_RETAIN);
            [ds release];
            [tv reloadData];
            obj = tv;
            break;
        }
        default:
            return;
    }

    if (!obj)
        return;

    [super addSubview:obj];
    control->backend = obj; /* 持有 alloc/retain 的 +1 */

    ui_uikit_set_z(obj, control->z);
    ui_uikit_restack(super);
}

void ui_backend_control_destroy(sc_ui_control* control)
{
    if (!control)
        return;

    UIView* obj = (UIView*) control->backend;
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

    UIView* obj = (UIView*) control->backend;
    if (obj)
        [obj setFrame:CGRectMake(control->x, control->y, control->width, control->height)];
}

void ui_backend_control_set_z(sc_ui_control* control)
{
    if (!control)
        return;

    UIView* obj = (UIView*) control->backend;
    if (!obj)
        return;

    ui_uikit_set_z(obj, control->z);
    ui_uikit_restack([obj superview]);
}

void ui_backend_control_set_text(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    NSString* text = ui_uikit_text(control);

    switch (control->kind)
    {
        case SC_UI_LABEL:
            [(UILabel*) control->backend setText:text];
            break;
        case SC_UI_EDIT:
            [(UITextField*) control->backend setText:text];
            break;
        case SC_UI_TEXT:
            [(UITextView*) control->backend setText:text];
            break;
        case SC_UI_BUTTON:
            [(UIButton*) control->backend setTitle:text forState:UIControlStateNormal];
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
        [(UISwitch*) control->backend setOn:(control->checked ? YES : NO)];
}

void ui_backend_control_set_items(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    if (control->kind == SC_UI_COMBO)
    {
        [(UIPickerView*) control->backend reloadAllComponents];
    }
    else if (control->kind == SC_UI_LIST)
    {
        [(UITableView*) control->backend reloadData];
    }
}

void ui_backend_control_set_selected_index(sc_ui_control* control)
{
    if (!control || !control->backend)
        return;

    int idx = control->selectedIndex;

    if (control->kind == SC_UI_COMBO)
    {
        UIPickerView* pk = (UIPickerView*) control->backend;
        if (idx >= 0 && idx < control->itemCount)
            [pk selectRow:idx inComponent:0 animated:NO];
    }
    else if (control->kind == SC_UI_LIST)
    {
        UITableView* tv = (UITableView*) control->backend;
        if (idx >= 0 && idx < control->itemCount)
            [tv selectRowAtIndexPath:[NSIndexPath indexPathForRow:idx inSection:0]
                            animated:NO
                      scrollPosition:UITableViewScrollPositionNone];
        else
            [tv deselectRowAtIndexPath:[tv indexPathForSelectedRow] animated:NO];
    }
}

int ui_backend_set_font(sc_ui_ctx* ctx, const char* path, float size)
{
    /* UIKit 原生控件由系统字体渲染（含 CJK），无需自行加载字体。 */
    (void) ctx; (void) path; (void) size;
    return 0;
}

#endif /* SC_UI_UIKIT */
