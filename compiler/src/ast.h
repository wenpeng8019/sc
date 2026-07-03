#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "shader_ast.h"   // ShaderStage（GPU/着色器扩展；仅为 Decl 增加一个阶段标记）

// ============================================================
// AST（抽象语法树）—— 整个编译器的核心数据结构
// ============================================================
// 整体层次：
//   Program → Decl[] → Stmt[] → Expr
//   - Program: 整个程序的根，包含多个顶层声明
//   - Decl:    程序结构对象（enum/struct/union/alias/fnctype/fnc/var/let）
//   - Stmt:    语句（表达式/var/let/return/if/while/for/break/continue）
//   - Expr:    表达式（字面量/标识符/运算/调用/下标/成员访问）
// 设计原则：
//   1. AST 与后端解耦 —— 同一 AST 喂给 C转译 / sc再生 / JSON导出 三个后端
//   2. 所有节点携带 line 字段 —— 便于错误定位和 IDE 源码跳转
//   3. unique_ptr 明确所有权 —— Expr/Stmt/Decl 均为独占所有权
// ============================================================

// 前向声明
struct TypeRef;
struct Field;
struct Expr;
struct Decl;
struct Stmt;

using ExprPtr = std::unique_ptr<Expr>;
using DeclPtr = std::unique_ptr<Decl>;
using StmtPtr = std::unique_ptr<Stmt>;

struct StructCommon {
    std::vector<Field>          fields;             // 通用字段结构信息
                                                    // > 结构/联合: 成员字段
                                                    // > 函数: 形参列表
                                                    // > let/var/tls: 声明变量列表
                                                    // > 枚举: 项列表
    std::shared_ptr<TypeRef>    type;               // 通用类型结构信息
                                                    // > 函数: 函数的返回类型
                                                    // > 别名：指向的目标类型
                                                    // > 枚举: 枚举的底层整数基类型
    bool                        variadic = false;   // 结构信息中是否有可变参数（函数/远程调用）
};

// --------------------------
// 类型引用：用来统一表示（描述）sc 中所有可能的类型写法
// --------------------------
struct TypeRef {

    // 类型名。即 "i4" "f8" "u1" 等内置类型，或用户 def 的自定义类型名
    // + 空字符串表示未指定类型，由 codegen 按默认规则推断：
    //   ptr > 0 → void*    （无类型指针默认指向 void）
    //   ptr = 0 → char*    （无类型对象默认字符串/字节缓冲区）
    std::string name;

    //---------

    // 对于指针类型：eg: name&，name&&
    // + 这里 ptr 用来记录 & 的个数，即指针层数，最终由 codegen 还原为 C 的 T* 形式。
    //   与 C 不同，sc 的指针声明在名字右侧（name& 而非 int *name）
    int ptr = 0;

    //---------

    // 对于胖指针类型（自动指针）：eg: name@
    // + fat=true 表示这是一个参与引用图与释放点验证的胖指针（见 builtins/auto_ptr.md）。
    //   C 侧展开为 sc_fat { void* p; int32_t* tar; int32_t* own; }（24 字节，首成员 p 即裸指针）。
    //   胖指针恒为单层（T@，不存在 T@@）；ptr 与 fat 互斥（fat 时 ptr==0）。
    bool fat = false;

    //---------

    // 对于半自动指针（单例指针）：eg: name@1
    // + autoFree=true 表示这是一个「单例指针」：物理上等同普通指针（ptr==1，C 侧即 T*，
    //   取值返回裸地址、只接受普通指针 & / nil 赋值），但附带自动指针 @ 的退域 RAII 语义：
    //   退出作用域（或重新赋值覆盖旧值）时，若指向对象非 nil 则自动 drop + free 销毁该对象。
    //   相当于「单例对象智能指针」（unique_ptr）。autoFree 时 ptr==1、fat==false、name 非空。
    bool autoFree = false;

    //---------

    // 对于瘦指针（自动指针另一形态）：eg: name*
    // + thin=true 表示真瘦指针 sc_thin（24B {p,tar,dtor}）：只统计目标入边 tar，
    //   不带/不统计持有者出边 own（不参与「未清出边」校验）；dtor 随句柄供裸 * 自析。
    //   thin 时 fat==true、ptr==0；与胖指针 @ 可互相赋值转换（拷 p/tar，各按己方记账）。
    bool thin = false;

    //---------

    // 类型限定符（const/volatile/restrict）：在 sc 中以「上下文标识符」书写，非关键字。
    // + qConst/qVolatile：写在类型名前（类型侧），约束「指向对象/对象本身」只读或易变。
    //     eg: var a: volatile i4    → volatile int32_t a
    //         var p: const node&    → const node *p（指针可改，指向只读）
    //         var x: const volatile u4& → const volatile uint32_t *x（MMIO 只读寄存器）
    //   注意「指针本身」是否只读由 let/var 决定（let p: node& → node *const p），
    //   与类型侧 const 正交。
    // + qRestrict：尾置（写在 & 之后），约束指针无别名（restrict）。
    //     eg: fnc copy: dst: i4& restrict, src: const i4& restrict
    bool qConst = false;
    bool qVolatile = false;
    bool qRestrict = false;

