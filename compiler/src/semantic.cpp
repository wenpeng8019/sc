// ============================================================
// 语义分析器 —— 基于 AST 的类型兼容和安全性检查
// ============================================================
// 在 parse 完成、codegen 之前执行。不修改 AST，仅做验证和报错。
//
// 检查项目：
//   - 赋值/初始化/返回的类型兼容（标量⇄指针不可混用）
//   - nil 只能赋给指针/数组类型
//   - 解引用 *x / 下标 x[] 的操作数必须是指针或数组
//   - 结构/联合按值递归包含检测（会导致无限大类型）
//   - 禁止返回局部变量地址、禁止将局部地址写入全局存储
//   - void 值不能作为表达式使用
//   - 无返回值函数（省略返回类型）不能 return 表达式
//
// 注意：语义检查不与 import 符号做硬绑定 —— 无法推导类型时返回
// invalid Ty，跳过该检查，避免因缺少模块依赖信息而误报错误。
// ============================================================
#include "semantic.h"
#include "error.h"

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <cerrno>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// 文件名是否以 .sc 结尾（区分 .sc 模块 inc 与 C 头 inc）
bool endsWithSc(const std::string& n) {
    return n.size() >= 3 && n.compare(n.size() - 3, 3, ".sc") == 0;
}

// 两字符串的 Levenshtein 编辑距离（用于 typo 近似名提示）。
int editDistance(const std::string& a, const std::string& b) {
    const size_t m = a.size(), n = b.size();
    std::vector<int> prev(n + 1), cur(n + 1);
    for (size_t j = 0; j <= n; j++) prev[j] = static_cast<int>(j);
    for (size_t i = 1; i <= m; i++) {
        cur[0] = static_cast<int>(i);
        for (size_t j = 1; j <= n; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost });
        }
        std::swap(prev, cur);
    }
    return prev[n];
}

// 在候选名集合里寻找与 nm 编辑距离最小且在阈值内的名字；无合适项返回空串。
// 阈值随名字长度递增（短名容错小，避免把毫不相干的短名误判为 typo）。
std::string closestName(const std::string& nm,
                        const std::vector<std::string>& cands) {
    if (nm.empty()) return {};
    const int maxDist = nm.size() <= 3 ? 1 : (nm.size() <= 6 ? 2 : 3);
    int best = maxDist + 1;
    std::string hit;
    for (const auto& c : cands) {
        if (c.empty() || c == nm) continue;
        // 长度差已超阈值则无需计算
        int dl = static_cast<int>(c.size()) - static_cast<int>(nm.size());
        if (dl < 0) dl = -dl;
        if (dl > maxDist) continue;
        int d = editDistance(nm, c);
        if (d < best) { best = d; hit = c; }
    }
    return best <= maxDist ? hit : std::string{};
}

// platform.h 预置头：scc 生成的 C 始终 #include 这些标准头，故其符号无需 inc 即可见。
// 程序若 inc 了「不在此集合」的额外 C 头（编译/run 模式下未做 libclang 枚举），
// 则无法确定该头提供的符号，需放宽未定义检查（见 lenientCalls）。
const std::unordered_set<std::string>& preludeHeaders() {
    static const std::unordered_set<std::string> s = {
        "stdint.h", "stddef.h", "stdbool.h", "stdarg.h",
        "stdio.h", "stdlib.h", "string.h", "time.h",
        "assert.h", "inttypes.h",
        "platform.h",   // scc 生成的 C 始终经 platform.h 带入；其自有符号由动态扫描登记
    };
    return s;
}

// ---- 内置 C 头（platform.h）符号动态注册表 ----
// 由 registerBuiltinHeaderSymbols（见文件末公开接口）按 header 内容填充。
// 区别于下面写死的 libc 白名单：platform.h 是可编辑/扩展的「我们自己的」头，
// 故按内容扫描其声明的符号，免去每次加 P_xxx/sc_xxx 都改编译器。
std::unordered_set<std::string>& builtinHeaderIdents() {
    static std::unordered_set<std::string> s; return s;   // 宏 / 函数名（值与可调用位置）
}
std::unordered_set<std::string>& builtinHeaderTypes() {
    static std::unordered_set<std::string> s; return s;   // typedef / struct-union 类型名
}

// 扫描内置 C 头文本，按行启发式提取其声明的符号名（宏 / 函数 / 类型）填入注册表。
// 仅用于「已知符号」放宽：多收录无害（只会少报未定义，绝不会误报），故取宽松匹配。
void scanBuiltinHeaderText(const std::string& text) {
    auto& idents = builtinHeaderIdents();
    auto& types  = builtinHeaderTypes();
    auto isIdentCh = [](char c) {
        return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
    };
    // '(' 前 / ';' 前紧邻的标识符（跳过尾随非标识符字符）
    auto identBefore = [&](const std::string& s, size_t pos) -> std::string {
        size_t q = pos; while (q > 0 && !isIdentCh(s[q - 1])) q--;
        size_t p = q;   while (p > 0 && isIdentCh(s[p - 1])) p--;
        return q > p ? s.substr(p, q - p) : std::string{};
    };

    size_t i = 0, n = text.size();
    while (i < n) {
        size_t e = text.find('\n', i);
        if (e == std::string::npos) e = n;
        std::string line = text.substr(i, e - i);
        i = e + 1;

        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;
        std::string t = line.substr(s);

        // 1) #define NAME / #define NAME(...) —— 宏（含平台判定宏、函数式宏）
        if (t[0] == '#') {
            size_t k = 1; while (k < t.size() && (t[k] == ' ' || t[k] == '\t')) k++;
            if (t.compare(k, 6, "define") == 0) {
                size_t p = k + 6; while (p < t.size() && (t[p] == ' ' || t[p] == '\t')) p++;
                size_t q = p;     while (q < t.size() && isIdentCh(t[q])) q++;
                if (q > p) idents.insert(t.substr(p, q - p));
            }
            continue;   // 其余预处理指令忽略
        }

        // 2) typedef ... NAME;  及  } NAME;（struct/union typedef 收尾）→ 类型名
        const bool isTypedef = t.compare(0, 8, "typedef ") == 0;
        if (isTypedef || t[0] == '}') {
            size_t semi = t.find(';');
            if (semi != std::string::npos) {
                std::string nm = identBefore(t, semi);
                if (!nm.empty()) types.insert(nm);
            }
            if (isTypedef) continue;
        }

        // 3) 函数定义/原型：仅认 static / extern / inline 起头的顶层声明行
        //    （平台函数皆 static inline）；取首个 '(' 前紧邻标识符为函数名。
        if (t.compare(0, 7, "static ") == 0 || t.compare(0, 7, "extern ") == 0 ||
            t.compare(0, 7, "inline ") == 0) {
            size_t lp = t.find('(');
            if (lp != std::string::npos) {
                std::string nm = identBefore(t, lp);
                if (!nm.empty()) idents.insert(nm);
            }
        }
    }
}


// 预置头（preludeHeaders）暴露的常用 libc 函数 / 宏 / 常量白名单：
// 这些名字始终可作为「已知可调用」与「已知标识符」，避免对合法 C 互操作误报未定义。
const std::unordered_set<std::string>& libcSymbols() {
    static const std::unordered_set<std::string> s = {
        // stdio.h 函数
        "printf", "fprintf", "sprintf", "snprintf",
        "vprintf", "vfprintf", "vsprintf", "vsnprintf",
        "scanf", "fscanf", "sscanf", "vscanf", "vfscanf", "vsscanf",
        "puts", "fputs", "putchar", "fputc", "putc",
        "getchar", "getc", "fgetc", "fgets",
        "fopen", "freopen", "fclose", "fread", "fwrite",
        "fseek", "ftell", "rewind", "fflush", "feof", "ferror",
        "perror", "remove", "rename", "tmpfile", "tmpnam",
        "setbuf", "setvbuf", "ungetc", "clearerr", "fgetpos", "fsetpos",
        // stdlib.h 函数
        "malloc", "calloc", "realloc", "free", "aligned_alloc",
        "abort", "exit", "_Exit", "atexit", "quick_exit", "at_quick_exit",
        "getenv", "system", "qsort", "bsearch",
        "rand", "srand", "atoi", "atol", "atoll", "atof",
        "strtol", "strtoll", "strtoul", "strtoull",
        "strtod", "strtof", "strtold",
        "abs", "labs", "llabs", "div", "ldiv", "lldiv",
        // string.h 函数
        "memcpy", "memmove", "memset", "memcmp", "memchr",
        "strcpy", "strncpy", "strcat", "strncat",
        "strcmp", "strncmp", "strcoll", "strxfrm",
        "strchr", "strrchr", "strspn", "strcspn", "strpbrk",
        "strstr", "strtok", "strlen", "strerror", "strdup",
        // time.h 函数
        "time", "clock", "difftime", "mktime",
        "asctime", "ctime", "gmtime", "localtime", "strftime",
        // stdarg.h
        "va_start", "va_arg", "va_end", "va_copy",
        // assert.h
        "assert", "static_assert",
        // 常用宏 / 常量（作为标识符出现）
        "NULL", "EOF", "stdin", "stdout", "stderr",
        "EXIT_SUCCESS", "EXIT_FAILURE", "RAND_MAX", "BUFSIZ",
        "SEEK_SET", "SEEK_CUR", "SEEK_END", "CLOCKS_PER_SEC",
    };
    return s;
}

// 预置头暴露的常用 libc 类型名白名单：无需 sc 侧 def 即可在类型位置使用，
// 避免对常见 C 互操作类型（FILE/size_t 等）误报「未定义的类型」。
const std::unordered_set<std::string>& libcTypes() {
    static const std::unordered_set<std::string> s = {
        "size_t", "ssize_t", "ptrdiff_t", "intptr_t", "uintptr_t",
        "intmax_t", "uintmax_t", "max_align_t",
        "int8_t", "int16_t", "int32_t", "int64_t",
        "uint8_t", "uint16_t", "uint32_t", "uint64_t",
        "FILE", "fpos_t", "time_t", "clock_t", "wchar_t",
        "va_list", "div_t", "ldiv_t", "lldiv_t",
    };
    return s;
}

// ---------------- 内部类型表示 ----------------
// 轻量级类型描述，比完整的 TypeRef 更紧凑、便于比较。
// valid=false 表示类型未知（如调用未登记的模块函数），跳过后续检查。
struct Ty {
    std::string name;   // 基类型名（"i4", "char", ""=void* 规则）
    int ptr = 0;        // 指针层数（& 个数）
    int arr = 0;        // 数组维度数（[] 个数）
    bool valid = false; // true=能确定类型，false=跳过检查
    bool isNil = false; // 字面量 nil，只能赋给指针/数组
    bool project = false; // 分身/切片句柄 T[...]：可整体被赋值为本体或 nil（语法糖）
    bool fat = false;   // 自动指针 T@（胖指针）：指针类，可与 T()/T@/nil 互赋
    bool immutable = false; // let 不可变绑定：禁止再赋值 / 自增自减
};

// ---------------- 运算符辅助函数 ----------------

// 赋值类运算符（包括复合赋值 += -= 等）
bool isAssignOp(const std::string& op) {
    return op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=" ||
           op == "%=" || op == "&=" || op == "|=" || op == "^=" ||
           op == "<<=" || op == ">>=";
}

// 指针或数组：均可解引用（胖指针 T@ 亦视为指针类）
bool isPointerLike(const Ty& t) { return t.ptr > 0 || t.arr > 0 || t.fat; }

// 把内部 Ty 渲染成可读类型串（用于诊断信息）：基名 + @/& 指针 + [] 数组。
std::string tyStr(const Ty& t) {
    if (t.isNil) return "nil";
    std::string s = t.name.empty() ? "void" : t.name;
    if (t.fat) s += "@";
    for (int i = 0; i < t.ptr; i++) s += "&";
    for (int i = 0; i < t.arr; i++) s += "[]";
    return s;
}

// 算术/取模类二元运算符（用于非法运算检测；不含比较、逻辑、位移与赋值）
bool isArithOp(const std::string& op) {
    return op == "+" || op == "*" || op == "/" || op == "%";
}

// 浮点基类型名
bool isFloatName(const std::string& n) { return n == "f4" || n == "f8"; }

// 表达式是否为「值为 0 的整数字面量」（含 0 / 0x0 / 0b0 等各进制，忽略后缀）。
bool isIntZeroLit(const Expr& e) {
    if (e.kind != Expr::IntLit) return false;
    char* end = nullptr;
    long long v = std::strtoll(e.text.c_str(), &end, 0);
    return end != e.text.c_str() && v == 0;
}

// ---------------- 整数字面量越界检查辅助 ----------------

