#pragma once
#include <memory>
#include <string>
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

// ---------- 表达式 ----------
// 表达式树是 AST 中最细粒度的节点。所有运算、字面量、调用都是表达式。
struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    enum Kind {
        // 叶子节点 —— 语法分析遇到终结符时直接创建
        IntLit,     // 整数字面量   eg. 42, 0xFF
        FloatLit,   // 浮点字面量   eg. 3.14
        StrLit,     // 字符串字面量 eg. "hello"
        CharLit,    // 字符字面量   eg. 'a'
        Ident,      // 标识符       eg. varName

        // 一元运算 —— parseUnary() 解析前缀运算符
        Unary,      // 前缀运算     eg. -x, !x, ~x, *p, &x
        PostUnary,  // 后缀运算     eg. i++, i--  (a 持有操作数, op 为 "++"/"--")

        // 二元运算 —— parseBinary() 解析，按优先级递归
        Binary,     // 二元运算     eg. a+b, a>b, a=b, a+=b
                    // op 存储运算符文本，a/b 分别为左右操作数

        // 三元运算 —— parseTernary() 解析
        Ternary,    // 三元条件     eg. a ? b : c  (a=条件, b=真值, c=假值)

        // 后缀链 —— parsePostfix() 循环解析，可连续组合
        Call,       // 函数调用     eg. foo(a, b)  (a=被调函数, args=实参列表)
        Index,      // 下标访问     eg. arr[0]     (a=数组, b=下标)
        Member,     // 成员访问     eg. obj.f  或 ptr->f
                    //              op="." 或 "->", text=成员名
    } kind;

    std::string text;       // 字面量值 / 标识符名（Ident时）/ 成员名（Member时）
    std::string op;         // 运算符拼写（Unary/PostUnary/Binary时）
                            // Member场景：op="." 或 "->"
    ExprPtr a, b, c;        // 子表达式指针
                            // Unary/PostUnary: a=操作数
                            // Binary: a=左操作数, b=右操作数
                            // Ternary: a=条件, b=真值分支, c=假值分支
                            // Call/Index/Member: a=被操作对象
    std::vector<ExprPtr> args;  // 函数调用的实参列表（Call 时有效）
    int line = 0;               // 表达式起始行号（用于错误报告）
};

// ---------- 类型引用 ----------
// TypeRef 统一表示 sc 中所有可能的类型写法。
// 与 C 不同，sc 的指针声明在名字一侧（name& 而非 int* name）。
// 解析时 & 被计入 ptr 字段，最终由 codegen 还原为 C 的 T* 形式。
struct Field;  // 前向声明，TypeRef 内嵌的字段列表需用到 Field

struct TypeRef {
    // 类型名："i4" "f8" "u1" 等内置类型，或用户 def 的自定义类型名
    // 空字符串表示未指定类型，由 codegen 按默认规则推断：
    //   ptr > 0 → void*    （无类型指针默认指向 void）
    //   ptr = 0 → char*    （无类型对象默认字符串/字节缓冲区）
    std::string name;

    int ptr = 0;             // 指针层数（源码中 name 后面 & 的个数）
    bool isArray = false;    // 是否为数组类型
    std::string arraySize;   // 数组长度表达式（空 = 未指定长度）

    // 内联结构/联合 —— 类型直接写在变量声明处，无需预先 def
    //   var obj: {x:i4, y:i4}  → hasInline=true, inlineUnion=false
    //   var tag: (i:i4, f:f4)  → hasInline=true, inlineUnion=true
    bool hasInline = false;
    bool inlineUnion = false; // true=(联合), false={结构}
    std::vector<Field> inlineFields;  // 内联类型的字段定义
};

