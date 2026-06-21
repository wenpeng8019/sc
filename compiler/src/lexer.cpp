// ============================================================
// 词法分析器 —— 源码字符串 → Token 序列
// ============================================================
// 核心功能：
//   1. 缩进处理（类 Python 的 INDENT/DEDENT 机制）
//   2. 关键字识别（哈希表查表，O(1)）
//   3. 字面量扫描（字符串、字符、整数、浮点、十六进制）
//   4. 运算符最长匹配（识别 >>= 而非单独的 > 和 >=）
//   5. 注释跳过（# 到行尾）
//   6. 括号内缩进抑制（parenDepth > 0 时不产生 Indent/Dedent）
//
// 缩进算法：
//   用 indents 栈记录每级缩进级别（每 4 空格 = 1 级，禁止 Tab）。
//   每遇到非空非注释行：
//     - 缩进 > 栈顶 → 推入新级别，产生 Indent
//     - 缩进 < 栈顶 → 连续弹出并产生 Dedent
//     - 缩进只能逐级增加（不允许跳级）
// ============================================================
#include "lexer.h"
#include "error.h"
#include <cctype>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace {

// 关键字映射表 —— 标识符扫描后查此表判断是否为关键字
const std::unordered_map<std::string, Tok> kKeywords = {
    {"def", Tok::KwDef},   {"fnc", Tok::KwFnc},
    {"rpc", Tok::KwRpc},
    {"var", Tok::KwVar},   {"let", Tok::KwLet},
    {"tls", Tok::KwTls},
    {"inc", Tok::KwInc},
    {"add", Tok::KwAdd},
    {"return", Tok::KwReturn}, {"if", Tok::KwIf},
    {"else", Tok::KwElse}, {"while", Tok::KwWhile},
    {"do", Tok::KwDo},
    {"for", Tok::KwFor},   {"case", Tok::KwCase},
    {"through", Tok::KwThrough}, {"break", Tok::KwBreak},
    {"goto", Tok::KwGoto},
    {"continue", Tok::KwContinue},
    {"run", Tok::KwRun},
    {"async", Tok::KwAsync},
    {"await", Tok::KwAwait},
    {"done", Tok::KwDone},
    {"final", Tok::KwFinal},
    {"print", Tok::KwPrint},
    {"sizeof", Tok::KwSizeof},
    {"offsetof", Tok::KwOffsetof},
};

// 校验字面量后缀是否为合法组合（限制为 C 标准后缀 + b/w 扩展）。
//   整数：可选无符号 u/U，可选大小 l/L/ll/LL/b/B/w/W（顺序不限，二者可组合）
//         b/B → 单字节（i1/u1），w/W → 双字节（i2/u2），l/ll → long/long long
//   浮点：可选单个 f/F（float）或 l/L（long double）
static bool validLiteralSuffix(const std::string& sfx, bool isFloat) {
    std::string low;
    for (char c : sfx) low += (char)std::tolower((unsigned char)c);
    if (isFloat) return low == "f" || low == "l";
    // 整数：无符号 u 可置于大小后缀之前或之后；大小后缀 l/ll/b/w 至多一种。
    static const std::unordered_set<std::string> kIntSuffix = {
        "", "u",
        "l",  "ul",  "lu",
        "ll", "ull", "llu",
        "b",  "ub",  "bu",
        "w",  "uw",  "wu",
    };
    return kIntSuffix.count(low) != 0;
}

// Lexer 内部类 —— 封装所有词法分析状态
struct Lexer {
    const std::string& s;         // 源码引用（不拷贝）
    std::vector<Token> out;       // 输出 token 序列

    size_t i = 0;                 // 当前扫描位置
    int line = 1;                 // 当前行号（1-based）

    int parenDepth = 0;           // 括号嵌套深度，>0 时抑制缩进/换行处理
    std::vector<int> indents{0};  // 缩进级别栈，0=文件顶层

    explicit Lexer(const std::string& src) : s(src) {}

    char peek(size_t off = 0) const { return i + off < s.size() ? s[i + off] : '\0'; }
    char get() { return i < s.size() ? s[i++] : '\0'; }
    void push(Tok k, std::string t = "") {
        out.push_back({k, std::move(t), line, pendingSpace});
        pendingSpace = false;
    }
    bool pendingSpace = false;    // 自上个 token 以来是否跳过空白（供 spaceBefore 使用）

    [[noreturn]] void err(const std::string& m) { throw CompileError{m, line}; }

