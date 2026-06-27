# feature47：分布式 token 依赖（tok / dep / form）—— 声明式跨量传播。
#
# tok 是「分布式 token」：以字符串 id 唯一标识的共享量，值为类型擦除 @（此例当 i8 装箱用）。
#   · tok t: "id"        声明 token 句柄（enforce 纯从：set 直接赋值）
#   · tok t: "id"<缩进体>  声明并挂 combine 回调（form 候选：紧随缩进体即 combine）
#   · form t, v          初始化 form token：灌初值并升格为 form 主
#   · dep all/any: ...   声明 token 间依赖（follow 回调，all=与门 / any=或门）
#   · t.get() / t.set(v, tag) 取值 / 设值（set 触发依赖级联，tag 随附标签）
# 均为模块域静态对象，注册延迟到模块 init（编译器生成 tok_bind / tok_depend）；运行时
# 始终随工程链接（builtins/tok/tok_impl.c），无需 inc。
#
# combine 体唯一上下文形参 this: __sctok_in&（成员皆 @）：
#   this->base 当前值 · this->input 本次输入 · this->sender 发送者 · this->tag set 标签。
# follow 体唯一上下文形参 this: __scdep_in&：
#   this->toks 依赖项数组 · this->count 个数 · this->active 触发动作码；
#   a:"id" 局部名糖 → this->toks[i]。

# ---- form 候选：温度读数，combine 取较大者（去抖/取峰）----
tok level: "sensor.level"
    var b: i8 = (this->base: i8)         # 当前值（@ → i8）
    var i: i8 = (this->input: i8)        # 新输入
    var m: i8 = b                        # 取较大者
    if i > b
        m = i
    return (m: @)                        # 装箱回 @

# ---- enforce 从：告警标志（无体，set 直接赋值）----
tok alert: "sensor.alert"

# ---- 依赖：level 变更即评估，超阈值则点亮 alert（or 门：任一变更触发）----
dep any: l:"sensor.level"
    var v: i8 = (l->get(): i8)
    if v > 100
        alert->set((1: @), 0)
    return false                         # 维持 or 门

fnc main: i4
    form level, (0: @)                   # 初始化 form 主，初值 0

    level->set((50: @), 0)               # 50 → 不超阈值
    var lv: i8 = (level->get(): i8)
    var al: i8 = (alert->get(): i8)
    printf("after 50:  level=%lld alert=%lld\n", lv, al)

    level->set((150: @), 0)              # 150 → 超阈值，触发 alert
    lv = (level->get(): i8)
    al = (alert->get(): i8)
    printf("after 150: level=%lld alert=%lld\n", lv, al)

    level->set((30: @), 0)               # 30 < 当前峰值 150：combine 取较大者 → 仍 150
    lv = (level->get(): i8)
    printf("after 30:  level=%lld\n", lv)

    return 0