// ---------- 字段 / 声明项 ----------
// Field 是一个多用途结构体，在不同 Decl 上下文中含义不同：
//   struct/union 成员 → name + type
//   函数参数          → name + type
//   var/let 变量项    → name + type + init（可选的初值表达式）
//   枚举项            → name + init（可选的枚举常量值）
//
// name 为空时表示匿名结构/联合成员（直接内嵌另一个 {}/() 类型）。
struct Field {
    std::string name;        // 字段/参数/变量/枚举项名称；空 = 匿名嵌入
    TypeRef type;            // 类型信息（含指针层数、数组标记、内联结构等）
    ExprPtr init;            // 初值表达式（var/let 声明和枚举项使用）
    int line = 0;            // 声明所在行号
};

// ---------- 语句 ----------
// 语句是函数体的基本单位。一条语句通常占一行（或一个缩进块）。
struct Decl;   // 前向声明，Stmt 中的 DeclS 需要引用 Decl
using DeclPtr = std::unique_ptr<Decl>;
struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    enum Kind {
        ExprS,      // 表达式语句      eg. foo(); a = 1;  (以 expr 字段存储)
        VarS,       // 变量声明语句    eg. var x: i4      (以 decls 字段存储多项)
        LetS,       // 常量声明语句    eg. let MAX = 100
        ReturnS,    // return 语句     eg. return expr;   (expr 可为空 = return;)
        IfS,        // if/else 条件分支
        WhileS,     // while 循环
        ForS,       // for 循环        for init; cond; step \n body
        BreakS,     // break 语句      (无附加数据)
        ContinueS,  // continue 语句   (无附加数据)
        DeclS,      // 内嵌类型声明    函数体内用 def 定义局部类型（不常见但允许）
    } kind;

    ExprPtr expr;                      // ExprS: 表达式的值
                                       // ReturnS: 返回的表达式（空=无返回值 return;）
                                       // IfS/WhileS: 条件表达式（必须可求值为布尔）
    std::vector<Field> decls;          // VarS/LetS: 声明的变量/常量列表
                                       // 单行多变量以逗号分隔，多行以缩进续行
    std::vector<StmtPtr> body;         // IfS/WhileS/ForS: 条件成立时执行的主体语句
    std::vector<StmtPtr> elseBody;     // IfS: else 分支（可能为空、单条 else if、或多条语句块）
    ExprPtr forInit, forCond, forStep; // ForS: for (init; cond; step) 三段表达式
    DeclPtr decl;                      // DeclS: 内嵌的类型定义（def）
    int line = 0;                      // 语句起始行号
};

// ---------- 程序结构对象（顶层声明） ----------
// sc 语言的核心概念："程序即结构树"。
// 程序由四种结构对象组成：def（类型）、fnc（函数）、var（变量）、let（常量）。
// 所有顶层元素在 AST 中都是 Decl，通过 kind 字段区分。
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

        // -- var/let 全局变量/常量 --
        VarD,       // 全局变量   var name:type [= init]
        LetD,       // 全局常量   let name:type = init
    } kind;

    std::string name;            // 类型名 / 函数名

    TypeRef type;                // AliasD: 别名指向的目标类型
                                 // EnumD:  枚举的底层整数基类型

    std::vector<Field> fields;   // 多用途：结构/联合的成员字段
                                 //        枚举的项列表（name+可选的=value）
                                 //        函数的形参列表
                                 //        var/let 的声明变量列表

    TypeRef retType;             // FuncTypeD/FuncD: 函数的返回类型声明

    std::string funcTypeName;    // fnc name -> func_type 中的预定义函数类型名
                                 // 非空时表示此函数"实现"某个已定义的函数类型，
                                 // 函数签名从该类型展开，无需重复声明参数和返回类型

    std::vector<StmtPtr> body;   // FuncD: 函数体的语句列表
                                 // （FuncTypeD 的 body 为空，只有签名无实现）

    int line = 0;                // 声明首行行号
};

// ---------- 程序根节点 ----------
// 语法分析的最终产出。一个完整的 sc 程序 = 一组有序的顶层声明。
// 所有后端（codegen_c / codegen_sc / ast_json）都从 Program 结构开始遍历。
struct Program {
    std::vector<DeclPtr> decls;  // 顶层声明列表，按源码中的书写顺序排列
};