    // 行首缩进计算 —— 产生 Indent/Dedent 的核心逻辑
    void handleIndent() {
        for (;;) {
            size_t start = i; int spaces = 0;

            // 严格缩进规则：仅允许空格，且必须为4的倍数
            while (peek() == '\t' || peek() == ' ') {
                char c = get();
                if (c == '\t') err("缩进不允许使用 Tab，请使用 4 个空格");
                spaces++;
            }
            if (spaces % 4 != 0) err("缩进必须为 4 的倍数空格");
            int level = spaces / 4;

            // 跳过注释行和空行（它们不改变缩进级别）
            if (peek() == '#') { while (peek() && peek() != '\n') get(); }
            if (peek() == '\n') { get(); line++; continue; }
            if (peek() == '\0') { i = start; return; }          // 文件尾纯空白行忽略

            applyLevel(level);
            return;
        }
    }

    // 比较当前缩进与栈顶，产生 Indent/Dedent/报错
    void applyLevel(int level) {
        if (level > indents.back()) {
            if (level != indents.back() + 1) err("缩进只能逐级增加");
            indents.push_back(level);
            push(Tok::Indent);
        } else {
            while (level < indents.back()) { indents.pop_back(); push(Tok::Dedent); }
            if (level != indents.back()) err("缩进级别不匹配");
        }
    }

    // 扫描字符串/字符字面量："..." 或 '...'
    // + 支持反斜杠转义（\" \' \\ \n \t 等），会原样保留到 token 文本中
    void lexString(char quote) {
        std::string v(1, quote);

        get();                                          // 跳过开始的引号
        while (peek() && peek() != quote) {
            if (peek() == '\\') v += get();             // 转义符：原样记录反斜杠
            if (peek() == '\n') err("字符串不能跨行");
            v += get();
        }
        if (!peek()) err("字符串未闭合");
        get();                                          // 跳过结束的引号

        v += quote;
        push(quote == '"' ? Tok::Str : Tok::Char, v);
    }

    // 扫描数字字面量：支持十进制整数、浮点数、十六进制整数
    // + 以及 C 风格字面量后缀：整数 u/U/l/L（可组合），浮点 f/F/l/L
    void lexNumber() {
        std::string v; bool isFloat = false;

        // 十六进制前缀 0x / 0X
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            v += get(); v += get();
            if (!isxdigit((unsigned char)peek())) err("0x 后期望十六进制数字");
            while (isxdigit((unsigned char)peek())) v += get();
        } else {
            // 十进制整数部分
            while (isdigit((unsigned char)peek())) v += get();
            // 小数点 + 小数部分 → 浮点数
            if (peek() == '.' && isdigit((unsigned char)peek(1))) {
                isFloat = true;
                v += get();
                while (isdigit((unsigned char)peek())) v += get();
            }
        }

        // 字面量后缀：
        //   整数 u/U + l/L/ll/LL（C 标准），扩展 b/B（单字节 i1/u1）w/W（双字节 i2/u2）
        //   浮点 f/F（float）或 l/L（long double）
        // 贪婪收集所有可能的后缀字母，随后校验是否为合法组合（限制为 C 标准 + b/w 扩展）。
        {
            const char* charset = isFloat ? "fFlL" : "uUlLbBwW";
            std::string sfx;
            while (peek() && std::strchr(charset, peek())) sfx += get();
            if (!sfx.empty() && !validLiteralSuffix(sfx, isFloat))
                err("非法字面量后缀: " + sfx);
            v += sfx;
        }

