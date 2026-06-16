// ============================================================
// 语法分析器 —— Token 序列 → Program AST 树
// ============================================================
// 采用递归下降（Recursive Descent）算法，每个非终结符对应一个解析函数。
//
// 表达式解析（Pratt parsing 风格，通过优先级驱动）：
//   parsePrimary()  → 原子：字面量、标识符、(表达式)
//   parsePostfix()  → 后缀链：调用() 下标[] 成员. -> 后缀++ --
//   parseUnary()    → 前缀一元：! ~ - + * & ++ --
//   parseBinary()   → 二元运算：按优先级递归（左结合）
//   parseTernary()  → 三元条件：? :
//   parseExpr()     → 赋值：= += -= 等（右结合）
//
// 声明解析：
//   parseDef()   → def 类型定义（enum/struct/union/alias）
//   parseFnc()   → fnc 函数定义（FuncTypeD 或 FuncD）
//   parseStmt()  → 单条语句分发
//
// 缩进处理：parser 消费 lexer 产生的 Indent/Dedent/Newline，
//   expect/accept 分别处理必须/可选的缩进 token。
//   exprBracket > 0 时用 skipLayout()/skipNlInBracket() 忽略括号内的布局 token。
// ============================================================
#include "parser.h"
#include "error.h"
#include <cstdlib>
#include <unordered_map>

namespace {

static bool isScModuleName(const std::string& s) {
    return s.size() >= 3 && s.substr(s.size() - 3) == ".sc";
}

// 二元运算符优先级表 —— 数字越大优先级越高
// + 返回 -1 表示不是已知的二元运算符
int binPrec(const std::string& op) {
    static const std::unordered_map<std::string, int> m = {
        {"||", 1}, {"&&", 2},                       // 逻辑
        {"|", 3}, {"^", 4}, {"&", 5},               // 位运算
        {"==", 6}, {"!=", 6},                       // 相等
        {"<", 7}, {">", 7}, {"<=", 7}, {">=", 7},   // 比较
        {"<<", 8}, {">>", 8},                       // 移位
        {"+", 9}, {"-", 9},                         // 加减
        {"*", 10}, {"/", 10}, {"%", 10},            // 乘除取模
    };
    auto it = m.find(op);
    return it == m.end() ? -1 : it->second;
}

// 判断是否为赋值类运算符（在 parseExpr 中与普通二元运算分开处理，右结合）
bool isAssignOp(const std::string& op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
           op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
           op == "<<=" || op == ">>=";
}

// Parser 内部类 —— 封装所有语法分析状态
struct Parser {
    const std::vector<Token>& t;                    // 输入 token 序列（引用外部持有）

    size_t p = 0;                                   // 当前读位置（指向下一个待消费的 token）
    int exprBracket = 0;                            // 表达式括号深度：>0 时括号内的换行/缩进 token 被 skip 跳过

    // 活动三目运算的括号深度栈：其分支内（同括号层）禁用裸强转，
    // + 保证 '?' 之后有且只有一个 ':' 归三目；强转需加括号
    std::vector<int> ternDepth;
    
    // 结构体内成员函数：parseDef 期间收集，parseProgram 提升为顶层 Decl
    std::vector<DeclPtr> pendingMethods;

    explicit Parser(const std::vector<Token>& toks) : t(toks) {}

    [[noreturn]] void err(const std::string& m) const { throw CompileError{m, cur().line}; }

    void expect(Tok k, const char* what) {
        if (!accept(k)) err(std::string("期望 ") + what + "，得到 '" + cur().text + "'");
    }

    // ----- Token 流访问原语 --------------------------------------------------

    const Token& cur() const { return t[p]; }
    const Token& peek(size_t off = 1) const { return t[p + off < t.size() ? p + off : t.size() - 1]; }
    bool at(Tok k) const { return cur().kind == k; }
    bool atOp(const char* s) const { return cur().kind == Tok::Op && cur().text == s; }
    const Token& advance() { return t[p < t.size() - 1 ? p++ : p]; }

    // accept: 若当前 token 匹配则消费并返回 true，否则不消费
    bool accept(Tok k) { if (at(k)) { advance(); return true; } return false; }
    bool acceptOp(const char* s) { if (atOp(s)) { advance(); return true; } return false; }

    // 跳过连续的换（空）行
    void skipNewlines() { while (accept(Tok::Newline)) {} }
    // 跳过连续换行和缩进
    void skipLayout() { while (at(Tok::Newline) || at(Tok::Indent) || at(Tok::Dedent)) advance(); }

    // ------------------------------------------------------------------------

    static std::string mangleMethodName(const std::string& owner, const std::string& name) {
        return owner + "_" + name;
    }

    ///////////////////////////////////////////////////////////////////////////
    // 程序（定义/声明中的）实际访问和操作的目标数据对象
    //
    // sc 的类型语法有独特之处：
    //   1. 指针标记 & 写在类型后（name: type& 而非 int* name）
    //   2. 数组标记 [size] 写在名字后
    //   3. 支持内联结构/联合（直接在变量声明处定义 {}/() 类型）
    //   4. 未指定类型时由代码生成阶段推断默认类型

    // 解析名字后的元类型后缀：[size]...（多维数组）
    // 注意：指针 & 已统一移到类型侧（如 name: type&），名字侧不再接受 &
    void parseMeta(TypeRef& ty) {

        // 多维数组：name[x][y] 与 C 对齐，每个 [维度] 追加一维
        while (accept(Tok::LBracket)) {
            std::string dim;
            if (!at(Tok::RBracket)) {
                if (!at(Tok::Int) && !at(Tok::Ident)) err("期望数组大小");
                dim = advance().text;  // 维度大小（数字或常量标识符）
            }
            expect(Tok::RBracket, "']'");
            ty.arrayDims.push_back(std::move(dim));
        }
    }

    // 判断冒号后是否开始一个类型：命名类型、内联 {}/()、或裸 &/&&（void* 指针）
    bool atTypeStart() const {
        return at(Tok::Ident) || at(Tok::LBrace) || at(Tok::LParen)
            || (at(Tok::Op) && (cur().text == "&" || cur().text == "&&"));
    }

    // 解析类型引用：可以是命名类型 type&&、内联 {struct}/(union)、或裸 &/&&（void*）
    TypeRef parseTypeRef() { TypeRef ty;
        
        // 内联结构/联合：直接以 { 或 ( 开头
        if (at(Tok::LBrace) || at(Tok::LParen)) {
            ty.hasInline = true;
            ty.inlineUnion = at(Tok::LParen);  // '(' → union, '{' → struct
            parseFieldBlock(ty.structCommon.fields);   // 递归解析字段列表
            return ty;
        }

        // 获取类型名
        if (at(Tok::Ident)) ty.name = advance().text;
        // 对于（无名）裸 & / &&，即 void* 指针
        else if (!(at(Tok::Op) && (cur().text == "&" || cur().text == "&&")))
            err("期望类型名");

        // 类型名后可跟 &/&& 表示指针（如 i4& = int32_t*，i4&& = int32_t**）
        for (;;) {
            if (acceptOp("&")) ty.ptr++;
            else if (acceptOp("&&")) ty.ptr += 2;
            else break;
        }
        return ty;
    }

