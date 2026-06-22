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
#include <algorithm>
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

// 是否为关键字 token（Tok 枚举中关键字段连续区间 KwDef..KwOffsetof）。
// 用于成员名位置容忍关键字拼写：成员/方法名永不可能是语句关键字，
// 故 .run() / .done() 等与语句关键字同名的方法可被正常解析。
bool isKeywordTok(Tok k) {
    return k >= Tok::KwDef && k <= Tok::KwOffsetof;
}

// Parser 内部类 —— 封装所有语法分析状态
struct Parser {
    const std::vector<Token>& t;                    // 输入 token 序列（引用外部持有）

    size_t p = 0;                                   // 当前读位置（指向下一个待消费的 token）
    int exprBracket = 0;                            // 表达式括号深度：>0 时括号内的换行/缩进 token 被 skip 跳过

    bool inMacroBody = false;                       // 正在解析 def 宏体：名字/标识符位置折叠 \ 粘贴与 `串化`
    // 活动三目运算的括号深度栈：其分支内（同括号层）禁用裸强转，
    // + 保证 '?' 之后有且只有一个 ':' 归三目；强转需加括号
    std::vector<int> ternDepth;
    
    // 结构体内成员函数：parseDef 期间收集，parseProgram 提升为顶层 Decl
    std::vector<DeclPtr> pendingMethods;

    // 当前正在解析函数体的 Decl（供 await 回写 hasAwait 标记其所在 rpc）
    Decl* curParseFn = nullptr;

    // future<ID>() 构造点收集的 ID（去重、首见序）：parseProgram 末尾合成 future_id 枚举
    std::vector<std::string> futureIds;

    explicit Parser(const std::vector<Token>& toks) : t(toks) {}

    [[noreturn]] void err(const std::string& m) const { throw CompileError{m, cur().line}; }
    [[noreturn]] void err(int line, const std::string& m) const { throw CompileError{m, line}; }

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

    // for 之后是否为 for-in 变体：形如 name [: type] [, idx...] in ...。
    // 判定：当前须为标识符（循环变量），且本行 ';'/换行前存在顶层上下文关键字 in。
    // 经典三段式 for init; cond; step 在首个 ';' 前不会出现顶层 in，故可区分。
    bool forInAhead() const {
        if (!at(Tok::Ident)) return false;
        int depth = 0;
        for (size_t i = p; i < t.size(); i++) {
            const Token& tk = t[i];
            if (tk.kind == Tok::LParen || tk.kind == Tok::LBracket || tk.kind == Tok::LBrace) depth++;
            else if (tk.kind == Tok::RParen || tk.kind == Tok::RBracket || tk.kind == Tok::RBrace) { if (depth) depth--; }
            else if (depth == 0) {
                if (tk.kind == Tok::Semi || tk.kind == Tok::Newline) return false;
                if (tk.kind == Tok::Ident && tk.text == "in") return true;
            }
        }
        return false;
    }

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

    // 解析强转目标类型部分到 Cast 节点：[const|volatile]* name [&|&&]* [restrict]
    // 与声明侧 parseTypeRef 的限定符规则一致（见 §4）：const/volatile 前缀类型侧，
    // restrict 尾置于指针。处于冒号后的类型位置，const/volatile/restrict 必为限定符。
    void parseCastType(Expr* c) {
        for (;;) {
            if (at(Tok::Ident) && cur().text == "const")    { c->castConst = true; advance(); }
            else if (at(Tok::Ident) && cur().text == "volatile") { c->castVolatile = true; advance(); }
            else break;
        }
        if (!at(Tok::Ident)) err("强转期望类型名");
        c->op = advance().text;     // 目标类型名存于 op（转换去向，非被操作主体）
        while (atOp("&") || atOp("&&"))
            c->castPtr += advance().text == "&&" ? 2 : 1;
        if (at(Tok::Ident) && cur().text == "restrict") {
            if (c->castPtr == 0) err("restrict 限定符仅对指针强转有意义");
            c->castRestrict = true; advance();
        }
    }