        push(isFloat ? Tok::Float : Tok::Int, v);
    }

    // 扫描标识符：字母或下划线开头，后跟字母数字下划线
    // + 扫描完后查 kKeywords 表判断是否为关键字
    void lexIdent() {
        std::string v;
        
        // 标识符必须以字母或下划线开头，后续可包含字母数字或下划线
        while (isalnum((unsigned char)peek()) || peek() == '_') v += get();

        // 查关键字表：命中则产生对应关键字 token，否则为普通标识符 token
        auto it = kKeywords.find(v);
        if (it != kKeywords.end()) {                    // 命中关键字
            // inc/add 是上下文关键字：仅在「顶层行首」（缩进级别 0 且为本行第一个
            // + 词法单元，允许前置 @ 导出符）才作为引入/添加指令；其余位置（嵌套作用域、
            //   或作为 rpc/fnc 名等）退化为普通标识符，避免与 add 等常见命名冲突
            if (it->second == Tok::KwInc || it->second == Tok::KwAdd) {
                const bool atTop = indents.back() == 0;
                const bool lineStart = out.empty()
                    || out.back().kind == Tok::Newline
                    || out.back().kind == Tok::Dedent
                    || (out.back().kind == Tok::Op && out.back().text == "@");
                if (!(atTop && lineStart)) {
                    push(Tok::Ident, v);
                    return;
                }
            }
            push(it->second, v);

            // inc/add 特殊处理：头文件/实现文件名（如 stdio.h、"my.h"、impl.c）
            // + 不是常规 token，直接捕获其后到行尾/注释前的原始文本作为一个 Str token
            if (it->second == Tok::KwInc || it->second == Tok::KwAdd) {
                while (peek() == ' ' || peek() == '\t') get();
                std::string h;
                while (peek() && peek() != '\n' && peek() != '#') h += get();
                while (!h.empty() && (h.back() == ' ' || 
                                      h.back() == '\t' ||
                                      h.back() == '\r')) h.pop_back();
                if (h.empty())
                    err(it->second == Tok::KwInc ? "inc 后期望头文件名"
                                                 : "add 后期望实现/库文件名");
                push(Tok::Str, h);
            }
        }
        else push(Tok::Ident, v);                         // 普通标识符
    }

    // 多字符运算符识别 —— 最长匹配原则（按长度从长到短尝试）
    // + 先试3字符运算符（<<= >>=）; 再试2字符，最后单字符
    //   → 单独处理为 Arrow token，不在 Op 中
    bool lexOp() {
        
        static const char* ops3[] = {"<<=", ">>="};                 // 三字符运算符（优先级最高）
        static const char* ops2[] = {"->", "==", "!=", "<=", ">=",  // 二字符运算符（按优先级排列，先匹配的优先）
                                     "&&", "||", "++", "--",
                                     "+=", "-=", "*=", "/=", "%=", 
                                     "&=", "|=", "^=", "<<", ">>"};
        // 尝试三字符匹配
        for (auto* op : ops3)
            if (s.compare(i, 3, op) == 0) { push(Tok::Op, op); i += 3; return true; }

        // '...' 可变参数
        if (s.compare(i, 3, "...") == 0) { push(Tok::Ellipsis, "..."); i += 3; return true; }

        // 尝试二字符匹配
        for (auto* op : ops2) {
            if (s.compare(i, 2, op) == 0) {
                if (std::string(op) == "->") push(Tok::Arrow, op);  // -> 特殊处理
                else push(Tok::Op, op);
                i += 2;
                return true;
            }
        }

        // 单字符运算符（@ 为顶层声明的导出前缀）
        static const std::string single = "+-*/%<>=!&|^~.?@";
        if (single.find(peek()) != std::string::npos) {
            push(Tok::Op, std::string(1, get()));
            return true;
        }

        return false;  // 不是运算符，让调用者继续尝试其他 token 类型
    }

    // 主扫描循环 —— 按当前字符分派到对应的扫描函数
    void run() {
        handleIndent();                                             // 先处理第一行的缩进
        while (i < s.size()) { char c = peek();
            
            // ---- 换行：括号内仍产生 Newline 但抑制 Indent/Dedent ----
            if (c == '\n') {
                get(); line++;
                push(Tok::Newline);
                if (parenDepth == 0) handleIndent();  // 仅在非括号内处理缩进
                continue;
            }

            // ---- 空白字符：跳过 ----
            if (c == ' ' || c == '\t' || c == '\r') { get(); pendingSpace = true; continue; }

            // ---- 注释：# 到行尾 ----
            if (c == '#') { while (peek() && peek() != '\n') get(); pendingSpace = true; continue; }

            // ---- 字面量 ----
            if (c == '"' || c == '\'') { lexString(c); continue; }
            if (isdigit((unsigned char)c)) { lexNumber(); continue; }
            if (isalpha((unsigned char)c) || c == '_') { lexIdent(); continue; }

            // ---- ret 调用语法糖的结果变量 $（独立标识符 token）----
            if (c == '$') { get(); push(Tok::Ident, "$"); continue; }

            // ---- 分隔符（括号需追踪深度）----
            switch (c) {
                case '(': get(); parenDepth++; push(Tok::LParen, "("); continue;
                case ')': get(); if (parenDepth) parenDepth--; push(Tok::RParen, ")"); continue;

                case '{': get(); push(Tok::LBrace, "{"); continue;
                case '}': get(); push(Tok::RBrace, "}"); continue;
                case '[': get(); push(Tok::LBracket, "["); continue;
                case ']': get(); push(Tok::RBracket, "]"); continue;

                case ',': get(); push(Tok::Comma, ","); continue;
                case ';': get(); push(Tok::Semi, ";"); continue;
                case ':':
                    if (peek(1) == ':') { get(); get(); push(Tok::DColon, "::"); }
                    else { get(); push(Tok::Colon, ":"); }
                    continue;
            }

            // ---- 运算符（最长匹配）----
            if (lexOp()) continue;

            // ---- 无法识别 ----
            err(std::string("无法识别的字符: '") + c + "'");
        }

        // 文件尾收尾：确保最后一条语句有换行结束（简化 parser 逻辑）
        if (!out.empty() && out.back().kind != Tok::Newline) push(Tok::Newline);

        // 弹出所有剩余缩进级别（自动闭合未结束的块）
        while (indents.size() > 1) { indents.pop_back(); push(Tok::Dedent); }

        // ---- 文件结束标记 ----
        push(Tok::End);  
    }
};

} // namespace

// 对外接口：创建 Lexer 实例并执行扫描
std::vector<Token> lex(const std::string& src) {
    Lexer lx(src);
    lx.run();
    return std::move(lx.out);
}
