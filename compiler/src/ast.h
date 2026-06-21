#pragma once
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

    std::string op;             // 针对目标项的运算和操作（Cast 时为目标类型名）
    int castPtr = 0;            // Cast: 目标类型的指针层数
    bool castIsFmt = false;     // Cast: op 为 printf 格式串字面量（含引号）而非类型名
                                // —— print 实参的格式覆盖 (expr: "%.5d")，仅 print 语境有意义

    // stringify<key:val,...> 选项块（仅 Call 且 callee 为 stringify 关键字时有效）；
    // + 值限整数字面量（如 compact:1），codegen 据此生成 (stringify_t){...} 复合字面量
    std::vector<std::pair<std::string, long long>> sofOpts;

    // future<ID>() 构造标记（仅 Call 且 callee 为 future 伪构造时有效）；
    // + ID 为 future_id 枚举常量名，编译期聚合成 future_id 枚举（type.h），
    //   codegen 生成 future__new_tagged(ID) 为该 future 设 id，供 async_loop 按 id 派发。
    std::string futureId;

    // T<atom>() 自动指针原子构造标记（仅 Call 且 callee 为类型名的胖目标构造时有效）；
    // + 置位 → T__new_ref 把对象头标记为原子，sc_fat_bind/unbind 对该对象 in/out 用原子 RMW。
    bool ctorAtom = false;

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
        PrintS,     // print 日志输出   print[<chn>] arg, arg, ...（括号可省）
                    //                > printChn=通道 u1 的 C 表达式文本（默认 "0"），透传给 C print
                    //                > printCompat=true 时为括号兼容模式（C printf 语法，实参原样传递）
                    //                  false 时为拼接糖：字符串字面量=纯文本；其余=按静态类型自动
                    //                  补说明符的变量；Cast(castIsFmt)=显式格式覆盖 (expr: "%fmt")
    } kind;

    ExprPtr expr;                       // ExprS: 表达式的值
                                        // ReturnS: 返回的表达式（空=无返回值 return;）
                                        // IfS/WhileS: 条件表达式（必须可求值为布尔）

    std::vector<Field> decls;           // LetS/VarS/TlsS: 声明的常量/变量/线程局部变量列表
                                        // + 单行多变量以逗号分隔，多行以缩进续行

    std::vector<StmtPtr> body;          // IfS/WhileS/ForS: 条件成立时执行的主体语句
    std::vector<StmtPtr> elseBody;      // IfS: else 分支（可能为空、单条 else if、或多条语句块）

    ExprPtr forInit, forCond, forStep;  // ForS: for (init; cond; step) 三段表达式

    std::vector<std::pair<std::string, long long>> runOpts;  // RunS: run<stack:N, prio:M> 线程属性（键:整数值），透传给 C

    std::string printChn;               // PrintS: <chn> 通道的 C 表达式文本（默认 "0"）
    std::vector<ExprPtr> printArgs;     // PrintS: 拼接实参列表（顺序）
    bool printCompat = false;           // PrintS: 括号形式 print(...) → C printf 兼容模式（实参原样传递）

    struct CaseArm {
        std::vector<ExprPtr> labels;    // 空=default 分支
        std::vector<StmtPtr> body;      // 分支体
        bool through = false;           // 末尾 through：贯穿到下一分支
        int line = 0;
    };
    std::vector<CaseArm> caseArms;      // CaseS: 分支列表（labels 为空表示 default）

    std::string text;                   // GotoS/LabelS: 标签名
    
    DeclPtr decl;                       // DeclS:（函数内部）内嵌的类型定义（def）

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
    } kind;

    std::string name;               // 类型名 / 函数名；IncD 时为头文件文本

    std::string origin;             // 外部符号来源（导入文件路径或 builtin 名称）
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
    bool linked = false;            // 链表结构体 def T: ~ {}：头部注入 _prev/_next 双向链指针

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
};

// ---------- 诊断信息 ----------
// 非致命的提示/警告（区别于 CompileError 的致命错误）。
// 目前用于外部描述符分析：导入但未被引用的模块等。供 --ast JSON 携带给插件展示。
struct Diagnostic {
    std::string msg;   // 提示文本
    int line = 0;      // 关联源码行（0=无）
};