    // 解析类型引用：可以是命名类型 type&&、内联 {struct}/(union)、或裸 &/&&（void*）
    TypeRef parseTypeRef() { TypeRef ty;
        // 前置类型限定符 const/volatile（上下文标识符：非关键字，按文本识别）。
        // 处于类型位置（冒号后），故 const/volatile 必为限定符，可叠加任意顺序。
        for (;;) {
            if (at(Tok::Ident) && cur().text == "const")    { ty.qConst = true; advance(); }
            else if (at(Tok::Ident) && cur().text == "volatile") { ty.qVolatile = true; advance(); }
            else break;
        }

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

        // 尾置 restrict 限定符（上下文标识符；约束指针无别名，仅对指针有意义）
        if (at(Tok::Ident) && cur().text == "restrict") { ty.qRestrict = true; advance(); }

        // 胖指针 T@（自动指针，见 builtins/auto_ptr.md）：恒为单层，与 & 互斥。
        // C 侧展开为 sc_fat，参与引用图与释放点验证。
        if (atOp("@")) {
            advance();
            if (ty.ptr > 0) err("胖指针 @ 不能与 & 组合（T@ 恒为单层）");
            if (ty.name.empty()) err("胖指针 @ 须作用于命名类型（如 T@）");
            ty.fat = true;
        }

        // 分身/切片句柄类型 T[...]（def T: <S> {} 机制）：类型侧方括号（数组维度在
        // 名字侧，故此处的 [ 必为句柄初值列表）。方括号内为 alloc 的实参初值表达式。
        if (!ty.name.empty() && ty.ptr == 0 && !ty.fat && at(Tok::LBracket)) {
            advance();                                  // '['
            ty.project = true;
            ty.projectArgs = std::make_shared<std::vector<ExprPtr>>();
            exprBracket++;
            skipNlInBracket();
            if (!at(Tok::RBracket)) {
                ty.projectArgs->push_back(parseExpr());
                while (accept(Tok::Comma)) {
                    skipNlInBracket();
                    ty.projectArgs->push_back(parseExpr());
                }
            }
            skipNlInBracket();
            exprBracket--;
            expect(Tok::RBracket, "']'");
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
        // 成员函数名容忍关键字拼写（同成员访问位置：方法名不可能是语句关键字，
        // 故 fnc run::/done:: 等与语句关键字同名的方法声明可正常解析）
        if (!at(Tok::Ident) && !isKeywordTok(cur().kind)) err("期望成员函数名");
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
                    // 无 body 非 :: 的 fnc name: → 每对象方法指针字段（MethodPtr）
                    // 与 `name: fnc:`（PlainPtr，名字在前、不注入接收者）区分：
                    // MethodPtr 按成员函数约定调用（自动注入接收者），但每对象各持指针。
                    Field f;
                    f.line = m->line;
                    f.name = m->methodName;
                    f.type.fnKind = TypeRef::FncKind::MethodPtr;
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

    // 宏体内的拼写折叠：把 token 粘贴 a\b 与串化 `x` 折叠进单一标识符串，
    // 保留原始 sc 拼写（\ 与 `）。emit-sc 原样回放；codegen_c 在 emit 点翻译为 ## / #。
    // first 已读入（首段标识符或空）。仅在 inMacroBody 时消费后续 \ / ` 段。
    std::string foldMacroSpelling(std::string s) {
        if (!inMacroBody) return s;
        for (;;) {
            if (at(Tok::Backslash)) {
                advance(); s += "\\";
                if (at(Tok::Ident))         s += advance().text;
                else if (at(Tok::Backtick)) s += "`" + advance().text + "`";
                else err("'\\' 粘贴后期望标识符或 `串化`");
            } else if (at(Tok::Backtick)) {
                s += "`" + advance().text + "`";
            } else break;
        }
        return s;
    }

    // 解析 var/let/tls 声明项：与字段类似，但额外支持 = 初值表达式和内联类型
    Field parseVarItem() { Field f;

        f.line = cur().line;                    // 须在（发生）报错前记录行号
        if (!at(Tok::Ident)) err("期望变量名");  // 当前 token 必须是标识符（变量名）
        f.name = foldMacroSpelling(advance().text); // 消费变量名；宏体内折叠 \ 粘贴
        parseMeta(f.type);                      // 解析名字后的 [] 数组元类型（name[10]: 表示数组；指针写在类型侧 name: type&）

        // C 桥接绑定 name:: type —— 尾置 :: 表示「认领一个 C 侧已定义的全局符号」：
        // 转 C 生成 extern T name;（不分配存储、无初值），仅把名字与类型登记进 sc 符号表。
        if (accept(Tok::DColon)) {
            f.cBridge = true;
            if (!atTypeStart()) err("C 桥接绑定 '::' 后期望类型名");
            TypeRef ty = parseTypeRef();
            ty.arrayDims.insert(ty.arrayDims.end(),
                                f.type.arrayDims.begin(),
                                f.type.arrayDims.end());
            f.type = std::move(ty);
            return f;                           // C 桥接绑定无初值
        }

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

        // <C, I>：ADT 容器标记（仅结构体）—— C=自定义容器类型，I=元素节点类型；
        // 转 C 时把 I 整体注入为 T 的首个 synthetic 成员（offset 0），从而 T& 与 I& 可零偏移互转
        // <S>：分身/切片实体标记（仅结构体）—— S=分身/切片类型；
        // 由解析后注入 pass 在 S 首部注入回指字段 _self: T&，并回填 S.projectEntity = T。
        std::string adtColl, adtItem, projectSelf;
        bool isAdt = false;
        if (!linked && atOp("<")) {
            advance();                                  // '<'
            if (!at(Tok::Ident)) err("期望容器/分身类型名");
            std::string first = advance().text;
            if (accept(Tok::Comma)) {
                isAdt = true;
                adtColl = first;
                if (!at(Tok::Ident)) err("期望元素节点类型名");
                adtItem = advance().text;
            } else {
                projectSelf = first;                    // <S>：分身/切片
            }
            if (!acceptOp(">")) err("'>'");
        }


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

            // ADT 容器：把 I 整体注入为首个 synthetic 成员 _adt
            d->adtColl = adtColl;
            d->adtItem = adtItem;
            if (isAdt) {
                for (auto& f : d->structCommon.fields)
                    if (f.name == "_adt")
                        err("_adt 为 ADT 容器结构体内置成员，不可显式定义");
                Field f;
                f.name = "_adt";
                f.type.name = adtItem;
                f.synthetic = true;
                f.line = d->line;
                d->structCommon.fields.insert(d->structCommon.fields.begin(), std::move(f));
            }

            // 分身/切片实体：记录 S 类型名，回指字段 _self 由注入 pass 在 S 上补齐
            d->projectSelf = projectSelf;

            expect(Tok::Newline, "换行");
            return d;
        }

        // 只有结构体支持链表标记 '~'
        if (linked) err("'~' 链表标记仅支持结构体 {}");
        if (isAdt) err("'<C, I>' ADT 容器标记仅支持结构体 {}");
        if (!projectSelf.empty()) err("'<S>' 分身/切片标记仅支持结构体 {}");


        // 3. 对于 ( ... ) 联合体（含 @( ... ) 标签联合）
        if (at(Tok::LParen) || (atOp("@") && peek().kind == Tok::LParen)) {

            d->kind = Decl::UnionD;
            d->tagged = acceptOp("@");          // @ 前缀：带标签的安全联合（sum type）
            parseFieldBlock(d->structCommon.fields);

            if (d->tagged) {
                if (d->structCommon.fields.empty())
                    err("标签联合至少需要一个变体");
                for (auto& f : d->structCommon.fields) {
                    if (f.name.empty())
                        err("标签联合的变体必须具名（匿名 {}/() 载荷不支持，请改用具名类型）");
                    if (f.name == "tag" || f.name == "u")
                        err("tag/u 为标签联合内置成员，不可用作变体名");
                    if (f.type.hasInline)
                        err("标签联合变体载荷不支持内联 {}/()，请改用具名类型");
                    if (!f.type.arrayDims.empty())
                        err("标签联合变体载荷暂不支持数组，请改用具名类型");
                }
            }

            expect(Tok::Newline, "换行");
            return d;
        }

        // 4. 枚举新形：def name: [ Item1 = 0, Item2 ... ] : base_type
        //    用 [] 包裹枚举项，与函数宏（裸标识符形参列表）区分。
        if (at(Tok::LBracket)) {
            d->kind = Decl::EnumD;
            advance();                                      // '['
            for (;;) {
                while (at(Tok::Newline) || at(Tok::Indent) || at(Tok::Dedent)) advance();
                if (at(Tok::RBracket)) break;
                Field item;
                item.line = cur().line;
                if (!at(Tok::Ident)) err("期望枚举项名");
                item.name = advance().text;                 // 枚举项名
                if (acceptOp("=")) item.init = parseExpr(); // 可选的显式值
                d->structCommon.fields.push_back(std::move(item));
                while (at(Tok::Newline) || at(Tok::Indent) || at(Tok::Dedent)) advance();
                accept(Tok::Comma);                         // 逗号或换行分隔，均可
            }
            expect(Tok::RBracket, "']'");
            expect(Tok::Colon, "枚举底层类型前的 ':'");
            d->structCommon.type = std::make_shared<TypeRef>(parseTypeRef());  // 枚举底层整数类型
            expect(Tok::Newline, "换行");
            return d;
        }

        // 5. 对象宏：def NAME: = value  → #define NAME value
        if (acceptOp("=")) {
            d->kind = Decl::MacroD;
            d->macroObject = true;
            d->expr = parseExpr();
            expect(Tok::Newline, "换行");
            return d;
        }

        // 6. 函数宏：def name: p1, p2, ... \n\tbody  → #define name(p1,...) \ body
        d->kind = Decl::MacroD;
        d->macroObject = false;
        if (!at(Tok::Newline)) {                            // 形参：裸标识符，逗号分隔，末尾可 ...
            for (;;) {
                if (accept(Tok::Ellipsis)) { d->structCommon.variadic = true; break; }
                if (!at(Tok::Ident)) err("期望宏形参名");
                Field pf; pf.line = cur().line; pf.name = advance().text;
                d->structCommon.fields.push_back(std::move(pf));
                if (!accept(Tok::Comma)) break;
            }
        }
        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "缩进的宏体");
        { bool save = inMacroBody; inMacroBody = true;
          parseStmts(d->body);
          inMacroBody = save; }
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
        // 允许上下文关键字 print 作为 C 接口函数名（io.sc 以 :: 声明 print 原语接口）
        if (!at(Tok::Ident) && !(!isRpc && at(Tok::KwPrint)))
            err(isRpc ? "期望 rpc 名" : "期望函数名");

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
            Decl* savedFn = curParseFn;
            curParseFn = d.get();
            parseStmts(d->body);
            curParseFn = savedFn;
            // rpc 内顶层 com 收发语句（com << v / com >> v）走异步形态：自动套用 rpc 的
            // await 状态机（类型在 codegen 期判定；此处以"顶层 << / >> 语句表达式"为信号）。
            // fnc 内的 << / >> 仍是同步形态（不置 hasAwait）。
            if (d->isRpc && !d->hasAwait)
                for (auto& s : d->body)
                    if (s->kind == Stmt::ExprS && s->expr &&
                        s->expr->kind == Expr::Binary &&
                        (s->expr->op == "<<" || s->expr->op == ">>")) {
                        d->hasAwait = true;
                        break;
                    }
        }
        // 只有参数，无函数体：函数类型（rpc 时为声明）
        else d->kind = Decl::FuncTypeD;
        accept(Tok::Dedent);