    //---------

    // 对于数组类型：eg: name[x][y]
    // + 这里要记录维度 shape 列表 → {"x","y"}；
    //   空字符串表示未指定长度（name[]）；
    //   非空即为数组类型（也就是数组的 size），与 C 的多维数组对齐
    std::vector<std::string> arrayDims; 

    //---------

    // 对于内联结构/联合：即类型直接写在变量声明处，无需预先 def
    // eg: var obj: {x:i4, y:i4}  → hasInline=true, inlineUnion=false
    //     var tag: (i:i4, f:f4)  → hasInline=true, inlineUnion=true
    bool hasInline = false;
    bool inlineUnion = false;           // true=(联合), false={结构}

    //---------

    enum class FncKind {
        None,
        PlainPtr,                       // 普通函数指针：fnc: ret, params...（名字在前，不注入接收者）
        MethodPtr,                      // 每对象方法指针：fnc name: ret, params...（fnc 在前，无函数体）
                                        //   按成员函数约定调用（自动注入 &o/p 为首参接收者），
                                        //   但每个对象各自持有指针（占存储、默认 nil），用于伪类无派生下的
                                        //   "每对象虚方法/接口"。C 字段为 ret (*name)(T*, ...)。
                                        //   赋值的函数(普通 fnc/lambda)须显式声明接收者 T& 为首参。
    };

    // 对于内联函数指针 —— 特指结构体成员的类型为函数签名（且无函数体）：
    // eg: def obj: { func: fnc: i4, x:i4, y:i4 } → 对应 C 中的展开为 int32_t (*func)(int32_t x, int32_t y)
    // ! 若字段签名后跟缩进函数体，则不是字段而是成员函数
    //   此时 parser 会将其提升为带 methodOwner 的顶层(Decl) FuncD，
    //   同时 C 中会对应生成 T_m(T *_this, ...) 成员函数声明
    FncKind fnKind = FncKind::None;

    //---------

    // 分身/切片句柄类型 T[...]（def T: <S> {} 机制）：
    //   project=true 表示这是一个分身句柄类型，name=实体类型 T；
    //   projectArgs=方括号内初值表达式（= T.alloc 去掉隐式 this 后的形参实参）。
    //   转 C 时展开为匿名句柄结构体 T__project（字段=alloc 参数 + S* _）。
    //   用 shared_ptr 包裹以保持 TypeRef 可拷贝（ExprPtr 不可拷贝）。
    bool project = false;
    std::shared_ptr<std::vector<ExprPtr>> projectArgs;

    //---------

    // C 桥接类型 ::name（严格命名域，类 C++）：类型名以 :: 前置书写，标记该基类型为
    //   C 命名域符号 —— 转 C 时原样落 C 类型名（不加 sc_ 前缀），语义层不报「未定义类型」。
    //   仅对具名基类型（name 非空）有意义；内联结构/函数指针无 :: 形态。
    bool cBridge = false;

    //---------

    StructCommon structCommon;
};

// --------------------------
// 字段 / 声明项：用来统一描述程序（定义/声明中的）实际访问和操作的目标数据对象
// --------------------------
// Field 是一个多用途结构体，在不同 Decl 上下文中含义不同：
// > let/var/tls 常/变量项    → name + type + init（可选的初值表达式）
// > struct/union 成员       → [name] + type
//   + 这里 name 可以为空，用来表示匿名结构，即直接内嵌另一个 {}/() TypeRef 类型，该 TypeRef 的 hasInline=true && !inlineFields.empty
// > 函数参数                → name + type
// > 枚举项                  → name + init（可选的枚举常量值）
struct Field {
    std::string name;                   // 变量/成员/参数/枚举项名称
    TypeRef     type;                   // 类型信息（含指针层数、数组标记、内联结构等）
    ExprPtr     init;                   // 初值表达式（用于 let/var/tls 声明、和枚举项）

    bool        synthetic = false;      // 编译器注入的隐藏成员（如链表 _prev/_next，emit-sc 不输出）

    bool        cBridge = false;        // C 桥接绑定（仅 var/let/tls 项）：源码以 name:: type 书写。
                                        // + 认领一个 C 侧已定义的全局符号，转 C 生成 extern T name;
                                        //   不分配存储、无初值；仅把名字与类型登记进 sc 符号表。

    int         line = 0;               // 声明所在行号
};

// --------------------------
// 表达式：AST 中最细粒度的节点（子树）。所有运算、字面量、调用都是表达式
// --------------------------
struct Expr {

    enum Kind {
        // 叶子节点 —— 语法分析遇到终结符时直接创建（也就是作为被访问/操作的数据对象）
        Ident,      // 标识符      eg. varName
        IntLit,     // 整数字面量   eg. 42, 0xFF
        FloatLit,   // 浮点字面量   eg. 3.14
        StrLit,     // 字符串字面量 eg. "hello"
        CharLit,    // 字符字面量   eg. 'a'