// 固定宽整型 → 位宽（8/16/32/64）；非固定宽（char/浮点/未知）返回 0 → 跳过检查。
// char 故意不纳入：其有无符号属性随平台而定，纳入会引入误报。
int intTypeBits(const std::string& name) {
    if (name == "i1" || name == "u1" || name == "bool") return 8;
    if (name == "i2" || name == "u2") return 16;
    if (name == "i4" || name == "u4" || name == "ret") return 32;
    if (name == "i8" || name == "u8") return 64;
    return 0;
}

// 固定宽整型是否有符号（iN / ret 有符号；uN / bool 无符号）。
bool isSignedIntName(const std::string& name) {
    return name == "i1" || name == "i2" || name == "i4" || name == "i8" || name == "ret";
}

// 解析整数字面量文本为无符号数值（剥离后缀，按进制解析）。
// 返回 false 表示无法可靠解析（含超 64 位 ERANGE）→ 调用方跳过检查（宁可漏报）。
// 回传 isHex：十六进制字面量常作位模式（0xFF→i1 视作 -1），后续对有符号目标放宽边界。
bool parseIntLiteral(const std::string& t, unsigned long long& mag, bool& isHex) {
    isHex = false;
    std::string digits;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        isHex = true;
        size_t i = 2;
        while (i < t.size() && std::isxdigit((unsigned char)t[i])) i++;
        if (i == 2) return false;
        digits = t.substr(0, i);                 // 含 0x 前缀，交 strtoull base 16
    } else {
        size_t i = 0;
        while (i < t.size() && std::isdigit((unsigned char)t[i])) i++;
        if (i == 0) return false;
        digits = t.substr(0, i);                 // base 0：支持前导 0 八进制
    }
    errno = 0;
    char* end = nullptr;
    mag = std::strtoull(digits.c_str(), &end, isHex ? 16 : 0);
    if (end == digits.c_str()) return false;
    if (errno == ERANGE) return false;           // 超 64 位：保守跳过
    return true;
}


// TypeRef → Ty 转换
Ty fromTypeRef(const TypeRef& t) {
    Ty ty;
    ty.name = t.name;
    ty.ptr = t.ptr;
    ty.arr = (int)t.arrayDims.size();
    ty.valid = true;
    ty.project = t.project;
    ty.fat = t.fat;
    // 函数指针字段（普通函数指针 / 每对象方法指针）作为值即一个指针：
    // 视为指针类，使其能与通用指针 & 互相赋值（如把 alloc 透传的 & 存入 MethodPtr 字段）。
    if (t.fnKind != TypeRef::FncKind::None && ty.ptr == 0) ty.ptr = 1;
    return ty;
}

// ---------------- 语义检查器 ----------------
// 单次语义分析的状态。先 collectTop() 登记所有顶层符号，
// 再做结构体按值环检测，最后遍历所有函数体 checkFunctions()。
struct Checker {
    const Program& prog;

    // 顶层符号表
    std::unordered_map<std::string, const Decl*> funcTypes; // 函数类型声明（用于查找 -> 引用）
    std::unordered_map<std::string, const Decl*> structs;   // 结构/联合定义
    std::unordered_map<std::string, std::string> aliases;   // 类型别名（name → 目标类型）
    std::unordered_map<std::string, Ty> globals;            // 全局 var/let/tls 的类型
    // 容器类型 C（def T: <C, I>）→ 元素节点类型 I：下标糖 t[key] → find 结果为 I&
    std::unordered_map<std::string, std::string> containerItem;

    // ---- 未定义符号诊断用符号表（本地 + 外部，含 C 头/模块合并的描述符）----
    std::unordered_map<std::string, const Decl*> funcSigs;  // 非方法函数名 → 声明（实参检查取签名）
    std::unordered_set<std::string> enumConsts;             // 枚举常量名（作为标识符已知）
    std::unordered_set<std::string> declNames;              // 所有顶层声明名（catch-all：类型/全局/外部模块符号）
    std::unordered_map<std::string, std::unordered_set<std::string>> structMethods;  // 结构名 → 成员函数名集
    std::unordered_map<std::string, const Decl*> macros;    // 宏名 → 宏定义（供顶层 mix 展开登记声明）
    // 放宽门控：存在「未枚举的额外 C 头」或 add 外部实现/库 → 可能引入未知符号，
    // 此时整体关闭未定义函数/标识符检查（宁可漏报不可误报）。实参个数/类型、成员检查不受影响。
    bool lenientCalls = false;

    explicit Checker(const Program& p) : prog(p) {}

    [[noreturn]] void err(int line, const std::string& msg) const {
        throw CompileError{msg, line};
    }

    // ---- 符号解析 ----

    // 解析结构体名：沿别名链最多追踪 8 步，防止循环别名死循环
    const Decl* resolveStruct(std::string name) const {
        for (int i = 0; i < 8 && !name.empty(); i++) {
            auto it = structs.find(name);
            if (it != structs.end()) return it->second;
            auto al = aliases.find(name);
            if (al == aliases.end()) return nullptr;
            name = al->second;
        }
        return nullptr;
    }

    // 将别名展开为最终类型名（最多 16 步），用于按值包含图的结点去重
    std::string resolveAliasToName(std::string name) const {
        for (int i = 0; i < 16 && !name.empty(); i++) {
            auto al = aliases.find(name);
            if (al == aliases.end()) return name;
            name = al->second;
        }
        return name;
    }

    // ---- 类型名校验 ----

    // sc 内置基本类型名（见 syntax.md §「内置类型」；含 char 别名 c1、可变参 va_list）。
    static bool isPrimitiveType(const std::string& n) {
        static const std::unordered_set<std::string> p = {
            "i1", "i2", "i4", "i8", "u1", "u2", "u4", "u8",
            "f4", "f8", "bool", "char", "ret", "c1", "va_list",
            "tril", "object", "sc_hyper",
        };
        return p.count(n) != 0;
    }

    // 类型名是否已知：内置基本类型、用户定义类型（struct/union/alias/enum/functype）、
    // 外部模块/C 头合并的声明名、常见 libc 互操作类型。
    bool isKnownTypeName(const std::string& n) const {
        if (isPrimitiveType(n)) return true;
        if (structs.count(n) || aliases.count(n) || funcTypes.count(n)) return true;
        if (declNames.count(n)) return true;     // 枚举类型 / 外部模块 / C 头类型 / 全局符号
        if (libcTypes().count(n)) return true;
        if (builtinHeaderTypes().count(n)) return true;   // platform.h 动态扫描的 typedef/结构类型
        return false;
    }

    // 未知类型名的近似提示（typo）：在已知类型名集合里找最近者。
    std::string hintTypeName(const std::string& n) const {
        std::vector<std::string> cands;
        for (const char* p : {"i1","i2","i4","i8","u1","u2","u4","u8",
                              "f4","f8","bool","char","ret"}) cands.push_back(p);
        for (auto& kv : structs)   cands.push_back(kv.first);
        for (auto& kv : aliases)   cands.push_back(kv.first);
        for (auto& kv : funcTypes) cands.push_back(kv.first);
        std::string c = closestName(n, cands);
        return c.empty() ? std::string{} : "，是否想用 '" + c + "'？";
    }

    // 校验出现在类型位置的基类型名是否合法。
    // · void 特例：sc 无 void 值类型 —— 作返回/值类型（无指针无数组）即报错；
    //   仅 void&/void**（指针，等价 void*）放行（与裸 & 同义）。
    // · 内联结构/联合、函数指针类型：基类型名由其自身结构承载，跳过。
    // · 其余未知名在非放宽模式下报「未定义的类型」（放宽门控同未定义标识符检查）。
    void checkTypeName(const TypeRef& t, int line) const {
        if (t.hasInline) return;                          // 内联 {..}/(..)：无具名基类型
        if (t.fnKind != TypeRef::FncKind::None) return;   // 函数指针类型：单独承载签名
        const std::string& n = t.name;
        if (n == "void") {
            if (t.ptr == 0 && t.arrayDims.empty())
                err(line, "sc 无 void 值类型：函数无返回值请省略返回类型，"
                          "空指针用裸 & 或 void&");
            return;                                       // void&/void** ≡ void*/void**：放行
        }
        if (n.empty()) return;                            // 省略类型名：void*/char* 由 codegen 兜底
        if (lenientCalls) return;                         // 存在未枚举 C 头 / add：可能引入未知类型
        if (!isKnownTypeName(n))
            err(line, "未定义的类型 '" + n + "'" + hintTypeName(n));
    }

    // ---- 按值递归包含检测 ----
    // 结构体 A 的字段按值包含结构体 B（非指针/非数组），则建立边 A→B。
    // 图中的回边意味着存在按值递归环，会导致 C 中类型无限大，编译报错。

    // 从一个 TypeRef 递归收集其按值包含的结构体
    void collectByValueDepsFromType(const TypeRef& t,
                                    std::vector<std::pair<std::string, int>>& out,
                                    int line) const {
        // 函数字段不参与按值包含图（函数指针大小固定）
        if (t.fnKind != TypeRef::FncKind::None) return;

        // 指针或数组字段不会形成"按值递归包含"（它们只是引用，大小固定）。
        // 胖指针 T@（fat）同理：C 侧为 sc_fat（24 字节固定），不按值嵌入目标类型。
        if (t.ptr == 0 && !t.fat && t.arrayDims.empty() && !t.name.empty()) {
            const std::string base = resolveAliasToName(t.name);
            if (structs.find(base) != structs.end()) out.push_back({base, line});
        }

        // 内联结构/联合：递归检查其字段
        if (t.hasInline) {
            for (auto& f : t.structCommon.fields)
                collectByValueDepsFromType(f.type, out, f.line ? f.line : line);
        }
    }

    // 构建包含图并用 DFS 检测环
    void checkAggregateByValueCycles() const {
        // 建图：每个结构体 A → 字段按值包含的结构体列表
        std::unordered_map<std::string, std::vector<std::pair<std::string, int>>> g;
        for (auto& kv : structs) g[kv.first] = {};

        for (auto& kv : structs) {
            const Decl* d = kv.second;
            auto& edges = g[d->name];
            for (auto& f : d->structCommon.fields) {
                collectByValueDepsFromType(f.type, edges, f.line ? f.line : d->line);
            }
        }

        // 三色 DFS 检测回边
        std::unordered_map<std::string, int> state;  // 0=未访问, 1=栈上, 2=完成
        std::vector<std::string> stack;

        auto findEdgeLine = [&](const std::string& from, const std::string& to) -> int {
            auto it = g.find(from);
            if (it == g.end()) return 0;
            for (auto& e : it->second) if (e.first == to) return e.second;
            return 0;
        };

        std::function<void(const std::string&)> dfs = [&](const std::string& u) {
            state[u] = 1;
            stack.push_back(u);

            auto it = g.find(u);
            if (it != g.end()) {
                for (auto& e : it->second) {
                    const std::string& v = e.first;
                    if (state[v] == 0) {
                        dfs(v);
                    } else if (state[v] == 1) {
                        // 回边：发现按值包含环
                        std::string cycle;
                        bool inCycle = false;
                        for (auto& n : stack) {
                            if (n == v) inCycle = true;
                            if (!inCycle) continue;
                            if (!cycle.empty()) cycle += " -> ";
                            cycle += n;
                        }
                        cycle += " -> " + v;

                        int line = e.second;
                        if (!line && stack.size() >= 2) {
                            line = findEdgeLine(stack[stack.size() - 2], stack.back());
                        }
                        if (!line) line = 1;

                        err(line, "结构/联合按值循环包含: " + cycle + "。请改为指针字段（&）打破递归包含");
                    }
                }
            }

            stack.pop_back();
            state[u] = 2;
        };

        for (auto& kv : g)
            if (state[kv.first] == 0) dfs(kv.first);
    }