    // 解析单个字段项：name[meta][: type] 或匿名 {}/()
    Field parseFieldItem() { Field f;

        f.line = cur().line;                    // 须在（发生）报错前记录行号，以便错误信息准确指向字段定义处
        
        // 对于匿名嵌套结构/联合：即直接以 { 或 ( 开头，没有字段名
        if (at(Tok::LBrace) || at(Tok::LParen)) {
            f.type.hasInline = true;
            f.type.inlineUnion = at(Tok::LParen);
            parseFieldBlock(f.type.structCommon.fields);
            return f;
        }

        if (!at(Tok::Ident)) err("期望字段名");  // 当前 token 必须是标识符（变量名）
        f.name = advance().text;                // 消费当前 token（作为变量名）并前进到下一个 token
        parseMeta(f.type);                      // 先解析名字后的 [] 数组元类型
        if (accept(Tok::Colon)) {               // 验证并消费 ':'，冒号后为显式类型（可选：省略时由代码生成推断默认类型，？成员字段可以吗）

            // 如果冒号后是 fnc
            // + 解析为内联函数类型（如 name: fnc[: ret, params...]）
            //   这里分为两种情况：函数指针类型的字段、或成员函数实现。具体由调用者来区分（见 parseFieldBlock）
            if (at(Tok::KwFnc)) {

                advance();                                          // 跳过 fnc 关键字
                TypeRef ty; ty.fnKind = TypeRef::FncKind::PlainPtr; // 创建函数指针类型引用，初始为普通函数指针
                if (accept(Tok::Colon)) {                           // ':' 可省略（无返回值且无参数，如成员函数 init: fnc）
                    parseFncVars(ty.structCommon);                  // 解析函数参数和返回类型（冒号分隔的参数列表，或单一返回类型）
                }
                f.type = std::move(ty);
            }
            // 否则解析为普通类型（如 name: type、name: type&，或裸 & 即 void*）
            else if (atTypeStart()) {
                TypeRef ty = parseTypeRef();                    // 解析冒号后的类型信息（支持内联结构/联合、类型侧指针）
                ty.arrayDims.insert(ty.arrayDims.end(),         // 合并名字侧数组维度
                                    f.type.arrayDims.begin(), 
                                    f.type.arrayDims.end());
                f.type = std::move(ty);
            }
        }
        return f;
    }

    // 解析结构体内部的 fnc 成员函数声明：fnc name[::][: ret, params...]
    // + 用于 def 结构体 {} 内部的函数声明，支持两种形式：
    //   > fnc name:: ret, params... → C 实现接口（无函数体，cImpl=true）
    //   > fnc name: ret, params...  → sc 实现（后续跟缩进函数体时）
    //   > fnc name: ret, params...  → 函数指针字段（无后续缩进体）
    DeclPtr parseMemberFnc() {
        auto d = std::make_unique<Decl>();
        d->line = cur().line;
        advance();                                  // 跳过 fnc 关键字
        if (!at(Tok::Ident)) err("期望成员函数名");
        d->methodName = advance().text;
        if (accept(Tok::DColon)) d->cImpl = true;   // :: → C 实现接口

        // 解析 : ret, params...（:: 后可直接跟签名，或可选的 ':'）
        if (d->cImpl && !at(Tok::Colon) && !at(Tok::Newline))
            parseFncVars(d->structCommon);
        else if (accept(Tok::Colon)) {
            if (!at(Tok::Newline))
                parseFncVars(d->structCommon);
        } else if (!at(Tok::Newline))
            err("期望 ':' 或换行");
        expect(Tok::Newline, "换行");

        // 无缩进 → 函数指针字段 或 C 实现声明
        if (!at(Tok::Newline) || peek().kind != Tok::Indent) {
            d->kind = Decl::FuncTypeD;
            return d;
        }

        // 有缩进函数体 → sc 实现成员函数
        if (d->cImpl) err("C 实现接口（::）不能有函数体");
        d->kind = Decl::FuncD;
        advance(); advance();  // Newline + Indent
        parseStmts(d->body);
        accept(Tok::Dedent);
        return d;
    }

    // 解析字段块：{ fields } 或 ( fields )，字段间以逗号或换行分隔。
    // + methodsOut 非空（顶层 def 结构体）时：
    //   > 函数签名字段后跟缩进块，则是成员函数实现
    //     此时提升为带 methodOwner 的顶层 FuncD（且该 field 不计入字段列表）
    //   > 无函数体则仍是普通函数指针字段 
    void parseFieldBlock(std::vector<Field>& out,
                         std::vector<DeclPtr>* methodsOut = nullptr) {

        Tok close = at(Tok::LBrace) ? Tok::RBrace : Tok::RParen;
        bool inParen = at(Tok::LParen);
        if (inParen) exprBracket++;         // 联合使用 ()，需增加括号计数以抑制内部缩进
        advance();                          // 跳过开始的 { 或 (
        for (;;) {
            skipLayout();                   // 跳过字段间的换行和缩进（逗号分隔时也可有换行/缩进）
            if (at(close)) break;
            if (at(Tok::End)) err("结构/联合未闭合");

            // fnc 关键字 → 成员函数声明（fnc name:: 或 fnc name:）
            if (at(Tok::KwFnc)) {
                auto m = parseMemberFnc();
                if (m->cImpl) {
                    // C 实现成员函数：提升为顶层方法（extern 原型 + 方法调用糖），
                    // 结构体本身不含函数指针字段（与 @fnc T::m 旧形态语义一致，
                    // 保持纯数据布局的 C ABI 契约）
                    if (!methodsOut)
                        err("成员函数只能在顶层 def 结构体内实现");
                    methodsOut->push_back(std::move(m));
                } else if (m->kind == Decl::FuncD) {
                    // sc 实现成员函数（有函数体）→ 提升为顶层方法
                    if (!methodsOut)
                        err("成员函数只能在顶层 def 结构体内实现");
                    methodsOut->push_back(std::move(m));
                } else {
                    // 无 body 非 :: → 普通函数指针字段
                    Field f;
                    f.line = m->line;
                    f.name = m->methodName;
                    f.type.fnKind = TypeRef::FncKind::PlainPtr;
                    f.type.structCommon = std::move(m->structCommon);
                    out.push_back(std::move(f));
                }
            }
            // 否则按普通字段处理
            else {
                Field f = parseFieldItem();                         // 解析单个字段项：name[meta][: type] 或匿名 {}/()
                if (acceptOp("=")) f.init = parseExpr();            // 字段初值（可选）：name: type = expr

                // 对于成员函数：字段为函数签名，且后面是缩进函数体
                if (f.type.fnKind == TypeRef::FncKind::PlainPtr
                     && at(Tok::Newline) && peek().kind == Tok::Indent) {

                    if (!methodsOut)
                        err("成员函数只能在顶层 def 结构体内实现");
                    if (f.init) err("成员函数不能带初值");

                    // 将 field 提升为顶层方法声明
                    auto m = std::make_unique<Decl>();
                    m->kind = Decl::FuncD;
                    m->line = f.line;
                    m->methodName = f.name;
                    m->structCommon = std::move(f.type.structCommon);

                    // 解析函数体
                    advance(); advance();  // Newline + Indent
                    parseStmts(m->body);
                    accept(Tok::Dedent);

                    methodsOut->push_back(std::move(m));
                }
                // 否则按普通字段处理
                else out.push_back(std::move(f));
            }

            skipLayout();
            accept(Tok::Comma);  // 逗号分隔是可选的（也可纯粹以换行分隔）
        }
        advance();  // 跳过结束的 } 或 )
        if (inParen) exprBracket--;
    }