        // 一元运算 —— parseUnary() 解析前缀运算符
        Unary,      // 前缀运算     eg. -x, !x, ~x, *p, &x
        PostUnary,  // 后缀运算     eg. i++, i--  
                    //             > a=操作数（子表达式）, op 为运算符文本

        // 二元运算 —— parseBinary() 解析，按优先级递归
        Binary,     // 二元运算     eg. a+b, a>b, a=b, a+=b
                    //             > a/b 分别为左右操作数（子表达式）, op 为运算符文本

        // 三元运算 —— parseTernary() 解析
        Ternary,    // 三元条件     eg. a ? b : c 
                    //             > a=条件（子表达式）, b=真值分支（子表达式）, c=假值分支（子表达式）

        // 后缀链 —— parsePostfix() 循环解析，可连续组合
        Index,      // 下标访问     eg. arr[0]
                    //             > a=数组（子表达式）, b=下标（子表达式）
        Member,     // 成员访问     eg. obj.f  或 ptr->f
                    //             > op="." 或 "->", text=成员名
        Call,       // 函数调用     eg. foo(a, b)
                    //             > a=被调函数（子表达式）, args=实参（子表达式）列表
        InitList,   // 初始化列表   eg: {1, 2, 3} 
                    //             > args=元素列表（可嵌套）
        Sizeof,     // sizeof(表达式或类型名)  
                    //             > a=内层表达式
        Offsetof,   // offsetof(Type, field)  
                    //             > text=类型名, op=成员名
        Cast,       // 强制类型转换  右值位置可裸写
                    //             + 可以不加括号：eg. expr: type& （作为右值时）
                    //             + 但如果继续 ->/. 等操作时，则需要加括号，eg. (expr: type&)->f
                    //             > a=被转换表达式, op=目标类型名, castPtr=指针层数（& 个数）
        FncLit,     // 匿名函数字面量  var = fnc: ret, params \n\tbody
                    //             > fncSig=签名, fncBody=函数体，赋值给同签名的函数指针变量
        Await,      // await E  挂起当前 rpc 状态机，等 E(产 future) 就绪后恢复
                    //             > a=被 await 的表达式（rpc 调用 或 返回 future& 的叶子原语调用）
        Async,      // async E  把 rpc 调用登记进事件循环，立即返回 future&（不阻塞）
                    //             > a=rpc 调用（Expr::Call）
        Sync,       // sync E   同步驱动 rpc：无目标=当前线程直接执行（替代裸 rpc 调用），返回其结果
                    //             > a=rpc 调用（Expr::Call），b=可选队列目标，c=可选状态出参 &st（i4，仅 <timeout> 有意义）
    } kind;

    std::string text;           // 标识符名 / 字面量值 / 成员名 / Offsetof 的目标类型名
                                // 始终表示表达式操作的目标项
    ExprPtr a, b, c;            // 子表达式指针
                                // Unary/PostUnary: a=操作数
                                // Binary: a=左操作数, b=右操作数
                                // Ternary: a=条件, b=真值分支, c=假值分支
                                // Index/Member/Call: a=被操作对象
    std::vector<ExprPtr> args;  // Call 函数调用的实参列表
                                // InitList 时为初始化元素列表

    // InitList 形态区分（数组用 [ ]，结构体/联合用 { }）：
    //   initBracket=true  → 方括号数组字面量 [a, b, c]（仅用于数组目标）
    //   initBracket=false → 花括号聚合字面量 {a, b} 或指定成员 {x=1, y=2}（结构体/联合目标）
    // initNames 与 args 平行：非空串表示该项为指定成员 name=expr（C99 .name=expr），
    //   空串表示位置初始化；整表为空表示全部位置初始化。
    bool initBracket = false;
    std::vector<std::string> initNames;

    std::string op;             // 针对目标项的运算和操作（Cast 时为目标类型名）
    int castPtr = 0;            // Cast: 目标类型的指针层数
    bool castIsFmt = false;     // Cast: op 为 printf 格式串字面量（含引号）而非类型名
                                // —— print 实参的格式覆盖 (expr: "%.5d")，仅 print 语境有意义
    // Cast 目标类型限定符（与声明侧同义，见 §4）：const/volatile 前缀到底层类型，
    // restrict 尾置于指针（仅指针强转有意义，约束无别名）。
    bool castConst = false;     // Cast: 目标类型 const 限定（前缀）
    bool castVolatile = false;  // Cast: 目标类型 volatile 限定（前缀）
    bool castRestrict = false;  // Cast: 目标指针 restrict 限定（尾置）
    bool castFat = false;       // Cast: 目标为自动指针 T@/裸 @（op 为空 → 裸 @ 类型擦除）
    bool castThin = false;      // Cast: 目标为瘦指针 T*/裸 *（op 为空 → 裸 * 类型擦除）；castFat 同置
    bool castCBridge = false;   // Cast: 目标为 C 域类型 ::T（严格命名域）：原样落 C 类型名（不加 sc_ 前缀）