    // ---- 表达式类型推导 ----
    // 遍历表达式树，推导出表达式的类型 Ty。
    // 推导失败返回 valid=false（多发生在调用未登记函数/模块时），
    // 调用方应跳过后续类型检查，避免误报。
    Ty inferExpr(const Expr& e,
                 const std::unordered_map<std::string, Ty>& locals,
                 int line) {
        switch (e.kind) {
            // -- 字面量：类型固定 ------------------------------------------------
            case Expr::IntLit:  return Ty{"i4", 0, 0, true, false};
            case Expr::FloatLit: return Ty{"f8", 0, 0, true, false};
            case Expr::StrLit:  return Ty{"char", 1, 0, true, false};  // 字符串字面量 = char*
            case Expr::CharLit: return Ty{"i1", 0, 0, true, false};
            case Expr::FncLit: return Ty{"fnc", 0, 0, true, false};  // 匿名函数字面量类型从 lhs 推断
            // -- await E：挂起等 future 就绪；结果类型擦除（void*），跳过赋值检查 --
            case Expr::Await: {
                if (e.a) (void)inferExpr(*e.a, locals, line);
                return Ty{};  // 类型擦除：由 codegen 按 LHS 声明类型强转还原
            }
            // -- async E：登记 rpc 调用进事件循环，返回 future& --
            case Expr::Async: {
                if (e.a) (void)inferExpr(*e.a, locals, line);
                return Ty{"future", 1, 0, true, false};
            }
            // -- 标识符：查找 locals → globals，nil/true/false 特殊处理 ----------
            case Expr::Ident: {
                if (e.cBridge) return Ty{};  // C 桥接 ::name：C 侧符号，类型不跟踪，跳过解析
                if (e.text == "nil") return Ty{"", 0, 0, true, true};
                if (e.text == "true" || e.text == "false") return Ty{"bool", 0, 0, true, false};
                if (e.text == "negative" || e.text == "unknown" || e.text == "positive")
                    return Ty{"tril", 0, 0, true, false};  // 三态字面量（-1/0/+1）
                if (e.text == "ok" && locals.find("ok") == locals.end()
                    && globals.find("ok") == globals.end())
                    return Ty{"ret", 0, 0, true, false};  // ADT 接口成功返回码（= 0）
                auto it = locals.find(e.text);
                if (it != locals.end()) return it->second;
                it = globals.find(e.text);
                if (it != globals.end()) return it->second;
                // 既非局部也非全局：若非已知标识符（函数/类型/枚举常量/libc/外部符号）则报未定义
                if (!lenientCalls && !isKnownIdent(e.text, locals))
                    err(e.line, "未定义的标识符 '" + e.text + "'" + hintIdent(e.text, locals));
                return Ty{};  // 已知但类型未跟踪（如 C 宏/外部符号）→ 跳过后续检查
            }
            // -- 一元运算：* 解引用 → ptr/arr-1; & 取地址 → ptr+1 ----------------
            case Expr::Unary: {
                Ty a = inferExpr(*e.a, locals, line);
                if ((e.op == "++" || e.op == "--") && e.a)
                    checkNotImmutable(*e.a, locals, e.line, "自增/自减");
                if (e.op == "*") {
                    if (a.valid && !isPointerLike(a)) err(e.line, "非法解引用：操作数不是指针/数组");
                    if (a.valid) {
                        if (a.arr > 0) a.arr--;
                        else if (a.ptr > 0) a.ptr--;
                    }
                    return a;
                }
                if (e.op == "&") {
                    if (a.valid) a.ptr++;
                    return a;
                }
                return a;  // ! ~ - + 等保持类型不变
            }
            // -- 下标：相当于 *，操作数必须是指针/数组 --------------------------
            case Expr::Index: {
                Ty a = inferExpr(*e.a, locals, line);
                // 容器下标糖：t[key,...] → find(...)，结果为元素节点类型 I&（未命中为 nil）
                if (a.valid && a.arr == 0 && a.ptr <= 1) {
                    auto ci = containerItem.find(resolveAliasToName(a.name));
                    if (ci != containerItem.end()) {
                        (void)inferExpr(*e.b, locals, line);
                        for (auto& k : e.args) (void)inferExpr(*k, locals, line);
                        return Ty{ci->second, 1, 0, true, false};
                    }
                }
                if (a.valid && !isPointerLike(a)) err(e.line, "非法下标：操作数不是指针/数组");
                if (a.valid) {
                    if (a.arr > 0) a.arr--;
                    else if (a.ptr > 0) a.ptr--;
                }
                (void)inferExpr(*e.b, locals, line);  // 下标表达式本身不参与类型推导
                return a;
            }
            // -- 成员访问：在结构体中查找字段名返回其类型 -----------------------
            case Expr::Member: {
                Ty base = inferExpr(*e.a, locals, line);
                if (!base.valid) return Ty{};
                const Decl* sd = resolveStruct(base.name);
                if (!sd) return Ty{};
                // prev/next 在链表结构体上映射为内部维护的 _prev/_next 指针字段
                std::string fn = e.text;
                if (sd->linked && (fn == "prev" || fn == "next")) fn = "_" + fn;
                for (auto& f : sd->structCommon.fields) {
                    if (f.name == fn) return fromTypeRef(f.type);
                }
                // 字段未命中：在「成员集完整可判定」的本地朴素结构体上，且非成员函数 → 报成员不存在
                if (memberCheckable(*sd, base)) {
                    auto mit = structMethods.find(sd->name);
                    const bool isMethod = mit != structMethods.end() && mit->second.count(e.text);
                    // cls 类的保留维度（CLS_ID/OBJ_KEY/OBJ_NAME/RLT_KEY/RLT_NAME）由编译器生成，
                    // 无显式成员声明，但可作为维度静态调用 o.OBJ_NAME(...)，此处放行
                    // （用户覆盖的维度已在 structMethods）。
                    const bool isReservedDim = sd->isClass &&
                        (e.text == "CLS_ID" || e.text == "OBJ_KEY" || e.text == "OBJ_NAME" ||
                         e.text == "RLT_KEY" || e.text == "RLT_NAME");
                    if (!isMethod && !isReservedDim)
                        err(e.line, std::string(sd->kind == Decl::UnionD ? "联合 '" : "结构体 '")
                            + base.name + "' 没有成员 '" + e.text + "'" + hintMember(*sd, e.text));
                }
                return Ty{};
            }
            // -- sizeof 返回 u8（size_t）---------------------------------------
            case Expr::Sizeof:
                (void)inferExpr(*e.a, locals, line);
                return Ty{"u8", 0, 0, true, false};
            // -- offsetof 返回 u8（size_t）-------------------------------------
            case Expr::Offsetof:
                return Ty{"u8", 0, 0, true, false};
            // -- 函数调用：多种内建伪调用优先，然后是普通函数调用 ----------------
            case Expr::Call: {
                // C 桥接调用 ::name(args)：C 函数/宏，跳过未定义检查；仅递归检查实参
                if (e.a && e.a->kind == Expr::Ident && e.a->cBridge) {
                    for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                    return Ty{};
                }
                // instanceOf(o, TypeName) 伪函数：第二实参是类名（非值），结果 bool
                if (e.a && e.a->kind == Expr::Ident && e.a->text == "instanceOf"
                    && !locals.count("instanceOf") && !globals.count("instanceOf")) {
                    if (e.args.size() != 2) err(e.line, "instanceOf 需要 2 个实参（对象, 类名）");
                    (void)inferExpr(*e.args[0], locals, line);   // 第一实参：object/类实例
                    if (e.args[1]->kind != Expr::Ident || !resolveStruct(e.args[1]->text))
                        err(e.line, "instanceOf 第二实参须为类（cls）名");
                    return Ty{"bool", 0, 0, true, false};
                }
                // base(x) 伪函数：等价于 *(T*)&x，结果类型为 T*+1 层指针
                if (e.a && e.a->kind == Expr::Ident && e.a->text == "base"                    && !locals.count("base") && !globals.count("base")) {
                    if (e.args.size() != 1) err(e.line, "base 需要 1 个实参");
                    if (e.args[0]->kind == Expr::Cast) {
                        Ty t = inferExpr(*e.args[0]->a, locals, line);
                        if (!t.valid) return Ty{};
                        return Ty{e.args[0]->op, e.args[0]->castPtr + 1, 0, true, false};
                    }
                    (void)inferExpr(*e.args[0], locals, line);
                    return Ty{"void", 1, 0, true, false};
                }
                // prev(x)/next(x) 伪函数：等价于链表 _prev/_next 的 base
                if (e.a && e.a->kind == Expr::Ident && (e.a->text == "prev" || e.a->text == "next")
                    && !locals.count(e.a->text) && !globals.count(e.a->text)) {
                    if (e.args.size() != 1) err(e.line, "prev/next 需要 1 个实参");
                    if (e.args[0]->kind == Expr::Cast) {
                        Ty t = inferExpr(*e.args[0]->a, locals, line);
                        if (!t.valid) return Ty{};
                        return Ty{e.args[0]->op, e.args[0]->castPtr + 1, 0, true, false};
                    }
                    (void)inferExpr(*e.args[0], locals, line);
                    return Ty{"void", 1, 0, true, false};
                }
                // T() 无参伪调用：堆分配构造糖，结果类型为 T&
                if (e.a->kind == Expr::Ident && e.args.empty()
                    && locals.find(e.a->text) == locals.end()
                    && globals.find(e.a->text) == globals.end()
                    && resolveStruct(e.a->text))
                    return Ty{resolveAliasToName(e.a->text), 1, 0, true, false};
                // stringify 格式化关键字：
                //   stringify(x) → string（单参数格式化）
                //   stringify(x, buf, n) → char&（三参数，结果写入 buf）
                if (e.a->kind == Expr::Ident && e.a->text == "stringify" && !e.args.empty()
                    && locals.find(e.a->text) == locals.end()
                    && globals.find(e.a->text) == globals.end()) {
                    for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                    if (e.args.size() == 3) return Ty{"char", 1, 0, true, false};
                    return Ty{"string", 0, 0, true, false};
                }
                // 普通函数调用：直接 name(...) 形态 → 未定义/实参检查
                if (e.a->kind == Expr::Ident) {
                    const std::string& nm = e.a->text;
                    const bool known = isKnownCallable(nm, locals);
                    if (!known && !lenientCalls)
                        err(e.line, "未定义的函数 '" + nm + "'" + hintCallable(nm, locals));
                    // 遍历实参（检查其内部表达式），收集类型供个数/类型校验
                    std::vector<Ty> ats;
                    ats.reserve(e.args.size());
                    for (auto& a : e.args) ats.push_back(inferExpr(*a, locals, line));
                    if (known) checkCallArgs(e, nm, ats, locals);
                    return Ty{};
                }
                // 间接调用（obj.m() / 函数指针表达式等）：推导被调表达式类型
                Ty callee = inferExpr(*e.a, locals, line);
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                if (callee.valid && callee.name == "v" && callee.ptr == 0 && callee.arr == 0)
                    err(e.line, "void 值不能作为表达式使用");
                // 调用结果类型未登记（语义层不跟踪函数返回类型；
                // stdin 单文件解析时 T() 的 T 也可能来自未合并依赖），
                // 返回 invalid 跳过后续检查，避免误报
                return Ty{};
            }
            // -- 后缀 ++/--：类型不变 -------------------------------------------
            case Expr::PostUnary:
                if (e.a) checkNotImmutable(*e.a, locals, e.line, "自增/自减");
                return inferExpr(*e.a, locals, line);
            // -- 三元条件 a ? b : c：取第一个有类型的边 -------------------------
            case Expr::Ternary: {
                (void)inferExpr(*e.a, locals, line);
                Ty b = inferExpr(*e.b, locals, line);
                Ty c = inferExpr(*e.c, locals, line);
                if (b.valid) return b;
                return c;
            }
            // -- 二元运算：赋值类做兼容检查并返回左值类型，其他返回左侧类型 -----
            case Expr::Binary: {
                Ty l = inferExpr(*e.a, locals, line);
                Ty r = inferExpr(*e.b, locals, line);
                if (isAssignOp(e.op)) {
                    // 赋值目标必须是左值（变量/成员/下标/解引用）
                    if (e.a) checkAssignTarget(*e.a, e.line);
                    // 不可变 let 绑定不能再赋值
                    if (e.a) checkNotImmutable(*e.a, locals, e.line, "给");
                    // 分身/切片句柄：s = 本体 / s = nil 均为语法糖，跳过常规赋值兼容检查
                    if (!l.project) checkAssignable(l, r, e.line);
                    // 整数字面量越界目标类型（仅纯赋值 =，复合赋值语义不同故不查）
                    if (e.op == "=" && e.b) checkIntLiteralRange(l, *e.b, e.line);
                    return l;
                }
                // 非法算术运算诊断（缺失操作符/类型不匹配）：
                //   两个指针/数组用 + * / %（C 中恒非法；指针求差 - 合法故已排除）
                //   取模 % 的操作数为浮点
                if (l.valid && r.valid && isArithOp(e.op)) {
                    if (isPointerLike(l) && isPointerLike(r))
                        err(e.line, "类型不匹配：两个指针/数组不能用 '" + e.op
                            + "' 运算" + fixHintPtrArith(*e.a, *e.b, e.op));
                    if (e.op == "%" && (isFloatName(l.name) || isFloatName(r.name))
                        && !isPointerLike(l) && !isPointerLike(r))
                        err(e.line, "类型不匹配：取模 '%' 的操作数必须为整数，实际为浮点");
                }
                // 除零诊断：整数字面量 0 作为 / 或 % 的除数（C 中为未定义行为）
                if ((e.op == "/" || e.op == "%") && e.b && isIntZeroLit(*e.b))
                    err(e.line, std::string("除数为零：") + (e.op == "%" ? "取模" : "除法")
                        + " '" + e.op + "' 的右操作数是字面量 0");
                return l.valid ? l : r;
            }
            // -- 类型强转 (T)x：返回声明类型 -----------------------------------
            case Expr::Cast:
                (void)inferExpr(*e.a, locals, line);
                return Ty{e.op, e.castPtr, 0, true, false};
            // -- 初始化列表 {a, b, c}：类型由赋值目标决定，此处返回 invalid -----
            case Expr::InitList:
                for (auto& a : e.args) (void)inferExpr(*a, locals, line);
                return Ty{};
        }
        return Ty{};
    }