    // 解析 var/let/tls 声明项：与字段类似，但额外支持 = 初值表达式和内联类型
    Field parseVarItem() { Field f;

        f.line = cur().line;                    // 须在（发生）报错前记录行号
        if (!at(Tok::Ident)) err("期望变量名");  // 当前 token 必须是标识符（变量名）
        f.name = advance().text;                // 消费当前 token（作为变量名）并前进到下一个 token
        parseMeta(f.type);                      // 解析名字后的 [] 数组元类型（name[10]: 表示数组；指针写在类型侧 name: type&）
        if (accept(Tok::Colon)) {               // 验证并消费 ':'，冒号后为显式类型（可选：省略时由代码生成推断默认类型）

            // 如果冒号后是 fnc
            // + 则解析为内联函数指针类型（如 name: fnc[: ret, params...]）
            if (at(Tok::KwFnc)) { 
                
                advance();                                          // 跳过 fnc 关键字
                TypeRef ty; ty.fnKind = TypeRef::FncKind::PlainPtr; // 创建函数指针类型引用，初始为普通函数指针                
                if (accept(Tok::Colon)) {                           // ':' 可省略（无返回值且无参数，如函数 func: fnc）
                    parseFncVars(ty.structCommon);                  // 解析函数参数和返回类型（冒号分隔的参数列表，或单一返回类型）
                }
                f.type = std::move(ty);
            }
            // 否则解析为普通类型（如 name: type、name: type&，或裸 & 即 void*）
            else if (atTypeStart()) {
                TypeRef ty = parseTypeRef();                    // 解析冒号后的类型信息（支持内联结构/联合、类型侧指针）
                ty.arrayDims.insert(ty.arrayDims.end(),         // 合并名字侧数组维度
                                    f.type.arrayDims.begin(),
                                    f.type.arrayDims.end());
                f.type = std::move(ty);
            }
        }

        if (acceptOp("=")) f.init = parseExpr();  // 可选的初值表达式
        return f;
    }

    // 解析 var/let/tls 列表：支持单行逗号分隔、和多行缩进续行两种写法
    void parseVarList(std::vector<Field>& out) {
        
        out.push_back(parseVarItem());
        while (accept(Tok::Comma)) out.push_back(parseVarItem());   // 单行：逗号分隔多项
        expect(Tok::Newline, "换行");

        if (accept(Tok::Indent)) {                                  // 多行缩进续行
            while (!at(Tok::Dedent) && !at(Tok::End)) {
                skipNewlines(); if (at(Tok::Dedent)) break;

                out.push_back(parseVarItem());
                while (accept(Tok::Comma)) out.push_back(parseVarItem());
                expect(Tok::Newline, "换行");
            }
            accept(Tok::Dedent);
        }
    }

    ///////////////////////////////////////////////////////////////////////////
    // def 类型定义解析
    //
    // def 支持四种类型定义：
    //   def name -> target_type       → 类型别名
    //   def name: { fields }          → 结构体
    //   def name: ( fields )          → 联合体
    //   def name: base \n\titem...    → 枚举

    DeclPtr parseDef() {

        auto d = std::make_unique<Decl>();
        d->line = cur().line;
        advance();                                      // 跳过 def 关键字
        if (!at(Tok::Ident)) err("期望类型名");           // 当前 token 必须是标识符（类型名）   
        d->name = advance().text;                       // 类型名

        // 1. 对于 -> 箭头语法：类型别名  def name -> target
        if (accept(Tok::Arrow)) {
            d->kind = Decl::AliasD;
            d->structCommon.type = std::make_shared<TypeRef>(parseTypeRef());
            expect(Tok::Newline, "换行");
            return d;
        }
        expect(Tok::Colon, "':'");

        // ~：链表标记（仅结构体）—— 转 C 时在成员前面注入 _prev/_next 自链指针
        bool linked = acceptOp("~");

        // 2. 对于 { ... } 结构体
        if (at(Tok::LBrace)) {

            d->kind = Decl::StructD;                        // 标记为结构体定义
            parseFieldBlock(d->structCommon.fields, &pendingMethods);    // 解析字段块
            for (auto& m : pendingMethods) {                // 处理成员函数：补齐 methodOwner 和 name（mangle 后）
                m->methodOwner = d->name;
                m->name = mangleMethodName(d->name, m->methodName);
            }

            // 如果需要注入链表指针，则在字段列表前注入 _prev 和 _next 字段
            d->linked = linked;
            if (linked) {
                for (auto& f : d->structCommon.fields)
                    if (f.name == "_prev" || f.name == "_next" ||
                        f.name == "prev" || f.name == "next")
                        err("prev/next/_prev/_next 为链表结构体内置成员，不可显式定义");

                std::vector<Field> injected;
                for (const char* n : {"_prev", "_next"}) {
                    Field f;
                    f.name = n;
                    f.type.ptr = 1;
                    f.synthetic = true;
                    f.line = d->line;
                    injected.push_back(std::move(f));
                }
                d->structCommon.fields.insert(d->structCommon.fields.begin(),
                                 std::make_move_iterator(injected.begin()),
                                 std::make_move_iterator(injected.end()));
            }

            expect(Tok::Newline, "换行");
            return d;
        }

        // 只有结构体支持链表标记 '~'
        if (linked) err("'~' 链表标记仅支持结构体 {}");

        // 3. 对于 ( ... ) 联合体
        if (at(Tok::LParen)) {

            d->kind = Decl::UnionD;
            parseFieldBlock(d->structCommon.fields);

            expect(Tok::Newline, "换行");
            return d;
        }

        // 4. 对于枚举：def name: base_type \n\tItem1, Item2 ...
        d->kind = Decl::EnumD;
        d->structCommon.type = std::make_shared<TypeRef>(parseTypeRef());                           // 枚举的底层整数类型
        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "缩进的枚举项");
        while (!at(Tok::Dedent) && !at(Tok::End)) {
            skipNewlines();
            if (at(Tok::Dedent)) break;

            for (;;) {
                Field item;
                item.line = cur().line;
                if (!at(Tok::Ident)) err("期望枚举项名");
                item.name = advance().text;                 // 枚举项名
                if (acceptOp("=")) item.init = parseExpr(); // 可选的显式值
                d->structCommon.fields.push_back(std::move(item));
                if (!accept(Tok::Comma)) break;             // 逗号分隔多项
            }
            expect(Tok::Newline, "换行");
        }
        accept(Tok::Dedent);
        return d;
    }