    // stringify<key:val,...> 选项块（仅 Call 且 callee 为 stringify 关键字时有效）；
    // + 值限整数字面量（如 compact:1），codegen 据此生成 (stringify_t){...} 复合字面量
    std::vector<std::pair<std::string, long long>> sofOpts;

    // sync<q, prio:N, delay:ms, timeout:ms> / async<q, prio:N, delay:ms> 选项块（仅 Sync/Async）；
    // + 队列目标 q 解析进 b；这里仅存 opt:val 选项，值为任意表达式（运行时求值，不做编译期范围校验）。
    //   codegen 据此把 prio/delay/timeout 透传给 queue 协议 post/sync/async。
    //   delay/priority 仅作用于 FIFO-pull 消费路径，池宿主路径忽略（池自调度）。timeout 仅 sync 有限超时（P5c）。
    std::vector<std::pair<std::string, ExprPtr>> syncOpts;

    // future<ID>() 构造标记（仅 Call 且 callee 为 future 伪构造时有效）；
    // + ID 为 future_id 枚举常量名，编译期聚合成 future_id 枚举（type.h），
    //   codegen 生成 future__new_tagged(ID) 为该 future 设 id，供 async_loop 按 id 派发。
    std::string futureId;

    // T<atom>() 自动指针原子构造标记（仅 Call 且 callee 为类型名的胖目标构造时有效）；
    // + 置位 → T__new_ref 把对象头标记为原子，sc_fat_bind/unbind 对该对象 in/out 用原子 RMW。
    bool ctorAtom = false;

    // T<raw>() 自动指针裸分配构造标记（仅 Call 且 callee 为类型名的胖目标构造时有效）；
    // + 置位 → T__new_ref 走 sc_alloc（libc/间接层）而非默认 chunk 池，释放走 sc_free。
    bool ctorRaw = false;

    // C 桥接标记（仅 Ident）：源码以 :: 前缀书写（::name）。
    // + name 是 C 侧的函数/宏/符号，不参与 sc 符号解析（跳过未定义检查），
    //   转 C 时原样 emit。后接 (...) 即 C 函数/宏调用；裸写即取 C 符号值。
    bool cBridge = false;

    int line = 0;               // 表达式起始行号（用于错误报告）

    // 匿名函数字面量（FncLit）专用
    StructCommon fncSig;                // 匿名函数签名（返回类型 + 参数列表）
    std::vector<StmtPtr> fncBody;       // 匿名函数体
};

// --------------------------
// 语句：函数体的基本单位。一条语句通常占一行（或一个缩进块）。
// --------------------------
struct Stmt {

    enum Kind {
        ExprS,      // 表达式语句       eg. foo(); a = 1;  (以 expr 字段存储)
                    // + 赋值/调用语句，也就是 I/O/JP
        LetS,       // 常量声明语句     eg. let MAX = 100
        VarS,       // 变量声明语句     eg. var x: i4      (以 decls 字段存储多项)
        TlsS,       // 线程局部变量声明  eg. tls cnt: i4   (static 存储期，每线程独立)
        ReturnS,    // return 语句     eg. return expr;   (expr 可为空 = return;)
        IfS,        // if/else 条件分支
        WhileS,     // while 循环
        DoWhileS,   // do-while 循环
        ForS,       // for 循环        for init; cond; step \n body
        CaseS,      // case 分支       case expr: labels/default + 自动 break
        BreakS,     // break 语句      (无附加数据)
        ContinueS,  // continue 语句   (无附加数据)
        GotoS,      // goto 标签跳转
        LabelS,     // 标签定义

