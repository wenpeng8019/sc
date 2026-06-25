#pragma once
#include <string>
#include <vector>

// ============================================================
// 词法分析器 —— 将源码字符串转换为 Token 序列
// ============================================================
// sc 语言采用类 Python 的缩进敏感语法，括号内抑制缩进处理。
//
// 词法分析阶段产生三种特殊 token：
//   Indent   - 缩进级别增加一级（行首空格/Tab 比上一行多时自动插入）
//   Dedent   - 缩进级别减少一级（行首缩进比上一行少时自动插入，可连续产生多个）
//   Newline  - 逻辑换行（语句结束符）
//
// 流程：lex(src) → vector<Token> → parse(tokens) → Program AST
// ============================================================

// Token 类型枚举 —— 所有语法单元（终结符）的种类
enum class Tok {
    // ---- 布局 token（词法阶段自动插入，源码中不可见）----
    End,        // 文件结束标记
    Newline,    // 换行符（语句分隔符），括号内仍产生但被 parser 跳过
    Indent,     // 缩进开始（行首缩进比上一级多一级时自动插入）
    Dedent,     // 缩进结束（行首缩进比上一级少时自动插入，可能连续多个）

    // ---- 字面量 ----
    Ident,      // 标识符    [A-Za-z_][A-Za-z0-9_]*
    Int,        // 整数字面量 eg. 42, 0xFF
    Float,      // 浮点字面量 eg. 3.14
    Str,        // 字符串     "hello\n"
    Char,       // 字符       'a'

    // ---- 关键字 ----
    KwDef,      // def  — 定义类型
    KwFnc,      // fnc  — 定义函数
    KwRpc,      // rpc  — 定义伪形参函数（参数/返回值展开为同名结构体）
    KwVar,      // var  — 定义变量
    KwLet,      // let  — 定义常量
    KwTls,      // tls  — 定义线程局部变量（static 存储期，每线程独立实例）
    KwInc,      // inc  — 引入头文件/模块（对齐 C 的 #include，后跟行尾文本）
    KwAdd,      // add  — 添加实现/库文件到工程（.c/.o/.a/.so...，后跟行尾文本）
    KwMix,      // mix  — 展开宏（def 宏的调用点；顶层展开声明 / 函数体展开语句）
    KwReturn,   // return
    KwIf,       // if
    KwElse,     // else
    KwWhile,    // while
    KwDo,       // do
    KwFor,      // for
    KwCase,     // case
    KwThrough,  // through
    KwGoto,     // goto
    KwBreak,    // break
    KwContinue, // continue
    KwRun,      // run  — 以 rpc 调用创建线程（多线程语言特性，依赖 m 模块）
    KwSync,     // sync — 同步驱动 rpc 流程：当前线程直接执行(无目标)/投递队列阻塞等回复(有目标)，返回结果
    KwAsync,    // async — 把 rpc 调用登记进当前线程事件循环，返回 future（异步特性，依赖 async 模块）
    KwAwait,    // await — 挂起当前 rpc，等待 future 就绪后恢复（异步特性，依赖 async 模块）
    KwDone,     // done — 标记 future 就绪并唤醒等待者：done future[, result]（异步特性）
    KwFinal,    // final — 域退出钩子：本作用域每个退出点运行其块（先于自动胖边清理）
    KwPrint,    // print — 日志输出关键字（拼接糖 + <chn> 通道，依赖 io 模块）
    KwTst,      // tst  — 定义单元测试用例块（tst "名字"，--test 模式收集运行）
    KwAssert,   // assert — 测试断言（assert 表达式[, "消息"]，失败记录并中止当前用例）
    KwSizeof,   // sizeof
    KwOffsetof, // offsetof
    KwCls,      // cls  — 定义类（复用 def 结构体机制 + 注入分派器 _class）
    KwDim,      // dim  — 类维度（折叠进唯一分派器的 switch 分支，返回恒 tril）

    // ---- 特殊 ----
    Ellipsis,   // ...  可变参数占位

    // ---- 分隔符 ----
    LParen,     // (
    RParen,     // )
    LBrace,     // {
    RBrace,     // }
    LBracket,   // [
    RBracket,   // ]
    Comma,      // ,
    Semi,       // ;
    Colon,      // :
    DColon,     // ::
    Arrow,      // ->    (特殊处理，区别于普通 Op)
    Backslash,  // \     宏体内：token 粘贴（a\b → C a##b）
    Backtick,   // `name` 宏体内：参数串化（`name` → C #name）；text 存内部标识符

    // ---- 运算符（由 lexOp() 最长匹配产生）----
    Op,         // 其余运算符，具体拼写保存在 Token::text 中
                // 包括: + - * / % < > = ! & | ^ ~ ? . @
                //       == != <= >= && || ++ --
                //       += -= *= /= %= &= |= ^= <<= >>=
                // @ 仅用于顶层声明前缀（导出标记）
};

// Token 结构体 —— 词法分析的最小输出单元
struct Token {
    Tok kind;           // token 种类
    std::string text;   // 源码文本（标识符名/字面量值/运算符拼写/关键字拼写）
    int line = 0;       // 所在行号（1-based），用于编译错误定位
    bool spaceBefore = false;  // 该 token 前是否存在空白（用于 ret 调用语法糖的空格消歧）
};

// 对外接口：输入源码字符串，输出 token 序列
// 内部实现见 lexer.cpp，采用手写状态机，一次遍历完成
std::vector<Token> lex(const std::string& src);