    // ---- 赋值兼容检查 ----
    // 核心规则：
    //   - nil 只能赋给指针/数组（nil 是空指针，不能赋给标量）
    //   - 指针/数组不能赋值为非指针标量（不允许隐式地址→整数转换）
    //   - 标量不能赋值为指针/数组（不允许隐式整数→地址转换）
    void checkAssignable(const Ty& lhs, const Ty& rhs, int line) {
        if (!lhs.valid || !rhs.valid) return;
        const bool lp = isPointerLike(lhs);
        const bool rp = isPointerLike(rhs);

        if (rhs.isNil) {
            if (!lp) err(line, "nil 只能赋给指针/数组类型（目标类型为 '" + tyStr(lhs) + "'）");
            return;
        }
        if (lp && !rp)
            err(line, "类型不匹配：不能把非指针标量 '" + tyStr(rhs)
                + "' 赋给指针/数组 '" + tyStr(lhs) + "'");
        if (!lp && rp)
            err(line, "类型不匹配：不能把指针/数组 '" + tyStr(rhs)
                + "' 赋给标量 '" + tyStr(lhs) + "'");
    }

    // ---- 整数字面量越界检查 ----
    // 编译期可定的整数字面量赋给固定宽整型，若超出目标范围则报错。
    // 严格零误报：仅处理纯整数字面量（可含一元 +/-）、目标为固定宽标量整型；
    //   - 有符号目标：十进制按有符号范围 [-2^(n-1), 2^(n-1)-1]；
    //     十六进制按位模式放宽到无符号上限（0xFF→i1 = -1 等全置位惯用法合法）；
    //   - 无符号目标：范围 [0, 2^n-1]；负字面量一律放行（-1 全置位惯用法常见）。
    void checkIntLiteralRange(const Ty& lhs, const Expr& rhs, int line) {
        if (!lhs.valid || lhs.ptr > 0 || lhs.arr > 0 || lhs.fat) return;
        const int bits = intTypeBits(lhs.name);
        if (bits == 0) return;                          // 非固定宽整型 → 跳过

        // 剥离一元 +/- 前缀，记录正负
        const Expr* lit = &rhs;
        bool neg = false;
        while (lit && lit->kind == Expr::Unary &&
               (lit->op == "-" || lit->op == "+") && lit->a) {
            if (lit->op == "-") neg = !neg;
            lit = lit->a.get();
        }
        if (!lit || lit->kind != Expr::IntLit) return;  // 非整数字面量 → 跳过

        unsigned long long mag = 0; bool isHex = false;
        if (!parseIntLiteral(lit->text, mag, isHex)) return;

        const unsigned long long umax = (bits == 64) ? ~0ull : ((1ull << bits) - 1);
        const std::string what = lit->text;             // 原始字面量串（含进制/后缀）

        if (isSignedIntName(lhs.name)) {
            const unsigned long long smax = (1ull << (bits - 1)) - 1;   // 2^(n-1)-1
            const unsigned long long minMag = 1ull << (bits - 1);       // |最小负值|
            if (neg) {
                if (mag > minMag)
                    err(line, "整数字面量 -" + what + " 超出有符号类型 '" + lhs.name
                        + "' 的范围 [-" + std::to_string(minMag) + ", "
                        + std::to_string(smax) + "]");
            } else if (isHex) {
                if (mag > umax)
                    err(line, "整数字面量 " + what + " 超出类型 '" + lhs.name
                        + "' 的位宽（" + std::to_string(bits) + " 位）所能表示的范围");
            } else {
                if (mag > smax)
                    err(line, "整数字面量 " + what + " 超出有符号类型 '" + lhs.name
                        + "' 的范围 [-" + std::to_string(minMag) + ", "
                        + std::to_string(smax) + "]");
            }
        } else {
            if (neg) return;                            // 负字面量赋无符号：放行
            if (mag > umax)
                err(line, "整数字面量 " + what + " 超出无符号类型 '" + lhs.name
                    + "' 的范围 [0, " + std::to_string(umax) + "]");
        }
    }


    // ---- 未定义符号诊断辅助 ----

    // 名字是否解析为「可调用」：局部/全局（函数指针变量）、自由函数（本地/外部）、
    // 类型名（T() 构造糖）、libc 白名单、catch-all 外部描述符。
    bool isKnownCallable(const std::string& nm,
                         const std::unordered_map<std::string, Ty>& locals) const {
        if (locals.count(nm) || globals.count(nm)) return true;       // 函数指针变量
        if (funcSigs.count(nm)) return true;                          // 自由函数
        if (resolveStruct(nm)) return true;                           // T() 构造糖
        if (aliases.count(nm)) return true;                           // 别名 → 可能是构造糖
        if (libcSymbols().count(nm)) return true;                     // libc 函数
        if (builtinHeaderIdents().count(nm)) return true;             // platform.h 动态扫描的宏/函数
        if (declNames.count(nm)) return true;                         // catch-all 外部/本地声明名
        return false;
    }

    // 名字是否解析为「已知标识符」（作为值使用）：在可调用基础上再加枚举常量。
    bool isKnownIdent(const std::string& nm,
                      const std::unordered_map<std::string, Ty>& locals) const {
        if (enumConsts.count(nm)) return true;
        return isKnownCallable(nm, locals);
    }

    // 结构体是否「成员集完整可判定」（可安全报成员不存在）：
    // 排除外部模块/ C 头结构、数组整体、ADT 容器、分身/切片句柄、标签联合、含匿名内嵌成员者。
    bool memberCheckable(const Decl& sd, const Ty& base) const {
        if (sd.external) return false;                  // 外部模块/ C 头：成员集可能不全
        if (base.arr > 0) return false;                 // 数组整体不做成员检查
        if (!sd.adtColl.empty()) return false;          // ADT 容器：注入合成成员 + 内建操作糖
        if (!sd.projectSelf.empty() || !sd.projectEntity.empty()) return false;  // 分身/切片句柄
        if (sd.tagged) return false;                    // 标签联合：变体构造/解构走专门语法
        // 匿名内嵌成员（name 空）：其内部成员可被直接访问，无法据顶层字段名判定 → 跳过
        for (auto& f : sd.structCommon.fields)
            if (f.name.empty()) return false;
        return true;
    }

    // typo 近似名提示：在「可调用名」候选集合里找最接近 nm 的名字。
    // 返回形如 "，是否想用 'x'？" 的尾缀；无合适候选返回空串。
    std::string hintCallable(const std::string& nm,
                             const std::unordered_map<std::string, Ty>& locals) const {
        std::vector<std::string> cands;
        for (auto& kv : funcSigs) cands.push_back(kv.first);
        for (auto& n : declNames) cands.push_back(n);
        for (auto& kv : structs) cands.push_back(kv.first);
        for (auto& kv : locals) cands.push_back(kv.first);
        for (auto& kv : globals) cands.push_back(kv.first);
        std::string c = closestName(nm, cands);
        return c.empty() ? std::string{} : "，是否想用 '" + c + "'？";
    }

    // typo 近似名提示：标识符（值）位置，在可调用名基础上再加枚举常量候选。
    std::string hintIdent(const std::string& nm,
                          const std::unordered_map<std::string, Ty>& locals) const {
        std::vector<std::string> cands;
        for (auto& kv : funcSigs) cands.push_back(kv.first);
        for (auto& n : declNames) cands.push_back(n);
        for (auto& n : enumConsts) cands.push_back(n);
        for (auto& kv : locals) cands.push_back(kv.first);
        for (auto& kv : globals) cands.push_back(kv.first);
        std::string c = closestName(nm, cands);
        return c.empty() ? std::string{} : "，是否想用 '" + c + "'？";
    }

    // typo 近似名提示：结构体成员位置，候选为该结构体的字段名 + 成员函数名。
    std::string hintMember(const Decl& sd, const std::string& nm) const {
        std::vector<std::string> cands;
        for (auto& f : sd.structCommon.fields)
            if (!f.name.empty()) cands.push_back(f.name);
        auto mit = structMethods.find(sd.name);
        if (mit != structMethods.end())
            for (auto& m : mit->second) cands.push_back(m);
        std::string c = closestName(nm, cands);
        return c.empty() ? std::string{} : "，是否想用 '" + c + "'？";
    }

    // 若表达式是「指向不可变 let 绑定的裸标识符」，报对应的修改错误。
    // 仅对裸 Ident 生效：*p/p[i]/p.f 等通过 let 指针的间接写入仍合法。
    void checkNotImmutable(const Expr& target,
                           const std::unordered_map<std::string, Ty>& locals,
                           int line, const char* action) {
        if (target.kind != Expr::Ident) return;
        const Ty* t = nullptr;
        auto it = locals.find(target.text);
        if (it != locals.end()) t = &it->second;
        else { auto g = globals.find(target.text); if (g != globals.end()) t = &g->second; }
        if (t && t->immutable)
            err(line, std::string("不能") + action + "不可变绑定 '" + target.text
                + "'（由 let 声明的常量）");
    }

    // 赋值左侧若是「确定不可寻址」的值（字面量、运算结果等）则报错。
    // 保守策略：仅对绝无可能成为左值的种类报告；Ident/Member/Index/*解引用/Cast/Call
    // 等可能是左值或语义糖（如 base()/分身句柄）的形式一律放行，以确保零误报。
    void checkAssignTarget(const Expr& t, int line) {
        const char* what = nullptr;
        switch (t.kind) {
            case Expr::IntLit: case Expr::FloatLit:
            case Expr::StrLit: case Expr::CharLit: what = "字面量"; break;
            case Expr::Binary:   what = "二元运算结果"; break;
            case Expr::Ternary:  what = "三元表达式结果"; break;
            case Expr::PostUnary: what = "自增/自减表达式结果"; break;
            case Expr::Sizeof:   what = "sizeof 结果"; break;
            case Expr::Offsetof: what = "offsetof 结果"; break;
            case Expr::InitList: what = "初始化列表"; break;
            case Expr::FncLit:   what = "匿名函数字面量"; break;
            case Expr::Unary:
                // *p 解引用是左值；-x / !x / ~x / &x 不是
                if (t.op != "*") what = "一元运算结果";
                break;
            default: break;
        }
        if (what)
            err(line, std::string("赋值目标不是左值：不能给") + what
                + "赋值（赋值左侧必须是变量、成员、下标或解引用）");
    }

    // 还原表达式的简短源码文本，用于在诊断里给出可操作的修复补丁建议。
    // 仅覆盖能稳定还原的叶子/链式形式（标识符、成员、下标、一元）；其余返回空，
    // 调用方据此回退到通用提示，避免生成误导性的补丁。
    std::string exprBrief(const Expr& e) const {
        switch (e.kind) {
            case Expr::Ident: return e.text;
            case Expr::Member:
                if (!e.a) return "";
                { std::string b = exprBrief(*e.a); return b.empty() ? "" : b + e.op + e.text; }
            case Expr::Index:
                if (!e.a || !e.b) return "";
                { std::string a = exprBrief(*e.a), b = exprBrief(*e.b);
                  return (a.empty() || b.empty()) ? "" : a + "[" + b + "]"; }
            case Expr::Unary:
                if (!e.a) return "";
                { std::string a = exprBrief(*e.a); return a.empty() ? "" : e.op + a; }
            default:
                return "";
        }
    }

    // 为「两个指针/数组做非法算术运算」生成具体修复补丁建议。
    // 若两端都能还原为简短源码文本，给出逐元素改写补丁（解引用 / 下标）；否则回退通用提示。
    std::string fixHintPtrArith(const Expr& a, const Expr& b, const std::string& op) const {
        std::string la = exprBrief(a), rb = exprBrief(b);
        if (la.empty() || rb.empty())
            return "（缺失操作符或应先解引用/取下标？）";
        return "（缺失操作符？若想逐元素运算可改写为 '*" + la + " " + op + " *" + rb
            + "' 或 '" + la + "[i] " + op + " " + rb + "[i]'）";
    }

    // 取自由函数的规范签名 Decl（展开 fnc name -> func_type 引用）；无则返回 nullptr。
    const Decl* resolveCallSig(const Decl* d) const {
        if (!d) return nullptr;
        if (!d->funcTypeName.empty()) {
            auto it = funcTypes.find(d->funcTypeName);
            return it != funcTypes.end() ? it->second : nullptr;
        }
        return d;
    }