        DeclS,      // 内嵌类型声明     函数体内用 def 定义局部类型（不常见但允许）
        MixS,       // 函数体内 mix 展开  mix name(args)；调用表达式存 expr（Expr::Call）
        InlineDefS, // 函数体内 inl 定义  inl name: [ret,] p:t, ...（真内联块）
                    //                > text=块名；decl=签名（DeclPtr，kind=FuncD，
                    //                  structCommon.type=返回类型/空=void，structCommon.fields=形参，
                    //                  body=块体语句）
                    //                > 定义处不产码，仅登记；调用点原地展开为
                    //                  { 形参临时=实参; body; label:; }。
                    //                > void inl：当语句用，裸 return→goto label；
                    //                  值 inl（有返回类型）：仅作 lhs=name() / var|let x=name() 右侧，
                    //                  每个 return v → lhs=v; goto label
        FinalS,     // final 域退出钩子  final \n body...（本作用域每个退出点运行 body，
                    //                先于自动胖边拆解/断言；body 存于 body 字段）
        RunS,       // run 线程语句    run[<opt:v,...>] rpc调用 [, thread出参地址]
                    //                > expr=rpc 调用（Expr::Call）
                    //                > forInit=可选出参（&t，t 为 thread&）
                    //                  + 有出参 → joinable（join 等待并回收）；无 → detach 自释放
                    //                > runOpts=可选线程属性（stack:u4 栈大小, prio:u1 优先级），透传给 C
        DoneS,      // done 标记就绪    done future [, result]（异步特性）
                    //                > expr=future（future&），forInit=可选结果（自动 void* 擦除）
                    //                  + 等价 future_done(future, result)；result 省略=NULL
        FormS,      // form token 初始化  form t, v[, ctx[, exec]]（灌初值 + 升格为 form 主 + 可选挂侧车/钩子）
                    //                > expr=tok 句柄（tok&），forInit=初值（void& 自动指针），
                    //                  forCond=可选节点私有上下文（&n 侧车，绑定到 tok ctx），
                    //                  forStep=可选节点处理钩子 exec（token_exec_fn，绑定到 tok exec）
                    //                  + 等价 token_form(t, v, 0, ctx, exec)；首个执行者成为分布式值主
        BackS,      // back 反向遍历    back t[, seed]（反向传播骨架）
                    //                > expr=tok 句柄（tok&），forInit=可选梯度种子（@ 自动擦除）
                    //                  + 等价 token_back(t, seed)；沿反向邻接按反拓扑序唤起 follow
        PrintS,     // print 日志输出   print[<chn>] arg, arg, ...（括号可省）
                    //                > printChn=通道 u1 的 C 表达式文本（默认 "0"），透传给 C print
                    //                > printCompat=true 时为括号兼容模式（C printf 语法，实参原样传递）
                    //                  false 时为拼接糖：字符串字面量=纯文本；其余=按静态类型自动
                    //                  补说明符的变量；Cast(castIsFmt)=显式格式覆盖 (expr: "%fmt")
        AssertS,    // assert 测试断言  assert 表达式[, 消息表达式]
                    //                > expr=布尔表达式（失败=假）
                    //                > assertMsg=可选消息表达式（const char*），空=无
                    //                > text=表达式源码串（失败报告回显，由 parser 据 token 重建）
        RetCallS,   // ret 调用语法糖   retOp func()[ \n body]
                    //                > expr=被调用的函数表达式（Expr::Call）
                    //                > retOp ∈ {"!",">","<",">=","<=","!!"}
                    //                > body=条件成立时执行的块（"!!" 形态无块）
                    //                  首次出现自动声明函数级 ret 变量 $，每次复用：
                    //                    ! f()   等价 if (!($=f())) { body }
                    //                    > f()   等价 if (($=f()) >  0) { body }（< <= >= 类推）
                    //                    !! f()  等价 $=f(); if ($ != ok) assert(false)
                    //                > retProp=true（尾置 ? 错误传播糖）：体执行后向上层
                    //                  return $（函数 void 则 return;），仅 ! / 比较形态可加 ?
    } kind;

    ExprPtr expr;                       // ExprS: 表达式的值
                                        // ReturnS: 返回的表达式（空=无返回值 return;）
                                        // IfS/WhileS: 条件表达式（必须可求值为布尔）

    std::vector<Field> decls;           // LetS/VarS/TlsS: 声明的常量/变量/线程局部变量列表
                                        // + 单行多变量以逗号分隔，多行以缩进续行

    std::vector<StmtPtr> body;          // IfS/WhileS/ForS: 条件成立时执行的主体语句
    std::vector<StmtPtr> elseBody;      // IfS: else 分支（可能为空、单条 else if、或多条语句块）

    ExprPtr forInit, forCond, forStep;  // ForS: for (init; cond; step) 三段表达式

    // ForS（for-in 变体）：for name[: T&] in coll [revert] [step e] [offset e] [num e]
    bool forIn = false;                 // true=for-in 变体（与经典三段式互斥）
    std::string forVar;                 // 循环变量名
    std::vector<std::string> forIdxVars; // 索引/坐标变量名（v 之后逗号分隔）：for v, i, j in ...
                                        //   数量须与集合维度一致（一维 0 或 1 个，N 维数组 N 个）；
                                        //   可索引集合（数组/标量/范围）→ 真实下标（revert 时倒序，
                                        //   v==coll[i] 恒等）；仅 next 迭代（串/链/容器）→ 0,1,2... 递增计数
    TypeRef forVarType;                 // 显式 name: T& 循环变量类型（forVarHasType 为真时有效）
    bool forVarHasType = false;
    ExprPtr forColl;                    // 集合表达式（数组/链/容器/串/整数计数）；范围字面量时为空
    ExprPtr forRangeLo, forRangeHi;     // 范围 [lo, hi] / [lo, hi) 的下/上界（forIsRange 时有效）
    bool forIsRange = false;            // true=范围字面量集合 [lo, hi]/[lo, hi)
    bool forRangeIncl = false;          // true=闭区间 ]（含上界），false=半开 )（不含上界）
    bool forRevert = false;             // revert 选项：逆序遍历
    ExprPtr forStepE, forOffsetE, forNumE;  // step/offset/num 选项表达式（空=默认 1/0/无上限）