    ///////////////////////////////////////////////////////////////////////////
    // fnc 函数定义解析
    //
    // fnc 支持三种形态：
    //   1. 函数类型定义：fnc name: ret, params... \n        （无函数体）
    //       → 只有签名，C 中生成 typedef 函数指针类型
    //   2. 函数实现：    fnc name: ret, params... \n\tbody  （直接定义+实现）
    //   3. 预定义类型实现：fnc name -> func_type \n\tbody    （实现已有函数类型）
    //
    // 函数签名可以单行写全部参数，也可以多行缩进（每行一个参数）。
    // 参数行与函数体之间用单独的 '-' 行分隔。

    // 前瞻判断：当前 token 序列是否像参数声明（Ident + 可能的 [] + Colon）
    bool looksLikeParam() const {

        // 当前 token 必须是标识符（参数名）
        if (!at(Tok::Ident)) return false;

        size_t q = p + 1;
        while (q < t.size()) { const Token& tk = t[q];  // 获取下一个 token

            // 如果下一个 token 是 [size] 元类型，则跳过整个 [size] 块继续向前看
            if (tk.kind == Tok::LBracket) { q++;
                while (q < t.size() && t[q].kind != Tok::RBracket &&
                       t[q].kind != Tok::Newline) q++;
                if (q < t.size() && t[q].kind == Tok::RBracket) { q++; continue; }
                return false;
            }
            return tk.kind == Tok::Colon;  // 参数一定有冒号后跟类型
        }
        return false;
    }

    // 解析 fnc 冒号后的单行项：可能是返回类型，也可能是参数
    void parseFncVars(StructCommon& structCommon) {

        for (bool haveRet = false;;) {

            // 对于可变参数 '...'：必须在参数列表末尾，且前面至少有一个参数
            if (at(Tok::Ellipsis)) {
                if (structCommon.fields.empty()) err("'...' 前必须有至少一个命名参数");
                structCommon.variadic = true;
                advance();
                return;
            }

            // 如果看起来像参数（标识符后跟冒号），则解析为参数
            if (looksLikeParam()) {
                structCommon.fields.push_back(parseFieldItem());
            }
            // 否则解析为（单例）返回值类型（冒号可省略）
            else  {
                if (haveRet) err("重复的返回类型");
                structCommon.type = std::make_shared<TypeRef>(parseTypeRef());
                haveRet = true;
            }

            // 如果后面不再是逗号分隔，则结束参数/返回值列表的解析
            if (!accept(Tok::Comma)) return;
        }
    }

    // 解析 fnc 定义：根据是否有函数体、以及签名的写法，区分函数类型定义、函数实现、预定义类型实现三种形态
    DeclPtr parseFnc(bool isRpc = false) {

        auto d = std::make_unique<Decl>();
        d->line = cur().line;
        d->isRpc = isRpc;

        advance();                                  // 跳过 fnc/rpc 关键字
        if (!at(Tok::Ident)) err(isRpc ? "期望 rpc 名" : "期望函数名");

        d->name = advance().text;                   // 函数名（rpc 时为接口名，后续会在函数名前加上 rpc_ 前缀）

        // rpc 伪形参糖不支持 :: 和 ->
        if (isRpc && at(Tok::DColon)) err("rpc 不支持 ::（C 实现接口）");
        if (isRpc && at(Tok::Arrow)) err("rpc 不支持实现预定义函数类型（->）");

        // :: 后缀 → 由 C 实现的接口（无函数体）
        if (accept(Tok::DColon)) d->cImpl = true;

        // 形态3：fnc name -> func_type —— 实现预定义函数类型
        if (accept(Tok::Arrow)) {

            if (d->cImpl)
                err("C 实现接口（::）不能使用预定义函数类型（->）");

            d->kind = Decl::FuncD;
            if (!at(Tok::Ident)) err("期望函数类型名");
            d->funcTypeName = advance().text;       // 记录引用的函数类型名

            expect(Tok::Newline, "换行");
            expect(Tok::Indent, "函数体");
            parseStmts(d->body);                    // 函数体由 codegen 从函数类型展开签名
            accept(Tok::Dedent);
            return d;
        }

        // 解析函数返回类型和参数列表
        // + :: 之后可直接跟签名（:: 即分隔），也可用 ':' 显式分隔（兼容）
        // + 非 :: 函数必须有 ':' 分隔签名
        if (d->cImpl && !at(Tok::Colon) && !at(Tok::Newline)) {
            // :: 后直接跟返回类型/参数（无 ':' 分隔）
            parseFncVars(d->structCommon);
        } else if (accept(Tok::Colon)) {
            if (!at(Tok::Newline)) {
                parseFncVars(d->structCommon);
            }
        } else if (!at(Tok::Newline) && !d->cImpl) {
            err("期望 ':' 或换行");
        }
        expect(Tok::Newline, "换行");

        // 无缩进块 → 形态1：纯函数类型定义（只有签名，无实现）
        if (!accept(Tok::Indent)) {
            d->kind = Decl::FuncTypeD;
            return d;
        }

        // 有缩进块 → 可能是形态2（函数实现）或多行参数续行
        bool isBody = false;
        for (;;) {
            skipNewlines();
            if (at(Tok::Dedent) || at(Tok::End)) break;

            // '-' 分隔符：之后是函数体语句
            if (atOp("-") && peek().kind == Tok::Newline) {
                advance(); advance();               // 跳过 '-' 和换行
                isBody = true;
                break;
            }

            // 看起来像参数声明 → 多行参数
            if (looksLikeParam()) {

                d->structCommon.fields.push_back(parseFieldItem());
                while (accept(Tok::Comma)) {        // 逗号分隔多项

                    if (at(Tok::Ellipsis)) {
                        if (d->structCommon.fields.empty()) err("'...' 前必须有参数");
                        d->structCommon.variadic = true;
                        advance();
                        break;
                    }

                    d->structCommon.fields.push_back(parseFieldItem());
                }
                expect(Tok::Newline, "换行");
                continue;
            }

            // 其他情况：以语句开头 → 函数体
            isBody = true;
            break;
        }

        // 有函数体的函数实现
        if (isBody) {
            if (d->cImpl) err("C 实现接口（::）不能有函数体");
            d->kind = Decl::FuncD;
            parseStmts(d->body);
        }
        // 只有参数，无函数体：函数类型（rpc 时为声明）
        else d->kind = Decl::FuncTypeD;
        accept(Tok::Dedent);

        // rpc 参数将成为结构体字段：所以不支持变参与数组参数
        if (d->isRpc) {
            if (d->structCommon.variadic) err("rpc 不支持可变参数 '...'");
            for (auto& f : d->structCommon.fields)
                if (!f.type.arrayDims.empty()) err("rpc 参数不支持数组，请改用指针（&）");
        }

        return d;
    }