        // rpc 参数将成为结构体字段：不支持变参（数组参数支持，映射为 <T*, size> 两字段）
        if (d->isRpc) {
            if (d->structCommon.variadic) err("rpc 不支持可变参数 '...'");
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

        // 宏体内的参数串化 `name`（→ C #name）：作为标识符原子，保留原始 ` 拼写
        if (at(Tok::Backtick)) {
            auto e = mk(Expr::Ident);
            e->text = foldMacroSpelling("`" + advance().text + "`");
            return e;
        }

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
            e->text = foldMacroSpelling(advance().text);
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
                // print 格式覆盖：(expr: "%fmt")——冒号后为字符串字面量时，
                // op 存格式串（含引号）而非类型名，仅 print 实参位置有意义。
                if (at(Tok::Str)) {
                    auto c = mk(Expr::Cast);
                    c->castIsFmt = true;
                    c->op = advance().text;
                    c->a = std::move(e);
                    e = std::move(c);
                    skipNlInBracket();
                    expect(Tok::RParen, "')'");
                    exprBracket--;
                    return e;
                }
                if (!at(Tok::Ident)) err("强转期望类型名");

                auto c = mk(Expr::Cast);    // 创建强制类型转换节点
                parseCastType(c.get());     // [const|volatile]* 类型名 [&]* [restrict]
                c->a = std::move(e);
                e = std::move(c);
                skipNlInBracket();
            }
            expect(Tok::RParen, "')'");