    // RunS: run<target, stack:N, prio:M> —— target（池/队列）解析进 runTarget，opt:val 选项存 runOpts。
    //   选项值为任意表达式（运行时求值，不做编译期范围校验），stack/prio 透传给 C 线程属性。
    ExprPtr runTarget;                                       // RunS: run<target> 目标（池入池；队列留待下轮），无则普通线程
    std::vector<std::pair<std::string, ExprPtr>> runOpts;    // RunS: run<...stack:N, prio:M> 线程属性选项

    std::string printChn;               // PrintS: <chn> 通道的 C 表达式文本（默认 "0"）
    std::vector<ExprPtr> printArgs;     // PrintS: 拼接实参列表（顺序）
    bool printCompat = false;           // PrintS: 括号形式 print(...) → C printf 兼容模式（实参原样传递）

    std::string retOp;                  // RetCallS: 语法糖操作符（"!" ">" "<" ">=" "<=" "!!"）
    bool retProp = false;               // RetCallS: 尾置 ? 错误传播糖（体后 return $）

    ExprPtr assertMsg;                  // AssertS: 可选失败消息表达式（const char*）

    struct CaseArm {
        std::vector<ExprPtr> labels;    // 空=default 分支
        std::vector<StmtPtr> body;      // 分支体
        bool through = false;           // 末尾 through：贯穿到下一分支
        std::string binding;            // 标签联合解构绑定名（Variant as x）；空=无绑定
        int line = 0;
    };
    std::vector<CaseArm> caseArms;      // CaseS: 分支列表（labels 为空表示 default）

    std::string text;                   // GotoS/LabelS: 标签名
    
    DeclPtr decl;                       // DeclS:（函数内部）内嵌的类型定义（def）

    bool exported = false;              // VarS/LetS: 宏体内 @ 前缀导出标记（外部链接 + extern 前置声明）；
                                        //   仅在 def 宏体内有意义，顶层 mix 展开后与顶层 @var/@let 同义

    int line = 0;                       // 语句起始行号
};

// --------------------------
// 顶层声明：即程序结构对象，所有顶层元素在 AST 中都是 Decl，通过 kind 字段区分。
// --------------------------
// 程序由四种结构对象组成：def（类型）、fnc（函数）、let（常量）、var（变量）、tls（线程局部变量）和 inc（头文件引入）
struct Decl {

    enum Kind {
        // -- def 定义的类型 --
        EnumD,      // 枚举       def name: base \n\tItem1=0, Item2 ...
        StructD,    // 结构体     def name: { field:type, ... }
        UnionD,     // 联合体     def name: ( field:type, ... )
        AliasD,     // 类型别名   def name -> target_type

        // -- tst 定义的单元测试用例 --
        TestD,      // 测试用例   tst "名字" \n\tbody（--test 模式收集运行；普通编译忽略）
                    //            name=显示名（字符串内文）；body=语句；testSkip=tst.skip 跳过

        // -- fnc 定义的函数 --
        FuncTypeD,  // 函数类型   fnc name: ret, p1:t1, p2:t2 \n（仅有签名，无函数体）
                    //            → C 中生成 typedef 函数指针类型声明
        FuncD,      // 函数实现   fnc name: ret, p1:t1, p2:t2 \n\tbody
                    //            或 fnc name -> func_type \n\tbody（实现预定义函数类型）

        // -- var/let/tls 全局变量/常量/线程局部变量 --
        VarD,       // 全局变量   var name:type [= init]
        LetD,       // 全局常量   let name:type = init
        TlsD,       // 线程局部变量 tls name:type [= init]（static，不可导出）

        // -- inc 头文件引入 --
        IncD,       // 引入头文件  inc stdio.h → #include <stdio.h>
                    //              inc "my.h"  → #include "my.h"

        // -- add 实现/库文件添加 --
        AddD,       // 添加实现/库文件到工程  add impl.c → 编译并链接
                    //              add libfoo.a / libfoo.so → 直接链接
                    //              纯构建指令，不产生 C 输出，name 为文件文本

        // -- def 定义的宏 / mix 展开 --
        MacroD,     // 结构化宏  def name: p1,p2,... \n\tbody → #define name(p1,...) \ body
                    //            对象宏 def NAME: = value → #define NAME value（macroObject=true）
                    //            函数宏参数存 structCommon.fields(+variadic)，body 存语句；
                    //            对象宏值存 expr。
        MixD,       // 顶层 mix 展开  mix name(args) → 输出 name(args)（无分号，宏体自含）
                    //            调用表达式存 expr（Expr::Call）

        // -- tok 分布式 token 依赖机制 --
        DepD,       // token 依赖关系  dep all/any: a:"id1", b:"id2" [map|loop o:"id3"] \n\tbody
                    //            depAll=门逻辑（true=与门 all / false=或门 any，与边拓扑正交）；
                    //            depItems=源依赖项 [(局部名, id 串)...]；depTargets=map/loop 目标项（可空）；
                    //            depLoop=边拓扑为受控反馈环（loop 替 map：豁免环检测、走 SCC 烘焙）；
                    //            tokFn=follow 回调 C 名；随模块默认 init 注册（token_depend[_map/_loop]）。
                    //            配套 follow FuncD（tokHidden）。map=DAG 边（环检测）；loop=反馈边（SCC）。
    } kind;