    ///////////////////////////////////////////////////////////////////////////

    // 创建表达式节点
    ExprPtr mk(Expr::Kind k) {
        auto e = std::make_unique<Expr>();
        e->kind = k;
        e->line = cur().line;
        return e;
    }

    // 括号内遇到 Newline/Indent/Dedent 时跳过（使得表达式可跨行书写）
    void skipNlInBracket() { if (exprBracket > 0) skipLayout(); }

    // 原子表达式 —— 递归下降的最底层
    ExprPtr parsePrimary() {
        skipNlInBracket();                  // 跳过括号内的换行和缩进（允许括号内跨行书写）

        // 字面量：整数、浮点、字符串、字符

        // 对于字符串字面量
        // + 支持相邻字符串字面量拼接（C 风格 "aa" "bb"；括号内可跨行）
        if (at(Tok::Str)) {
            auto e = mk(Expr::StrLit);
            std::string v = advance().text;
            skipNlInBracket();
            while (at(Tok::Str)) {
                const std::string nx = advance().text;
                v.pop_back();              // 去掉前段收尾引号
                v += nx.substr(1);         // 续接后段（去掉开头引号）
                skipNlInBracket();
            }
            e->text = std::move(v);
            return e;
        }
        // 对于数值字面量
        if (at(Tok::Int) || at(Tok::Float) || at(Tok::Char)) {
            auto e = mk(at(Tok::Int) ? Expr::IntLit : 
                      at(Tok::Float) ? Expr::FloatLit : Expr::CharLit);
            e->text = advance().text;
            return e;
        }
        // 对于标识符
        if (at(Tok::Ident)) {
            auto e = mk(Expr::Ident);
            e->text = advance().text;
            return e;
        }

        // 对于匿名函数字面量：fnc: ret, params \n - \n\tbody
        // + 绑定到函数指针变量时，签名需与变量类型一致
        if (at(Tok::KwFnc) && peek().kind == Tok::Colon) {
            advance(); advance();  // 跳过 fnc 和 :
            auto e = mk(Expr::FncLit);
            e->line = cur().line;
            // 解析返回类型和参数（复用 parseFncVars）
            if (!at(Tok::Newline))
                parseFncVars(e->fncSig);
            expect(Tok::Newline, "换行");
            // 可选 '-' 分隔符
            if (atOp("-") && peek().kind == Tok::Newline) {
                advance(); advance();
            }
            expect(Tok::Indent, "匿名函数体需要缩进块");
            parseStmts(e->fncBody);
            accept(Tok::Dedent);
            skipNewlines();
            return e;
        }

        // 对于括号分组：(expr)；
        // + 若括号内出现 "expr : type"，则为强制类型转换
        //   (p + 1: Node&)->next  等价于 C 的 ((Node*)(p + 1))->next
        if (accept(Tok::LParen)) {
            exprBracket++;

            auto e = parseExpr();           // 先解析括号内的表达式（可能是普通的括号分组，也可能是强转表达式的主体部分）
            skipNlInBracket();              // 跳过括号内的换行和缩进（允许括号内跨行书写）
            if (accept(Tok::Colon)) {       // 如果冒号存在，则解析为强制类型转换表达式
                skipNlInBracket();
                if (!at(Tok::Ident)) err("强转期望类型名");

                auto c = mk(Expr::Cast);    // 创建强制类型转换节点
                c->op = advance().text;     // 目标类型名存于 op（转换去向，非被操作主体）
                while (atOp("&") || atOp("&&"))
                    c->castPtr += advance().text == "&&" ? 2 : 1;
                c->a = std::move(e);
                e = std::move(c);
                skipNlInBracket();
            }
            expect(Tok::RParen, "')'");

            exprBracket--;
            return e;
        }

        // 对于初始化列表：{e1, e2, ...}，可嵌套，允许尾逗号
        if (accept(Tok::LBrace)) {
            exprBracket++;
            
            auto e = mk(Expr::InitList);
            skipNlInBracket();
            if (!at(Tok::RBrace)) {
                for (;;) {
                    e->args.push_back(parseExpr());     // 解析逗号分隔的表达式列表
                    skipNlInBracket();
                    if (!accept(Tok::Comma)) break;     // 后面不再是逗号，则结束列表
                    skipNlInBracket();
                    if (at(Tok::RBrace)) break;         // 逗号后允许直接跟 '}' 结束列表（尾逗号）
                }
            }
            skipNlInBracket();
            expect(Tok::RBrace, "'}'");

            exprBracket--;
            return e;
        }

        // 对于 sizeof(expr | type)
        if (at(Tok::KwSizeof)) {
            auto e = mk(Expr::Sizeof);
            advance();
            expect(Tok::LParen, "'('");
            exprBracket++;
            e->a = parseExpr();
            exprBracket--;
            expect(Tok::RParen, "')'");
            return e;
        }

        // 对于 offsetof(Type, field)
        if (at(Tok::KwOffsetof)) {
            auto e = mk(Expr::Offsetof);
            advance();
            expect(Tok::LParen, "'('");
            if (!at(Tok::Ident)) err("offsetof 第一参数期望类型名");
            e->text = advance().text;
            expect(Tok::Comma, "','");
            if (!at(Tok::Ident)) err("offsetof 第二参数期望字段名");
            e->op = advance().text;
            expect(Tok::RParen, "')'");
            return e;
        }

        err("期望表达式，得到 '" + cur().text + "'");
    }