    // 自由函数调用的实参个数 / 类型检查（仅对签名完整、非可变参数、非 rpc 的本地函数）。
    // ats 为调用方已预先求得的各实参类型（避免重复推导）。
    void checkCallArgs(const Expr& call, const std::string& nm,
                       const std::vector<Ty>& ats,
                       const std::unordered_map<std::string, Ty>& locals) {
        // 仅当 nm 唯一解析到一个自由函数、且不是被局部/全局变量遮蔽（函数指针）时才检查
        if (locals.count(nm) || globals.count(nm)) return;
        auto fit = funcSigs.find(nm);
        if (fit == funcSigs.end()) return;
        const Decl* sig = resolveCallSig(fit->second);
        if (!sig) return;
        if (sig->external) return;                 // 外部模块函数：签名可能含 ABI 细节，跳过
        if (sig->structCommon.variadic) return;    // 可变参数：不校验个数/尾部类型
        if (sig->isRpc) return;                    // rpc：通过 run/<< 语法糖调用，参数语义特殊

        const auto& params = sig->structCommon.fields;
        const size_t np = params.size(), na = ats.size();
        // sc 允许省略尾部实参（缺参自动补 0/nil，见 feature2 §实参默认自动补 0）；
        // 故仅「实参过多」才是错误，过少合法。
        if (na > np) {
            err(call.line, "函数 '" + nm + "' 期望至多 " + std::to_string(np) +
                " 个实参（缺省自动补 0），实际传入 " + std::to_string(na) + " 个");
        }
        // 逐位实参类型兼容（沿用保守的指针/标量混用规则）
        for (size_t i = 0; i < na && i < np; i++) {
            Ty pt = fromTypeRef(params[i].type);
            checkAssignable(pt, ats[i], call.line);
        }
    }

    // ---- 逃逸分析辅助函数 ----

    // 判断表达式是否是对局部变量取地址 &localVar 或 &localVar.field
    bool isAddrOfLocalExpr(const Expr& e,
                           const std::unordered_map<std::string, Ty>& locals) const {
        if (e.kind != Expr::Unary || e.op != "&" || !e.a) return false;
        if (e.a->kind == Expr::Ident) {
            auto it = locals.find(e.a->text);
            if (it != locals.end()) return true;
        }
        if (e.a->kind == Expr::Member && e.a->a && e.a->a->kind == Expr::Ident) {
            auto it = locals.find(e.a->a->text);
            if (it != locals.end()) return true;
        }
        return false;
    }

    // 递归检查表达式树中是否包含对局部变量取地址的操作
    bool containsAddrOfLocal(const Expr& e,
                             const std::unordered_map<std::string, Ty>& locals) const {
        if (isAddrOfLocalExpr(e, locals)) return true;
        if (e.a && containsAddrOfLocal(*e.a, locals)) return true;
        if (e.b && containsAddrOfLocal(*e.b, locals)) return true;
        if (e.c && containsAddrOfLocal(*e.c, locals)) return true;
        for (auto& x : e.args) if (x && containsAddrOfLocal(*x, locals)) return true;
        return false;
    }

    // 判断表达式根是否为全局变量（*p, a[i], p->f 递归查看根，&x 也穿透）
    bool rootedAtGlobal(const Expr& e,
                        const std::unordered_map<std::string, Ty>& locals) const {
        if (e.kind == Expr::Ident) {
            if (locals.find(e.text) != locals.end()) return false;
            return globals.find(e.text) != globals.end();
        }
        if ((e.kind == Expr::Member || e.kind == Expr::Index) && e.a)
            return rootedAtGlobal(*e.a, locals);
        if (e.kind == Expr::Unary && e.a && (e.op == "*" || e.op == "&"))
            return rootedAtGlobal(*e.a, locals);
        return false;
    }

    // ---- 变量声明 ----

    // 确定变量声明的类型：优先用显式声明的类型，否则从初值的字面量推导。
    // 无类型且无初值无法推断 → 报错（强制显式类型，与主流语言一致）。
    Ty declaredOrInferredType(const Field& f,
                              const std::unordered_map<std::string, Ty>& locals) {
        const bool declared = f.type.hasInline || !f.type.name.empty() ||
                              f.type.ptr > 0 || !f.type.arrayDims.empty() ||
                              f.type.fnKind != TypeRef::FncKind::None;
        if (declared) return fromTypeRef(f.type);
        if (!f.init)
            err(f.line, "变量缺少类型：无类型且无初值无法推断，请显式声明类型（如 var x: i4）");

        Ty t = inferExpr(*f.init, locals, f.line);
        if (t.isNil) err(f.line, "nil 不能用于无类型推断，请显式声明指针类型");
        return t;
    }

    // var/let/tls 多变量声明：逐项推导类型、检查初值兼容、登记到 locals
    void checkVarDecls(const std::vector<Field>& ds,
                       std::unordered_map<std::string, Ty>& locals,
                       bool isLet = false) {
        for (auto& f : ds) {
            checkTypeName(f.type, f.line);   // 校验显式声明的类型名（void 值类型 / 未定义类型）
            Ty lhs = declaredOrInferredType(f, locals);
            if (f.init) {
                Ty rhs = inferExpr(*f.init, locals, f.line);
                checkAssignable(lhs, rhs, f.line);
                // 仅当有显式目标类型时查越界：无类型声明的 lhs 由字面量自身推断
                // （含后缀/大值），按构造必然容纳，且 inferExpr 对字面量统一记 i4
                // 会误判，故跳过。
                const bool declared = f.type.hasInline || !f.type.name.empty() ||
                                      f.type.ptr > 0 || !f.type.arrayDims.empty() ||
                                      f.type.fnKind != TypeRef::FncKind::None;
                if (declared) checkIntLiteralRange(lhs, *f.init, f.line);
            }
            lhs.immutable = isLet;            // let 绑定标记为不可变
            locals[f.name] = lhs;
        }
    }