    std::string name;               // 类型名 / 函数名；IncD 时为头文件文本

    std::string origin;             // 外部符号来源（导入文件路径或 builtin 名称）
    std::string inlinedFrom;        // 经 `add <file>.sc` 内联到本单元的来源文件绝对路径（空=本单元原生）。
                                    // 本声明属本编译单元（external=false，正常发码），但源自另一 .sc：
                                    //   · inc/add 相对路径按该来源目录解析（而非容器目录）；
                                    //   · codegen #line 映射回原文件（调试断点/单步落在被 add 的 .sc）；
                                    //   · AST 归属：插件据此把成员标注/跳转到被 add 的源文件。
    bool external = false;          // 来自 inc 导入的外部符号（AST/插件可与本地符号区分）
    bool used = false;              // external 描述符是否被当前单元引用（仅对 external 有意义；
                                    // 由 analyzeExternalUsage 统计，供插件区分"已引用/仅导入未用"）
    int  externDeclared = -1;       // 仅 external IncD：该来源(模块/头文件)声明的描述符总数；
                                    // -1=未知（C 头退化文本匹配模式无法枚举全集）。供插件显示"已用 N / 共 M"
    bool externAnalyzed = false;    // 仅 external IncD：是否已确定该来源的符号全集
                                    //（.sc 合并 / libclang 解析 / 退化读到头文件）→ 允许"导入未使用"警告
    bool exported = false;          // @前缀标记：导出对象（--emit-c 时生成 .h 声明）
    bool genTypeHeader = false;     // 编译器合成的 future_id 枚举：转译工程下写入 type.h（各 .c #include），
                                    //   emit-sc 不输出；stdout/内联模式则就地内联进 .c（自包含）
    bool testSkip = false;          // TestD: tst.skip 形态（跳过执行，报告记 skipped）

    bool linked = false;            // 链表结构体 def T: ~ {}：头部注入 _prev/_next 双向链指针

    bool isClass = false;           // cls 类定义（StructD）：首部注入 synthetic _class 分派器指针，
                                    // 各 dim 折叠进唯一分派器 T_hyper_impl 的 switch 分支

    std::string modName;            // mod 模块单例（StructD）：本结构为 `mod N` 生成的单例类型 N_m，
                                    // modName=实例名 N（codegen_sc 据此回写 `mod N:` 块，
                                    // 并抑制配套 VarD 实例；codegen_c 按普通 struct+var 处理）。
    bool modInstance = false;       // mod 配套实例（VarD）：`mod N` 自动生成的 `var N: N_m`，
                                    // codegen_sc 跳过（已并入 mod 块回写），codegen_c 正常发出。

    // ---- tok 分布式 token 机制 ----
    bool isTok = false;             // tok 句柄（VarD）：`tok t:"id"` 降解的 `var t: tok&`，模块域静态；
                                    // codegen_c 在模块 init 注入 t = tok_bind(tokId, tokFn)。
    std::string tokId;              // isTok VarD：token 的字符串 id（跨进程唯一键）；
                                    //   DepD 不用此字段（id 在 depItems 内）。
    std::string tokFn;              // isTok VarD：combine 回调 C 名（空=无体，纯 enforce/从）；
                                    //   DepD：follow 回调 C 名。
    bool tokHidden = false;         // 合成的 combine/follow FuncD：codegen_sc 跳过（已并入 tok/dep 块回写），
                                    //   codegen_c 正常 static 发出。
    bool depAll = false;            // DepD：门逻辑（true=与门 all / false=或门 any；与 map/loop 边拓扑正交）。
    bool depLoop = false;           // DepD：边拓扑为受控反馈环（loop 替 map）——豁免环检测，走独立烘焙路径（SCC 缩点 + token_depend_loop）。
    std::vector<std::pair<std::string, std::string>> depItems;  // DepD：源依赖项（触发/上游）[(局部名, id 串)...]
    std::vector<std::pair<std::string, std::string>> depTargets; // DepD：map 目标项（输出/下游）[(局部名, id 串)...]
                                    //   非空 = `dep ...: 源 map 目标 - 体`，显式声明 源→目标 依赖图边
                                    //   （编译期环检测）；follow 体可同时按局部名引用源与目标 token。

    bool heapOnly = false;          // 堆专属类型 def/cls NAME&: {}（名后紧跟 &）：
                                    // 应用层不存在 NAME 值类型，仅 NAME&（普通指针）/NAME@（自动指针）。
                                    // 禁止一切值形态（局部/全局 var、值成员、值参、值返回、值数组）。
                                    // NAME() 仍堆构造（malloc+init）产出 NAME& 指针；
                                    // 普通指针 .drop() 由 codegen 注入结构体块释放。
    bool isDim = false;             // 维度（FuncD，methodOwner=类名、methodName=维度名）：
                                    // 不单独生成函数，codegen 折叠进所属类的分派器 case；返回恒 tril

