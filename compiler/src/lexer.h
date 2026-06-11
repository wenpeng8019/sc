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
    KwVar,      // var  — 定义变量
    KwLet,      // let  — 定义常量
    KwReturn,   // return
    KwIf,       // if
    KwElse,     // else
    KwWhile,    // while
    KwFor,      // for
    KwBreak,    // break
    KwContinue, // continue

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
    Arrow,      // ->    (特殊处理，区别于普通 Op)

    // ---- 运算符（由 lexOp() 最长匹配产生）----
    Op,         // 其余运算符，具体拼写保存在 Token::text 中
                // 包括: + - * / % < > = ! & | ^ ~ ? .
                //       == != <= >= && || ++ --
                //       += -= *= /= %= &= |= ^= <<= >>=
};

// Token 结构体 —— 词法分析的最小输出单元
struct Token {
    Tok kind;           // token 种类
    std::string text;   // 源码文本（标识符名/字面量值/运算符拼写/关键字拼写）
    int line = 0;       // 所在行号（1-based），用于编译错误定位
};

// 对外接口：输入源码字符串，输出 token 序列
// 内部实现见 lexer.cpp，采用手写状态机，一次遍历完成
std::vector<Token> lex(const std::string& src);