    // ---- 函数返回类型 ----
    // 返回类型省略 = void（内部标记 "v"，用于检查 return 语句兼容性）。
    // 引用的函数类型在依赖未合并时查不到 → 返回 invalid，调用方跳过检查。
    Ty funcRetType(const Decl& d) const {
        // -> func_type 引用：从 funcTypes 表展开返回类型
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it == funcTypes.end()) return Ty{};  // 未合并依赖，跳过检查
            const auto& rt = it->second->structCommon.type;
            if (!rt) return Ty{"v", 0, 0, true, false};
            Ty t = fromTypeRef(*rt);
            if (!t.valid || (t.name.empty() && t.ptr == 0 && t.arr == 0))
                return Ty{"v", 0, 0, true, false};
            return t;
        }
        // 直接声明的返回类型
        const auto& rt = d.structCommon.type;
        if (!rt) return Ty{"v", 0, 0, true, false};
        Ty t = fromTypeRef(*rt);
        if (!t.valid || (t.name.empty() && t.ptr == 0 && t.arr == 0))
            return Ty{"v", 0, 0, true, false};
        return t;
    }

    // ---- 语句遍历 ----
    // 对一条语句及其子语句做语义检查。locals 传递当前作用域的局部变量表，
    // if/while/for/case 的分支在 locals 副本上检查（不影响外层）。
    void checkStmt(const Stmt& s,
                   std::unordered_map<std::string, Ty>& locals,
                   const Ty& retTy) {
        switch (s.kind) {
            // -- 表达式语句：赋值时检查逃逸（禁止局部地址泄露到全局存储）--------
            case Stmt::ExprS:
                if (s.expr && s.expr->kind == Expr::Binary && isAssignOp(s.expr->op)) {
                    if (containsAddrOfLocal(*s.expr->b, locals) &&
                        rootedAtGlobal(*s.expr->a, locals)) {
                        err(s.line, "禁止将局部变量地址写入全局存储");
                    }
                }
                (void)inferExpr(*s.expr, locals, s.line);
                break;
            // -- 变量/常量/线程局部声明 ----------------------------------------
            case Stmt::VarS:
            case Stmt::LetS:
            case Stmt::TlsS:
                checkVarDecls(s.decls, locals, s.kind == Stmt::LetS);
                break;
            // -- return：检查返回类型兼容 + 禁止返回局部地址 --------------------
            case Stmt::ReturnS:
                if (s.expr) {
                    if (retTy.valid && retTy.name == "v" && retTy.ptr == 0)
                        err(s.line, "无返回值函数不能 return 表达式（返回类型省略即 void）");
                    if (containsAddrOfLocal(*s.expr, locals))
                        err(s.line, "禁止返回局部变量地址");
                    Ty rt = inferExpr(*s.expr, locals, s.line);
                    checkAssignable(retTy, rt, s.line);
                }
                break;
            // -- if/else：条件检查，两个分支独立作用域 --------------------------
            case Stmt::IfS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals, b = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                for (auto& x : s.elseBody) checkStmt(*x, b, retTy);
                break;
            }
            // -- ret 调用语法糖：登记函数级 $（ret），检查被调用表达式与体 -------
            case Stmt::RetCallS: {
                locals["$"] = Ty{"ret", 0, 0, true, false};   // $ 为 ret 类型结果变量
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- while：条件检查，体在独立作用域 ---------------------------------
            case Stmt::WhileS: {
                (void)inferExpr(*s.expr, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- do-while：先检查体，再检查条件（条件可引用体中声明的变量）-------
            case Stmt::DoWhileS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                (void)inferExpr(*s.expr, a, s.line);
                break;
            }
            // -- for：init/cond/step 三段，体在独立作用域 ------------------------
            case Stmt::ForS: {
                if (s.forIn) {
                    // for-in：推断集合/范围/选项，登记循环变量类型，再检查体
                    Ty coll;
                    if (s.forIsRange) {
                        if (s.forRangeLo) (void)inferExpr(*s.forRangeLo, locals, s.line);
                        if (s.forRangeHi) (void)inferExpr(*s.forRangeHi, locals, s.line);
                    } else if (s.forColl) {
                        coll = inferExpr(*s.forColl, locals, s.line);
                    }
                    if (s.forStepE)   (void)inferExpr(*s.forStepE, locals, s.line);
                    if (s.forOffsetE) (void)inferExpr(*s.forOffsetE, locals, s.line);
                    if (s.forNumE)    (void)inferExpr(*s.forNumE, locals, s.line);
                    // 循环变量类型：显式注解优先，否则按集合元素类型推断
                    Ty vt;
                    if (s.forVarHasType) {
                        vt = fromTypeRef(s.forVarType);
                    } else if (s.forIsRange) {
                        vt = Ty{"i4", 0, 0, true, false};
                    } else if (coll.valid) {
                        const std::string cn = resolveAliasToName(coll.name);
                        auto ci = containerItem.find(cn);
                        if (coll.arr > 0)                       vt = Ty{coll.name, coll.ptr, 0, true, false};
                        else if (ci != containerItem.end())     vt = Ty{ci->second, 1, 0, true, false};
                        else if (cn == "chain")                 vt = Ty{"", 1, 0, true, false};
                        else if (cn == "char")                  vt = Ty{"char", 0, 0, true, false};
                        else                                    vt = Ty{"i4", 0, 0, true, false};
                    }
                    auto a = locals;
                    if (!s.forVar.empty() && vt.valid) a[s.forVar] = vt;
                    for (auto& iv : s.forIdxVars)               // 索引/坐标变量：整型计数
                        if (!iv.empty()) a[iv] = Ty{"i4", 0, 0, true, false};
                    for (auto& x : s.body) checkStmt(*x, a, retTy);
                    break;
                }
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line);
                if (s.forCond) (void)inferExpr(*s.forCond, locals, s.line);
                if (s.forStep) (void)inferExpr(*s.forStep, locals, s.line);
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- case 多分支：每个 arm 独立作用域 -------------------------------
            case Stmt::CaseS: {
                Ty st = inferExpr(*s.expr, locals, s.line);
                // 标签联合解构：变体名标签不当变量推断；Variant as x 绑定载荷类型
                const Decl* tu = (st.valid && st.ptr == 0 && st.arr == 0)
                                 ? resolveStruct(st.name) : nullptr;
                if (tu && tu->kind == Decl::UnionD && tu->tagged) {
                    // 穷尽性检查：无 default 分支时必须覆盖全部变体（缺失即报错）。
                    // 零误报前提：case 表达式已确定解析到标签联合；default 分支（labels
                    // 为空）存在即视为穷尽；逐 arm 收集 Ident 标签为「已覆盖变体」。
                    bool hasDefault = false;
                    std::unordered_set<std::string> covered;
                    for (auto& arm : s.caseArms) {
                        if (arm.labels.empty()) { hasDefault = true; continue; }
                        for (auto& lab : arm.labels)
                            if (lab->kind == Expr::Ident) covered.insert(lab->text);
                    }
                    if (!hasDefault) {
                        std::string missing;
                        for (auto& f : tu->structCommon.fields)
                            if (!covered.count(f.name)) {
                                if (!missing.empty()) missing += ", ";
                                missing += f.name;
                            }
                        if (!missing.empty())
                            err(s.line, "标签联合 '" + tu->name
                                + "' 的 case 未覆盖全部变体（缺少 " + missing
                                + "），请补充对应分支或添加 default ':' 分支");
                    }
                    for (auto& arm : s.caseArms) {
                        auto a = locals;
                        if (!arm.binding.empty() && !arm.labels.empty()
                            && arm.labels[0]->kind == Expr::Ident) {
                            for (auto& f : tu->structCommon.fields)
                                if (f.name == arm.labels[0]->text) {
                                    a[arm.binding] = fromTypeRef(f.type);
                                    break;
                                }
                        }
                        for (auto& x : arm.body) checkStmt(*x, a, retTy);
                    }
                    break;
                }
                for (auto& arm : s.caseArms) {
                    auto a = locals;
                    for (auto& lab : arm.labels) (void)inferExpr(*lab, a, s.line);
                    for (auto& x : arm.body) checkStmt(*x, a, retTy);
                }
                break;
            }
            // -- goto / label：无额外类型检查 ----------------------------------
            case Stmt::GotoS:
                break;
            case Stmt::LabelS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- break / continue：无类型数据 ----------------------------------
            case Stmt::BreakS:
            case Stmt::ContinueS:
                break;
            // -- def（内嵌类型声明）：无需检查 ---------------------------------
            case Stmt::DeclS:
                break;
            // -- mix（宏展开）：实参为宏形参/C 名，无 sc 类型，跳过检查 -----------
            case Stmt::MixS:
                break;
            // -- final 域退出钩子：体在独立作用域检查 ---------------------------
            case Stmt::FinalS: {
                auto a = locals;
                for (auto& x : s.body) checkStmt(*x, a, retTy);
                break;
            }
            // -- run：rpc 线程创建调用的实参与 thread 出参 ----------------------
            case Stmt::RunS:
                (void)inferExpr(*s.expr, locals, s.line);
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line);
                break;
            // -- done：future + 可选结果（结果在 codegen 自动 void* 擦除） --------
            case Stmt::DoneS:
                (void)inferExpr(*s.expr, locals, s.line);              // future
                if (s.forInit) (void)inferExpr(*s.forInit, locals, s.line); // 结果
                break;
            // -- print：逐项检查实参表达式（格式覆盖 Cast 解包到被格式化的子表达式）
            case Stmt::PrintS:
                for (auto& a : s.printArgs) {
                    const Expr* v = (a->kind == Expr::Cast && a->castIsFmt) ? a->a.get() : a.get();
                    (void)inferExpr(*v, locals, s.line);
                }
                break;
            // -- assert：检查断言表达式与可选消息表达式 --------------------------
            case Stmt::AssertS:
                (void)inferExpr(*s.expr, locals, s.line);
                if (s.assertMsg) (void)inferExpr(*s.assertMsg, locals, s.line);
                break;
        }
    }

    // ---- 宏展开：顶层 mix 声明登记（通用能力，不针对任何特定宏） --------------
    //
    // 宏体不在 sc 语义层展开（仅作为 C #define 透传），故宏体里声明的符号本不可见。
    // 顶层 mix 实例化一个宏时，这里做一次轻量展开（形参替换 + `\` 粘贴解析，并递归
    // 处理宏体内的嵌套 mix），把宏体声明出的全局符号（var/let/tls、def 内嵌类型、
    // fnc/rpc 函数）登记进语义层，使下游代码可直接引用，无需手写 let X:: 认领。

    // 渲染宏实参为单个 token 文本：标识符若在形参表中则替换为绑定值，否则取其原文；
    // 字面量取其拼写文本。仅用于推导宏体里 `\` 粘贴出的声明名。
    std::string renderMacroArg(const Expr& e,
                               const std::unordered_map<std::string, std::string>& pm) const {
        if (e.kind == Expr::Ident) {
            auto it = pm.find(e.text);
            return it != pm.end() ? it->second : e.text;
        }
        return e.text;   // IntLit/StrLit/CharLit 等：text 持有拼写
    }

    // 解析宏体声明名里的 `\` 粘贴：按 '\\' 切分，逐段在形参表中替换后拼接。
    // 例：raw="ARGS_\\name", pm={name:verbose} → "ARGS_verbose"；
    //     raw="get_\\fld",   pm={fld:x}        → "get_x"。
    std::string substMacroSpelling(const std::string& raw,
                                   const std::unordered_map<std::string, std::string>& pm) const {
        std::string out, seg;
        auto flush = [&](const std::string& s) {
            auto it = pm.find(s);
            out += (it != pm.end()) ? it->second : s;
        };
        for (char ch : raw) {
            if (ch == '\\') { flush(seg); seg.clear(); }
            else seg += ch;
        }
        flush(seg);
        return out;
    }

    // 登记宏体内 def 内嵌类型 / fnc / rpc 声明出的符号（名字经 `\` 粘贴替换）。
    void registerMixDecl(const Decl& d, const std::unordered_map<std::string, std::string>& pm) {
        std::string nm = substMacroSpelling(d.name, pm);
        if (nm.empty()) return;
        switch (d.kind) {
            case Decl::FuncD:
            case Decl::FuncTypeD:
                if (!d.methodOwner.empty())
                    structMethods[d.methodOwner].insert(
                        d.methodName.empty() ? nm : d.methodName);
                else
                    funcSigs.emplace(nm, &d);
                declNames.insert(nm);
                break;
            case Decl::EnumD:
                declNames.insert(nm);
                for (auto& it : d.structCommon.fields)
                    if (!it.name.empty()) enumConsts.insert(substMacroSpelling(it.name, pm));
                break;
            case Decl::MacroD:
                macros[nm] = &d;       // 宏定义出的宏：纳入索引，供后续 mix 展开
                break;
            default:
                declNames.insert(nm);  // struct/union/alias 等：catch-all 放宽未定义诊断
                break;
        }
    }

    // 展开顶层 mix（及宏体内嵌套 mix），仅登记宏体声明出的符号名与可解析类型。
    // depth 限深防止宏相互递归导致死循环。
    void expandMixDecls(const std::string& macroName,
                        const std::vector<std::string>& argToks, int depth) {
        if (depth > 64) return;
        auto mit = macros.find(macroName);
        if (mit == macros.end()) return;
        const Decl* m = mit->second;

        // 形参 → 实参 token 绑定（泛型宏：先类型参数 <T,...>，再文本名参数）
        std::unordered_map<std::string, std::string> pm;
        size_t ai = 0;
        for (const auto& tp : m->macroTypeParams)
            if (ai < argToks.size()) pm[tp] = argToks[ai++];
        const auto& params = m->structCommon.fields;
        for (size_t i = 0; i < params.size() && ai < argToks.size(); i++, ai++)
            if (!params[i].name.empty()) pm[params[i].name] = argToks[ai];

        for (auto& s : m->body) {
            switch (s->kind) {
                case Stmt::VarS:
                case Stmt::LetS:
                case Stmt::TlsS:
                    for (auto& f : s->decls) {
                        std::string nm = substMacroSpelling(f.name, pm);
                        if (nm.empty()) continue;
                        declNames.insert(nm);
                        Ty t = fromTypeRef(f.type);
                        if (t.valid && !(t.name.empty() && t.ptr == 0 && t.arr == 0))
                            globals.emplace(nm, t);
                    }
                    break;
                case Stmt::DeclS:
                    if (s->decl) registerMixDecl(*s->decl, pm);
                    break;
                case Stmt::MixS: {
                    if (!s->expr || s->expr->kind != Expr::Call) break;
                    const Expr& c = *s->expr;
                    if (!c.a || c.a->kind != Expr::Ident) break;
                    std::vector<std::string> sub;
                    sub.reserve(c.args.size());
                    for (auto& a : c.args)
                        sub.push_back(a ? renderMacroArg(*a, pm) : std::string{});
                    expandMixDecls(c.a->text, sub, depth + 1);
                    break;
                }
                default: break;
            }
        }
    }

    // ---- 顶层遍历：收集符号 + 检查全局变量初值 -------------------------------
    void collectTop() {

        // 第一遍：登记函数类型、结构体、别名到符号表
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                structs[d->name] = d.get();
            if (d->kind == Decl::AliasD)
                aliases[d->name] = d->structCommon.type->name;
        }

        // 容器映射 C → I（def T: <C, I>）：下标糖 t[key] 推导 find 结果类型 I&
        for (auto& d : prog.decls)
            if (d->kind == Decl::StructD && !d->adtColl.empty())
                containerItem[resolveAliasToName(d->adtColl)] = d->adtItem;

        // 宏定义索引：供顶层 mix 展开登记其声明出的符号（见 collectTop 末尾）
        for (auto& d : prog.decls)
            if (d->kind == Decl::MacroD)
                macros[d->name] = d.get();

        // 未定义符号诊断：登记可调用名/枚举常量/成员函数/catch-all 声明名，并计算放宽门控
        for (auto& d : prog.decls) {
            switch (d->kind) {
                case Decl::FuncD:
                case Decl::FuncTypeD:
                    if (!d->methodOwner.empty())               // 成员函数：按 obj.m() 调用，单列
                        structMethods[d->methodOwner].insert(
                            d->methodName.empty() ? d->name : d->methodName);
                    else
                        funcSigs.emplace(d->name, d.get());    // 自由函数：本地优先（emplace 不覆盖已有）
                    declNames.insert(d->name);
                    break;
                case Decl::EnumD:
                    declNames.insert(d->name);
                    for (auto& it : d->structCommon.fields)
                        if (!it.name.empty()) enumConsts.insert(it.name);
                    break;
                case Decl::IncD: {
                    if (endsWithSc(d->name)) break;            // .sc 模块：描述符已合并，跳过
                    std::string bare = d->name;
                    if (!bare.empty() && (bare.front() == '<' || bare.front() == '"'))
                        bare = bare.substr(1, bare.size() >= 2 ? bare.size() - 2 : 0);
                    // 非预置头且未被 libclang 枚举 → 其符号未知，放宽未定义检查
                    if (!preludeHeaders().count(bare) && !d->externAnalyzed)
                        lenientCalls = true;
                    break;
                }
                case Decl::AddD:
                    lenientCalls = true;                       // add 外部实现/库：可能引入未声明的 C 符号
                    break;
                default:
                    if (!d->name.empty()) declNames.insert(d->name);
                    break;
            }
        }

        // 顶层 mix 展开：把宏体里 var/let/tls 声明出的全局符号登记进语义层（宏的通用能力，
        // 不针对任何特定宏）。宏展开不在 sc 语义层执行，故这些符号本不可见；这里做一次轻量
        // 展开（形参替换 + `\` 粘贴解析，递归处理宏体内的嵌套 mix），仅收集「声明名及可解析
        // 的类型」，使下游代码可直接引用，无需手写 let X:: 认领。
        for (auto& d : prog.decls) {
            if (d->kind != Decl::MixD || !d->expr) continue;
            if (d->macroConsumed) continue;     // 泛型 mix 已单态化为具体声明，无需轻量登记
            const Expr& c = *d->expr;
            if (c.kind != Expr::Call || !c.a || c.a->kind != Expr::Ident) continue;
            std::unordered_map<std::string, std::string> none;
            std::vector<std::string> toks;
            toks.reserve(c.args.size());
            for (auto& a : c.args) toks.push_back(a ? renderMacroArg(*a, none) : std::string{});
            expandMixDecls(c.a->text, toks, 0);
        }

        // 第二遍：检查全局 var/let/tls 声明
        for (auto& d : prog.decls) {            
            if (d->kind != Decl::VarD && d->kind != Decl::LetD && d->kind != Decl::TlsD)
                continue;

            for (auto& f : d->structCommon.fields) {

                // 优先使用显式声明的类型，否则从初值推导或默认 char*
                Ty lhs = fromTypeRef(f.type);
                if (!lhs.valid || (lhs.name.empty() && lhs.ptr == 0 && lhs.arr == 0)) {
                    lhs = f.init ? inferExpr(*f.init, globals, f.line)
                                 : Ty{"char", 1, 0, true, false};
                }
                if (f.init) {
                    Ty rhs = inferExpr(*f.init, globals, f.line);
                    checkAssignable(lhs, rhs, f.line);
                }
                lhs.immutable = (d->kind == Decl::LetD);   // 全局 let 常量不可再赋值
                globals[f.name] = lhs;
            }
        }
    }

    // ---- 遍历所有函数体 ---------------------------------
    void checkFunctions() {

        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD) continue;

            std::unordered_map<std::string, Ty> locals;

            // 建立函数的形参符号表（含 -> func_type 展开）
            const Decl* sig = d.get();
            if (!d->funcTypeName.empty()) {
                auto it = funcTypes.find(d->funcTypeName);
                if (it != funcTypes.end()) sig = it->second;
            }
            for (auto& p : sig->structCommon.fields)
                locals[p.name] = fromTypeRef(p.type);

            // com 通道末参校验（Q5）：rpc 的 com& 参数（设备通讯端点）必须位于参数表末位，
            // 以便后续 << / >> 通讯语法糖将 com 通道隐式补足为最末实参。
            // 仅约束 rpc：MethodPtr 实现（read/write/error）是 fnc，其 _this: com& 为首参接收者，不在此列。
            if (d->isRpc) {
                const auto& ps = sig->structCommon.fields;
                for (size_t i = 0; i + 1 < ps.size(); i++) {
                    if (ps[i].type.fnKind == TypeRef::FncKind::None &&
                        ps[i].type.ptr >= 1 &&
                        resolveAliasToName(ps[i].type.name) == "com")
                        err(ps[i].line ? ps[i].line : d->line,
                            "com& 通讯通道参数 '" + ps[i].name +
                            "' 必须位于 rpc '" + d->name + "' 的参数表末位");
                }
            }
            // 方法：隐式 this 参数
            if (!d->methodOwner.empty()) {
                locals["this"] = Ty{d->methodOwner, 1, 0, true, false};
                // 分身/切片类型的方法：额外提供 self 上下文（指向本体实体 T）
                auto so = structs.find(d->methodOwner);
                if (so != structs.end() && !so->second->projectEntity.empty())
                    locals["self"] = Ty{so->second->projectEntity, 1, 0, true, false};
            }

            Ty ret = funcRetType(*d);
            for (auto& s : d->body) checkStmt(*s, locals, ret);
        }

        // tst 测试用例体：以空局部环境 + void 返回类型做语义检查（同普通 void 函数体）。
        for (auto& d : prog.decls) {
            if (d->kind != Decl::TestD) continue;
            std::unordered_map<std::string, Ty> locals;
            const Ty voidRet{"v", 0, 0, true, false};
            for (auto& s : d->body) checkStmt(*s, locals, voidRet);
        }
    }

    // ---- 自动指针 T@ 边界检查（§13.5）----
    // T@ 数组：局部（一维/多维）已实现（元素逐个根边、退域逐元素嵌套清理、下标赋值记账）→ 放行；
    // 非局部（字段/全局/参数/返回）仍未实现引用图清理 → 报错（静默会泄露）。
    void checkFatTypeRef(const TypeRef& t, int line, const char* where,
                         bool allowLocalArray = false) const {
        if (t.fat && !t.arrayDims.empty()) {
            if (!allowLocalArray)
                err(line, std::string("暂不支持 T@ 数组（") + where +
                    "）：该位置的引用图清理与下标赋值记账尚未实现；"
                    "如需指针数组请用裸指针 T& 数组，或改用局部 T@ 数组");
        }
        // 内联结构/联合字段递归
        if (t.hasInline)
            for (auto& f : t.structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : line, "内联字段");
    }

    // 类型按值是否内嵌自动指针 T@（直接 T@ 字段，或按值聚合/内联结构递归含 T@）。
    // 用于跨 C ABI 守卫：含 T@ 的结构体按值跨边界（C 侧/传输）会破坏胖指针引用图与 ARC。
    bool typeEmbedsFat(const TypeRef& t, std::unordered_set<std::string>& visited) const {
        if (t.fnKind != TypeRef::FncKind::None) return false;   // 函数指针固定大小，不内嵌
        if (t.fat) return true;                                 // T@ 字段：内嵌 sc_fat
        if (t.hasInline)
            for (auto& f : t.structCommon.fields)
                if (typeEmbedsFat(f.type, visited)) return true;
        // 按值聚合（非指针/非数组）：递归其字段
        if (t.ptr == 0 && t.arrayDims.empty() && !t.name.empty()) {
            const std::string base = resolveAliasToName(t.name);
            if (!visited.insert(base).second) return false;     // 环：已访问
            const Decl* sd = resolveStruct(base);
            if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD))
                for (auto& f : sd->structCommon.fields)
                    if (typeEmbedsFat(f.type, visited)) return true;
        }
        return false;
    }

    // 跨 C ABI 守卫：检查导出/rpc/C 实现函数的单个参数/返回类型。
    //   · 直接 T@：仅 rpc / cImpl 拒绝（导出 @fnc 允许以 T@ 移交所有权）。
    //   · 按值结构体内嵌 T@：导出 / rpc / cImpl 一律拒绝（C 侧无法维护 ARC）。
    void checkAbiFatType(const TypeRef& t, int line, const Decl& fn, const char* slot) const {
        const bool transport = fn.isRpc || fn.cImpl;   // 跨传输 / 跨 C 实现
        if (t.fat) {
            if (transport)
                err(line, std::string(fn.isRpc ? "rpc '" : "C 实现接口 '") + fn.name +
                    "' 的" + slot + "为自动指针 T@：跨传输/跨 C ABI 无法维护胖指针引用图与 ARC，"
                    "请改用裸指针 T& 或值类型");
            return;   // 导出 @fnc 直接 T@：允许（所有权移交）
        }
        if (t.ptr == 0 && t.arrayDims.empty()) {
            std::unordered_set<std::string> visited;
            if (typeEmbedsFat(t, visited))
                err(line, std::string("'") + fn.name + "' 的" + slot +
                    "（按值聚合 '" + t.name + "'）内嵌自动指针 T@ 成员：不能跨 C ABI 传递"
                    "（C 侧无法维护胖指针 ARC），请改用裸指针 T& 或移除该成员后传递");
        }
    }

    // 对一个导出/rpc/cImpl 函数声明做参数与返回类型的跨 C ABI 守卫。
    void checkAbiFatFn(const Decl& d) const {
        for (auto& p : d.structCommon.fields)
            checkAbiFatType(p.type, p.line ? p.line : d.line, d, ("参数 '" + p.name + "'").c_str());
        if (d.structCommon.type)
            checkAbiFatType(*d.structCommon.type, d.line, d, "返回类型");
    }


    void checkFatBoundariesStmt(const Stmt& s) const {
        for (auto& d : s.decls)
            checkFatTypeRef(d.type, d.line ? d.line : s.line, "局部变量/常量",
                            /*allowLocalArray*/ true);
        for (auto& b : s.body) checkFatBoundariesStmt(*b);
        for (auto& b : s.elseBody) checkFatBoundariesStmt(*b);
        for (auto& arm : s.caseArms)
            for (auto& b : arm.body) checkFatBoundariesStmt(*b);
        if (s.decl)
            for (auto& f : s.decl->structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : s.line, "局部类型字段");
    }

    void checkFatBoundaries() const {
        for (auto& d : prog.decls) {
            // 结构/联合字段 + 全局变量类型 + 函数参数/返回类型
            for (auto& f : d->structCommon.fields)
                checkFatTypeRef(f.type, f.line ? f.line : d->line,
                                d->kind == Decl::FuncD ? "函数参数" : "结构字段/全局变量");
            if (d->structCommon.type)
                checkFatTypeRef(*d->structCommon.type, d->line, "返回类型");
            if (d->kind == Decl::FuncD)
                for (auto& s : d->body) checkFatBoundariesStmt(*s);
            // 跨 C ABI 守卫：导出 / rpc / C 实现函数的参数/返回不得跨边界携带 T@（详见 §18）
            if ((d->kind == Decl::FuncD || d->kind == Decl::FuncTypeD)
                && (d->exported || d->isRpc || d->cImpl))
                checkAbiFatFn(*d);
        }
    }

    // ---- 同名顶层符号重复定义检测 ---------------------------
    // sc 无用户级前向声明（定义顺序无关，C 前置声明由编译器自动生成），故非外部的
    // 同名定义即真冲突。保守起见仅在「确定的完整定义」类别内、且两者均非外部时报错：
    //   类型（struct/union/enum/alias）/ 自由函数 / 成员函数（按 owner::method）/ 全局量。
    void checkDuplicateDefs() {
        std::unordered_map<std::string, int> types, funcs, methods, gvars;

        auto report = [&](std::unordered_map<std::string, int>& seen,
                          const std::string& key, const std::string& what,
                          const std::string& disp, int line) {
            auto it = seen.find(key);
            if (it != seen.end())
                err(line, "重复定义" + what + " '" + disp + "'（已在第 "
                    + std::to_string(it->second) + " 行定义）");
            else
                seen[key] = line;
        };

        for (auto& d : prog.decls) {
            if (d->external) continue;                 // 外部模块/ C 头合并符号：跳过
            switch (d->kind) {
                case Decl::StructD:
                case Decl::UnionD:
                case Decl::EnumD:
                case Decl::AliasD:
                    if (!d->name.empty())
                        report(types, d->name, "类型", d->name, d->line);
                    break;
                case Decl::FuncD:
                    if (d->name.empty() && d->methodName.empty()) break;
                    if (!d->methodOwner.empty()) {
                        std::string mn = d->methodName.empty() ? d->name : d->methodName;
                        report(methods, d->methodOwner + "::" + mn, "成员函数",
                               d->methodOwner + "." + mn, d->line);
                    } else {
                        report(funcs, d->name, "函数", d->name, d->line);
                    }
                    break;
                case Decl::VarD:
                case Decl::LetD:
                case Decl::TlsD:
                    for (auto& f : d->structCommon.fields)
                        if (!f.name.empty())
                            report(gvars, f.name, "全局变量/常量", f.name,
                                   f.line ? f.line : d->line);
                    break;
                default:
                    break;
            }
        }
    }

    // ---- 死代码（不可达语句）检测 ---------------------------
    // 在一个语句块内，return/break/continue/goto 之后、下一个标签之前的语句不可达。
    // 保守策略（宁可漏报不可误报）：只把这四种「确定终止」语句视为终止符；
    // if/while 等复合语句即便所有分支都终止也不视为终止符。标签语句重置可达（潜在 goto 目标）。
    static bool isTerminatorStmt(const Stmt& s) {
        return s.kind == Stmt::ReturnS || s.kind == Stmt::BreakS
            || s.kind == Stmt::ContinueS || s.kind == Stmt::GotoS;
    }

    void checkDeadCodeChildren(const Stmt& s) {
        checkDeadCodeBlock(s.body);
        checkDeadCodeBlock(s.elseBody);
        for (auto& arm : s.caseArms) checkDeadCodeBlock(arm.body);
    }

    void checkDeadCodeBlock(const std::vector<StmtPtr>& body) {
        bool reachable = true;
        for (auto& sp : body) {
            const Stmt& s = *sp;
            if (!reachable) {
                if (s.kind == Stmt::LabelS) {
                    reachable = true;                  // 标签：潜在 goto 目标，恢复可达
                } else if (s.kind == Stmt::DeclS) {
                    continue;                          // 内嵌类型定义不产生执行代码，跳过
                } else {
                    err(s.line, "不可达代码：此语句位于 return/break/continue/goto 之后");
                    break;                             // 每块仅报首条，避免连环误导
                }
            }
            checkDeadCodeChildren(s);
            if (isTerminatorStmt(s)) reachable = false;
        }
    }

    void checkDeadCode() {
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD)
                checkDeadCodeBlock(d->body);
    }

    // ---- 非 void 函数缺少 return 检测 -------------------------
    // 报错条件：非 void 函数体不能「保证终止」（可能从结尾自然落出而无返回值）。
    // 保守策略（零误报优先）：blockTerminates/stmtTerminates 宽松地认定「会终止」——
    // 只要存在任何无法排除的终止路径就视为已终止，从而绝不对合法代码误报；
    // 代价是可能漏报部分确有缺失的路径（可接受）。

    // 调用是否为「确定不返回」的 libc 终止函数（abort/exit/_Exit/quick_exit/longjmp）。
    static bool isNoreturnCall(const Expr& e) {
        if (e.kind != Expr::Call || !e.a || e.a->kind != Expr::Ident) return false;
        const std::string& n = e.a->text;
        return n == "abort" || n == "exit" || n == "_Exit" || n == "_exit"
            || n == "quick_exit" || n == "longjmp" || n == "siglongjmp";
    }

    // 表达式是否为恒真常量（while/for 无限循环条件）。
    static bool exprIsTrueConst(const Expr& e) {
        if (e.kind == Expr::Ident) return e.text == "true";
        if (e.kind == Expr::IntLit) {
            const char* p = e.text.c_str();
            return std::strtoll(p, nullptr, 0) != 0;
        }
        return false;
    }

    // 块内是否存在「跳出本层循环」的 break（不下钻到内层循环/switch，那里的 break 另有所属）。
    bool loopHasBreak(const std::vector<StmtPtr>& body) const {
        for (auto& sp : body) {
            const Stmt& s = *sp;
            switch (s.kind) {
                case Stmt::BreakS: return true;
                case Stmt::IfS:
                    if (loopHasBreak(s.body) || loopHasBreak(s.elseBody)) return true;
                    break;
                case Stmt::LabelS: case Stmt::FinalS:
                    if (loopHasBreak(s.body)) return true;
                    break;
                // 内层 while/do/for/case 自带 break 作用域，不计入本层
                default: break;
            }
        }
        return false;
    }

    // 执行该语句是否「保证不落到下一条语句」（return/goto/break/continue/无限循环/noreturn 调用，
    // 或 if-else 两分支均终止）。无法确定时返回 false（保守：视为会落出）。
    bool stmtTerminates(const Stmt& s) const {
        switch (s.kind) {
            case Stmt::ReturnS: case Stmt::GotoS:
            case Stmt::BreakS:  case Stmt::ContinueS:
                return true;
            case Stmt::ExprS:
                return s.expr && isNoreturnCall(*s.expr);
            case Stmt::IfS:
                // 必须有 else 分支，且两分支都终止
                return !s.elseBody.empty()
                    && blockTerminates(s.body) && blockTerminates(s.elseBody);
            case Stmt::WhileS:
                return s.expr && exprIsTrueConst(*s.expr) && !loopHasBreak(s.body);
            case Stmt::DoWhileS:
                return s.expr && exprIsTrueConst(*s.expr) && !loopHasBreak(s.body);
            case Stmt::ForS:
                // 经典 for（非 for-in）：无条件或恒真条件，且体内无 break → 无限循环
                return !s.forIn && (!s.forCond || exprIsTrueConst(*s.forCond))
                    && !loopHasBreak(s.body);
            case Stmt::CaseS: {
                // 保守：只要所有分支都终止则视为终止（覆盖标签联合穷尽解构等
                // 无 default 但实际穷尽的常见形式）；宁可漏报不误报。
                if (s.caseArms.empty()) return false;
                for (auto& arm : s.caseArms)
                    if (!blockTerminates(arm.body)) return false;
                return true;
            }
            default:
                return false;
        }
    }

    // 块是否保证终止：取最后一条「有执行意义」的语句（跳过内嵌类型定义）判定。
    bool blockTerminates(const std::vector<StmtPtr>& body) const {
        for (auto it = body.rbegin(); it != body.rend(); ++it) {
            const Stmt& s = **it;
            if (s.kind == Stmt::DeclS) continue;   // 类型定义不产生执行代码
            return stmtTerminates(s);
        }
        return false;                              // 空块自然落出
    }

    void checkMissingReturn() {
        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD || d->external) continue;
            if (d->isRpc) continue;                // rpc 实为 void，经出参结构返回
            Ty rt = funcRetType(*d);
            if (!rt.valid || rt.name == "v" || rt.name == "void" || rt.name.empty())
                continue;                          // void 或未知返回类型不查
            if (d->body.empty()) continue;
            if (!blockTerminates(d->body)) {
                std::string nm = d->methodOwner.empty()
                    ? d->name
                    : d->methodOwner + "." + (d->methodName.empty() ? d->name : d->methodName);
                err(d->line, "非 void 函数 '" + nm + "' 可能在结尾缺少 return（返回类型为 '"
                    + tyStr(rt) + "'）");
            }
        }
    }

    // 顶层声明的类型位置校验：函数返回/形参、结构体/联合字段、全局 var/let/tls 的类型名。
    //   仅校验本单元自身声明（external 的依赖/根注入声明已在其所属单元校验过）。
    void checkDeclTypes() {
        for (auto& d : prog.decls) {
            if (d->external) continue;
            switch (d->kind) {
                case Decl::FuncD:
                case Decl::FuncTypeD:
                    if (d->structCommon.type) checkTypeName(*d->structCommon.type, d->line);
                    for (auto& p : d->structCommon.fields)
                        checkTypeName(p.type, p.line ? p.line : d->line);
                    break;
                case Decl::StructD:
                case Decl::UnionD:
                case Decl::VarD:
                case Decl::LetD:
                case Decl::TlsD:
                    for (auto& f : d->structCommon.fields)
                        checkTypeName(f.type, f.line ? f.line : d->line);
                    break;
                default:
                    break;
            }
        }
    }

    // 维度无歧义：维度名是全局选择子，同名维度（跨类）共享一条分派消息，
    //   因此其参数签名必须一致（参数个数与各参数类型逐一相同），否则动态分派会错位。
    static bool sameDimParam(const TypeRef& a, const TypeRef& b) {
        return a.name == b.name && a.ptr == b.ptr && a.fat == b.fat &&
               a.project == b.project && a.arrayDims.size() == b.arrayDims.size();
    }
    void checkDimConsistency() {
        std::unordered_map<std::string, const Decl*> seen;  // 维度名 → 首次声明
        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD || !d->isDim) continue;
            auto it = seen.find(d->methodName);
            if (it == seen.end()) { seen[d->methodName] = d.get(); continue; }
            const Decl* a = it->second;
            const Decl* b = d.get();
            const auto& fa = a->structCommon.fields;
            const auto& fb = b->structCommon.fields;
            bool same = fa.size() == fb.size();
            if (same)
                for (size_t i = 0; i < fa.size(); i++)
                    if (!sameDimParam(fa[i].type, fb[i].type)) { same = false; break; }
            if (!same)
                err(b->line, "维度 '" + b->methodName + "'（" + b->methodOwner
                    + "）参数签名与 " + a->methodOwner + " 中的定义不一致："
                    "同名维度是同一全局消息，参数须完全一致（另一处见第 "
                    + std::to_string(a->line) + " 行）");
        }
    }

    // ---- 主入口：三阶段检查 ---------------------------------
    void run() {
        collectTop();                   // 1. 收集顶层符号 + 检查全局初值
        checkDuplicateDefs();           // 1.5 同名顶层符号重复定义检测
        checkDimConsistency();          // 1.55 维度无歧义：同名维度参数签名须一致
        checkDeclTypes();               // 1.6 顶层声明的类型位置校验（返回/形参/字段/全局）
        checkAggregateByValueCycles();  // 2. 按值包含环检测
        checkFatBoundaries();           // 2.5 自动指针 T@ 边界检查
        checkDeadCode();                // 2.6 不可达（死）代码检测
        checkMissingReturn();           // 2.7 非 void 函数缺少 return 检测
        checkFunctions();               // 3. 遍历所有函数体
    }
};

} // namespace