    // 后缀表达式 —— 循环解析调用链、下标、成员访问、后缀++
    ExprPtr parsePostfix() {
        auto e = parsePrimary();                // 递归优先解析原子表达式，后续在此基础上解析后缀操作

        // stringify<key:val, ...>(...) 选项块：仅 stringify 关键字后紧跟 '<' 时触发。
        // 选项值限整数字面量（如 compact:1）；解析后随调用链附到对应 Call 节点。
        std::vector<std::pair<std::string, long long>> pendingSofOpts;
        bool hasSofOpts = false;
        if (e->kind == Expr::Ident && e->text == "stringify" && atOp("<")) {
            advance();  // 消费 '<'
            skipNlInBracket();
            if (!atOp(">")) {
                for (;;) {
                    skipNlInBracket();
                    if (!at(Tok::Ident)) err("stringify 选项期望键名");
                    std::string key = advance().text;
                    expect(Tok::Colon, "':'");
                    skipNlInBracket();
                    long long sign = 1;
                    if (acceptOp("-")) sign = -1;
                    else acceptOp("+");
                    if (!at(Tok::Int)) err("stringify 选项 '" + key + "' 的值需为整数字面量");
                    long long v = sign * std::strtoll(advance().text.c_str(), nullptr, 0);
                    pendingSofOpts.push_back({key, v});
                    skipNlInBracket();
                    if (!acceptOp(",")) break;
                }
            }
            if (!acceptOp(">")) err("stringify 选项块期望 '>'");
            hasSofOpts = true;
        }
        for (;;) {
            if (accept(Tok::LParen)) { // 函数调用：expr(args)
                exprBracket++;
                auto call = mk(Expr::Call);
                call->a = std::move(e);  // a = 被调函数表达式
                if (hasSofOpts) { call->sofOpts = std::move(pendingSofOpts); hasSofOpts = false; }
                skipNlInBracket();
                if (!at(Tok::RParen)) {
                    for (;;) {
                        call->args.push_back(parseExpr());
                        skipNlInBracket();
                        if (!accept(Tok::Comma)) break;  // 逗号分隔的参数列表
                    }
                }
                skipNlInBracket();
                expect(Tok::RParen, "')'");
                exprBracket--;
                e = std::move(call);
            } else if (accept(Tok::LBracket)) { // 下标：expr[index]
                exprBracket++;
                auto ix = mk(Expr::Index);
                ix->a = std::move(e);
                ix->b = parseExpr();
                expect(Tok::RBracket, "']'");
                exprBracket--;
                e = std::move(ix);
            } else if (atOp(".") || at(Tok::Arrow)) { // 成员访问：expr.field 或 expr->field
                auto m = mk(Expr::Member);
                m->op = at(Tok::Arrow) ? "->" : ".";
                advance();
                m->a = std::move(e);
                if (!at(Tok::Ident)) err("期望成员名");
                m->text = advance().text;           // text = 成员名
                e = std::move(m);
            } else if (atOp("++") || atOp("--")) {  // 后缀自增/自减
                auto u = mk(Expr::PostUnary);
                u->op = advance().text;
                u->a = std::move(e);
                e = std::move(u);
            } else break;  // 无更多后缀操作，退出循环
        }
        if (hasSofOpts) err("stringify<...> 选项块后须紧跟调用 '(...)'");
        return e;
    }

    // 前缀一元表达式 —— 递归处理多个连续前缀运算符（如 !!x, *&x）
    ExprPtr parseUnary() {

        skipNlInBracket();
        if (atOp("!") || atOp("~") || atOp("-") || atOp("+") ||
            atOp("*") || atOp("&") || atOp("++") || atOp("--")) {
            auto u = mk(Expr::Unary);
            u->op = advance().text;
            u->a = parseUnary();                // 递归：允许连续一元运算符
            return u;
        }

        return parsePostfix();
    }

    // 二元表达式 —— 按优先级递归的左结合解析
    // minPrec: 当前上下文允许的最低优先级，低于此优先级停止解析
    ExprPtr parseBinary(int minPrec) {
        auto lhs = parseUnary();                // 递归优先解析一元表达式

        for (;;) {
            if (exprBracket > 0 && at(Tok::Newline))
                skipLayout();                   // 括号内跳过换行
            if (cur().kind != Tok::Op) break;

            int prec = binPrec(cur().text);
            if (prec < minPrec) break;          // 优先级低于阈值，留给上层处理

            auto b = mk(Expr::Binary);
            b->op = advance().text;
            b->a = std::move(lhs);
            b->b = parseBinary(prec + 1);       // 递归：prec+1 保证左结合
            lhs = std::move(b);
        }
        return lhs;
    }

    // 三元条件表达式 —— 右结合
    ExprPtr parseTernary() {
        auto c = parseBinary(1);                // 递归优先解析二元表达式

        if (acceptOp("?")) {
            auto e = mk(Expr::Ternary);
            e->a = std::move(c);                // 条件
            ternDepth.push_back(exprBracket);   // 分支内禁用裸强转（':' 归三目）
            e->b = parseExpr();                 // 真值分支
            expect(Tok::Colon, "':'");          // 必须的 : 分隔符
            e->c = parseTernary();              // 假值分支（右结合，可嵌套 a?b:c?d:e）
            ternDepth.pop_back();
            return e;
        }
        return c;
    }

    // 顶层表达式 —— 处理裸右值强转与赋值（右结合）
    ExprPtr parseExpr() {
        auto lhs = parseTernary();              // 递归优先解析三元表达式

        // 裸右值强转：expr: type&...（仅作右值时免括号；需继续 ->/. 等
        // 后缀操作时仍需 (expr: type&) 括号形态）。限制：
        //   1. 三目分支内（同括号层）禁用 —— ':' 归三目，强转请加括号
        //   2. 三目整体不可直接裸转（a?b:c: t 报错），需括号
        if (at(Tok::Colon) && peek().kind == Tok::Ident &&
            lhs->kind != Expr::Ternary &&
            (ternDepth.empty() || exprBracket > ternDepth.back())) {
            advance();  // ':'
            auto c = mk(Expr::Cast);
            c->op = advance().text;     // 目标类型名存于 op（转换去向，非被操作主体）
            while (atOp("&") || atOp("&&"))
                c->castPtr += advance().text == "&&" ? 2 : 1;
            c->a = std::move(lhs);
            lhs = std::move(c);
        }

        // 赋值运算符：右结合（递归调用 parseExpr 而非 parseTernary）
        if (cur().kind == Tok::Op && isAssignOp(cur().text)) {
            auto b = mk(Expr::Binary);          // 赋值也用 Binary 节点，op="=" / "+=" 等
            b->op = advance().text;
            b->a = std::move(lhs);
            b->b = parseExpr();                 // 右结合递归
            return b;
        }

        return lhs;
    }

    ///////////////////////////////////////////////////////////////////////////
    // 语句解析
    //
    // 语句以换行结束，缩进块用 Indent/Dedent 界定。
    // 控制流语句（if/while/for）的条件后可跟多行续行条件（续行运算符 + 条件），
    // 通过 '-' 分隔符区分续行条件和语句体。

    // 创建语句节点
    StmtPtr mkStmt(Stmt::Kind k) {
        auto s = std::make_unique<Stmt>();
        s->kind = k;
        s->line = cur().line;
        return s;
    }

    // 解析连续（兄弟）语句序列
    // + 直到遇到 Dedent 或 End（也就是当前缩进块结束）
    void parseStmts(std::vector<StmtPtr>& out) {
        while (!at(Tok::Dedent) && !at(Tok::End)) {
            skipNewlines();
            if (at(Tok::Dedent) || at(Tok::End)) break;
            out.push_back(parseStmt());
        }
    }