            exprBracket--;
            return e;
        }

        // 聚合字面量 —— 花括号 {} 用于结构体/联合：位置 {e1, e2} 或指定成员 {a=1, b=2}
        if (accept(Tok::LBrace)) {
            exprBracket++;

            auto e = mk(Expr::InitList);
            e->initBracket = false;
            skipNlInBracket();
            if (!at(Tok::RBrace)) {
                for (;;) {
                    // 指定成员 name = expr（C99 .name=expr）：Ident 紧跟单个 '='
                    if (at(Tok::Ident) && peek().kind == Tok::Op && peek().text == "=") {
                        e->initNames.push_back(advance().text);  // 成员名
                        advance();                                // '='
                        skipNlInBracket();
                        e->args.push_back(parseExpr());
                    } else {
                        e->initNames.push_back("");               // 位置初始化
                        e->args.push_back(parseExpr());
                    }
                    skipNlInBracket();
                    if (!accept(Tok::Comma)) break;     // 后面不再是逗号，则结束列表
                    skipNlInBracket();
                    if (at(Tok::RBrace)) break;         // 逗号后允许直接跟 '}' 结束列表（尾逗号）
                }
            }
            skipNlInBracket();
            expect(Tok::RBrace, "'}'");
            // 全部位置初始化时清空 initNames（便于后端按位置处理）
            bool anyNamed = false;
            for (auto& n : e->initNames) if (!n.empty()) anyNamed = true;
            if (!anyNamed) e->initNames.clear();

            exprBracket--;
            return e;
        }

        // 数组字面量 —— 方括号 [] 用于数组：[e1, e2, ...]，可嵌套，允许尾逗号
        if (accept(Tok::LBracket)) {
            exprBracket++;

            auto e = mk(Expr::InitList);
            e->initBracket = true;
            skipNlInBracket();
            if (!at(Tok::RBracket)) {
                for (;;) {
                    e->args.push_back(parseExpr());     // 解析逗号分隔的表达式列表
                    skipNlInBracket();
                    if (!accept(Tok::Comma)) break;     // 后面不再是逗号，则结束列表
                    skipNlInBracket();
                    if (at(Tok::RBracket)) break;       // 逗号后允许直接跟 ']' 结束列表（尾逗号）
                }
            }
            skipNlInBracket();
            expect(Tok::RBracket, "']'");

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

        // future<ID>(...) 构造点：仅 future 关键字后紧跟 '<' 时触发。
        // ID 为单个标识符（future_id 枚举常量名）；解析后随调用链附到对应 Call 节点。
        std::string pendingFutureId;
        bool hasFutureId = false;
        if (e->kind == Expr::Ident && e->text == "future" && atOp("<")) {
            advance();  // 消费 '<'
            skipNlInBracket();
            if (!at(Tok::Ident)) err("future<ID> 期望事件 ID 标识符");
            pendingFutureId = advance().text;
            skipNlInBracket();
            if (!acceptOp(">")) err("future<ID> 期望 '>'");
            hasFutureId = true;
            // 收集 ID（去重、首见序）：parseProgram 合成 future_id 枚举
            if (std::find(futureIds.begin(), futureIds.end(), pendingFutureId) == futureIds.end())
                futureIds.push_back(pendingFutureId);
        }
        // T<atom>() 自动指针原子构造：类型名后紧跟 `< atom > (` 的精确四 token 形态时触发
        // （`atom` 为上下文标记，此位置不可能是比较表达式）。标记附到 Call 节点。
        bool pendingAtom = false;
        if (e->kind == Expr::Ident && atOp("<") &&
            peek().kind == Tok::Ident && peek().text == "atom" &&
            peek(2).kind == Tok::Op && peek(2).text == ">" &&
            peek(3).kind == Tok::LParen) {
            advance(); advance(); advance();   // 消费 `< atom >`
            pendingAtom = true;
        }
        for (;;) {
            if (accept(Tok::LParen)) { // 函数调用：expr(args)
                exprBracket++;
                auto call = mk(Expr::Call);
                call->a = std::move(e);  // a = 被调函数表达式
                if (hasSofOpts) { call->sofOpts = std::move(pendingSofOpts); hasSofOpts = false; }
                if (hasFutureId) { call->futureId = std::move(pendingFutureId); hasFutureId = false; }
                if (pendingAtom) { call->ctorAtom = true; pendingAtom = false; }
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
            } else if (accept(Tok::LBracket)) { // 下标：expr[index]；容器 expr[key, ...] → find 糖
                exprBracket++;
                auto ix = mk(Expr::Index);
                ix->a = std::move(e);
                skipNlInBracket();
                ix->b = parseExpr();
                while (true) {                  // 多键（容器 find 实参）：逗号分隔收入 args
                    skipNlInBracket();
                    if (!accept(Tok::Comma)) break;
                    skipNlInBracket();
                    ix->args.push_back(parseExpr());
                }
                skipNlInBracket();
                expect(Tok::RBracket, "']'");
                exprBracket--;
                e = std::move(ix);
            } else if (atOp(".") || at(Tok::Arrow)) { // 成员访问：expr.field 或 expr->field
                auto m = mk(Expr::Member);
                m->op = at(Tok::Arrow) ? "->" : ".";
                advance();
                m->a = std::move(e);
                // 成员名位置容忍关键字拼写：成员/方法名不可能是语句关键字，
                // 故 .run()/.done() 等与语句关键字同名的方法可正常解析
                if (!at(Tok::Ident) && !isKeywordTok(cur().kind)) err("期望成员名");
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
        if (hasFutureId) err("future<ID> 后须紧跟构造调用 '()'");
        return e;
    }

    // 前缀一元表达式 —— 递归处理多个连续前缀运算符（如 !!x, *&x）
    ExprPtr parseUnary() {

        skipNlInBracket();
        // async E：把 rpc 调用登记进事件循环，返回 future&（不阻塞）
        if (at(Tok::KwAsync)) {
            auto e = mk(Expr::Async);
            advance();
            e->a = parseUnary();                // 操作数：rpc 调用
            return e;
        }
        // await E：挂起当前 rpc，等 E 产的 future 就绪后恢复（仅 rpc 体内）
        if (at(Tok::KwAwait)) {
            auto e = mk(Expr::Await);
            advance();
            e->a = parseUnary();                // 操作数：rpc 调用 或 返回 future& 的叶子原语调用
            if (curParseFn) curParseFn->hasAwait = true;  // 标记所在函数为状态机
            return e;
        }
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

        // 尾置 '? \n'（错误传播糖标记）不作三元：三元必有 '? expr : expr'
        if (atOp("?") && peek().kind != Tok::Newline) {
            advance();                          // 消费 '?'
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
            parseCastType(c.get());     // [const|volatile]* 类型名 [&]* [restrict]
            c->a = std::move(lhs);
            lhs = std::move(c);
        }
        // 裸右值 print 格式覆盖：expr: "%fmt"（与 (expr: "%fmt") 等价，免括号）。
        else if (at(Tok::Colon) && peek().kind == Tok::Str &&
                 lhs->kind != Expr::Ternary &&
                 (ternDepth.empty() || exprBracket > ternDepth.back())) {
            advance();  // ':'
            auto c = mk(Expr::Cast);
            c->castIsFmt = true;
            c->op = advance().text;     // 格式串字面量（含引号）
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
                // 标签联合解构绑定：Variant as x —— 把当前变体载荷拷贝绑定到 x（只读视图）
                if (at(Tok::Ident) && cur().text == "as") {
                    advance();                              // 跳过 as
                    if (!at(Tok::Ident)) err("'as' 后期望绑定名");
                    arm.binding = advance().text;
                } else {
                    while (accept(Tok::Comma))              // 支持逗号分隔多个标签
                        arm.labels.push_back(parseExpr());
                }
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

    // 解析 print 语句：print[<chn>] arg, arg, ...
    //   <chn>：可选通道（整数字面量或宏/常量标识符），默认 "0"，透传给 C print。
    //   括号可省：print a, b 与 print(a, b) 等价（括号内允许跨行）。
    //   实参逐项 parseExpr：字符串字面量→纯文本；其余→按类型自动补说明符；
    //   (expr: "%fmt") / expr: "%fmt"（Cast.castIsFmt）→显式格式（在表达式层解析）。
    StmtPtr parsePrintStmt() {
        auto s = mkStmt(Stmt::PrintS);
        advance();                          // 跳过 print 关键字
        s->printChn = "0";
        if (atOp("<")) {                    // <chn> 通道块
            advance();
            if (at(Tok::Int) || at(Tok::Ident)) s->printChn = advance().text;
            else err("print 通道 <chn> 需为整数字面量或宏/常量名");
            if (!acceptOp(">")) err("print 通道块期望 '>'");
        }
        bool wrapped = false;
        if (at(Tok::LParen)) { advance(); exprBracket++; skipNlInBracket(); wrapped = true; }
        s->printCompat = wrapped;           // 括号形式 = C printf 兼容模式
        if (!(wrapped ? at(Tok::RParen) : at(Tok::Newline))) {
            for (;;) {
                s->printArgs.push_back(parseExpr());
                if (wrapped) skipNlInBracket();
                if (!accept(Tok::Comma)) break;
                if (wrapped) skipNlInBracket();
            }
        }
        if (wrapped) { expect(Tok::RParen, "')'"); exprBracket--; }
        expect(Tok::Newline, "换行");
        return s;
    }

    // 由 token 序列 [from, to) 重建源码文本：按 spaceBefore 还原原始空白，
    // 用于 assert 失败时回显断言表达式源码（保真度足够诊断，无需精确列号）。
    std::string spanText(size_t from, size_t to) const {
        std::string r;
        for (size_t i = from; i < to && i < t.size(); i++) {
            if (i > from && t[i].spaceBefore) r += ' ';
            r += t[i].text;
        }
        return r;
    }

    // 解析 assert 语句：assert 表达式[, 消息表达式]
    //   失败语义在 --test 模式由 codegen 注入（记录 file:line + 表达式源码后中止当前用例）。
    //   表达式为比较运算（== != < > <= >=）时，codegen 额外回显左/右操作数的值。
    StmtPtr parseAssertStmt() {
        auto s = mkStmt(Stmt::AssertS);
        advance();                          // 跳过 assert 关键字
        size_t from = p;
        s->expr = parseExpr();              // 布尔表达式（顶层逗号留给消息分隔）
        s->text = spanText(from, p);        // 表达式源码串（失败回显）
        if (accept(Tok::Comma)) s->assertMsg = parseExpr();  // 可选失败消息
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

    // ret 调用语法糖：
    //   !! func()                失败即断言中止：$=func(); if ($ != ok) assert(false)
    //   ! func() \n body         if (!($=func())) { body }
    //   > / < / >= / <= func() \n body   if (($=func()) OP 0) { body }
    // 操作符与函数调用之间须有空格（用于与普通取反表达式 !foo() 消歧）。
    StmtPtr tryParseRetCall() {
        if (cur().kind != Tok::Op) return nullptr;
        const std::string op = cur().text;

        // !! func() —— 两个相邻的 '!'（中间无空格）
        if (op == "!" && peek().kind == Tok::Op && peek().text == "!" && !peek().spaceBefore) {
            advance(); advance();                       // 跳过两个 '!'
            if (!cur().spaceBefore) err("'!!' 与函数调用之间须有空格");
            auto s = mkStmt(Stmt::RetCallS);
            s->retOp = "!!";
            s->expr = parseExpr();
            expect(Tok::Newline, "换行");
            return s;
        }

        // ! / > / < / >= / <= func() \n body
        const bool isCmp = (op == ">" || op == "<" || op == ">=" || op == "<=");
        if (op == "!" || isCmp) {
            // '!' 必须带空格才作糖（无空格视为普通取反表达式）；比较运算符开头必为糖
            if (op == "!" && !peek().spaceBefore) return nullptr;
            advance();                                  // 跳过操作符
            if (!cur().spaceBefore) err("ret 调用语法糖的操作符与函数调用之间须有空格");
            auto s = mkStmt(Stmt::RetCallS);
            s->retOp = op;
            s->expr = parseExpr();
            if (acceptOp("?")) s->retProp = true;       // 尾置 ? 错误传播糖
            // 处理体可选（仅 ? 传播而不处理时允许空体）
            expect(Tok::Newline, "换行");
            if (accept(Tok::Indent)) { parseStmts(s->body); accept(Tok::Dedent); }
            return s;
        }
        return nullptr;
    }

    // 单条语句解析 —— 按首 token 分派
    StmtPtr parseStmt() {

        // 这里要先判断是否为标签语句（Ident + Colon + Newline）
        // + 因为需要和普通表达式语句（如函数调用）区分
        if (at(Tok::Ident) && peek().kind == Tok::Colon && peek(2).kind == Tok::Newline)
            return parseLabelStmt();

        // ret 调用语法糖：!! / ! / > / < / >= / <= 开头（操作符与函数名间须有空格）
        if (StmtPtr rc = tryParseRetCall()) return rc;

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

            // mix 宏展开语句：mix name(args) → 展开为 C 宏调用（无分号包裹，宏体自含）
            case Tok::KwMix: {
                auto s = mkStmt(Stmt::MixS);
                advance();                          // 跳过 mix
                s->expr = parseExpr();              // 宏调用表达式（Expr::Call）
                expect(Tok::Newline, "换行");
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

            // final 域退出钩子：final \n body...
            //   登记本作用域的退出钩子，在每个退出点（正常落出/return/break/continue）
            //   先于自动胖边拆解执行 body（body 存于 s->body）。
            case Tok::KwFinal: {
                auto s = mkStmt(Stmt::FinalS);
                advance();                  // 跳过 final 关键字
                parseBlock(s->body);
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

            // for 循环：经典三段式 for init; cond; step \n body
            //          或 for-in：for name[: T&] in coll [revert][step e][offset e][num e] \n body
            case Tok::KwFor: {
                auto s = mkStmt(Stmt::ForS);
                advance();
                if (forInAhead()) {                                 // ---- for-in 变体 ----
                    s->forIn = true;
                    s->forVar = advance().text;                     // 循环变量名
                    if (accept(Tok::Colon)) {                       // 可选 name: T& 类型注解
                        s->forVarType = parseTypeRef();
                        s->forVarHasType = true;
                    }
                    while (accept(Tok::Comma)) {                     // 索引/坐标变量：, i, j, ...
                        if (!at(Tok::Ident)) err("for-in 索引变量期望标识符");
                        const std::string idx = advance().text;
                        if (idx == s->forVar) err("for-in 索引变量名不能与循环变量同名");
                        for (auto& x : s->forIdxVars)
                            if (x == idx) err("for-in 索引变量名重复");
                        s->forIdxVars.push_back(idx);
                    }
                    if (!(at(Tok::Ident) && cur().text == "in")) err("for-in 期望 'in'");
                    advance();                                      // 消费上下文关键字 in
                    if (at(Tok::LBracket)) {                        // 范围字面量 [lo, hi] / [lo, hi)
                        s->forIsRange = true;
                        advance();                                  // [
                        exprBracket++;
                        skipNlInBracket();
                        s->forRangeLo = parseExpr();
                        skipNlInBracket();
                        expect(Tok::Comma, "','");
                        skipNlInBracket();
                        s->forRangeHi = parseExpr();
                        skipNlInBracket();
                        if (accept(Tok::RBracket)) s->forRangeIncl = true;       // ] 闭区间
                        else if (accept(Tok::RParen)) s->forRangeIncl = false;   // ) 半开区间
                        else err("范围期望 ']' 或 ')'");
                        exprBracket--;
                    } else {
                        s->forColl = parseExpr();                   // 数组/链/容器/串/整数计数
                    }
                    // 尾随选项：revert / step e / offset e / num e（任意顺序，各至多一次）
                    while (at(Tok::Ident)) {
                        const std::string& kw = cur().text;
                        if (kw == "revert")      { advance(); s->forRevert = true; }
                        else if (kw == "step")   { advance(); s->forStepE = parseExpr(); }
                        else if (kw == "offset") { advance(); s->forOffsetE = parseExpr(); }
                        else if (kw == "num")    { advance(); s->forNumE = parseExpr(); }
                        else break;
                    }
                    parseBlock(s->body);
                    return s;
                }
                if (at(Tok::Newline)) { parseBlock(s->body); return s; }  // for\n（省略 ;;）等价 while(true)
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

            // run 线程语句：run[<opt:v, ...>] rpc调用 [, &thread指针]
            //   有出参 → joinable（join 等待并回收）；无 → detach 自释放
            //   可选 <stack:N, prio:M> 选项块：线程属性透传给 C（值限非负整数字面量）
            case Tok::KwRun: {
                auto s = mkStmt(Stmt::RunS);
                advance();
                // run<key:val, ...> 选项块：紧跟 run 后的 '<' 触发（run 目标为
                // + 标识符调用，'<' 不会是其合法起始，故无歧义）。键校验延后到 codegen。
                if (atOp("<")) {
                    advance();  // 消费 '<'
                    skipNlInBracket();
                    if (!atOp(">")) {
                        for (;;) {
                            skipNlInBracket();
                            if (!at(Tok::Ident)) err("run 选项期望键名");
                            std::string key = advance().text;
                            expect(Tok::Colon, "':'");
                            skipNlInBracket();
                            acceptOp("+");
                            if (!at(Tok::Int))
                                err("run 选项 '" + key + "' 的值需为非负整数字面量");
                            long long v = std::strtoll(advance().text.c_str(), nullptr, 0);
                            s->runOpts.push_back({key, v});
                            skipNlInBracket();
                            if (!accept(Tok::Comma)) break;
                        }
                    }
                    if (!acceptOp(">")) err("run 选项块期望 '>'");
                }
                s->expr = parseExpr();
                if (!s->expr || s->expr->kind != Expr::Call ||
                    !s->expr->a || s->expr->a->kind != Expr::Ident)
                    err("run 期望 rpc 调用形式 name(args)");
                if (accept(Tok::Comma)) s->forInit = parseExpr();   // thread 出参地址
                expect(Tok::Newline, "换行");
                return s;
            }

            // done 标记就绪语句：done future [, result]
            //   等价 future_done(future, result)；result 省略=NULL，自动 void* 擦除
            case Tok::KwDone: {
                auto s = mkStmt(Stmt::DoneS);
                advance();
                s->expr = parseExpr();                              // future
                if (accept(Tok::Comma))
                    s->forInit = parseExpr();                       // 可选结果
                expect(Tok::Newline, "换行");
                return s;
            }

            // print 日志输出语句：print[<chn>] arg, arg, ...（括号可省）
            //   <chn> = u1 通道（整数字面量或宏/常量名），默认 0，透传给 C print。
            //   实参为 python 风格拼接：字符串字面量=纯文本；其余表达式按静态类型
            //   自动补 printf 说明符；(expr: "%fmt") 可显式指定该实参格式。
            case Tok::KwPrint:
                return parsePrintStmt();

            // assert 测试断言：assert 表达式[, 消息]（仅 --test 模式有运行语义）
            case Tok::KwAssert:
                return parseAssertStmt();

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

    // 解析顶层测试用例：tst[.skip] "名字" \n\tbody
    //   一等测试块，每文件可任意多个。普通编译忽略；--test 模式收集为 runner 入口。
    DeclPtr parseTestDecl() {
        auto d = std::make_unique<Decl>();
        d->line = cur().line;
        d->kind = Decl::TestD;
        advance();                          // 跳过 tst 关键字
        if (atOp(".")) {                    // tst.skip 跳过形态
            advance();
            if (!(at(Tok::Ident) && cur().text == "skip"))
                err("tst. 之后仅支持 skip（tst.skip \"...\"）");
            advance();
            d->testSkip = true;
        }
        if (!at(Tok::Str)) err("tst 后期望用例名（字符串字面量）");
        const std::string raw = advance().text;          // 含引号的源码串
        d->name = raw.size() >= 2 ? raw.substr(1, raw.size() - 2) : raw;  // 去外层引号，转义保留
        expect(Tok::Newline, "换行");
        expect(Tok::Indent, "tst 缩进块");
        parseStmts(d->body);
        accept(Tok::Dedent);
        return d;
    }

    Program parseProgram() { Program prog;

        for (;;) { skipNewlines();                                  // 跳过顶层连续空行
            if (at(Tok::End)) break;

            // @@ 根模块标记：独立顶层声明，标注本单元为显式根模块（全局前奏提供者）。
            // 其 @导出 由编译器默认注入所有依赖单元；语法插件据此静态识别根模块。
            if (atOp("@@")) {
                advance();
                prog.isRoot = true;
                expect(Tok::Newline, "换行");
                continue;
            }

            // @ 导出前缀：作用于紧随的 inc/def/fnc/var/let
            bool exported = acceptOp("@");
            switch (cur().kind) {

                // tst 单元测试用例：顶层一等测试块（不可 @ 导出）
                case Tok::KwTst: {
                    if (exported) err("tst 测试用例不支持 @ 导出");
                    prog.decls.push_back(parseTestDecl());
                    break;
                }

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

                // mix 宏展开（顶层）：mix name(args) → 展开宏，产生声明
                case Tok::KwMix: {
                    if (exported) err("mix 不支持 @ 导出");
                    auto d = std::make_unique<Decl>();
                    d->line = cur().line;
                    d->kind = Decl::MixD;
                    advance();                          // 跳过 mix
                    d->expr = parseExpr();              // 宏调用表达式（Expr::Call）
                    expect(Tok::Newline, "换行");
                    prog.decls.push_back(std::move(d));
                    break;
                }

                default:
                    // 顶层只允许程序结构对象的几种关键字
                    err("顶层只允许 inc/def/fnc/rpc/var/let/tls/mix，得到 '" + cur().text + "'");
            }
        }

        // ── 分身/切片注入 pass：对每个 def T: <S> 实体，在 S（分身/切片类型）首部
        //    注入回指字段 _self: T&，并回填 S.projectEntity = T，供 self 关键字与
        //    赋值语法糖定位本体。要求 T 与 S 在同一翻译单元。
        for (auto& dT : prog.decls) {
            if (!dT || dT->kind != Decl::StructD || dT->projectSelf.empty()) continue;
            Decl* dS = nullptr;
            for (auto& d : prog.decls) {
                if (d && d->kind == Decl::StructD && d->name == dT->projectSelf) { dS = d.get(); break; }
            }
            if (!dS)
                err(dT->line, "分身/切片实体 '" + dT->name + "' 的分身类型 '" +
                    dT->projectSelf + "' 未定义（须与实体在同一翻译单元）");
            if (!dS->projectEntity.empty())
                err(dT->line, "分身/切片类型 '" + dS->name + "' 已是实体 '" +
                    dS->projectEntity + "' 的分身，不能再作为 '" + dT->name + "' 的分身");
            dS->projectEntity = dT->name;
            for (auto& f : dS->structCommon.fields)
                if (f.name == "_self")
                    err(dS->line, "_self 为分身/切片结构体内置回指成员，不可显式定义");
            Field f;
            f.name = "_self";
            f.type.name = dT->name;
            f.type.ptr = 1;                              // _self: T&
            f.synthetic = true;
            f.line = dS->line;
            // 若 S 是链表结构体（已注入 _prev/_next），回指字段插在其后；否则插在最前
            size_t pos = 0;
            while (pos < dS->structCommon.fields.size() &&
                   (dS->structCommon.fields[pos].name == "_prev" ||
                    dS->structCommon.fields[pos].name == "_next"))
                pos++;
            dS->structCommon.fields.insert(dS->structCommon.fields.begin() + pos, std::move(f));
        }

        // ── future<ID> 聚合 pass：把各构造点收集的事件 ID 合成一个 future_id 枚举，
        //    插入 decls 首部（无依赖）。转译工程下写入 type.h（各 .c #include）；
        //    stdout/内联模式则就地内联进 .c。供 async_loop(async_proc) 按 id 派发。
        if (!futureIds.empty()) {
            auto e = std::make_unique<Decl>();
            e->kind = Decl::EnumD;
            e->name = "future_id";
            e->genTypeHeader = true;
            e->structCommon.type = std::make_shared<TypeRef>();
            e->structCommon.type->name = "i4";
            for (auto& id : futureIds) {
                Field f;
                f.name = id;
                e->structCommon.fields.push_back(std::move(f));
            }
            prog.decls.insert(prog.decls.begin(), std::move(e));
            prog.futureIds = futureIds;
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