// 对外接口：对 AST 执行完整语义检查，错误通过 CompileError 抛出
void semanticCheck(const Program& prog) {
    Checker c(prog);
    c.run();
}

// 对外接口：注册内置 C 头（platform.h）符号，使其在语义检查中视为已知。
void registerBuiltinHeaderSymbols(const std::string& headerText) {
    scanBuiltinHeaderText(headerText);
}

// ============================================================
// 外部描述符使用分析
// ============================================================
// 思路：先扫描"本单元自身代码"（非 external 声明）引用到的所有名字（标识符、
// 类型名、成员名），汇成引用集；再据此判定每个 external 描述符是否被用到，
// 最后对"贡献了描述符却整体未被引用"的 .sc 模块给出导入未使用警告。
//
// 采集采取"宁多勿少"策略（连同表达式 text/op 一并收集）——过度收集只会让更多
// 描述符被判为已用、从而少报警告，方向偏宽松，避免误报，符合半宽松检查诉求。
namespace {

void usageType(const TypeRef& t, std::unordered_set<std::string>& refs);
void usageExpr(const Expr& e, std::unordered_set<std::string>& refs);
void usageStmt(const Stmt& s, std::unordered_set<std::string>& refs);

void usageFields(const std::vector<Field>& fs, std::unordered_set<std::string>& refs) {
    for (auto& f : fs) {
        usageType(f.type, refs);
        if (f.init) usageExpr(*f.init, refs);
    }
}

// 收集类型名（含内联结构/联合成员、内联函数指针的参数与返回类型）
void usageType(const TypeRef& t, std::unordered_set<std::string>& refs) {
    if (!t.name.empty()) refs.insert(t.name);
    usageFields(t.structCommon.fields, refs);
    if (t.structCommon.type) usageType(*t.structCommon.type, refs);
}

// 收集表达式涉及的名字：Ident 名、成员名（Member.text）、强转/offsetof 类型名等
void usageExpr(const Expr& e, std::unordered_set<std::string>& refs) {
    if (!e.text.empty()) refs.insert(e.text);
    if (!e.op.empty()) refs.insert(e.op);
    if (e.a) usageExpr(*e.a, refs);
    if (e.b) usageExpr(*e.b, refs);
    if (e.c) usageExpr(*e.c, refs);
    for (auto& a : e.args) if (a) usageExpr(*a, refs);
}

void usageStmt(const Stmt& s, std::unordered_set<std::string>& refs) {
    if (s.expr) usageExpr(*s.expr, refs);
    if (s.forInit) usageExpr(*s.forInit, refs);
    if (s.forCond) usageExpr(*s.forCond, refs);
    if (s.forStep) usageExpr(*s.forStep, refs);
    usageFields(s.decls, refs);
    for (auto& b : s.body) usageStmt(*b, refs);
    for (auto& b : s.elseBody) usageStmt(*b, refs);
    for (auto& arm : s.caseArms) {
        for (auto& l : arm.labels) if (l) usageExpr(*l, refs);
        for (auto& b : arm.body) usageStmt(*b, refs);
    }
    if (s.decl) {                                   // 函数体内内嵌 def
        usageFields(s.decl->structCommon.fields, refs);
        if (s.decl->structCommon.type) usageType(*s.decl->structCommon.type, refs);
        for (auto& b : s.decl->body) usageStmt(*b, refs);
    }
}

} // namespace