    // 解析缩进块：\n Indent stmts... Dedent
    void parseBlock(std::vector<StmtPtr>& out) {
        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "缩进块");
        parseStmts(out);
        accept(Tok::Dedent);
    }

    // 解析条件表达式 + 缩进块（if/while 共用）
    // + 支持多行条件：后续行以二元运算符开头时作为条件续行
    //   '-' 分隔符区分续行条件与语句体
    void parseCondBlock(ExprPtr& cond, std::vector<StmtPtr>& out) {

        cond = parseExpr();                 // 解析条件表达式
        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "缩进块");

        // 支持多行条件续行：行首为二元运算符时（如 &&、||）视为同一条件的延续
        while (cur().kind == Tok::Op && binPrec(cur().text) >= 0 &&
               !(atOp("-") && peek().kind == Tok::Newline)) {

            auto b = mk(Expr::Binary);
            b->op = advance().text;         // 解析二元运算符
            b->a = std::move(cond);
            b->b = parseExpr();             // 新行上的右操作数（表达式）
            cond = std::move(b);
            expect(Tok::Newline, "换行");
        }
        // '-' 分隔符：跳过，之后是真正的语句体
        if (atOp("-") && peek().kind == Tok::Newline) { advance(); advance(); }

        parseStmts(out);
        accept(Tok::Dedent);
    }

    // 解析 if 语句：
    // if cond:
    //     body...
    // [elif cond:
    //     body...]
    // [else:
    //     body...]
    StmtPtr parseIfStmt() {

        auto s = mkStmt(Stmt::IfS);
        advance();                          // 跳过 if 关键字

        parseCondBlock(s->expr, s->body);   // 条件 + if 主体

        if (at(Tok::KwElse)) {
            advance();                      // 跳过 else 关键字
            
            // 对于 else if ，逻辑可以（递归）折叠为一级（避免深层嵌套）
            // + 对应的 parser 中 elseBody[0] 是 IfS 时，codegen 生成 else if 链
            if (at(Tok::KwIf)) 
                s->elseBody.push_back(parseIfStmt());

            // 普通 else 块
            else parseBlock(s->elseBody);  
        }
        return s;
    }

    // 解析 case 语句
    // case expr:
    //     1, 2:
    //         ...
    //     :
    //         ...
    StmtPtr parseCaseStmt() {

        auto s = mkStmt(Stmt::CaseS);
        advance();                          // 跳过 case 关键字

        s->expr = parseExpr();              // 解析 case 表达式
        expect(Tok::Colon, "':'");

        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "缩进块");
        for (bool haveDefault = false;;) {
            skipNewlines();
            if (at(Tok::Dedent) || at(Tok::End)) break;     // 缩进块结束，即 case 语句结束

            // 解析 case 分支
            Stmt::CaseArm arm;
            arm.line = cur().line;

            // 对于无标签的 default 分支
            if (accept(Tok::Colon)) {
                if (haveDefault) err("case 中 default 分支重复");
                haveDefault = true;
            }
            // 对于普通带标签的 case 分支
            else {
                arm.labels.push_back(parseExpr());          // 解析 case 标签表达式
                while (accept(Tok::Comma))                  // 支持逗号分隔多个标签
                    arm.labels.push_back(parseExpr());
                expect(Tok::Colon, "':'");
            }

            // case 分支体：换行 + 缩进块
            expect(Tok::Newline, "换行");
            expect(Tok::Indent, "case 分支体");
            for (;;) {
                skipNewlines();
                if (at(Tok::Dedent) || at(Tok::End)) break; // 缩进块结束，即 case 分支体结束

                // 通过 'through' 关键字支持 case 分支贯通（fallthrough）
                if (at(Tok::KwThrough)) {
                    advance();                              // 跳过 through 关键字
                    expect(Tok::Newline, "换行");
                    arm.through = true;
                    skipNewlines();
                    if (!at(Tok::Dedent)) err("through 必须位于 case 分支末尾");
                    break;
                }

                // 解析 case 分支体内的一条语句
                arm.body.push_back(parseStmt());
            }
            accept(Tok::Dedent);

            s->caseArms.push_back(std::move(arm));
        }

        if (s->caseArms.empty()) err("case 语句至少需要一个分支");
        accept(Tok::Dedent);
        return s;
    }

    // 解析 do-while 语句：
    // do 
    //     body...
    // while cond
    StmtPtr parseDoWhileStmt() {

        auto s = mkStmt(Stmt::DoWhileS);
        advance();                          // 跳过 do 关键字

        parseBlock(s->body);
        if (!accept(Tok::KwWhile)) err("do 语句后必须跟 while 条件");

        s->expr = parseExpr();
        expect(Tok::Newline, "换行");
        return s;
    }

    // 解析 goto 语句：
    // goto label
    StmtPtr parseGotoStmt() {

        auto s = mkStmt(Stmt::GotoS);
        advance();                          // 跳过 goto 关键字
        if (!at(Tok::Ident)) err("goto 后期望标签名");

        s->text = advance().text;
        expect(Tok::Newline, "换行");
        return s;
    }

    // 解析标签语句：
    // label:       
    //     body...  
    StmtPtr parseLabelStmt() {

        auto s = mkStmt(Stmt::LabelS);
        s->text = advance().text;           // 获取并跳过 label 标识符
        expect(Tok::Colon, "':'");

        // 冒号后必须换行 + 缩进块
        expect(Tok::Newline, "换行");
        expect(Tok::Indent,  "标签缩进块");
        parseStmts(s->body);
        accept(Tok::Dedent);
        return s;
    }

    // 单条语句解析 —— 按首 token 分派
    StmtPtr parseStmt() {

        // 这里要先判断是否为标签语句（Ident + Colon + Newline）
        // + 因为需要和普通表达式语句（如函数调用）区分
        if (at(Tok::Ident) && peek().kind == Tok::Colon && peek(2).kind == Tok::Newline)
            return parseLabelStmt();

        switch (cur().kind) {

            // goto 标签语句：goto label
            case Tok::KwGoto:
                return parseGotoStmt();

            // var / let / tls 声明语句（局部变量，块级作用域）
            case Tok::KwVar:
            case Tok::KwLet:
            case Tok::KwTls: {
                auto s = mkStmt(cur().kind == Tok::KwVar ? Stmt::VarS
                              : cur().kind == Tok::KwLet ? Stmt::LetS : Stmt::TlsS);
                advance();
                parseVarList(s->decls);
                return s;
            }
            
            // 函数体内嵌套的类型定义（较少见但允许）
            case Tok::KwDef: {
                auto s = mkStmt(Stmt::DeclS);
                s->decl = parseDef();
                if (!pendingMethods.empty())
                    err("局部类型不支持成员函数");
                return s;
            }

            case Tok::KwFnc:
                err("暂不支持嵌套函数定义");  // 限制：函数只能在顶层定义

            // return [expr]
            case Tok::KwReturn: {
                auto s = mkStmt(Stmt::ReturnS);
                advance();
                if (!at(Tok::Newline)) s->expr = parseExpr();  // 可选的返回值
                expect(Tok::Newline, "换行");
                return s;
            }

            case Tok::KwBreak: {
                auto s = mkStmt(Stmt::BreakS);
                advance();
                expect(Tok::Newline, "换行");
                return s;
            }

            case Tok::KwContinue: {
                auto s = mkStmt(Stmt::ContinueS);
                advance();
                expect(Tok::Newline, "换行");
                return s;
            }

            // if 条件分支（支持 else 和 else if 链）
            case Tok::KwIf:
                return parseIfStmt();

            // while 循环
            case Tok::KwWhile: {
                auto s = mkStmt(Stmt::WhileS);
                advance();
                parseCondBlock(s->expr, s->body);
                return s;
            }
            
            // do-while 循环
            case Tok::KwDo:
                return parseDoWhileStmt();

            // for 循环：for init; cond; step \n body
            case Tok::KwFor: {
                auto s = mkStmt(Stmt::ForS);
                advance();
                if (!at(Tok::Semi)) s->forInit = parseExpr();       // 初始化表达式（可为空）
                expect(Tok::Semi, "';'");
                if (!at(Tok::Semi)) s->forCond = parseExpr();       // 条件表达式（可为空）
                expect(Tok::Semi, "';'");
                if (!at(Tok::Newline)) s->forStep = parseExpr();    // 步进表达式（可为空）
                parseBlock(s->body);
                return s;
            }

            case Tok::KwCase:
                return parseCaseStmt();

            case Tok::KwThrough:
                err("through 只能出现在 case 分支末尾");

            // run 线程语句：run rpc调用 [, &thread指针]
            //   有出参 → joinable（join 等待并回收）；无 → detach 自释放
            case Tok::KwRun: {
                auto s = mkStmt(Stmt::RunS);
                advance();
                s->expr = parseExpr();
                if (!s->expr || s->expr->kind != Expr::Call ||
                    !s->expr->a || s->expr->a->kind != Expr::Ident)
                    err("run 期望 rpc 调用形式 name(args)");
                if (accept(Tok::Comma)) s->forInit = parseExpr();   // thread 出参地址
                expect(Tok::Newline, "换行");
                return s;
            }

            // wait 条件等待语句：wait cond, mutex [, nsec [, sec]]
            //   nsec/sec 全 0 或省略 → 无限等待；调用前须已持有 mutex
            case Tok::KwWait: {
                auto s = mkStmt(Stmt::WaitS);
                advance();
                s->expr = parseExpr();                              // cond
                expect(Tok::Comma, "','");
                s->forInit = parseExpr();                           // mutex
                if (accept(Tok::Comma)) {
                    s->forCond = parseExpr();                       // 纳秒
                    if (accept(Tok::Comma)) 
                        s->forStep = parseExpr();                   // 秒
                }
                expect(Tok::Newline, "换行");
                return s;
            }

            // 默认：表达式语句（赋值、函数调用等）
            default: {
                auto s = mkStmt(Stmt::ExprS);
                s->expr = parseExpr();

                // 单行多语句：逗号运算符 a, b, c
                while (accept(Tok::Comma)) {
                    auto b = mk(Expr::Binary);
                    b->op = ",";
                    b->a = std::move(s->expr);
                    b->b = parseExpr();
                    s->expr = std::move(b);
                }
                skipNewlines();
                return s;
            }
        }
    }

    ///////////////////////////////////////////////////////////////////////////
    // 程序顶层解析
    ///////////////////////////////////////////////////////////////////////////
    //
    // sc 程序的顶层结构：连续的 inc/def/fnc/var/let 声明
    // + 声明前可加 @ 前缀表示导出对象（--emit-c 时生成 .h 声明）

    Program parseProgram() { Program prog;

        for (;;) { skipNewlines();                                  // 跳过顶层连续空行
            if (at(Tok::End)) break;

            // @ 导出前缀：作用于紧随的 inc/def/fnc/var/let
            bool exported = acceptOp("@");
            switch (cur().kind) {

                // inc 头文件引入：lexer 已将头文件名捕获为 Str token
                case Tok::KwInc: {

                    auto d = std::make_unique<Decl>();
                    d->line = cur().line;
                    d->kind = Decl::IncD;
                    advance();                                      // 跳过 inc 关键字
                    if (!at(Tok::Str)) err("inc 后期望头文件名");

                    d->name = advance().text;
                    d->external = isScModuleName(d->name);
                    d->origin = d->name;

                    // 这里先把 sc 模块导入当作外部符号记录，后端会进一步展开
                    if (d->external) {
                        prog.externSymbols.push_back(d->name);
                    }
                    d->exported = exported;
                    expect(Tok::Newline, "换行");
                    prog.decls.push_back(std::move(d));
                    break;
                }

                // add 实现/库文件添加：lexer 已将文件名捕获为 Str token
                // + 纯构建指令（编译/链接 .c/.o/.a/.so），不产生 C 输出、不导出
                case Tok::KwAdd: {

                    if (exported) err("add 不支持 @ 导出");
                    auto d = std::make_unique<Decl>();
                    d->line = cur().line;
                    d->kind = Decl::AddD;
                    advance();                                      // 跳过 add 关键字
                    if (!at(Tok::Str)) err("add 后期望实现/库文件名");

                    d->name = advance().text;
                    expect(Tok::Newline, "换行");
                    prog.decls.push_back(std::move(d));
                    break;
                }

                // 类型定义
                case Tok::KwDef:
                    prog.decls.push_back(parseDef());   
                    prog.decls.back()->exported = exported;

                    // 对于结构体内的成员函数，提升为顶层 Decl（随所属类型导出）
                    for (auto& m : pendingMethods) {
                        m->exported = exported;
                        prog.decls.push_back(std::move(m));
                    }
                    pendingMethods.clear();
                    break;

                // 函数定义
                case Tok::KwFnc:
                    prog.decls.push_back(parseFnc());   
                    prog.decls.back()->exported = exported;
                    break;

                // rpc 伪形参函数（参数结构体糖）
                case Tok::KwRpc:
                    prog.decls.push_back(parseFnc(true));
                    prog.decls.back()->exported = exported;
                    break;

                // 全局变量/常量/线程局部变量：包装成 VarD/LetD/TlsD 类型的 Decl
                case Tok::KwVar:
                case Tok::KwLet:
                case Tok::KwTls: {                    
                    if (cur().kind == Tok::KwTls && exported)
                        err("tls 变量为线程局部 static 存储，不可 @ 导出");

                    auto d = std::make_unique<Decl>();
                    d->line = cur().line;
                    d->kind = cur().kind == Tok::KwVar ? Decl::VarD
                            : cur().kind == Tok::KwLet ? Decl::LetD : Decl::TlsD;
                    d->exported = exported;
                    advance();
                    parseVarList(d->structCommon.fields);  // 解析一项或多项（逗号或多行）
                    prog.decls.push_back(std::move(d));
                    break;
                }

                default:
                    // 顶层只允许程序结构对象的几种关键字
                    err("顶层只允许 inc/def/fnc/rpc/var/let/tls，得到 '" + cur().text + "'");
            }
        }
        return prog;
    }
};

} // namespace

// 对外接口：创建 Parser 实例并执行完整的程序解析
Program parse(const std::vector<Token>& toks) {
    Parser ps(toks);
    return ps.parseProgram();
}