    bool tagged = false;            // 标签联合 def T: @( v1 / v2:T / ... )：UnionD 加隐藏 tag，
                                    // 安全构造 T.Variant(payload) + case 解构（Variant as x）。
                                    // 字段即变体：type.name 为空表示无载荷变体。

    std::string adtColl;            // ADT 容器结构体 def T: <C, I> {}：C=容器类型名
    std::string adtItem;            // 同上：I=元素节点类型名（注入为 T 首个 synthetic 成员 _adt）

    std::string projectSelf;        // 分身/切片实体 def T: <S> {}：S=分身/切片类型名（在 T 上标记）
    std::string projectEntity;      // 分身/切片类型 S 上标记：其实体类型 T 名（注入 pass 回填，
                                    // 供 self 上下文关键字定位实体、_self 回指字段定型）

    std::vector<StmtPtr> body;      // FuncD: 函数体的语句列表
                                    // + FuncTypeD 的 body 为空，只有签名无实现

    bool isRpc = false;             // rpc 声明：参数/返回值展开为同名结构体，实际函数为 void name_rpc(struct name*)
                                    // + FuncD=定义（含体），FuncTypeD=仅声明（实现在外部）
    bool hasAwait = false;          // rpc 体内含 await：编译为状态机（stackless coroutine），
                                    // 生成启动器 name__async + 状态机 worker；不能被 run。由解析扫描体设置。
    bool cImpl = false;             // :: 后缀标记：由 C 实现的接口（仅 FuncTypeD）
                                    // + 无函数体，转 C 时生成 extern 声明而非 typedef
                                 
    std::string funcTypeName;       // fnc name -> func_type 中的预定义函数类型名
                                    // 非空时表示此函数"实现"某个已定义的函数类型，
                                    // 函数签名从该类型展开，无需重复声明参数和返回类型

    std::string methodOwner;        // 方法所属结构名：结构体内实现的成员函数（FuncD）
                                    // 或 fnc obj::m 仅声明形态（FuncTypeD，C 侧实现）
    std::string methodName;         // 方法名（name 为修饰名 owner_method）

    bool macroObject = false;       // MacroD: true=对象宏 def NAME: = value（无参）；false=函数宏
    ExprPtr expr;                   // MacroD(对象宏): 值表达式；MixD: mix 调用（Expr::Call）

    std::vector<std::string> macroTypeParams;  // MacroD: <T,...> 类型参数名（泛型宏/单态化模板）。
                                    // 非空 → 语言级泛型宏：每个 mix 实例克隆宏体、替换类型参数、
                                    // 重新解析为具体声明并参与语义检查（而非文本 #define）。
                                    // 普通文本宏此列表为空（保持 #define 行为）。
    bool macroConsumed = false;     // MixD: 该 mix 已被泛型单态化消费（已生成具体声明），codegen 不再输出。

    bool genericInst = false;       // 泛型单态化产物（StructD/FuncD/EnumD/AliasD/...）：
                                    // 跨模块编译时其类型定义聚合进工程级 generic.h（跨单元一致、
                                    // 去重），使模块导出签名引用实例类型时可见。普通声明为 false。
    std::string genericKey;         // 所属泛型实例键（如 "Vec<i4,int>"），跨单元去重用。

    // GPU/着色器扩展（syntax-g）：FuncD 的着色阶段标记。仅在 .sg 源（shader 模式）
    // 解析时由 parser 置位；None 即普通 sc 函数。shader 专属语义/代码生成在
    // shader_sema.* / codegen_glsl.*。
    ShaderStage shaderStage = ShaderStage::None;

    //---------

    StructCommon structCommon;

    int line = 0;                   // 声明首行行号
};

// ---------- 程序根节点 ----------
// 语法分析的最终产出。一个完整的 sc 程序 = 一组有序的顶层声明。
// 所有后端（codegen_c / codegen_sc / ast_json）都从 Program 结构开始遍历。
struct Program {
    std::vector<DeclPtr> decls;             // 顶层声明列表，按源码中的书写顺序排列
    std::vector<std::string> externSymbols; // 当前单元引用到的外部符号（模块/头文件导入后汇总）
    std::vector<std::string> futureIds;     // future<ID>() 构造点收集的 ID（去重、首见序）；
                                            // 非空时由解析器合成 future_id 枚举插入 decls 首部，转译工程写出 type.h。
    bool isRoot = false;                    // 头部含 @@ 标记：本单元为显式根模块（全局前奏提供者），
                                            // 其 @导出 默认注入到所有依赖单元，供编译期对接语法插件静态发现。
};

// ---------- 诊断信息 ----------
// 非致命的提示/警告（区别于 CompileError 的致命错误）。
// 目前用于外部描述符分析：导入但未被引用的模块等。供 --ast JSON 携带给插件展示。
struct Diagnostic {
    std::string msg;   // 提示文本
    int line = 0;      // 关联源码行（0=无）
};