// 采集本单元自身代码（非 external 声明）引用到的所有名字
std::unordered_set<std::string> collectExternalRefs(const Program& prog) {
    std::unordered_set<std::string> refs;
    for (auto& d : prog.decls) {
        if (d->external) continue;                  // 只看本单元自己写的代码
        usageFields(d->structCommon.fields, refs);  // 签名/字段/参数类型
        if (d->structCommon.type) usageType(*d->structCommon.type, refs);  // 返回/别名/枚举基类型
        for (auto& s : d->body) usageStmt(*s, refs);  // 函数体
    }
    return refs;
}

std::vector<Diagnostic> analyzeExternalUsage(Program& prog) {

    // 1. 采集本单元自身代码引用到的名字
    std::unordered_set<std::string> refs = collectExternalRefs(prog);

    // 2. 标记每个 external 描述符的 used 状态
    //    方法：以方法名是否被调用（成员访问名进入 refs）判定；
    //    其余（类型/函数/全局量）：以其名字是否被引用判定。
    for (auto& d : prog.decls) {
        if (!d->external || d->kind == Decl::IncD) continue;
        d->used = !d->methodOwner.empty() ? refs.count(d->methodName) > 0
                                          : refs.count(d->name) > 0;
    }

    // 3. 按来源汇总；对"声明了描述符却整体未被引用"的来源给出导入未使用警告。
    //    来源总数优先取 Decl::externDeclared（C 头由 libclang 给出真实总数，可能
    //    因降噪阈值未逐个合成 Decl）；.sc / 退化模式回退为已合成描述符计数。
    std::unordered_map<std::string, int> originCount;  // 来源 → 已合成描述符数
    std::unordered_map<std::string, int> originUsed;   // 来源 → 其中已用数
    for (auto& d : prog.decls) {
        if (!d->external || d->kind == Decl::IncD) continue;
        originCount[d->origin]++;
        if (d->used) originUsed[d->origin]++;
    }
    std::vector<Diagnostic> warns;
    for (auto& d : prog.decls) {
        if (d->kind != Decl::IncD || !d->external) continue;
        const int used = originUsed.count(d->origin) ? originUsed[d->origin] : 0;
        const bool isSc = d->name.size() >= 3 &&
                          d->name.compare(d->name.size() - 3, 3, ".sc") == 0;
        if (isSc) {
            // .sc 模块：合并的导出声明即其符号全集，按已合成描述符计数填充统计字段
            d->externDeclared = originCount.count(d->origin) ? originCount[d->origin] : 0;
            d->externAnalyzed = true;
        }
        // 已确定符号全集、来源确有描述符、且无一被引用 → 警告
        if (d->externAnalyzed && d->externDeclared != 0 && used == 0) {
            std::string disp = d->name;  // 去掉 C 头两侧的 <>/"" 修饰，避免重复引号
            if (disp.size() >= 2 &&
                ((disp.front() == '<' && disp.back() == '>') ||
                 (disp.front() == '"' && disp.back() == '"')))
                disp = disp.substr(1, disp.size() - 2);
            warns.push_back({"外部来源 \"" + disp + "\" 已导入，但其描述符均未被引用", d->line});
        }
    }
    return warns;
}
