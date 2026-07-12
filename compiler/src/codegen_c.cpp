// ============================================================
// C 代码生成器 —— Program AST → C 源码字符串
// ============================================================
// 核心工作：
//   1. 类型映射：sc 内置类型 → C 标准类型（i4→int32_t 等）
//   2. 默认类型推断：
//        - 有初值的无类型 var/let：按初值字面量推断（见 inferLiteralType）
//        - 其余无类型对象→char*，无类型指针→void*
//   3. 函数类型展开：fnc name -> func_type 从函数类型表查找签名
//   4. 输出顺序：类型定义 → 全局变量 → 函数原型 → 函数实现
// 两遍扫描：第一遍输出类型/变量/原型，第二遍输出函数体。
// ============================================================
#include "codegen_c.h"
#include "error.h"
#include "graph/graph.h"   // builtins/graph 图算法规范接口（编译期烘焙 dep…map 深度等图属性为生成代码常量）
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <list>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace {

// #line 指令的源路径规范化：反斜杠→正斜杠。Windows 路径（D:\a\b）直接嵌入 C 字符串
// 会构成非法转义序列（\a \b …，报 C4129 且路径错乱）；C 预处理接受正斜杠路径，
// POSIX 本就是正斜杠，故统一转换无副作用。
inline std::string lineSafePath(const std::string& p) {
    std::string o = p;
    for (char& c : o) if (c == '\\') c = '/';
    return o;
}

// 自动指针 T@ 引用检查开关（--check=ref）：默认关闭，仅栈悬挂断言受其控制。
bool g_refCheck = false;
// 越界 canary 开关（--check=mem）：默认关闭，开启则 ref 头堆对象注入头尾哨兵，释放时校验。
bool g_memCheck = false;
// 运行时指针/下标守卫开关（--check=ptr）：默认关闭，开启则在解引用/指针下标处注入 nil 校验、
// 在编译期已知维度的栈数组下标处注入越界校验（命中即 abort）。
bool g_ptrCheck = false;
// 栈悬挂断言 site 文案用的源码文件名（与 #line 的 srcFile 解耦，避免强制注入 #line）。
std::string g_refSrcFile;

// 项目根目录（= builtins 目录的上级）。头支撑模块手写头的 #include 路径优先相对此根
// 计算，令任意分组层级（builtins/adt、templates/.scenv/modules/wsi）均落为根相对路径，随
// -I <项目根> 可解析。空=未知（回退祖父目录名前缀的历史行为）。
std::string g_projectRoot;

// 单元测试模式开关（--test）：本单元为测试目标 → tst 编译为测试函数 + 合成 runner main，
// 屏蔽用户 main。默认关闭：tst/assert 不产出运行代码。
bool g_testMode = false;

// C 字符串字面量转义：包裹引号并转义 \ " 及控制符，供 assert 表达式源码回显等用。
std::string cStrLit(const std::string& s) {
    std::string r = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '\\': r += "\\\\"; break;
            case '"':  r += "\\\""; break;
            case '\n': r += "\\n"; break;
            case '\r': r += "\\r"; break;
            case '\t': r += "\\t"; break;
            default:   r += (char)c;
        }
    }
    r += "\"";
    return r;
}

// sc 内置基本类型 → C 标准类型映射
std::string mapBase(const std::string& n) {
    static const std::unordered_map<std::string, std::string> m = {
        {"i1", "int8_t"},  {"i2", "int16_t"}, {"i4", "int32_t"}, {"i8", "int64_t"},
        {"u1", "uint8_t"}, {"u2", "uint16_t"}, {"u4", "uint32_t"}, {"u8", "uint64_t"},
        {"f4", "float"},   {"f8", "double"},
        {"bool", "bool"},     // 布尔：透传 C99 bool（每个单元自动含 stdbool.h，true/false 亦源于此）
        {"char", "char"},     // 字符：与 C 字符串字面量/接口互操作用（区别于 i1/u1）
        {"ret", "int32_t"},   // ADT 接口返回码：i4 的语义别名（ok=0 表成功，非 0 表失败）
        {"tril", "int8_t"},   // 三态：negative/-1 unknown/0 positive/+1（维度 dim 应答状态）
        {"object", "object"}, // 类型擦除引用：sc_hyper*（指向类对象 _class 槽，见 class.md）
        {"sc_hyper", "sc_hyper"}, // 通用分派器函数指针 typedef（类首成员 _class 的类型）
        {"va_list", "va_list"},  // 透传：可变参数列表类型
    };
    auto it = m.find(n);
    return it == m.end() ? n : it->second;
}

// sc 命名域 → C 标识符：默认加 `sc_` 前缀，避免与外部 C 符号命名冲突。
// 例外：
//   - C 入口 `main` 保持原名（C 运行时约定）。
//   - 已带 `sc_`/`sc__` 前缀的内部合成名（lambda/test/模块生命周期/运行时辅助）不重复加。
// 外部 C 符号不经此函数——它们经前置 `::name`（cBridge）与 `def name::` 原样落 C。
inline std::string scPfx(const std::string& n) {
    if (n == "main") return n;
    if (n.rfind("sc_", 0) == 0) return n;   // 已前缀（含内部 sc__）
    return "sc_" + n;
}

// 无类型 var/let 声明（var x: = 初值）时，依据初值字面量推断默认类型。
// 规则：
//   字符串字面量 "..."  → char*（ptr=1）
//   字符字面量   '...'   → char
//   浮点字面量   3.14    → 默认 f8（double，避免精度损失）；带 f/F 后缀 → f4
//   整数字面量   42      → 默认 i4（32 位）；数值超出 32 位或带 l/L 后缀 → i8；
//                          带 u/U 后缀取无符号 u4，超出 32 位 → u8
// 穿透前缀正负号（var n: = -5）。返回 true 表示成功推断（填入 base/ptr），
// false 表示初值非字面量，沿用旧默认规则（char*/void*）。

// 拆分整数字面量为「数字部分」+「后缀部分」。正确处理十六进制：
//   十六进制里的 b/B 是合法数字（hex 后无法附加 b 字节后缀，词法贪婪吞为数字），
//   故 0xEFCDAB89 的 B 属数字而非后缀；十进制里 b/B/w/W 才是扩展后缀。
//   只有后缀区可含 u/U/l/L/b/B/w/W，调用方据此判后缀、剥离 b/B/w/W。
void splitIntLiteral(const std::string& t, std::string& digits, std::string& sfx) {
    size_t i = 0;
    if (t.size() >= 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        i = 2;
        while (i < t.size() && std::isxdigit((unsigned char)t[i])) i++;
    } else {
        while (i < t.size() && (std::isdigit((unsigned char)t[i]) || t[i] == '.')) i++;
    }
    digits = t.substr(0, i);
    sfx    = t.substr(i);
}

bool inferLiteralType(const Expr& init, std::string& base, int& ptr) {
    const Expr* e = &init;
    while (e->kind == Expr::Unary && (e->op == "-" || e->op == "+") && e->a)
        e = e->a.get();
    switch (e->kind) {
        case Expr::StrLit:  base = "char"; ptr = 1; return true;
        case Expr::CharLit: base = "char"; ptr = 0; return true;
        case Expr::FloatLit: {
            bool f32 = e->text.find_first_of("fF") != std::string::npos;
            base = f32 ? "f4" : "f8"; ptr = 0; return true;
        }
        case Expr::IntLit: {
            const std::string& t = e->text;
            std::string digits, sfx;
            splitIntLiteral(t, digits, sfx);
            bool uns = sfx.find_first_of("uU") != std::string::npos;
            bool lng = sfx.find_first_of("lL") != std::string::npos;
            bool byte = sfx.find_first_of("bB") != std::string::npos;   // 扩展：单字节 i1/u1
            bool word = sfx.find_first_of("wW") != std::string::npos;   // 扩展：双字节 i2/u2
            unsigned long long mag = 0;
            try { mag = std::stoull(digits, nullptr, 0); } catch (...) { mag = 0; }
            if (byte)      base = uns ? "u1" : "i1";
            else if (word) base = uns ? "u2" : "i2";
            else if (uns)  base = (lng || mag > 0xFFFFFFFFull) ? "u8" : "u4";
            else           base = (lng || mag > 0x7FFFFFFFull) ? "i8" : "i4";
            ptr = 0; return true;
        }
        default: return false;
    }
}

bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string moduleHeaderName(const std::string& s) {
    return moduleFileToken(s) + ".h";
}

// 头支撑模块手写头的 #include 路径：<模块目录>/<stem>.h（模块目录名恒等于 stem）。
// 优先相对项目根（g_projectRoot = builtins 上级）计算，令任意分组层级
// （builtins/adt → "builtins/adt/adt.h"、templates/.scenv/modules/wsi → "templates/.scenv/modules/wsi/wsi.h"）
// 均落为根相对路径且经 -I <项目根> 可解析。无根信息或模块在根外（相对路径逃出根）时，
// 回退历史行为「祖父目录名/stem/stem.h」。moduleDir 期望即 p.parent_path()。
std::string headerBackedPath(const std::filesystem::path& moduleDir,
                             const std::string& stem) {
    namespace fs = std::filesystem;
    if (!g_projectRoot.empty()) {
        std::error_code ec;
        fs::path rel = fs::relative(moduleDir, fs::path(g_projectRoot), ec);
        if (!ec && !rel.empty()) {
            const std::string r = rel.generic_string();
            if (r != "." && r.rfind("..", 0) != 0)   // 未逃出项目根
                return r + "/" + stem + ".h";
        }
    }
    const fs::path gp = moduleDir.parent_path();
    const std::string gpName = gp.empty() ? std::string() : gp.filename().string();
    return (gpName.empty() ? "" : gpName + "/") + stem + "/" + stem + ".h";
}

// CGen 内部类 —— 封装 C 代码生成的状态
struct CGen {
    const Program& prog;
    std::ostringstream out;     // 输出流
    int depth = 0;              // 当前缩进深度（每级4空格）

    // ---- 实参求值顺序化（消除 C 实参/操作数求值顺序未定义的跨平台歧义）----
    // 当一个调用含 ≥2 个「有副作用」的实参时，把这些实参按从左到右顺序抽取为
    // 有序临时变量声明，再以临时引用改写调用，从而保证求值次序确定（左→右）。
    struct SeqHoist { const Expr* e; std::string name; std::string ctype; };
    std::vector<SeqHoist> seqList;                                   // 待发射的前置临时（求值顺序）
    std::unordered_map<const Expr*, std::string> seqSubMap;          // 子表达式 → 临时名
    const std::unordered_map<const Expr*, std::string>* curSeqSub = nullptr;  // 当前活动替换表
    int seqTmpSeq = 0;          // 顺序化临时计数（全 TU 单调，避免同作用域重名）
    std::string structOwnerTag; // 正在 emit 字段的属主聚合标签（如 "struct com"）：
                                //   供 MethodPtr 字段前置隐式接收者 T* 之用
    std::string srcFile;        // 非空时输出 #line 指令，调试器映射回 .sc 源码
    bool inMacro = false;       // 正在 emit def 宏体：抑制 #line 指令（#define 行内不可含 #line）
    std::unordered_map<std::string, const Decl*> funcTypes;  // 函数类型名→Decl 映射
    std::unordered_map<std::string, const Decl*> rpcs;       // rpc 名→Decl（run 语句查询）
    bool usesRun = false;       // 程序中出现 run 语句：需输出 thread_run 原型
    int  usesStrof = 0;         // stringify(值) 格式化关键字首次出现行号（需 adt string 可见；stringify_t 在 op.h 默认带入）

    // ---- 类机制（cls / dim / object）支撑 ----
    std::vector<const Decl*> classDecls;                       // 本单元所有 cls 类（StructD, isClass）
    std::set<std::string> classNames;                          // 类名集合（含外部）：dim 调用 / instanceOf / object 强转识别
    std::set<std::string> externalClassNames;                  // 外部（其它单元定义）cls 类名：本单元仅引用其分派器（extern 原型）
    std::map<std::string, std::vector<const Decl*>> classDims; // 类名 → 用户 dim 声明（源序）
    std::map<std::string, int> dimSelectors;                   // 全局维度名 → 选择子 id（用户 dim 从 5 起）
    int nextDimSel = 7;                                        // 下一个用户维度选择子（0..6 保留）
    bool usesClassRt = false;                                  // 本单元需输出类机制运行时序言（tril/object/cls）

    // 维度名 → 全局选择子 id（保留：CLS_ID=0 / REF=1 / DROP=2 / OBJ_KEY=3 / OBJ_NAME=4 /
    //   RLT_KEY=5 / RLT_NAME=6；REF/DROP 为 object@ 根本机制故紧跟 CLS_ID；其余按首现顺序从 7 起）
    int dimId(const std::string& name) {
        if (name == "CLS_ID")   return 0;
        if (name == "REF")      return 1;
        if (name == "DROP")     return 2;
        if (name == "OBJ_KEY")  return 3;
        if (name == "OBJ_NAME") return 4;
        if (name == "RLT_KEY")  return 5;
        if (name == "RLT_NAME") return 6;
        auto it = dimSelectors.find(name);
        if (it != dimSelectors.end()) return it->second;
        int id = nextDimSel++;
        dimSelectors[name] = id;
        return id;
    }
    // cls 类是否含某名字的真实字段（跳过 synthetic 注入成员）
    bool classHasField(const Decl& c, const std::string& fname) const {
        for (auto& f : c.structCommon.fields)
            if (!f.synthetic && f.name == fname) return true;
        return false;
    }
    // (owner, dimName) 是否为该类的维度（含用户覆盖的保留维度）
    bool isDimOf(const std::string& owner, const std::string& name) const {
        auto it = classDims.find(owner);
        if (it != classDims.end())
            for (auto* d : it->second) if (d->methodName == name) return true;
        // 保留维度：任意类皆可调用（未实现走 default 返回 unknown）
        return false;
    }
    // 名字是否为可分派维度名（保留维度 或 已注册的用户维度选择子）
    bool isDimCallName(const std::string& name) const {
        return name == "CLS_ID" || name == "OBJ_KEY" || name == "OBJ_NAME" ||
               name == "RLT_KEY" || name == "RLT_NAME" ||
               dimSelectors.count(name) > 0;
    }

    // ---- 静态全局对象 / 模块级生命周期（init/drop）支撑 ----
    // 入口单元（含非外部 main）：main 序言注入全局/模块 init，尾声注入 drop。
    // 库模块（无 main）：emit 模块级 sc_mod_<token>_init/drop，由 main 与父模块递归调用。
    bool isEntryUnit = false;       // 本单元定义了非外部 main
    bool inMainFunc = false;        // 当前正在 emit main 函数体（lambda 内须置回 false）
    bool mainHasTeardown = false;   // 入口 main 有待注入的尾声（全局 drop 或模块 drop）
    std::string modToken;           // 本单元模块 token（文件名 stem 经 C 标识符净化）
    std::vector<std::string> depTokens;  // 直接依赖 .sc 模块 token（去重、按声明序）
    struct GLife { std::string var; std::string fn; std::string ownArg; };  // 全局对象名 + init/drop 方法 C 名（+init 隐藏 own 实参）
    std::vector<GLife> gInits;      // 本单元自有全局 init（源序，逆序 drop）
    std::vector<GLife> gDrops;      // 本单元自有全局 drop（源序登记，发出时逆序）
    // 全局（非函数内）cls 实例：模块 init 序言安装分派器指针 _class（须早于 gInits，
    //   使全局对象的 init 体内类型查询/dim 调用可用）。pair = {对象名, 类名}。
    std::vector<std::pair<std::string, std::string>> gClassInstalls;

    // ---- 分布式 token（tok/dep）注册支撑 ----
    // tok 句柄与 dep 依赖关系均为模块域静态对象，注册延迟到模块 init（tok_bind/tok_depend），
    //   在 gInits 之后注入（绑定先于依赖；依赖项引用已绑定的 tok 句柄）。
    struct TokBind { std::string var; std::string id; std::string fn; int depth = 0; int sccId = 0; int sccSize = 0; int critical = 0; int slack = -1; int fanin = 0; int fanout = 0; int reach = 0; int batchWidth = 0; int checkpoint = 0; int domSize = 0; bool inMap = false; };  // 句柄变量 + id 串 + combine C 名（空=无）+ 依赖图深度 + SCC 反馈簇 + 关键路径标志/松弛（slack=-1 未烘焙）+ 扇入扇出度 + 可达下游数 + 波次并行宽度 + 支配检查点/子树规模 + 是否在 map DAG 中
    struct DepReg  { bool all; std::string fn; std::vector<std::string> vars; std::vector<std::string> targetVars; bool loop = false; }; // 门逻辑 + follow C 名 + 源依赖项 tok 句柄变量 + map 目标句柄变量 + 是否 dep loop 受控反馈环
    std::vector<TokBind> tokBinds;  // 本单元 tok 句柄绑定（源序）
    std::vector<DepReg>  depRegs;   // 本单元 dep 依赖注册（源序）

    // ---- 单元测试（--test）支撑 ----
    struct TestCase { const Decl* d; std::string cname; };  // tst 用例 + 生成的 C 函数名
    std::vector<TestCase> testCases;  // 本单元 tst 用例（源序），测试目标时填充

    // ---- 伪 class 支撑：类型注册表与变量类型跟踪 ----
    std::unordered_map<std::string, const Decl*> aggrs;    // struct/union 名 → Decl
    std::unordered_map<std::string, std::string> aliases;  // 别名 → 目标类型名
    // 顶层函数表：函数名 → Decl（缺参调用 0 补全查签名用）
    std::unordered_map<std::string, const Decl*> funcs;
    // 顶层方法表：所属类型 → 方法名 → Decl（结构内实现或 fnc T::m 声明）
    std::unordered_map<std::string, std::unordered_map<std::string, const Decl*>> methods;
    // 变量的轻量类型信息（类型名 + 指针层数 + 数组维数），用于方法调用识别
    struct VType { std::string name; int ptr = 0; int arr = 0; bool fat = false; bool thin = false; };
    std::unordered_map<std::string, VType> globalsT, localsT;
    // 自动指针 T@：被构造为胖目标（T()）的类型名集合（决定生成 T__new_ref 带头分配）
    std::set<std::string> fatTypeNames;
    // 裸 @（类型擦除）根变量名集合：供 `e = T()` 重新赋值时按变量名回收构造类型到 fatTypeNames。
    std::set<std::string> bareFatVarNames;
    // 胖根指针作用域栈：每层一组本块内声明的 T@ 根变量，退块/return 时逆序 unbind。
    //   dtorArg = 目标类型 T 有 drop 时的析构实参串 "(void (*)(void *))T_drop"（无则空），
    //   解绑（in→0）时按目标静态类型调析构清理子成员（见 auto_ptr.md §5）。
    //   bare = 裸 @（类型擦除，sc_afat）：解绑走 sc_afat_unbind（dtor 随句柄）。
    struct FatRoot { std::string name; std::string dtorArg; bool bare = false; bool thin = false; };
    std::vector<std::vector<FatRoot>> fatScopes;
    // 与 fatScopes 平行：每层本块内声明的 T@ 数组根变量（元素各为一条根边），退块/return 时
    // 逐元素 unbind（覆盖整个引用图，避免泄漏）。局部 T@ 数组（一维/多维）。
    //   dtorArg = 元素类型 T 的析构实参串（同 FatRoot），逐元素解绑时按元素类型调析构。
    struct FatArrayVar { std::string name; std::vector<std::string> dims; std::string dtorArg; };
    std::vector<std::vector<FatArrayVar>> fatArrayScopes;
    // 与 fatScopes 平行：每层本块内被 & 借出（须注入 sc_ref 头）的普通栈变量，
    // 退块时（拆边后）逐个 sc_ref_check 检测外部悬挂（§4.2/§7.3）。仅 --check=ref 开启时填充。
    struct FatStackVar { std::string name; std::string site; };
    std::vector<std::vector<FatStackVar>> fatStackScopes;
    // 与 fatScopes 平行：每层本块内一维栈数组（超额分配尾哨兵），退块/return/break 处校验
    // 尾区是否被破坏，捕获栈数组越界写。仅 --check=mem 开启时填充。
    struct MemCanaryVar { std::string name; std::string elemTy; std::vector<std::string> dims; std::string site; };
    std::vector<std::vector<MemCanaryVar>> memCanaryScopes;
    // 全局栈数组（文件作用域）尾哨兵：超额分配后由 constructor 填充、destructor 退出时校验。
    std::vector<MemCanaryVar> globalCanaries;
    // 全局 T@（文件作用域 var/let，标量或数组）：进程退出由 destructor 钩子逐元素拆边
    // （带 drop），释放其持有的系统资源（文件/锁/socket 等）。dims 为空=标量。
    std::vector<FatArrayVar> globalFatVars;
    // 容器下标糖 t[k,...]：用到的 find 方法（按首用顺序去重），为各自生成 _find__idx
    // 内联包装（出参回填 + 命中判定），使下标糖退化为一次普通函数调用而非 GNU 语句表达式。
    std::vector<const Decl*> indexHelperFinds;
    // 与 fatScopes 平行：每层本块内登记的 final 域退出钩子（按源序）。退出点（正常落出/
    // return/break/continue）先于本块胖边拆解，按 LIFO 逐块发出其 body（§16.2 defer 等价）。
    std::vector<std::vector<const Stmt*>> fatFinalScopes;
    // 与 fatScopes 平行：每层本块内声明的栈值对象（类型有 drop 方法），退域 LIFO 自动调
    // drop（RAII 闭环）。name=变量名，dropFn=drop 方法的 C 名（T_drop）。
    struct DropVar { std::string name; std::string dropFn; };
    std::vector<std::vector<DropVar>> dropScopes;
    // 半自动指针 T@1（单例指针）：物理普通指针 T* + 退域 RAII。退域/重新赋值覆盖旧值时，
    // 若指向对象非 nil 则自动 drop + sc_free 销毁。autoFreeScopes 与 fatScopes 平行逐层登记；
    // autoFreeVars 为本函数内 name→信息映射，供赋值语句拦截（先销毁旧对象再存新指针）。
    struct AutoFreeVar { std::string name; std::string ctype; std::string dropFn; };
    std::vector<std::vector<AutoFreeVar>> autoFreeScopes;
    std::map<std::string, AutoFreeVar> autoFreeVars;
    int autoFreeTmpSeq = 0;
    // 本函数内被显式 x.drop()/x->drop() 调用的变量名（预扫描）：move 语义——显式 drop
    // 抑制该变量的退域自动 drop（用户接管其生命周期，避免双重释放）。
    std::set<std::string> manualDropVars;
    // 本函数内被 &var 借入胖指针的普通栈变量名集合（预扫描得出，决定声明处注入 ref 头）。
    std::set<std::string> fatBorrowVars;
    // 分身/切片句柄变量（var s: T[...]）：变量名 → 实体类型 T 名
    std::unordered_map<std::string, std::string> projVarsG, projVarsL;
    // 函数指针变量的内联签名（var cb: fnc: ...）：缺参补全查询用
    std::unordered_map<std::string, const TypeRef*> fnVarsG, fnVarsL;
    // 数组变量的维度表（string 格式化顶层数组需要维度信息）
    std::unordered_map<std::string, std::vector<std::string>> varDimsG, varDimsL;
    // 枚举类型名集合（string 格式化按整数）
    std::unordered_set<std::string> enums;
    // 枚举常量名集合（含各枚举的成员项）：Ident 引用处判定是否加 sc_ 前缀（sc 域常量）。
    std::unordered_set<std::string> enumConsts;
    // sc 域宏名集合（def NAME: / def name: 非 :: 桥接宏）：Ident/调用引用处加 sc_ 前缀。
    std::unordered_set<std::string> macroNames;
    // 容器类型名集合（def T: <C, I> 的 C）：t[key,...] → t.find(...) 糖识别用
    std::unordered_set<std::string> adtColls;
    int forSeq = 0;             // for-in 临时变量序号（生成唯一 _fi/_fb/... 名）
    bool inFunc = false;        // 当前是否在函数体内（决定变量注册到哪个表）
    const Decl* curFnSig = nullptr;  // 当前普通函数签名（return 临时变量取返回类型用）
    std::string curMethodOwner;      // 当前方法的接收者类型 T（非方法为空）；供 this 的类型推断
    bool retDollarDeclared = false;  // ret 调用语法糖：当前函数是否已声明 $（_sc_ret）

    // ---- rpc 支撑：实际函数体内，参数引用改写为 _p->name ----
    const Decl* curRpc = nullptr;               // 非空时正在输出 rpc 实际函数体
    std::unordered_set<std::string> rpcParams;  // 当前 rpc 的参数名集合
    bool emitRpcCallOK = false;                  // 正在 sync/await 驱动下发出 rpc 调用（放行裸调用禁令）
    int asyncState = 0;                         // 异步 rpc 状态机：当前 await 段编号
    int comRpcIdx = 0;                           // 异步 com<<>>rpc 序列化：帧 _crpcN 计数

    // ---- 匿名函数字面量（FncLit）支撑 ----
    // 不捕获外层变量的「伪闭包」：提升为顶层 static 函数，表达式处替换为函数名。
    std::ostringstream lambdaOut;                       // 提升后的 static 函数定义（在函数体前回填）
    std::unordered_map<const Expr*, std::string> lambdaNames;  // FncLit 节点 → 生成的函数名
    int lambdaSeq = 0;                                  // 函数名序号

    // ---- inl 真内联块支撑 ----
    // 函数体内命名代码块：定义处仅登记，调用点原地展开为
    //   { 形参临时=实参; 块体; label:; }，共享外层栈帧。
    //   void inl：裸 return→goto label；值 inl：return v→lhs=v; goto label。
    struct InlineFrame {
        std::string endLabel;   // 本层尾标签
        std::string lhs;        // 值上下文的赋值目标（C 代码串；空=void/丢弃上下文）
    };
    std::unordered_map<std::string, const Stmt*> inlineDefs;  // 块名 → InlineDefS（函数域，emitFunc 清空）
    std::vector<std::string> inlineActive;             // 正在展开的块名栈（递归检测）
    std::vector<InlineFrame> inlineFrames;             // 展开中各层帧栈（return→goto/赋值）
    int inlineSeq = 0;                                  // 尾标签唯一序号
    // ---- 带值 inl 作子表达式：自动临时提升 ----
    // 嵌套在更大表达式里的值 inl 调用先展开到自动临时 _inlN，原位以临时名引用。
    // 用链表栈保存各层替换表（内联体展开时体内语句可再嵌套提升 → 需稳定地址的多层）。
    std::list<std::unordered_map<const Expr*, std::string>> inlSubStack;      // 各层：inl 调用节点 → 临时名
    std::vector<const std::unordered_map<const Expr*, std::string>*> inlSubPrev; // 各层压入前的 curInlSub
    const std::unordered_map<const Expr*, std::string>* curInlSub = nullptr;  // 当前活动替换表
    int inlTmpSeq = 0;                                  // 提升临时计数（全 TU 单调）
    std::unordered_map<std::string, std::string> inlParamSub;  // 形参名 → 直接代入文本（快路径，无临时）

    // ---- 聚合体定义顺序无关支撑 ----
    // 按值包含要求被包含者「完整类型」先于包含者出现（前置声明仅满足指针引用）。
    // 第一遍输出 struct/union 时惰性前移其按值依赖，使源码定义顺序无关（见 doc §5.3）。
    std::unordered_set<const Decl*> emittedAggr;        // 已输出完整定义的聚合体
    std::unordered_set<const Decl*> emittedProject;     // 已输出的分身句柄 T__project

    explicit CGen(const Program& p) : prog(p) {}

    void indent() { for (int i = 0; i < depth; i++) out << "    "; }

    // ---------------- 类型处理 ----------------
    // 解析类型引用，确定 C 中的底层类型名和指针层数
    // 默认规则：无类型无指针→char*，无类型有指针→void*
    // sc 类型名 → C 类型标识符：
    //   内置基本类型经 mapBase；sc 声明的聚合/别名/枚举/类/函数类型加 `sc_` 前缀；
    //   其余（外部 C 类型，如 FILE/pthread_t 等）原样透传。
    std::string mapType_(const std::string& n) const {
        std::string b = mapBase(n);
        if (b != n) return b;                       // 内置基本类型（含 object/sc_hyper/va_list）
        if (aggrOf(n) || aliases.count(n) || enums.count(n) ||
            classNames.count(n) || funcTypes.count(n))
            return scPfx(n);                        // sc 声明的类型
        return n;                                   // 外部 C 类型，原样
    }

    void resolveType(const TypeRef& t, std::string& base, int& ptr) const {
        if (t.name.empty() && !t.hasInline) {
            if (t.ptr > 0) { base = "void"; ptr = t.ptr; }
            else { base = "char"; ptr = 1; }
        } else {
            base = t.cBridge ? t.name : mapType_(t.name);  // ::name → C 域类型，原样落 C 类型名（不加 sc_ 前缀）
            ptr = t.ptr;
        }
        // 类型侧 const/volatile 限定「指向对象/对象本身」，前缀到底层类型名。
        std::string q;
        if (t.qConst) q += "const ";
        if (t.qVolatile) q += "volatile ";
        base = q + base;
    }

    // 声明一个字段/变量：T [*...]name[size]
    void emitDeclarator(const Field& f, bool asConst = false, const char* nameOverride = nullptr) {
        // 声明名：默认用形参/字段名（this→_this）；nameOverride 非空时以其为 C 变量名
        //（供 inl 展开将非字面值实参先算入唯一临时时用）。
        auto oname = [&](const std::string& dflt) -> std::string {
            return nameOverride ? std::string(nameOverride) : dflt;
        };
        // 分身/切片句柄字段/参数 name: T[...] → struct T__project name
        // （rpc 伪形参与普通结构体成员皆可承载分身句柄，其本质即一个结构体）
        if (f.type.project) {
            if (asConst) out << "const ";
            out << "struct " << mapType_(f.type.name) << "__project "
                << oname(f.name == "this" ? "_this" : f.name);
            return;
        }
        // 胖指针（自动指针 T@）：C 侧统一为 sc_fat（24 字节，首成员 p 即裸指针）。
        // 裸 @（类型擦除，name 空）：sc_afat（前 24B 同构 + dtor 槽）。瘦 @^：sc_thin（24B {p,tar,dtor}）。
        if (f.type.fat) {
            if (asConst) out << "const ";
            out << (f.type.thin ? "sc_thin " : (f.type.name.empty() ? "sc_afat " : "sc_fat "))
                << oname(f.name == "this" ? "_this" : f.name);
            for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
            return;
        }
        if (f.type.fnKind != TypeRef::FncKind::None) {
            if (f.type.structCommon.type) {
                std::string base; int ptr;
                resolveType(*f.type.structCommon.type, base, ptr);
                out << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
            } else out << "void ";  // 省略返回类型 = void
            out << "(*" << oname(f.name) << ")(";
            bool firstParam = true;
            if (f.type.fnKind == TypeRef::FncKind::MethodPtr && !structOwnerTag.empty()) {
                // 每对象方法指针：C 类型隐式前置接收者 T*（声明/调用端隐藏接收者）
                out << structOwnerTag << " *";
                firstParam = false;
            }            for (size_t i = 0; i < f.type.structCommon.fields.size(); i++) {
                if (!firstParam) out << ", ";
                firstParam = false;
                emitDeclarator(f.type.structCommon.fields[i]);
            }
            if (f.type.structCommon.variadic) out << (firstParam ? "..." : ", ...");
            out << ")";
            return;
        }
        if (f.type.hasInline) { // 内联/匿名结构联合
            out << (f.type.inlineUnion ? "union" : "struct") << " {\n";
            depth++;
            for (auto& sub : f.type.structCommon.fields) {
                indent();
                emitDeclarator(sub);
                out << ";\n";
            }
            depth--;
            indent();
            out << "}";
            if (nameOverride || !f.name.empty()) out << " " << oname(f.name);
            for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
            return;
        }
        std::string base; int ptr;
        resolveType(f.type, base, ptr);   // base 已含类型侧 const/volatile（限定指向对象）
        if (ptr == 0) {
            // 标量/对象：let（asConst）→ 对象本身 const（与类型侧 const 去重）
            if (asConst && !f.type.qConst) out << "const ";
            out << base << " ";
        } else {
            out << base << " ";
            for (int i = 0; i < ptr; i++) {
                out << "*";
                // 最外层指针承载「指针本身」限定：let → 指针 const；restrict → 别名约束
                if (i == ptr - 1) {
                    if (asConst) out << "const";
                    if (f.type.qRestrict) out << (asConst ? " restrict" : "restrict");
                    if (asConst || f.type.qRestrict) out << " ";
                }
            }
        }
        out << oname(f.name == "this" ? "_this" : f.name);  // 参数名 this → _this
        for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
    }

    bool shouldStaticize(const Decl& d) const {
        return !d.exported && !d.external;
    }

    // 泛型实例类型是否「自包含」（仅依赖 platform.h：基本类型/指针，不按值内嵌用户聚合）。
    //   自包含者其完整定义可安全聚合进早含的 generic.h（跨单元一致、消除导出签名引用未定义）；
    //   否则（按值内嵌用户聚合/胖指针/切片）仅各单元内联，模块头改以前向 typedef 暴露（指针签名可用）。
    bool isSelfContainedInstance(const Decl& d) const {
        if (d.kind == Decl::EnumD) return true;
        if (d.kind == Decl::AliasD)
            return !aggrOf(d.structCommon.type ? d.structCommon.type->name : std::string{});
        if (d.kind != Decl::StructD && d.kind != Decl::UnionD) return false;
        for (auto& f : d.structCommon.fields) {
            if (f.synthetic) continue;
            if (f.type.ptr > 0) continue;                     // 指针字段：仅需前向，自包含
            if (f.type.fat || f.type.project) return false;   // 胖指针/切片句柄：依赖运行时定义
            if (f.type.hasInline) return false;               // 内联匿名聚合：保守内联
            if (aggrOf(f.type.name)) return false;            // 值内嵌用户聚合：非自包含
        }
        return true;
    }

    // 本单元是否存在任何泛型实例声明（决定是否需要 #include generic.h）。
    bool unitHasGenericInst() const {
        for (auto& d : prog.decls) if (d->genericInst) return true;
        return false;
    }

    // 向工程级 generic.h 追加本单元贡献：所有泛型实例聚合的前向 typedef（fwdOut）+ 自包含
    //   实例（仅基本类型/指针字段）的完整类型定义（defOut）。跨单元按类型名去重（seen*）。
    //   非自包含实例（按值内嵌用户聚合）不入头，仅各单元内联，其指针签名靠前向 typedef 跨模块可见。
    void appendGenericFragment(std::set<std::string>& seenFwd,
                               std::set<std::string>& seenDef,
                               std::string& fwdOut, std::string& defOut) {
        // 渲染 emitTypeDecl 所需的最小类型索引（聚合/别名/枚举），不影响 .c 主流程。
        for (auto& d : prog.decls) {
            if (d->kind == Decl::StructD || d->kind == Decl::UnionD) aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD && d->structCommon.type)
                aliases[d->name] = d->structCommon.type->name;
            if (d->kind == Decl::EnumD) enums.insert(d->name);
        }
        // 1) 前向 typedef：所有泛型实例 struct/union（保证指针签名跨模块可见）
        for (auto& d : prog.decls) {
            if (!d->genericInst) continue;
            if (d->kind != Decl::StructD && d->kind != Decl::UnionD) continue;
            if (!seenFwd.insert(d->name).second) continue;
            const bool asUnion = d->kind == Decl::UnionD && !d->tagged;
            fwdOut += std::string("typedef ") + (asUnion ? "union" : "struct")
                    + " " + mapType_(d->name) + " " + mapType_(d->name) + ";\n";
        }
        // 2) 完整定义：自包含实例（struct/union/enum/alias）
        for (auto& d : prog.decls) {
            if (!d->genericInst || !isSelfContainedInstance(*d)) continue;
            if (d->kind != Decl::StructD && d->kind != Decl::UnionD &&
                d->kind != Decl::EnumD && d->kind != Decl::AliasD) continue;
            if (!seenDef.insert(d->name).second) continue;
            out.str("");
            out.clear();
            emitTypeDecl(*d);
            defOut += out.str();
        }
        out.str("");
        out.clear();
    }

    // ---------------- 伪 class：类型查询与方法识别 ----------------
    // 类型名 → struct/union 节点（穿透别名，最多 8 层防环）
    const Decl* aggrOf(std::string name) const {
        for (int i = 0; i < 8 && !name.empty(); i++) {
            auto it = aggrs.find(name);
            if (it != aggrs.end()) return it->second;
            auto al = aliases.find(name);
            if (al == aliases.end()) return nullptr;
            name = al->second;
        }
        return nullptr;
    }

    // 堆专属类型 def/cls NAME&: {}：只可作指针，普通指针 .drop() 由 codegen 注入块释放。
    bool isHeapOnly(const std::string& name) const {
        const Decl* d = aggrOf(name);
        return d && d->heapOnly;
    }

    // prev/next 上下文关键字：链表结构体上映射到内置 _prev/_next
    static std::string memberFieldName(const Decl& sd, const std::string& name) {
        if (sd.linked && (name == "prev" || name == "next")) return "_" + name;
        return name;
    }

    // 链表结构体首个真实成员（跳过前置注入的 synthetic _prev/_next）
    const Field* firstRealField(const Decl* sd) const {
        if (!sd) return nullptr;
        for (auto& f : sd->structCommon.fields) if (!f.synthetic) return &f;
        return nullptr;
    }

    // prev 边界安全前驱依赖内置 chain（adt.sc 提供 chain_prev 契约）。
    // 仅在实际为「链表结构体」生成 chain_prev 调用处校验，避免与同名 ADT 成员函数 prev 冲突。
    void requireChain(int line) const {
        if (aggrOf("chain")) return;
        for (auto& d : prog.decls)
            if (d->kind == Decl::IncD && endsWith(d->name, "adt.sc")) return;
        throw CompileError{"prev 边界安全前驱依赖内置 chain，请先 inc adt.sc", line};
    }

    // ---- T() 伪调用预扫描：收集需要生成 T__new 辅助函数的类型 ----
    std::set<std::string> heapNews;

    void scanExprForNew(const Expr& e) {
        // T() / T(args) 堆构造糖：登记 T 以生成 T__new（带参时再生成 T__new_init）。
        // futureId 形态走下方 future 专路；stringify 不是类型名（aggrOf 为空）自然跳过。
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident && e.futureId.empty())
            if (const Decl* sd = aggrOf(e.a->text)) heapNews.insert(sd->name);
        if (!e.futureId.empty()) {
            usesFutureId = true;  // future<ID>(ctx?) → 需 future__new_tagged
            if (const Decl* fd = aggrOf("future")) heapNews.insert(fd->name);  // 保证 future__new 生成
        }
        // print / stringify 格式化关键字使用标记（原型/辅助函数需先于函数体输出）
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident) {
            if (e.a->text == "stringify" && !e.args.empty() && !usesStrof) usesStrof = e.line;
            // stringify 单参数包装堆构造 string&，需保证 string__new 生成
            if (e.a->text == "stringify" && !e.args.empty())
                if (const Decl* ss = aggrOf("string")) heapNews.insert(ss->name);
        }
        if (e.a) scanExprForNew(*e.a);
        if (e.b) scanExprForNew(*e.b);
        if (e.c) scanExprForNew(*e.c);
        for (auto& a : e.args) scanExprForNew(*a);
    }

    // 类机制：本单元是否引用 tril/object 类型或类字面量（无 cls 定义时也可能需运行时序言）
    bool unitUsesClassTypes() {
        bool used = false;
        auto chkTy = [&](const TypeRef& t) { if (t.name == "tril" || t.name == "object") used = true; };
        std::function<void(const Expr&)> chkExpr = [&](const Expr& e) {
            if (e.kind == Expr::Ident &&
                (e.text == "negative" || e.text == "unknown" || e.text == "positive")) used = true;
            if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident && e.a->text == "instanceOf") used = true;
            if (e.a) chkExpr(*e.a);
            if (e.b) chkExpr(*e.b);
            if (e.c) chkExpr(*e.c);
            for (auto& a : e.args) chkExpr(*a);
        };
        std::function<void(const Stmt&)> chkStmt = [&](const Stmt& s) {
            for (auto& f : s.decls) { chkTy(f.type); if (f.init) chkExpr(*f.init); }
            if (s.expr) chkExpr(*s.expr);
            for (auto& a : s.printArgs) chkExpr(*a);
            if (s.forInit) chkExpr(*s.forInit);
            if (s.forCond) chkExpr(*s.forCond);
            if (s.forStep) chkExpr(*s.forStep);
            for (auto& b : s.body) chkStmt(*b);
            for (auto& b : s.elseBody) chkStmt(*b);
        };
        for (auto& d : prog.decls) {
            if (d->external) continue;
            if (d->structCommon.type) chkTy(*d->structCommon.type);
            for (auto& f : d->structCommon.fields) chkTy(f.type);
            for (auto& s : d->body) chkStmt(*s);
        }
        return used;
    }

    // 本单元是否需要类机制运行时（定义了 cls 类，或引用 tril/object/类字面量/instanceOf）。
    //   供工程管线判定是否写出共享 class.h 并让本单元 #include 之（与 usesClassRt 同义）。
    bool computeUsesClassRuntime() {
        for (auto& d : prog.decls)
            if (d->kind == Decl::StructD && d->isClass && !d->external) return true;
        return unitUsesClassTypes();
    }

    void scanStmtForNew(const Stmt& s) {
        if (s.kind == Stmt::RunS) usesRun = true;
        if (s.kind == Stmt::PrintS) {
            for (auto& a : s.printArgs) scanExprForNew(*a);
        }
        if (s.expr) scanExprForNew(*s.expr);
        for (auto& f : s.decls) if (f.init) scanExprForNew(*f.init);
        if (s.forInit) scanExprForNew(*s.forInit);
        if (s.forCond) scanExprForNew(*s.forCond);
        if (s.forStep) scanExprForNew(*s.forStep);
        if (s.forColl) scanExprForNew(*s.forColl);
        if (s.forRangeLo) scanExprForNew(*s.forRangeLo);
        if (s.forRangeHi) scanExprForNew(*s.forRangeHi);
        if (s.forStepE) scanExprForNew(*s.forStepE);
        if (s.forOffsetE) scanExprForNew(*s.forOffsetE);
        if (s.forNumE) scanExprForNew(*s.forNumE);
        for (auto& b : s.body) scanStmtForNew(*b);
        for (auto& b : s.elseBody) scanStmtForNew(*b);
        for (auto& arm : s.caseArms) {
            for (auto& l : arm.labels) scanExprForNew(*l);
            for (auto& b : arm.body) scanStmtForNew(*b);
        }
    }

    // 自动指针 T@：收集胖目标类型名（递归内联结构体；遍历语句中的 var 声明）
    void noteFatType(const TypeRef& t) {
        if (t.fat && !t.name.empty()) fatTypeNames.insert(t.name);
        if (t.hasInline)
            for (auto& f : t.structCommon.fields) noteFatType(f.type);
    }
    void collectFatTypesStmt(const Stmt& s) {
        for (auto& f : s.decls) {
            noteFatType(f.type);
            // 裸 @ 由 T() 构造时，目标类型名不出现在声明类型里（被擦除），
            // 须从初值 T() 收集，以生成带头堆构造 T__new_ref（否则链接缺符号）。
            if (f.type.fat && f.type.name.empty()) {
                bareFatVarNames.insert(f.name);
                if (f.init)
                    if (const Decl* td = typeCallee(*f.init)) fatTypeNames.insert(td->name);
            }
        }
        // 裸 @ 重新赋值 `e = T()`：左值类型被擦除，须据已登记的裸 @ 变量名收集构造类型。
        if (s.kind == Stmt::ExprS && s.expr && s.expr->kind == Expr::Binary
            && s.expr->op == "=" && s.expr->a && s.expr->a->kind == Expr::Ident
            && bareFatVarNames.count(s.expr->a->text) && s.expr->b)
            if (const Decl* td = typeCallee(*s.expr->b))
                fatTypeNames.insert(td->name);
        if (s.decl) {
            for (auto& f : s.decl->structCommon.fields) noteFatType(f.type);
            if (s.decl->structCommon.type) noteFatType(*s.decl->structCommon.type);
        }
        for (auto& b : s.body) collectFatTypesStmt(*b);
        for (auto& b : s.elseBody) collectFatTypesStmt(*b);
        for (auto& arm : s.caseArms)
            for (auto& b : arm.body) collectFatTypesStmt(*b);
    }

    // 生成堆构造辅助函数：T *T__new(void) = malloc + 默认值/清零 + init
    void emitNewHelpers() {
        for (auto& tn : heapNews) {
            auto it = aggrs.find(tn);
            if (it == aggrs.end()) continue;
            const Decl* sd = it->second;
            const std::string tc = mapType_(tn);   // C 符号名（含 sc_ 前缀）；tn 保留原名用于查表
            out << "static inline " << tc << " *" << tc << "__new(void) {\n"
                << "    " << tc << " *_p = (" << tc << " *)sc_alloc(sizeof(" << tc << "));\n"
                << "    if (_p) {\n";
            if (sd->kind == Decl::StructD && hasFieldDefaults(sd))
                out << "        *_p = " << tc << "__default();\n";
            else
                out << "        memset(_p, 0, sizeof(" << tc << "));\n";
            if (sd->isClass)
                out << "        _p->_class = " << tc << "_hyper_impl;\n";
            const Decl* im = findMethod(tn, "init");
            if (im && im->structCommon.fields.empty())
                out << "        " << scPfx(im->name) << "(_p"
                    << (typeHasFatMember(tn) ? ", SC_OWN_RAW" : "") << ");\n";
            out << "    }\n    return _p;\n}\n\n";

            // 带参堆构造 T__new_init：T(args) → T__new() 分配 + init(args) 转发
            // （仅当 init 带用户参数时生成；无参 init 的构造仍走 T__new() 自动 init）
            if (im && !im->structCommon.fields.empty()) {
                out << "static inline " << tc << " *" << tc << "__new_init(";
                for (size_t i = 0; i < im->structCommon.fields.size(); i++) {
                    if (i) out << ", ";
                    emitDeclarator(im->structCommon.fields[i]);
                }
                out << ") {\n"
                    << "    " << tc << " *_p = " << tc << "__new();\n"
                    << "    if (_p) " << scPfx(im->name) << "(_p";
                for (auto& f : im->structCommon.fields)
                    out << ", " << (f.name == "this" ? "_this" : f.name);
                if (typeHasFatMember(tn)) out << ", SC_OWN_RAW";
                out << ");\n    return _p;\n}\n\n";
            }

            // 胖目标 T@：带 sc_ref 头的堆构造 T__new_ref（头在块首，实体在 +SC_REF_HDR）。
            // _flags = SC_REF_ATOM（T<atom>()，原子引用计数）| SC_REF_RAW（T<raw>()，裸分配）。
            // 默认（_flags 无 RAW）走 sc_chunk 池化；T<raw>() 走 sc_alloc（libc/间接层）。
            if (fatTypeNames.count(tn)) {
                out << "static inline " << tc << " *" << tc << "__new_ref(int32_t _flags) {\n";
                if (g_memCheck) {
                    // --check=mem 且 T<raw>()：扩块 [头哨兵|sc_ref 头|实体|尾哨兵]，codegen SC_CANARY 守护。
                    // 默认（chunk）：走 sc_chunk，越界由 chunk 自带 MEM_DEBUG 尾金丝雀负责。
                    out << "    sc_ref *_h; " << tc << " *_p;\n"
                        << "    if (_flags & SC_REF_RAW) {\n"
                        << "        char *_b = (char *)malloc(SC_CANARY + SC_REF_HDR + sizeof("
                        << tc << ") + SC_CANARY);\n"
                        << "        if (!_b) return 0;\n"
                        << "        _h = (sc_ref *)(_b + SC_CANARY);\n"
                        << "        _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags | SC_REF_CANARY;\n"
                        << "        uintptr_t _m = sc_canary_magic(_b);\n"
                        << "        ((uintptr_t *)_b)[0] = _m; ((uintptr_t *)_b)[1] = sizeof(" << tc << ");\n"
                        << "        *(uintptr_t *)(_b + SC_CANARY + SC_REF_HDR + sizeof(" << tc << ")) = _m;\n"
                        << "        _p = (" << tc << " *)(_b + SC_CANARY + SC_REF_HDR);\n"
                        << "    } else {\n"
                        << "        _h = (sc_ref *)sc_chunk(SC_REF_HDR + sizeof(" << tc << "));\n"
                        << "        if (!_h) return 0;\n"
                        << "        _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;\n"
                        << "        _p = (" << tc << " *)((char *)_h + SC_REF_HDR);\n"
                        << "    }\n";
                } else {
                    // 默认 chunk 池化；T<raw>() 退化为裸分配（sc_alloc）。
                    out << "    sc_ref *_h = (sc_ref *)((_flags & SC_REF_RAW)\n"
                        << "        ? sc_alloc(SC_REF_HDR + sizeof(" << tc << "))\n"
                        << "        : sc_chunk(SC_REF_HDR + sizeof(" << tc << ")));\n"
                        << "    if (!_h) return 0;\n"
                        << "    _h->in = 0; _h->out = 0; _h->heap = 1; _h->flags = _flags;\n"
                        << "    " << tc << " *_p = (" << tc << " *)((char *)_h + SC_REF_HDR);\n";
                }
                if (sd->kind == Decl::StructD && hasFieldDefaults(sd))
                    out << "    *_p = " << tc << "__default();\n";
                else
                    out << "    memset(_p, 0, sizeof(" << tc << "));\n";
                if (sd->isClass)
                    out << "    _p->_class = " << tc << "_hyper_impl;\n";
                if (im && im->structCommon.fields.empty())
                    out << "    " << scPfx(im->name) << "(_p"
                        << (typeHasFatMember(tn) ? ", &_h->out" : "") << ");\n";
                out << "    return _p;\n}\n\n";

                // 带参 + 胖目标：T__new_ref_init(init形参..., _atom) = 带头分配（复用
                // __new_ref，init 带参时其内不自动 init）+ init(args) 转发。含胖成员时
                // self_own 取本对象真实出边 &_h->out（由 _p 回退 SC_REF_HDR 还原头）。
                if (im && !im->structCommon.fields.empty()) {
                    out << "static inline " << tc << " *" << tc << "__new_ref_init(";
                    for (size_t i = 0; i < im->structCommon.fields.size(); i++) {
                        if (i) out << ", ";
                        emitDeclarator(im->structCommon.fields[i]);
                    }
                    out << ", int32_t _flags) {\n"
                        << "    " << tc << " *_p = " << tc << "__new_ref(_flags);\n"
                        << "    if (_p) " << scPfx(im->name) << "(_p";
                    for (auto& f : im->structCommon.fields)
                        out << ", " << (f.name == "this" ? "_this" : f.name);
                    if (typeHasFatMember(tn))
                        out << ", &((sc_ref *)((char *)_p - SC_REF_HDR))->out";
                    out << ");\n    return _p;\n}\n\n";
                }
            }
        }
        // future<ID>(ctx?) 构造：在 future__new 基础上打 id 标签 + 可选用户上下文 ctx
        if (usesFutureId) {
            const std::string fc = mapType_("future");
            out << "static inline " << fc << " *" << fc << "__new_tagged(int _id, void *_ctx) {\n"
                << "    " << fc << " *_p = " << fc << "__new();\n"
                << "    if (_p) { _p->id = _id; _p->ctx = _ctx; }\n"
                << "    return _p;\n}\n\n";
        }
    }

    // 容器下标糖包装：为每个被 t[k,...] 用到的 find 方法生成一次性内联包装
    //   static inline I *C_find__idx(C *_self, 键...) {
    //       I *_o = (void*)0; return (C_find(_self, &_o, 键...) == 0) ? _o : (void*)0;
    //   }
    // 把出参回填 + 命中判定收进普通函数调用，替代原先依赖 GNU 语句表达式 ({...}) 的实现，
    // 使生成代码在 MSVC 等不支持语句表达式的编译器上同样可编译。须在 find 原型之后输出。
    void emitIndexHelpers() {
        for (const Decl* fm : indexHelperFinds) {
            const TypeRef& ot = fm->structCommon.fields[0].type;        // out: I&&
            const std::string itemTy = mapType_(resolveAliasName(ot.name));
            auto elemPtrStars = [&]{ for (int i = 1; i < ot.ptr; i++) out << "*"; };  // _o 比 out 少一级指针
            out << "static inline " << itemTy << " ";
            elemPtrStars();
            out << scPfx(fm->name) << "__idx(" << mapType_(fm->methodOwner) << " *_self";
            for (size_t i = 1; i < fm->structCommon.fields.size(); i++) {
                out << ", ";
                emitDeclarator(fm->structCommon.fields[i]);
            }
            out << ") {\n    " << itemTy << " ";
            elemPtrStars();
            out << "_o = (void *)0;\n    return (" << scPfx(fm->name) << "(_self, &_o";
            for (size_t i = 1; i < fm->structCommon.fields.size(); i++)
                out << ", " << fm->structCommon.fields[i].name;
            out << ") == 0) ? _o : (void *)0;\n}\n\n";
        }
    }

    // ---------------- stringify 格式化关键字：按静态类型生成格式化器 ----------------
    // stringify(expr) → stringify_KEY(expr)，返回 adt string（调用者负责 drop）。
    // stringify(expr, 缓存, 大小) → stringify_KEY_buf(expr, 缓存, 大小)，在给定缓存内
    // 构建（截断保证 NUL 结尾），返回 char*（即缓存首址，无需 drop）。
    // 输出为 JSON 字符串格式（键加双引号）。按类型生成的静态内联格式化器在函数体
    // 输出后回填（emitSofHelpers）：开启头文件模式时写入独立 stringify.h，由 .c include。
    struct SofReq {
        std::string key;                // 包装函数名后缀（类型唯一键）
        std::string cParam;             // 包装函数形参 C 声明
        std::string name;               // sc 类型名
        int ptr = 0;                    // 指针层数
        bool needBuf = false;           // 需生成缓存变体 stringify_KEY_buf
        std::vector<std::string> dims;  // 数组维度（仅一维）
    };
    std::map<std::string, SofReq> sofReqs;  // key → 顶层格式化请求
    std::set<std::string> sofAggrs;         // 需生成 sc__sof_T 的聚合（不含 string）
    // 非空时：stringify 支撑代码写入独立头文件（此名），.c 在回填处 #include 它；
    // 空时回退为内联进 .c（--emit-c 输出到 stdout 时保持单文件自包含）。
    std::string sofHeaderName;
    std::string sofHeaderOut;           // 头文件模式下回填生成的头文件全文

    // future<ID> 聚合枚举 future_id 的承载头：非空=头文件模式（.c 顶部 #include 它，
    //   枚举写入独立 type.h 由 main 落盘）；空=内联模式（枚举就地写进 .c，自包含）。
    std::string typeHeaderName;
    bool usesFutureId = false;          // 出现 future<ID>() 构造：需生成 future__new_tagged 辅助

    // cls/dim 全局选择子（SC_CLS_<T> / SC_DIM_<Name>）的承载头：非空=头文件模式
    //   （.c #include 它，枚举由工程级 class.h 提供，跨单元一致编号）；空=内联模式
    //   （枚举就地写进 .c，单文件自包含）。由 emitC 工程/文件模式置为 "class.h"。
    std::string classHeaderName;

    // 泛型单态化实例类型（struct/union/enum/alias）的承载头：非空=头文件模式（自包含
    //   实例类型定义聚合进工程级 generic.h，跨单元去重一致，模块导出签名引用实例类型时
    //   可见）；空=内联模式（实例类型就地写进 .c，单文件自包含）。由 emitC 工程/文件模式
    //   置为 "generic.h"。注意：实例的成员函数始终随各单元 .c 以 static 发出（不进头）。
    std::string genericHeaderName;

    // 根模块导出注入：非空时本单元 .c 在所有 inc 之后追加 #include 该接口头（scm_<root>.h），
    //   使根（集成单元）的 @导出 类型/操作在依赖单元 C 层可见。仅项目构建的非根单元设置。
    std::string rootPreludeHeader;

    // 头支撑单元自检结果：本单元 srcFile 形如 <root>/<stem>/<stem>.sc 且同目录存在手写
    //   <stem>.h（builtins 三件套 / 通用 M.sc+M.h 子项目）。此时本单元 .c 直接 #include
    //   该手写头，并跳过自身 @导出类型的内联定义/前置声明（由头提供），与消费方对该模块的
    //   视图对称——既避免拼接 <stem>_impl.c 时类型重定义，又带入仅头部宏（如 MEM_*）供
    //   拼接的 _impl.c 使用。空=非头支撑单元（按既有内联策略输出自身类型）。
    bool unitHeaderBacked = false;
    std::string headerBackedInclude;    // 待 #include 的手写头路径（如 "builtins/mem/mem.h"）

    // op 单元特例：op.sc 为默认导入的语言运行时模块，其全部类型/接口（含非 @导出的
    //   机制内部类型 limit/operand 等）均由手写 op.h 提供，且 op.h 已经 platform.h
    //   默认带入。故 op 单元 .c 跳过自身「所有」类型定义与 :: 接口原型的输出（不止
    //   @导出者），仅保留 init/drop 与拼接进来的 op_impl.c。
    bool unitOpModule = false;

    // 穿透别名得到最终类型名（最多 8 层防环）
    std::string resolveAliasName(std::string n) const {
        for (int i = 0; i < 8; i++) {
            auto it = aliases.find(n);
            if (it == aliases.end()) break;
            n = it->second;
        }
        return n;
    }

    // 标量分类：'i' 有符号 / 'u' 无符号 / 'f' 浮点 / 'b' bool / 'c' 字符 / 0 非标量
    char scalarClass(const std::string& rawName) const {
        std::string n = resolveAliasName(rawName);
        if (enums.count(n)) return 'i';
        if (n == "i1" || n == "i2" || n == "i4" || n == "i8" || n == "ret") return 'i';
        if (n == "u1" || n == "u2" || n == "u4" || n == "u8") return 'u';
        if (n == "f4" || n == "f8") return 'f';
        if (n == "bool") return 'b';
        if (n == "char" || n == "c1") return 'c';
        return 0;
    }

    // 标量字节宽度（1/2/4/8）：用于 operand 序列化指令按宽度派发 sc_<op>_s/_l/_ll。
    // 非标量（聚合/未知）返回 0；枚举按底层 int（4 字节）。
    int scalarByteSize(const std::string& rawName) const {
        std::string n = resolveAliasName(rawName);
        if (enums.count(n)) return 4;
        if (n == "i1" || n == "u1" || n == "bool" || n == "char" || n == "c1") return 1;
        if (n == "i2" || n == "u2") return 2;
        if (n == "i4" || n == "u4" || n == "f4" || n == "ret") return 4;
        if (n == "i8" || n == "u8" || n == "f8") return 8;
        return 0;
    }

    // sc 类型 → C 类型文本（聚合用规范名，标量经 mapBase）
    std::string cTypeOf(const std::string& name, int ptr) const {
        std::string base;
        if (name.empty()) base = ptr > 0 ? "void" : "char";
        else if (const Decl* sd = aggrOf(name)) base = mapType_(sd->name);
        else base = mapType_(resolveAliasName(name));
        std::string s = base;
        if (ptr > 0) {
            s += " ";
            for (int i = 0; i < ptr; i++) s += "*";
        }
        return s;
    }

    // 数组表达式的维度（Ident 查变量表，Member 查字段定义）
    bool exprDims(const Expr& e, std::vector<std::string>& dims) const {
        if (e.kind == Expr::Ident) {
            auto it = varDimsL.find(e.text);
            if (inFunc && it != varDimsL.end()) { dims = it->second; return true; }
            it = varDimsG.find(e.text);
            if (it != varDimsG.end()) { dims = it->second; return true; }
            return false;
        }
        if (e.kind == Expr::Member) {
            VType base;
            if (!exprVType(*e.a, base)) return false;
            const Decl* sd = aggrOf(base.name);
            if (!sd) return false;
            for (auto& f : sd->structCommon.fields)
                if (f.name == e.text) { dims = f.type.arrayDims; return true; }
        }
        return false;
    }

    // string 格式化调用点：校验类型并登记格式化请求，输出包装函数调用
    void emitStrofCall(const Expr& e) {
        if (e.args.size() != 1 && e.args.size() != 3)
            throw CompileError{"stringify(值[, 缓存, 大小]) 需要 1 或 3 个实参", e.line};
        if (!aggrOf("string"))
            throw CompileError{"stringify(...) 格式化依赖内置 string，请先 inc adt.sc", e.line};
        VType vt;
        if (!exprVType(*e.args[0], vt))
            throw CompileError{"stringify(...) 无法推断实参类型", e.line};
        SofReq r;
        r.name = vt.name;
        r.ptr = vt.ptr;
        if (vt.arr > 0) {
            if (!exprDims(*e.args[0], r.dims) || (int)r.dims.size() != vt.arr)
                throw CompileError{"string(...) 无法确定数组维度", e.line};
            if (r.dims.size() > 1)
                throw CompileError{"stringify(...) 暂不支持多维数组", e.line};
        }
        const Decl* sd = aggrOf(vt.name);
        if (r.dims.empty() && vt.ptr == 0 && !sd && !scalarClass(vt.name))
            throw CompileError{"stringify(...) 不支持该类型：" +
                (vt.name.empty() ? std::string("(无类型)") : vt.name), e.line};
        // 类型唯一键：规范名 + _p（每层指针）+ _a维度
        std::string key = sd ? sd->name
                             : (vt.name.empty() ? std::string("x") : resolveAliasName(vt.name));
        for (int i = 0; i < vt.ptr; i++) key += "_p";
        for (auto& d : r.dims) {
            key += "_a";
            for (char c : d) key += (isalnum((unsigned char)c) ? c : '_');
        }
        r.key = key;
        std::string ct = cTypeOf(vt.name, vt.ptr);
        bool starEnd = !ct.empty() && ct.back() == '*';
        r.cParam = ct + (starEnd ? "" : " ") + (r.dims.empty() ? "_v" : "*_v");
        bool buf = e.args.size() == 3;
        r.needBuf = buf;
        auto it = sofReqs.find(r.key);
        if (it == sofReqs.end()) sofReqs[r.key] = r;
        else if (buf) it->second.needBuf = true;  // 已登记请求按需追加缓存变体
        // stringify<...> 选项块 → (stringify_t){ ... } 复合字面量（末参传入）
        long long optCompact = 0;
        for (auto& o : e.sofOpts) {
            if (o.first == "compact") optCompact = o.second;
            else throw CompileError{"stringify 选项未知键：'" + o.first + "'（当前仅支持 compact）", e.line};
        }
        std::string optLit = "(sc_stringify_t){ .compact = " + std::to_string(optCompact) + " }";
        out << "stringify_" << r.key << (buf ? "_buf" : "") << "(";
        emitExpr(*e.args[0], true);
        if (buf) {
            out << ", ";
            emitExpr(*e.args[1], true);
            out << ", (uint64_t)(";
            emitExpr(*e.args[2], true);
            out << ")";
        }
        out << ", " << optLit << ")";
    }

    // 收集需要生成格式化器的聚合（递归按值字段；指针打地址不递归）
    void collectSofAggr(const std::string& name) {
        const Decl* sd = aggrOf(name);
        if (!sd || sd->name == "string") return;
        if (!sofAggrs.insert(sd->name).second) return;
        for (auto& f : sd->structCommon.fields) {
            if (f.synthetic || f.type.fnKind != TypeRef::FncKind::None || f.type.hasInline)
                continue;
            if (f.type.ptr > 0) continue;
            collectSofAggr(f.type.name);
        }
    }

    // 生成“把 lv 的值格式化追加到 _o”的语句（递归处理数组维度）
    // depthC：本值所在容器的缩进层级 C 表达式（美化模式换行缩进用；compact 模式忽略）
    void emitSofValue(const std::string& lv, const std::string& name, int ptr,
                      const std::vector<std::string>& dims, size_t di, int ind,
                      const std::string& depthC) {
        auto pad = [&] { for (int i = 0; i < ind; i++) out << "    "; };
        char sc = scalarClass(name);
        if (di < dims.size()) {
            // 一维 char 数组：按文本引用输出
            if (sc == 'c' && ptr == 0 && dims.size() - di == 1) {
                pad(); out << "sc_string_append_char(_o, '\"');\n";
                pad(); out << "sc_string_append_n(_o, (char *)(" << lv << "), strnlen("
                           << lv << ", (size_t)(" << dims[di] << ")));\n";
                pad(); out << "sc_string_append_char(_o, '\"');\n";
                return;
            }
            std::string iv = "_i" + std::to_string(di);
            std::string childDepth = "(" + depthC + ") + 1";
            pad(); out << "sc_string_append_char(_o, '[');\n";
            pad(); out << "for (size_t " << iv << " = 0; " << iv << " < (size_t)("
                       << dims[di] << "); " << iv << "++) {\n";
            pad(); out << "    if (" << iv << ") sc_string_append(_o, \",\");\n";
            pad(); out << "    sc__sof_nl(_o, _opt, " << childDepth << ");\n";
            emitSofValue(lv + "[" + iv + "]", name, ptr, dims, di + 1, ind + 1, childDepth);
            pad(); out << "}\n";
            pad(); out << "if ((size_t)(" << dims[di] << ")) sc__sof_nl(_o, _opt, " << depthC << ");\n";
            pad(); out << "sc_string_append_char(_o, ']');\n";
            return;
        }
        if (ptr > 0) {
            const Decl* sd = aggrOf(name);
            pad();
            if (sc == 'c' && ptr == 1) {
                out << "sc__sof_cstr(_o, " << lv << ");\n";          // char* → "文本"
            } else if (ptr == 1 && sd && sd->name != "string") {
                // 结构体一级指针：类型名@地址（不深递归）
                out << "sc__sof_named_ptr(_o, \"" << sd->name << "\", (const void *)("
                    << lv << "));\n";
            } else if (ptr == 1 && sc && sc != 'c') {
                // 标量一级指针：&值（nil → nil）
                out << "if (!(" << lv << ")) sc_string_append(_o, \"nil\");\n";
                pad();
                switch (sc) {
                    case 'i': out << "else sc__sof_amp_i64(_o, (long long)(*(" << lv << ")));\n"; break;
                    case 'u': out << "else sc__sof_amp_u64(_o, (unsigned long long)(*(" << lv << ")));\n"; break;
                    case 'f': out << "else sc__sof_amp_f64(_o, (double)(*(" << lv << ")));\n"; break;
                    case 'b': out << "else sc__sof_amp_bool(_o, (unsigned char)(*(" << lv << ")));\n"; break;
                }
            } else {
                out << "sc__sof_ptr(_o, (const void *)(" << lv << "));\n";  // void*/多级/string* → 0x地址
            }
            return;
        }
        if (sc) {
            pad();
            switch (sc) {
                case 'i': out << "sc__sof_i64(_o, (long long)(" << lv << "));\n"; break;
                case 'u': out << "sc__sof_u64(_o, (unsigned long long)(" << lv << "));\n"; break;
                case 'f': out << "sc__sof_f64(_o, (double)(" << lv << "));\n"; break;
                case 'b': out << "sc__sof_bool(_o, (unsigned char)(" << lv << "));\n"; break;
                case 'c': out << "sc__sof_char(_o, (char)(" << lv << "));\n"; break;
            }
            return;
        }
        const Decl* sd = aggrOf(name);
        if (sd && sd->name == "string") { pad(); out << "sc__sof_str(_o, &(" << lv << "));\n"; return; }
        if (sd) { pad(); out << "sc__sof_" << sd->name << "(_o, &(" << lv << "), _opt, " << depthC << ");\n"; return; }
        pad(); out << "sc_string_append(_o, \"null\");\n";
    }

    // 聚合格式化器：{"字段": 值, ...}（JSON：键加双引号；synthetic/函数指针字段跳过）
    // compact=1 紧凑单行；否则多行美化（2 空格逐层缩进）。
    void emitSofAggrBody(const Decl& sd) {
        out << "static void sc__sof_" << sd.name << "(sc_string *_o, " << mapType_(sd.name)
            << " *_v, sc_stringify_t _opt, int _depth) {\n"
            << "    sc_string_append(_o, \"{\");\n";
        bool any = false;
        for (auto& f : sd.structCommon.fields) {
            if (f.synthetic || f.type.fnKind != TypeRef::FncKind::None) continue;
            if (any) out << "    sc_string_append(_o, \",\");\n";
            out << "    sc__sof_nl(_o, _opt, _depth + 1);\n";
            out << "    sc_string_append(_o, _opt.compact ? \"\\\"" << f.name << "\\\":\" : \"\\\""
                << f.name << "\\\": \");\n";
            if (f.type.hasInline) out << "    sc_string_append(_o, \"null\");\n";
            else emitSofValue("_v->" + f.name, f.type.name, f.type.ptr,
                              f.type.arrayDims, 0, 1, "_depth + 1");
            any = true;
        }
        if (any) out << "    sc__sof_nl(_o, _opt, _depth);\n";
        out << "    sc_string_append(_o, \"}\");\n}\n\n";
    }

    // 顶层包装：string *stringify_KEY(T, stringify_t) —— 堆构造 string、格式化、返回指针
    //（string 为堆专属类型，调用方负责 .drop()/sc_free）
    void emitSofWrapper(const SofReq& r) {
        out << "static sc_string *stringify_" << r.key << "(" << r.cParam << ", sc_stringify_t _opt) {\n"
            << "    sc_string *_o = sc_string__new();\n";
        const Decl* sd = aggrOf(r.name);
        if (r.dims.empty() && r.ptr == 1 && sd) {
            // 聚合一级指针：解引用展开内容（nil → "nil"）
            out << "    if (!_v) sc_string_append(_o, \"nil\");\n";
            if (sd->name == "string") out << "    else sc__sof_str(_o, _v);\n";
            else out << "    else sc__sof_" << sd->name << "(_o, _v, _opt, 0);\n";
        } else {
            emitSofValue("_v", r.name, r.ptr, r.dims, 0, 1, "0");
        }
        out << "    return _o;\n}\n\n";
        if (!r.needBuf) return;
        // 缓存变体：char *stringify_KEY_buf(T, 缓存, 大小, stringify_t) —— 截断拷贝进缓存，返回缓存首址
        out << "static char *stringify_" << r.key << "_buf(" << r.cParam
            << ", char *_buf, uint64_t _n, sc_stringify_t _opt) {\n"
            << "    if (!_buf || !_n) return _buf;\n"
            << "    sc_string *_s = stringify_" << r.key << "(_v, _opt);\n"
            << "    uint64_t _l = _s->size < _n - 1 ? _s->size : _n - 1;\n"
            << "    if (_l && _s->data) memcpy(_buf, _s->data, (size_t)_l);\n"
            << "    _buf[_l] = 0;\n"
            << "    sc_string_drop(_s);\n"
            << "    sc_free(_s);\n"
            << "    return _buf;\n}\n\n";
    }

    // string 格式化支撑代码回填：原语 + 聚合格式化器 + 顶层包装
    void emitSofHelpers() {
        if (sofReqs.empty()) return;
        // 头文件模式：先把支撑代码生成到临时流，包上 include guard 后存入
        // sofHeaderOut，并在当前位置向 .c 输出 #include；否则直接内联进 .c。
        std::ostringstream savedOut;
        const bool toHeader = !sofHeaderName.empty();
        if (toHeader) {
            savedOut = std::move(out);
            out = std::ostringstream();
            std::string guard;
            for (char c : sofHeaderName) guard += std::isalnum((unsigned char)c)
                ? (char)std::toupper((unsigned char)c) : '_';
            out << "#ifndef " << guard << "\n#define " << guard << "\n"
                << "/* 由 scc 生成：stringify 关键字按类型生成的 JSON 格式化器。\n"
                   "   需在 string 类型、op 的 stringify_t（op.h）与各结构体 typedef 之后 #include\n"
                   "   （由生成的 .c 自动完成）。 */\n";
        }
        out << "/* ---- stringify 关键字支撑：格式化原语与按类型生成的格式化器（JSON） ---- */\n"
            << "static inline void sc__sof_i64(sc_string *_o, long long _v) {\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"%lld\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_u64(sc_string *_o, unsigned long long _v) {\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"%llu\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_f64(sc_string *_o, double _v) {\n"
            << "    char _b[40]; snprintf(_b, sizeof(_b), \"%g\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_bool(sc_string *_o, unsigned char _v) {\n"
            << "    sc_string_append(_o, _v ? \"true\" : \"false\"); }\n"
            << "static inline void sc__sof_char(sc_string *_o, char _v) {\n"
            << "    sc_string_append_char(_o, '\\''); sc_string_append_char(_o, _v); sc_string_append_char(_o, '\\''); }\n"
            << "static inline void sc__sof_cstr(sc_string *_o, const char *_v) {\n"
            << "    if (!_v) { sc_string_append(_o, \"nil\"); return; }\n"
            << "    sc_string_append_char(_o, '\"'); sc_string_append(_o, (char *)_v); sc_string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_ptr(sc_string *_o, const void *_v) {\n"
            << "    if (!_v) { sc_string_append(_o, \"nil\"); return; }\n"
            << "    char _b[24]; snprintf(_b, sizeof(_b), \"0x%llx\", (unsigned long long)(uintptr_t)_v);\n"
            << "    sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_named_ptr(sc_string *_o, const char *_tn, const void *_v) {\n"
            << "    if (!_v) { sc_string_append(_o, \"nil\"); return; }\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"@0x%llx\", (unsigned long long)(uintptr_t)_v);\n"
            << "    sc_string_append_char(_o, '\"'); sc_string_append(_o, (char *)_tn);\n"
            << "    sc_string_append(_o, _b); sc_string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_amp_i64(sc_string *_o, long long _v) {\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"\\\"&%lld\\\"\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_u64(sc_string *_o, unsigned long long _v) {\n"
            << "    char _b[28]; snprintf(_b, sizeof(_b), \"\\\"&%llu\\\"\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_f64(sc_string *_o, double _v) {\n"
            << "    char _b[44]; snprintf(_b, sizeof(_b), \"\\\"&%g\\\"\", _v); sc_string_append(_o, _b); }\n"
            << "static inline void sc__sof_amp_bool(sc_string *_o, unsigned char _v) {\n"
            << "    sc_string_append(_o, _v ? \"\\\"&true\\\"\" : \"\\\"&false\\\"\"); }\n"
            << "static inline void sc__sof_str(sc_string *_o, sc_string *_v) {\n"
            << "    sc_string_append_char(_o, '\"');\n"
            << "    if (_v->data) sc_string_append_n(_o, _v->data, _v->size);\n"
            << "    sc_string_append_char(_o, '\"'); }\n"
            << "static inline void sc__sof_nl(sc_string *_o, sc_stringify_t _opt, int _depth) {\n"
            << "    if (_opt.compact) return;\n"
            << "    sc_string_append_char(_o, '\\n');\n"
            << "    for (int _i = 0; _i < _depth; _i++) sc_string_append(_o, \"  \"); }\n\n";
        // 聚合传递闭包（仅按需：按值聚合 / 一维数组元素 / 一级指针解引用）
        for (auto& kv : sofReqs) {
            const SofReq& r = kv.second;
            if (!r.dims.empty()) { if (r.ptr == 0) collectSofAggr(r.name); }
            else if (r.ptr <= 1) collectSofAggr(r.name);
        }
        for (auto& n : sofAggrs)
            out << "static void sc__sof_" << n << "(sc_string *_o, " << mapType_(n) << " *_v, sc_stringify_t _opt, int _depth);\n";
        if (!sofAggrs.empty()) out << "\n";
        for (auto& n : sofAggrs) emitSofAggrBody(*aggrs.at(n));
        for (auto& kv : sofReqs) emitSofWrapper(kv.second);
        if (toHeader) {
            out << "#endif\n";
            sofHeaderOut = out.str();
            out = std::move(savedOut);
            out << "#include \"" << sofHeaderName << "\"\n\n";
        }
    }

    // 查找顶层方法：类型名（穿透别名）→ 方法 Decl（未找到 nullptr）
    const Decl* findMethod(std::string typeName, const std::string& m) const {
        for (int i = 0; i < 8 && !typeName.empty(); i++) {
            auto it = methods.find(typeName);
            if (it != methods.end()) {
                auto mit = it->second.find(m);
                if (mit != it->second.end()) return mit->second;
            }
            auto al = aliases.find(typeName);
            if (al == aliases.end()) return nullptr;
            typeName = al->second;
        }
        return nullptr;
    }

    // ── for-in 容器协议探测（按方法约定，不绑定具体库类型名）─────────────────────
    // 映射协议（键值）：类型 T 同时具备 first + key_at + value_at（游标 + 键值取值）。
    //   for k, v in map 降解：游标遍历、key_at/value_at 取键值，value_at 空句柄即终止
    //   （哨兵无关——dict 的 -1 与 bst/lru 的 0 皆通用）。dict/bst/lru 实现之。
    bool isMapProto(const std::string& T) const {
        return findMethod(T, "first") && findMethod(T, "key_at") && findMethod(T, "value_at");
    }
    // 序列索引协议（单值）：类型 T 具 len + 单「整型参」的 get/at 取值方法（按下标遍历）。
    //   for v in seq [, i] 降解为计数循环。返回取值方法 Decl*（无则 nullptr）。list/array 实现之。
    //   注意整型参判定排除 dict/lru 的 get(key:&)（指针参）——它们走映射协议。
    const Decl* idxGetterOf(const std::string& T) const {
        if (!findMethod(T, "len")) return nullptr;
        const char* names[2] = {"get", "at"};
        for (int k = 0; k < 2; k++) {
            const Decl* g = findMethod(T, names[k]);
            if (!g) continue;
            const auto& ps = g->structCommon.fields;
            if (ps.size() != 1) continue;
            const TypeRef& pt = ps[0].type;
            if (pt.ptr != 0 || !pt.arrayDims.empty()) continue;
            char c = scalarClass(pt.name);
            if (c == 'i' || c == 'u') return g;
        }
        return nullptr;
    }

    // 类型 T 是否含 ≥1 个自动指针 T@ 成员（决定其 init 是否需隐藏 _self_own 参数：
    // init 经 this 给胖成员绑「新边」时，own 取自该参数携带的持有者出边上下文，见 §7.4）。
    bool typeHasFatMember(const std::string& typeName) const {
        const Decl* sd = aggrOf(typeName);
        if (!sd) return false;
        for (auto& f : sd->structCommon.fields)
            if (f.type.fat) return true;
        return false;
    }

    // 当前是否在「含胖成员类型」的 init 体内（其签名带 _self_own）：
    // 决定 this->member = T()/fat 是否可经裸接收者绑新边（own=_self_own，运行时按 REAL 分支）。
    bool curInFatInit = false;

    // 自动指针解绑实参：目标类型 T 有析构 drop → "(void (*)(void *))T_drop"（in→0 时调以清理
    // 子成员，见 auto_ptr.md §5）；无 drop 则空串（解绑点退化为普通 sc_fat_unbind，无析构步）。
    std::string fatDtorArg(const std::string& typeName) const {
        if (typeName.empty()) return "";
        // object@：类型擦除，静态查不到具体析构 → 走通用蹦床 sc_obj_drop（运行时经 SC_DIM_DROP 派发）。
        if (typeName == "object") return "(void (*)(void *))sc_obj_drop";
        const Decl* dm = findMethod(typeName, "drop");
        if (!dm) return "";
        return "(void (*)(void *))" + scPfx(dm->name);
    }

    // T@ 胖目标构造的 sc_ref.flags 实参：合并 <atom>（原子引用计数）/ <raw>（裸分配）标记。
    static std::string ctorRefFlags(const Expr& e) {
        std::string s;
        if (e.ctorAtom) s = "SC_REF_ATOM";
        if (e.ctorRaw)  s = s.empty() ? "SC_REF_RAW" : (s + " | SC_REF_RAW");
        return s.empty() ? "0" : s;
    }

    // 发一条胖指针解绑语句：有 dtorArg 走 sc_fat_unbind_d（带目标类型析构），否则 sc_fat_unbind。
    //   bare = 裸 @（sc_afat）：走 sc_afat_unbind（析构器随句柄，类型擦除）。
    //   thin = 瘦 @^（sc_thin）：走 sc_thin_unbind（dtor 随句柄，无 own 记账）。
    void emitFatUnbind(const std::string& lvAddr, const std::string& dtorArg, bool bare = false,
                       bool thin = false) {
        indent();
        if (thin) out << "sc_thin_unbind(&" << lvAddr << ");\n";
        else if (bare) out << "sc_afat_unbind(&" << lvAddr << ");\n";
        else if (dtorArg.empty()) out << "sc_fat_unbind(&" << lvAddr << ");\n";
        else out << "sc_fat_unbind_d(&" << lvAddr << ", " << dtorArg << ");\n";
    }

    // RAII 预扫描：收集本函数内被显式 x.drop()/x->drop() 调用的变量名（move 语义抑制），
    // 以及被显式取址 &x 的变量名（可能经别名指针管理生命周期，保守抑制以免双重释放）。
    // 注意：方法调用 o.m() 的接收者取址是 codegen 隐式注入，AST 中无 & 节点，不会误伤。
    void scanManualDropsExpr(const Expr* e) {
        if (!e) return;
        if (e->kind == Expr::Call && e->a && e->a->kind == Expr::Member
            && e->a->text == "drop" && e->a->a && e->a->a->kind == Expr::Ident)
            manualDropVars.insert(e->a->a->text);
        if (e->kind == Expr::Unary && e->op == "&" && e->a && e->a->kind == Expr::Ident)
            manualDropVars.insert(e->a->text);
        scanManualDropsExpr(e->a.get());
        scanManualDropsExpr(e->b.get());
        scanManualDropsExpr(e->c.get());
        for (auto& a : e->args) scanManualDropsExpr(a.get());
        for (auto& s : e->fncBody) scanManualDropsStmt(s.get());
    }
    void scanManualDropsStmt(const Stmt* s) {
        if (!s) return;
        scanManualDropsExpr(s->expr.get());
        scanManualDropsExpr(s->forInit.get());
        scanManualDropsExpr(s->forCond.get());
        scanManualDropsExpr(s->forStep.get());
        for (auto& f : s->decls) scanManualDropsExpr(f.init.get());
        for (auto& a : s->printArgs) scanManualDropsExpr(a.get());
        for (auto& b : s->body) scanManualDropsStmt(b.get());
        for (auto& b : s->elseBody) scanManualDropsStmt(b.get());
        for (auto& arm : s->caseArms)
            for (auto& b : arm.body) scanManualDropsStmt(b.get());
    }
    void scanManualDrops(const std::vector<StmtPtr>& body) {
        manualDropVars.clear();
        autoFreeVars.clear();
        for (auto& s : body) scanManualDropsStmt(s.get());
    }

    // 容器下标糖识别：e 是否为「对容器实例的 find 下标」。命中条件——e.a 静态类型是
    // 某 def T: <C, I> 的容器 C（值或一级指针、非数组）且 C 具备 find 方法。命中返回
    // find 方法 Decl 并回填接收者类型 *recv；否则 nullptr（退回普通数组/指针下标）。
    const Decl* containerFindOf(const Expr& e, VType* recv = nullptr) const {
        if (e.kind != Expr::Index || !e.a) return nullptr;
        VType bt;
        if (!exprVType(*e.a, bt) || bt.arr != 0 || bt.ptr > 1) return nullptr;
        if (!adtColls.count(resolveAliasName(bt.name))) return nullptr;
        const Decl* fm = findMethod(bt.name, "find");
        if (!fm || fm->structCommon.fields.empty()) return nullptr;  // 须有 out 参数
        if (recv) *recv = bt;
        return fm;
    }

    // T() 类型伪调用：被调对象是聚合类型名（无参、未被变量遮蔽）
    // → 堆构造糖，返回解析后的聚合类型 Decl（否则 nullptr）
    const Decl* typeCallee(const Expr& call) const {
        if (!call.a || call.a->kind != Expr::Ident) return nullptr;
        // 普通 T()/T(args)：T(args) 经 init 转发构造；future<ID>(ctx) 走 future 专路
        const std::string& n = call.a->text;
        if (localsT.count(n) || globalsT.count(n)) return nullptr;
        return aggrOf(n);
    }

    static bool hasFieldDefaults(const Decl* d) {
        for (auto& f : d->structCommon.fields) if (f.init) return true;
        return false;
    }

    // 标签联合识别：name 经别名解析后是否为 def T: @( ... ) 标签联合
    const Decl* taggedUnionOf(const std::string& name) const {
        const Decl* d = aggrOf(name);
        return (d && d->kind == Decl::UnionD && d->tagged) ? d : nullptr;
    }
    // 在标签联合 d 中查找变体字段（name 为变体名），未找到返回 nullptr
    static const Field* taggedVariant(const Decl* d, const std::string& v) {
        for (auto& f : d->structCommon.fields) if (f.name == v) return &f;
        return nullptr;
    }
    // 判断 Member 表达式是否为「标签联合变体构造引用」T.Variant
    //   （T 为标签联合类型名且未被局部/全局变量遮蔽，Variant 为其变体）→ 返回变体字段
    const Field* taggedCtorMember(const Expr& m, const Decl** outDecl = nullptr) const {
        if (m.kind != Expr::Member || m.op != "." || !m.a || m.a->kind != Expr::Ident)
            return nullptr;
        const std::string& tn = m.a->text;
        if (localsT.count(tn) || globalsT.count(tn)) return nullptr;
        const Decl* d = taggedUnionOf(tn);
        if (!d) return nullptr;
        const Field* v = taggedVariant(d, m.text);
        if (outDecl) *outDecl = d;
        return v;
    }

    // 表达式的轻量类型推断（仅覆盖方法调用需要的场景）
    bool exprVType(const Expr& e, VType& vt) const {
        switch (e.kind) {
            case Expr::Ident: {
                auto it = localsT.find(e.text);
                if (it != localsT.end()) { vt = it->second; return true; }
                it = globalsT.find(e.text);
                if (it != globalsT.end()) { vt = it->second; return true; }
                // 方法接收者 this：类型为 T&（裸一级指针），供胖成员读/解绑的类型推断
                if (e.text == "this" && !curMethodOwner.empty()) {
                    vt = {curMethodOwner, 1, 0};
                    return true;
                }
                return false;
            }
            case Expr::Member: {
                // 分身/切片句柄成员 s._：类型为分身类型 S 的指针（S&）
                if (e.text == "_" && e.a && e.a->kind == Expr::Ident) {
                    const std::string ent = projEntityOf(e.a->text);
                    if (!ent.empty()) {
                        if (const Decl* td = aggrOf(ent))
                            if (!td->projectSelf.empty()) { vt = {td->projectSelf, 1, 0}; return true; }
                    }
                }
                VType base;
                if (!exprVType(*e.a, base)) return false;
                const Decl* sd = aggrOf(base.name);
                if (!sd) return false;
                const std::string fn = memberFieldName(*sd, e.text);
                for (auto& f : sd->structCommon.fields)
                    if (f.name == fn) {
                        vt = {f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
                        vt.fat = f.type.fat;
                        vt.thin = f.type.thin;
                        return true;
                    }
                return false;
            }
            case Expr::Index:
                // 容器下标糖 t[key,...] → find：结果为元素节点类型 I&（out 参数去一级指针）
                if (const Decl* fm = containerFindOf(e)) {
                    const TypeRef& ot = fm->structCommon.fields[0].type;
                    vt = {ot.name, ot.ptr > 0 ? ot.ptr - 1 : 0, 0};
                    return true;
                }
                if (!exprVType(*e.a, vt)) return false;
                if (vt.arr) vt.arr--; else if (vt.ptr) vt.ptr--;
                return true;
            case Expr::Unary:
                if (e.op == "*") {
                    if (!exprVType(*e.a, vt)) return false;
                    if (vt.ptr) vt.ptr--;
                    return true;
                }
                if (e.op == "&") {
                    if (!exprVType(*e.a, vt)) return false;
                    vt.ptr++;
                    return true;
                }
                // 算术/逻辑前缀（-x, +x, ~x, !x, ++x, --x）结果类型 ≈ 操作数类型
                return exprVType(*e.a, vt);
            case Expr::IntLit:   vt = {"i4", 0, 0}; return true;
            case Expr::FloatLit: vt = {"f8", 0, 0}; return true;
            case Expr::CharLit:  vt = {"char", 0, 0}; return true;
            case Expr::StrLit:   vt = {"char", 1, 0}; return true;
            case Expr::Cast:
                vt = {e.op, e.castPtr, 0};
                vt.fat = e.castFat;   // (e: T@) / (e: @) 强转结果为自动指针（name 空 → 裸 @）
                return true;
            case Expr::Binary:
                // 赋值表达式的结果类型 = 左操作数类型（支持 (p = T())->m() 等）
                if (!e.op.empty() && e.op.back() == '='
                    && e.op != "==" && e.op != "!=" && e.op != "<=" && e.op != ">=")
                    return exprVType(*e.a, vt);
                // 算术/位运算结果类型 ≈ 左操作数类型（供 print 推断说明符）
                if (e.op == "+" || e.op == "-" || e.op == "*" || e.op == "/" || e.op == "%"
                    || e.op == "&" || e.op == "|" || e.op == "^" || e.op == "<<" || e.op == ">>")
                    return exprVType(*e.a, vt);
                return false;
            case Expr::Call:
                // inl 内联块调用 → 其返回类型（供无类型 var=inl 推断 + 提升临时 ctype）
                if (e.a && e.a->kind == Expr::Ident) {
                    auto iit = inlineDefs.find(e.a->text);
                    if (iit != inlineDefs.end()) {
                        const auto& rt = iit->second->decl->structCommon.type;
                        if (rt && (rt->fat || !(rt->name.empty() && rt->ptr == 0))) {
                            vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
                            vt.fat = rt->fat;
                            return true;
                        }
                        return false;   // void inl：无返回类型
                    }
                }
                // T() 伪调用结果类型：T&（使链式方法调用可推断；T(args) 同样为 T&）
                if (const Decl* td = typeCallee(e)) {
                    vt = {td->name, 1, 0};
                    return true;
                }
                // stringify(值[, 缓存, 大小]) 结果类型：char*（缓存形态）/ string（使 print 可推断）
                if (e.a && e.a->kind == Expr::Ident && e.a->text == "stringify" && !e.args.empty()
                    && !localsT.count("stringify") && !globalsT.count("stringify")) {
                    if (e.args.size() == 3) vt = {"char", 1, 0};
                    else vt = {"string", 0, 0};
                    return true;
                }
                // 方法调用 o.m(...) / p->m(...) → 方法返回类型（使 print 等可推断）
                if (e.a && e.a->kind == Expr::Member && !callableField(*e.a)) {
                    VType base;
                    if (exprVType(*e.a->a, base) && base.arr == 0 && base.ptr <= 1) {
                        if (const Decl* md = findMethod(base.name, e.a->text)) {
                            const auto& rt = md->structCommon.type;
                            // 胖返回（T@/裸 @）：name 可空（裸 @），按 fat 判定非 void
                            if (rt && (rt->fat || !(rt->name.empty() && rt->ptr == 0))) {
                                vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
                                vt.fat = rt->fat;
                                return true;
                            }
                        }
                    }
                }
                // 函数指针字段调用（含每对象方法指针）→ 该函数类型的返回类型
                if (e.a && e.a->kind == Expr::Member) {
                    if (const Field* cf = callableField(*e.a)) {
                        const auto& rt = cf->type.structCommon.type;
                        if (rt && (rt->fat || !(rt->name.empty() && rt->ptr == 0))) {
                            vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
                            vt.fat = rt->fat;
                            return true;
                        }
                    }
                }
                // 普通函数调用 f(...) → 仅当返回 T@ 胖指针时解析（非胖保持旧行为，
                // 不改动 print/stringify 等既有类型推断，避免撼动 golden）
                if (e.a && e.a->kind == Expr::Ident) {
                    auto fit = funcs.find(e.a->text);
                    if (fit != funcs.end()) {
                        const Decl* sig = fit->second;
                        if (!sig->funcTypeName.empty()) {
                            auto ft = funcTypes.find(sig->funcTypeName);
                            if (ft != funcTypes.end()) sig = ft->second;
                        }
                        const auto& rt = sig->structCommon.type;
                        if (rt && rt->fat) {
                            vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
                            vt.fat = true;
                            return true;
                        }
                    }
                }
                return false;
            default: return false;
        }
    }

    // 表达式静态类型是否胖指针 T@；是则置 *outType 为目标类型名
    bool isFatExpr(const Expr& e, std::string* outType = nullptr) const {
        VType vt;
        if (exprVType(e, vt) && vt.fat) { if (outType) *outType = vt.name; return true; }
        return false;
    }

    // ---------------- 实参求值顺序化 ----------------
    // 表达式是否含「可观测副作用」：调用、赋值/复合赋值、自增自减。
    bool seqHasSideEffect(const Expr& e) const {
        switch (e.kind) {
            case Expr::Call: case Expr::Sync: case Expr::Async: case Expr::Await:
                return true;
            case Expr::PostUnary:
                return true;                                  // i++ / i--
            case Expr::Unary:
                if (e.op == "++" || e.op == "--") return true;
                return e.a && seqHasSideEffect(*e.a);
            case Expr::Binary:
                if (!e.op.empty() && e.op.back() == '=' &&
                    e.op != "==" && e.op != "!=" && e.op != "<=" && e.op != ">=")
                    return true;                              // 赋值 / 复合赋值
                return (e.a && seqHasSideEffect(*e.a)) || (e.b && seqHasSideEffect(*e.b));
            case Expr::Ternary:
                return (e.a && seqHasSideEffect(*e.a)) || (e.b && seqHasSideEffect(*e.b))
                    || (e.c && seqHasSideEffect(*e.c));
            case Expr::Index:
                return (e.a && seqHasSideEffect(*e.a)) || (e.b && seqHasSideEffect(*e.b));
            case Expr::Member: case Expr::Cast: case Expr::Sizeof:
                return e.a && seqHasSideEffect(*e.a);
            default: return false;
        }
    }

    // 求一个「有副作用」实参表达式的 C 类型，用于顺序化临时声明。
    // 仅当能可靠定型、且非胖指针/非数组/非 void 时返回 true（否则保守放弃该调用顺序化）。
    bool seqArgCType(const Expr& e, std::string& ctype) const {
        if (isFatExpr(e)) return false;          // 胖指针含移动/借用簿记，保守不顺序化
        VType vt;
        bool ok = exprVType(e, vt);
        if (!ok && e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident) {
            // 普通函数 / rpc 调用：exprVType 仅对胖返回定型，这里直接取签名返回类型
            const std::string& fn = e.a->text;
            const Decl* sig = nullptr;
            auto fit = funcs.find(fn);
            if (fit != funcs.end()) {
                sig = fit->second;
                if (!sig->funcTypeName.empty()) {
                    auto ft = funcTypes.find(sig->funcTypeName);
                    if (ft != funcTypes.end()) sig = ft->second;
                }
            } else {
                auto rit = rpcs.find(fn);
                if (rit != rpcs.end()) sig = rit->second;
            }
            if (sig) {
                const auto& rt = sig->structCommon.type;
                if (rt && !rt->fat && !(rt->name.empty() && rt->ptr == 0)
                    && rt->arrayDims.empty()) {
                    vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
                    ok = true;
                }
            }
        }
        if (!ok || vt.fat || vt.arr > 0) return false;
        if (vt.name.empty() && vt.ptr == 0) return false;     // void / 无类型
        ctype = cTypeOf(vt.name, vt.ptr);
        return !ctype.empty() && ctype != "void";
    }

    // 规划一个语句级表达式的顺序化：递归找出「实参含 ≥2 个有副作用项且全部可定型」
    // 的调用，把这些有副作用的实参登记为有序临时（seqList 按求值顺序，sub 为替换表）。
    // live=false 表示处于短路/三元分支等条件求值区域 —— 不可外提（会改变求值次数/语义）。
    void planSeq(const Expr& e, bool live,
                 std::vector<SeqHoist>& list,
                 std::unordered_map<const Expr*, std::string>& sub) {
        switch (e.kind) {
            case Expr::Call: {
                if (asInlineCall(&e)) break;                 // inl 调用交由 inl 值提升处理，勿递归其子树
                if (e.a) planSeq(*e.a, live, list, sub);     // 被调对象（o.m 的 o 等）
                int seCount = 0;
                for (auto& a : e.args) if (a && seqHasSideEffect(*a)) seCount++;
                bool doSeq = live && seCount >= 2;
                if (doSeq)
                    for (auto& a : e.args)
                        if (a && seqHasSideEffect(*a)) {
                            std::string ct;
                            if (!seqArgCType(*a, ct)) { doSeq = false; break; }
                        }
                for (auto& a : e.args)                        // 实参均为无条件求值
                    if (a) planSeq(*a, live, list, sub);
                if (doSeq)
                    for (auto& a : e.args) {                  // 左→右登记有副作用实参
                        if (!a || !seqHasSideEffect(*a)) continue;
                        std::string ct;
                        if (!seqArgCType(*a, ct)) continue;
                        std::string nm = "_sq" + std::to_string(seqTmpSeq++);
                        list.push_back({a.get(), nm, ct});
                        sub[a.get()] = nm;
                    }
                break;
            }
            case Expr::Binary:
                // && / || 右操作数短路求值：不可外提
                if (e.a) planSeq(*e.a, live, list, sub);
                if (e.b) planSeq(*e.b, live && e.op != "&&" && e.op != "||", list, sub);
                break;
            case Expr::Ternary:
                if (e.a) planSeq(*e.a, live, list, sub);
                if (e.b) planSeq(*e.b, false, list, sub);     // 分支条件求值
                if (e.c) planSeq(*e.c, false, list, sub);
                break;
            case Expr::Unary: case Expr::PostUnary: case Expr::Cast: case Expr::Sizeof:
                if (e.a) planSeq(*e.a, live, list, sub);
                break;
            case Expr::Index: case Expr::Member:
                if (e.a) planSeq(*e.a, live, list, sub);
                if (e.b) planSeq(*e.b, live, list, sub);
                break;
            default: break;
        }
    }

    // 为语句级表达式 e 发射前置顺序化临时，并设置 curSeqSub 使后续 emitExpr 替换。
    // 返回 true 表示已发射临时（调用方在发射主表达式后须调用 seqHoistEnd）。
    bool seqHoistBegin(const Expr& e) {
        if (curSeqSub) return false;                          // 嵌套上下文保守跳过
        if (inMacro) return false;                            // def 宏体须保持单逻辑行，不外提
        std::vector<SeqHoist> list;
        std::unordered_map<const Expr*, std::string> sub;
        planSeq(e, true, list, sub);
        if (list.empty()) return false;
        seqList = std::move(list);
        seqSubMap = std::move(sub);
        curSeqSub = &seqSubMap;
        for (auto& h : seqList) {
            indent();
            out << h.ctype;
            if (!h.ctype.empty() && h.ctype.back() != '*') out << ' ';
            out << h.name << " = ";
            auto it = seqSubMap.find(h.e);                    // 发射本临时值时不自替换
            std::string nm = it->second;
            seqSubMap.erase(it);
            emitExpr(*h.e, true);
            seqSubMap[h.e] = nm;                              // 复原供外层最终表达式替换
            out << ";\n";
        }
        return true;
    }
    void seqHoistEnd() { curSeqSub = nullptr; seqSubMap.clear(); seqList.clear(); }

    // ---------------- 带值 inl 作子表达式：自动临时提升 ----------------
    struct InlHoist { const Expr* e; std::string name; };   // e=inl 调用节点，name=临时名

    // 规划一个语句级表达式内的 inl 值提升：递归找出「无条件求值区」的 inl 调用，
    // 按求值顺序（嵌套实参内的 inl 先登记）登记为自动临时（list + sub 替换表）。
    // live=false 表示 &&/|| 右侧、三元 ?: 分支等条件求值区域——inl 不可外提 → 报错。
    void planInl(const Expr& e, bool live,
                 std::vector<InlHoist>& list,
                 std::unordered_map<const Expr*, std::string>& sub) {
        switch (e.kind) {
            case Expr::Call: {
                if (asInlineCall(&e)) {
                    if (!live)
                        throw CompileError{"inl 内联块 '" + e.a->text +
                            "' 不能出现在条件求值位置（&&/|| 右侧、三元 ?: 分支）；"
                            "请先用中间变量保存其值", e.line};
                    // 实参内嵌套的 inl 由 emitInlineExpand 在展开块内部自行提升（勿在此递归，
                    // 否则与展开块内提升重复发射）。
                    std::string nm = "_inl" + std::to_string(inlTmpSeq++);
                    list.push_back({ &e, nm });
                    sub[&e] = nm;
                    return;
                }
                if (e.a) planInl(*e.a, live, list, sub);
                for (auto& a : e.args) if (a) planInl(*a, live, list, sub);
                break;
            }
            case Expr::Binary:
                if (e.a) planInl(*e.a, live, list, sub);
                if (e.b) planInl(*e.b, live && e.op != "&&" && e.op != "||", list, sub);
                break;
            case Expr::Ternary:
                if (e.a) planInl(*e.a, live, list, sub);
                if (e.b) planInl(*e.b, false, list, sub);
                if (e.c) planInl(*e.c, false, list, sub);
                break;
            case Expr::Unary: case Expr::PostUnary: case Expr::Cast: case Expr::Sizeof:
                if (e.a) planInl(*e.a, live, list, sub);
                break;
            case Expr::Index: case Expr::Member:
                if (e.a) planInl(*e.a, live, list, sub);
                if (e.b) planInl(*e.b, live, list, sub);
                break;
            case Expr::Sync:
                // 带队列阻塞 sync<q>：内含局部 struct 装填 + 调用，原以 GNU 语句表达式
                //   ({...}) 发射（MSVC 不支持）——提升为语句前临时（与 inl 同框架）。
                //   仅对合法 rpc 调用形式提升；否则留待 emitExpr 报原错误。
                if (e.b && e.a && e.a->kind == Expr::Call && e.a->a &&
                    e.a->a->kind == Expr::Ident && rpcs.count(e.a->a->text)) {
                    if (!live)
                        throw CompileError{"sync<q> 不能出现在条件求值位置（&&/|| 右侧、三元 ?: 分支）；"
                                           "请先用中间变量保存其值", e.line};
                    std::string nm = "_syncq" + std::to_string(inlTmpSeq++);
                    list.push_back({ &e, nm });
                    sub[&e] = nm;
                    return;
                }
                if (e.a) planInl(*e.a, live, list, sub);
                break;
            default: break;
        }
    }

    // 为一组「无条件求值」表达式发射 inl 值提升临时（各 inl 调用先原地展开到 _inlN），
    // 并压入替换层使后续 emitExpr 以临时名引用。返回 true=已压层（须配对 inlHoistEnd）。
    bool inlHoistBegin(const std::vector<const Expr*>& exprs) {
        if (inMacro) return false;                        // def 宏体须保持单逻辑行，不外提
        std::vector<InlHoist> list;
        std::unordered_map<const Expr*, std::string> sub;
        for (auto* e : exprs) if (e) planInl(*e, true, list, sub);
        if (list.empty()) return false;
        inlSubStack.push_back(std::move(sub));
        inlSubPrev.push_back(curInlSub);
        curInlSub = &inlSubStack.back();                  // 展开各 inl 时，其嵌套 inl 实参可被替换
        for (auto& h : list) {
            if (const Stmt* def = asInlineCall(h.e)) {
                std::string ct = inlRetCType(*def);
                indent();
                out << ct;
                if (!ct.empty() && ct.back() != '*') out << ' ';
                out << h.name << ";\n";                    // 先声明临时（值 inl 必有返回类型）
                emitInlineExpand(*def, h.e->args, h.e->a->text, h.name);
            } else {
                emitSyncHoist(*h.e, h.name);               // 带队列 sync<q>：语句块发射，结果存 h.name
            }
        }
        return true;
    }
    void inlHoistEnd() {
        curInlSub = inlSubPrev.back();
        inlSubPrev.pop_back();
        inlSubStack.pop_back();
    }

    // 带队列阻塞 sync<q> 的语句级发射（替代 GNU 语句表达式 ({...})，MSVC 兼容）：
    //   把「struct 装填 + q->sync 调用」发为独立语句块，返回槽存入临时 name。
    //   随后 emitExpr 遇该 sync 节点经 curInlSub 替换为 name（见 planInl / emitExpr 2051）。
    void emitSyncHoist(const Expr& e, const std::string& name) {
        const Decl* r = rpcs.at(e.a->a->text);       // planInl 已保证 e.a 为已知 rpc 调用
        const Expr& call = *e.a;
        if (r->hasAwait)
            throw CompileError{"sync 不能驱动异步 rpc '" + e.a->a->text +
                               "'（含 await，请用 async）", e.line};
        if (r->structCommon.variadic)
            throw CompileError{"sync<q> 暂不支持可变参数 rpc：" + r->name, e.line};
        if (call.args.size() > r->structCommon.fields.size())
            throw CompileError{"rpc 实参数量超出：" + r->name, e.line};
        VType qvt;
        if (!exprVType(*e.b, qvt) || qvt.name != "queue" || !aggrOf("queue"))
            throw CompileError{"sync<q> 的目标须为 queue 端点", e.line};
        checkRpcOptKeys(e, true);                    // <prio:N, delay:ms, timeout:ms>
        const bool isPtr = qvt.ptr >= 1;
        const bool hasRet = rpcHasRet(*r);
        if (hasRet) { indent(); emitRetType(*r); out << " " << name << ";\n"; }
        indent(); out << "{ struct " << scPfx(r->name) << " _rp = {0}; ";
        for (size_t i = 0; i < call.args.size(); i++) {
            const Field& f = r->structCommon.fields[i];
            out << "_rp." << f.name << " = ";
            emitExpr(*call.args[i], true); out << "; ";
            if (!f.type.arrayDims.empty()) {         // 数组实参：额外装填 size（字节数）
                out << "_rp." << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f); out << "; ";
            }
        }
        // 可选状态出参 &st（i4 左值地址）：仅在带 timeout 时有意义，接收 sync 返回码
        if (e.c) {
            if (e.c->kind != Expr::Unary || e.c->op != "&" || !e.c->a)
                throw CompileError{"sync 状态出参须为变量地址形式 &var（i4）", e.line};
            if (!hasOpt(e.syncOpts, "timeout"))
                throw CompileError{"sync 状态出参仅在带 <timeout:ms> 时有效", e.line};
            emitExpr(*e.c->a, true); out << " = ";    // st = q->sync(...)
        }
        emitComBasePtr(*e.b, isPtr);
        out << "->sync(";
        emitComBasePtr(*e.b, isPtr);
        out << ", (void (*)(void *))" << scPfx(r->name) << "_rpc, &_rp, sizeof(_rp), ";
        emitOptOr0(e.syncOpts, "prio", "int32_t"); out << ", ";
        emitOptOr0(e.syncOpts, "delay", "int64_t"); out << ", ";
        emitOptOr0(e.syncOpts, "timeout", "int64_t");
        out << "); ";
        if (hasRet) out << name << " = _rp._; ";      // 返回槽（消费者已回填；超时为 0）
        out << "}\n";
    }

    // 决定语句 s 的哪些「无条件求值」表达式须做 inl 值提升，并发射临时。
    // 快路径（整体 RHS/init 是 inl 调用、裸 inl 调用）不提升——各 case 直接原地展开。
    bool beginInlHoistFor(const Stmt& s) {
        switch (s.kind) {
            case Stmt::ExprS: {
                if (!s.expr) return false;
                if (asInlineCall(s.expr.get())) return false;         // 裸 name(args) 快路径
                if (s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                    asInlineCall(s.expr->b.get())) return false;      // lhs = name(args) 快路径
                return inlHoistBegin({ s.expr.get() });
            }
            case Stmt::ReturnS:
                if (!s.expr) return false;
                return inlHoistBegin({ s.expr.get() });
            case Stmt::VarS: case Stmt::LetS: case Stmt::TlsS: {
                if (s.decls.size() == 1 && s.decls[0].init &&
                    asInlineCall(s.decls[0].init.get())) return false; // 单声明整体 inl 初值快路径
                std::vector<const Expr*> es;
                for (auto& f : s.decls) if (f.init) es.push_back(f.init.get());
                return es.empty() ? false : inlHoistBegin(es);
            }
            case Stmt::PrintS: {
                std::vector<const Expr*> es;
                for (auto& a : s.printArgs) if (a) es.push_back(a.get());
                return es.empty() ? false : inlHoistBegin(es);
            }
            case Stmt::AssertS: {
                std::vector<const Expr*> es;
                if (s.expr) es.push_back(s.expr.get());
                if (s.assertMsg) es.push_back(s.assertMsg.get());
                return es.empty() ? false : inlHoistBegin(es);
            }
            default: return false;
        }
    }

    // 取址 &access 的「最近胖 hop」：从 access 的 base 链向内 walk，首个胖子表达式
    // 即该子成员所住堆对象的 hop（其 tar 即子成员共享的 in 计数）。无胖 base 返回 nullptr。
    const Expr* fatHopOf(const Expr& access) const {
        const Expr* cur = (access.kind == Expr::Member || access.kind == Expr::Index)
                              ? access.a.get() : nullptr;
        while (cur) {
            if (isFatExpr(*cur)) return cur;
            if ((cur->kind == Expr::Member || cur->kind == Expr::Index) && cur->a)
                cur = cur->a.get();
            else break;
        }
        return nullptr;
    }

    // 访问路径的根标识符（沿 Member/Index 的 .a 向内到底）。非标识符根返回 nullptr。
    const Expr* rootIdentOf(const Expr& e) const {
        const Expr* cur = &e;
        while (cur) {
            if (cur->kind == Expr::Ident) return cur;
            if ((cur->kind == Expr::Member || cur->kind == Expr::Index) && cur->a)
                cur = cur->a.get();
            else return nullptr;
        }
        return nullptr;
    }

    // ---- Step4b 预扫描：找出本函数内被 &var 借入胖指针的普通栈变量 ----
    std::unordered_map<std::string, bool> preFatMap;  // 局部名 → 是否 T@（预扫描临时）

    void preCollectLocalFat(const std::vector<StmtPtr>& stmts) {
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            if (s.kind == Stmt::VarS || s.kind == Stmt::LetS || s.kind == Stmt::TlsS)
                for (auto& d : s.decls) preFatMap[d.name] = d.type.fat;
            preCollectLocalFat(s.body);
            preCollectLocalFat(s.elseBody);
            for (auto& arm : s.caseArms) preCollectLocalFat(arm.body);
        }
    }

    // rhs 若是 &<根为非胖局部> → 标记该根变量需注入 ref 头
    void preNoteBorrow(const Expr& rhs) {
        if (rhs.kind != Expr::Unary || rhs.op != "&" || !rhs.a) return;
        const Expr* root = rootIdentOf(*rhs.a);
        if (!root) return;
        auto it = preFatMap.find(root->text);
        if (it != preFatMap.end() && !it->second) fatBorrowVars.insert(root->text);
    }

    void preCollectFatBorrows(const std::vector<StmtPtr>& stmts) {
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            if (s.kind == Stmt::VarS || s.kind == Stmt::LetS)
                for (auto& d : s.decls)
                    if (d.type.fat && d.init) preNoteBorrow(*d.init);
            if (s.kind == Stmt::ExprS && s.expr && s.expr->kind == Expr::Binary &&
                s.expr->op == "=" && s.expr->a && s.expr->a->kind == Expr::Ident) {
                auto it = preFatMap.find(s.expr->a->text);
                if (it != preFatMap.end() && it->second && s.expr->b) preNoteBorrow(*s.expr->b);
            }
            preCollectFatBorrows(s.body);
            preCollectFatBorrows(s.elseBody);
            for (auto& arm : s.caseArms) preCollectFatBorrows(arm.body);
        }
    }

    // 进入函数体前：重建 fatBorrowVars
    void preScanFatBorrows(const std::vector<StmtPtr>& body) {
        fatBorrowVars.clear();
        preFatMap.clear();
        preCollectLocalFat(body);
        preCollectFatBorrows(body);
    }

    // 胖指针 base 解析为裸 T*：((T*)(<base>).p)，供成员/解引用复用
    // 类型名经 mapType_ 归一（sc 声明类型 → sc_ 前缀名），避免发出裸 sc 域类型名。
    void emitFatBaseAsRaw(const Expr& base, const std::string& tt) {
        out << "((" << mapType_(tt) << " *)(";
        emitExpr(base, true);
        out << ").p)";
    }

    // 标量/指针上下文（条件、与 nil 比较）发射：胖指针 T@ 取其 .p 作真值，
    // 否则原样发射（sc_fat 是结构体，不能直接作条件或与 NULL 比较）。
    void emitScalarized(const Expr& e) {
        if (isFatExpr(e)) { out << "("; emitExpr(e, true); out << ").p"; }
        else emitExpr(e, true);
    }


    const Field* callableField(const Expr& m) const {
        if (m.kind != Expr::Member) return nullptr;
        VType base;
        if (!exprVType(*m.a, base)) return nullptr;
        const Decl* sd = aggrOf(base.name);
        if (!sd) return nullptr;
        for (auto& f : sd->structCommon.fields)
            if (f.name == m.text && f.type.fnKind != TypeRef::FncKind::None) return &f;
        return nullptr;
    }

    // ---------------- 缺参调用 0 补全 ----------------
    // 允许实参少于形参：缺少的参数按类型补默认值
    //   指针/数组/函数指针 → NULL；聚合按值 → (T){0}；其余标量 → 0
    void emitDefaultArg(const Field& p) {
        if (p.type.fnKind != TypeRef::FncKind::None) { out << "NULL"; return; }
        std::string base; int ptr;
        resolveType(p.type, base, ptr);
        if (ptr > 0 || !p.type.arrayDims.empty()) { out << "NULL"; return; }
        if (const Decl* sd = aggrOf(p.type.name)) {
            out << "(" << mapType_(sd->name) << "){0}";
            return;
        }
        out << "0";
    }

    // 方法实参：ADT 容器接口 T&⟷I& 零偏移自动重解释。
    // 形参声明为 I&（或 I&&）、实参为 <*, I> 容器元素 T&（或 T&&）时，
    // 把实参指针重解释为 I*（I 注入在 T 首位，offset 0 无需调整）。
    void emitMethodArg(const Expr& arg, const Field* param) {
        if (param && param->type.arrayDims.empty() && param->type.ptr > 0) {
            VType at;
            if (exprVType(arg, at) && (at.ptr > 0 || at.arr > 0)) {
                const Decl* ad = aggrOf(at.name);
                std::string pbase = resolveAliasName(param->type.name);
                if (ad && !ad->adtItem.empty() && !pbase.empty()
                    && pbase == resolveAliasName(ad->adtItem)) {
                    out << "(" << mapType_(pbase);
                    for (int i = 0; i < param->type.ptr; i++) out << " *";
                    out << ")(";
                    emitExpr(arg, true);
                    out << ")";
                    return;
                }
            }
        }
        emitExpr(arg, true);
    }

    //   函数指针变量/参数（内联签名或命名函数类型）→ 顶层 fnc → rpc 包装
    const std::vector<Field>* calleeParams(const std::string& n) const {
        // 变量（含参数）优先：遮蔽同名函数
        const bool isLocal = inFunc && localsT.count(n);
        if (isLocal || globalsT.count(n)) {
            auto& fnVars = isLocal ? fnVarsL : fnVarsG;
            auto fv = fnVars.find(n);
            if (fv != fnVars.end()) return &fv->second->structCommon.fields;
            // 命名函数类型的变量：var cb&: my_fnc_type
            std::string tn = (isLocal ? localsT : globalsT).at(n).name;
            for (int i = 0; i < 8 && !tn.empty(); i++) {
                auto it = funcTypes.find(tn);
                if (it != funcTypes.end()) return &it->second->structCommon.fields;
                auto al = aliases.find(tn);
                if (al == aliases.end()) break;
                tn = al->second;
            }
            return nullptr;
        }
        auto f = funcs.find(n);
        if (f != funcs.end()) {
            const Decl* sig = f->second;
            if (!sig->funcTypeName.empty()) {  // fnc name -> func_type：签名从类型展开
                auto it = funcTypes.find(sig->funcTypeName);
                if (it == funcTypes.end()) return nullptr;
                sig = it->second;
            }
            return &sig->structCommon.fields;
        }
        auto r = rpcs.find(n);
        if (r != rpcs.end()) return &r->second->structCommon.fields;
        return nullptr;
    }


    // ---------------- 表达式 ----------------
    void emitExpr(const Expr& e, bool top = false) {
        // inl 值提升替换：嵌套值 inl 调用已先行展开到自动临时，原位以临时名引用
        if (curInlSub) {
            auto it = curInlSub->find(&e);
            if (it != curInlSub->end()) { out << it->second; return; }
        }
        // 顺序化替换：被外提为有序临时的子表达式，原位以临时名引用（值已先行算好）
        if (curSeqSub) {
            auto it = curSeqSub->find(&e);
            if (it != curSeqSub->end()) { out << it->second; return; }
        }
        switch (e.kind) {
            case Expr::IntLit: {
                // b/w 为 sc 扩展后缀（单/双字节），C 不识别，仅从后缀区剥离；保留 u/l。
                // 注意：十六进制数字里的 b/B 属数字（如 0xEFCDAB89），不能误删。
                std::string digits, sfx;
                splitIntLiteral(e.text, digits, sfx);
                sfx.erase(std::remove_if(sfx.begin(), sfx.end(),
                    [](char c){ return c=='b'||c=='B'||c=='w'||c=='W'; }), sfx.end());
                out << digits << sfx;
                break;
            }
            case Expr::FloatLit:
            case Expr::StrLit: case Expr::CharLit:
                out << e.text;
                break;
            case Expr::Ident:
                // inl 形参快路径代入：形参名直接替换为实参文本（字面值/安全裸变量，无临时）
                if (!inlParamSub.empty()) {
                    auto pit = inlParamSub.find(e.text);
                    if (pit != inlParamSub.end()) { out << pit->second; break; }
                }
                if (e.cBridge) { out << e.text; break; }    // C 桥接 ::name：原样 emit C 符号
                if (e.text == "this") out << "_this";      // 方法内接收者
                else if (e.text == "$") out << "_sc_ret";  // ret 调用语法糖结果变量（$ 非 C99 合法名，映射）
                else if (e.text == "self" && !localsT.count("self") && !globalsT.count("self"))
                    out << "(_this->_self)";  // 分身/切片内上下文关键字：回指本体实体
                                              // （无同名局部时；MethodPtr 实现可用 self 作接收者参数名）
                else if (e.text == "nil") out << "NULL";   // 空指针常量
                else if (e.text == "main" && !localsT.count("main") && !globalsT.count("main"))
                    out << "((struct pool *)-1)";          // queue 宿主哨兵：当前/主线程（pool& 值 -1）
                else if (e.text == "ok" && !localsT.count("ok") && !globalsT.count("ok"))
                    out << "0";                            // ADT 接口成功返回码（类型 ret）
                else if (e.text == "negative" && !localsT.count("negative") && !globalsT.count("negative"))
                    out << "SC_TRIL_NEG";                  // tril 三态字面量
                else if (e.text == "unknown" && !localsT.count("unknown") && !globalsT.count("unknown"))
                    out << "SC_TRIL_UNK";
                else if (e.text == "positive" && !localsT.count("positive") && !globalsT.count("positive"))
                    out << "SC_TRIL_POS";
                else if (curRpc && rpcParams.count(e.text))
                    out << "_p->" << e.text;               // rpc 实际函数内：参数即结构体成员
                else if (localsT.count(e.text)) out << e.text;   // 局部/参数：块作用域，不加前缀
                else if (globalsT.count(e.text) || funcs.count(e.text) ||
                         rpcs.count(e.text) || enumConsts.count(e.text) ||
                         macroNames.count(e.text))
                    out << scPfx(e.text);                  // sc 域全局/函数/rpc/枚举常量/宏：加 sc_ 前缀
                else if (mapType_(e.text) != e.text)
                    out << mapType_(e.text);               // sc 声明的类型名作表达式（如 mix 宏的类型实参 def_vec(Point,..)）→ 映射为 C 类型名 sc_Point
                else out << e.text;                        // true/false（stdbool.h）、外部 C（由 ::/def 或语义层把关）
                break;
            case Expr::Unary:
                // --check=ptr：裸指针解引用 *p → *SC_PTRCHK(p, site)（nil 校验，胖指针走独立路径不拦）
                if (g_ptrCheck && e.op == "*" && e.a && !isFatExpr(*e.a)) {
                    out << "*SC_PTRCHK(";
                    emitExpr(*e.a);
                    out << ", \"" << ptrSite(e, "解引用") << "\")";
                    break;
                }
                out << e.op;
                out << "(";
                emitExpr(*e.a);
                out << ")";
                break;
            case Expr::PostUnary:
                emitExpr(*e.a);
                out << e.op;
                break;
            case Expr::Binary:
                if (!top) out << "(";
                // 胖指针 T@ 参与 == / != 比较：两侧各取 .p（nil 侧发射 NULL），
                // 因 sc_fat 是结构体不能直接比较。
                if ((e.op == "==" || e.op == "!=") && (isFatExpr(*e.a) || isFatExpr(*e.b))) {
                    emitScalarized(*e.a);
                    out << " " << e.op << " ";
                    emitScalarized(*e.b);
                } else {
                    emitExpr(*e.a);
                    out << " " << e.op << " ";
                    emitExpr(*e.b);
                }
                if (!top) out << ")";
                break;
            case Expr::Ternary:
                if (!top) out << "(";
                emitExpr(*e.a);
                out << " ? ";
                emitExpr(*e.b);
                out << " : ";
                emitExpr(*e.c);
                if (!top) out << ")";
                break;
            case Expr::Call: {
                // inl 内联块调用未被值提升即到达此处 = 非法位置（循环/if 条件、
                // &&/|| 右侧、三元 ?: 分支等条件求值区域）。
                if (asInlineCall(&e))
                    throw CompileError{"inl 内联块 '" + e.a->text +
                        "' 只能出现在无条件求值的位置（赋值右侧 / return / var|let 初始化 / "
                        "print|assert 实参 / 调用实参等），不能用于循环或 if 条件、&&/|| 右侧、"
                        "三元 ?: 分支；请先用中间变量保存其值", e.line};
                // C 桥接调用 ::name(args)：原样 emit C 函数/宏调用，跳过所有 sc 调用糖
                if (e.a && e.a->kind == Expr::Ident && e.a->cBridge) {
                    out << e.a->text << "(";
                    for (size_t i = 0; i < e.args.size(); i++) {
                        if (i) out << ", ";
                        emitExpr(*e.args[i], true);
                    }
                    out << ")";
                    break;
                }
                // rpc 只能经驱动调用（sync/async/run/<<），禁止裸 rpc() 直接调用：
                // 流程语义须显式选定驱动形态。当前线程直接执行请用 `sync work(args)`。
                if (!emitRpcCallOK && e.a && e.a->kind == Expr::Ident &&
                    rpcs.count(e.a->text))
                    throw CompileError{"rpc '" + e.a->text +
                        "' 不能直接调用，须经驱动：当前线程执行用 sync work(args)，"
                        "另有 async/run/队列 << 形态", e.line};
                // base/prev/next 内置函数：链表结构体字段导航
                if (e.a && e.a->kind == Expr::Ident && !localsT.count(e.a->text) &&
                    !globalsT.count(e.a->text) && !funcs.count(e.a->text)) {
                    const std::string kw = e.a->text;
                    if ((kw == "base" || kw == "prev" || kw == "next") && e.args.size() == 1) {
                        const Expr* src = &*e.args[0];
                        bool typed = src->kind == Expr::Cast;
                        if (typed) src = src->a.get();
                        VType vt;
                        bool hasVt = exprVType(*src, vt);
                        const Decl* sd = hasVt ? aggrOf(vt.name) : nullptr;

                        auto emitRawSrc = [&]() {
                            if (vt.ptr > 0 || vt.arr > 0) emitExpr(*src, true);
                            else {
                                out << "&(";
                                emitExpr(*src, true);
                                out << ")";
                            }
                        };

                        // base(o: T) 直接把节点首址重解释为 T*
                        auto emitBaseCast = [&](const Expr& castExpr) {
                            out << "((" << mapType_(castExpr.text);
                            for (int i = 0; i < castExpr.castPtr + 1; i++) out << "*";
                            out << ")(";
                            emitRawSrc();
                            out << "))";
                        };

                        if (kw == "base") {
                            if (typed) { emitBaseCast(*e.args[0]); break; }
                            if (hasVt && sd && (sd->linked || !sd->adtItem.empty())) {
                                const Field* rf = firstRealField(sd);
                                if (rf) {
                                    if (vt.ptr > 0 || vt.arr > 0) {
                                        out << "((void *)&((";
                                        emitExpr(*src, true);
                                        out << ")->" << rf->name << "))";
                                    } else {
                                        out << "((void *)&((";
                                        emitExpr(*src, true);
                                        out << ")." << rf->name << "))";
                                    }
                                    break;
                                }
                            }
                            out << "((void *)";
                            emitRawSrc();
                            out << ")";
                            break;
                        }

                        if (kw == "prev" || kw == "next") {
                            // _prev 在偏移 0，_next 在偏移 sizeof(void*)
                            const char* off = kw == "prev" ? "0" : "sizeof(void *)";
                            // prev：边界安全前驱（head→NULL），经 adt 契约 chain_prev
                            if (kw == "prev") {
                                requireChain(e.line);
                                if (typed) {
                                    const Expr& c = *e.args[0];
                                    out << "((" << mapType_(c.text);
                                    for (int i = 0; i < c.castPtr + 1; i++) out << "*";
                                    out << ")sc_chain_prev(";
                                    emitRawSrc();
                                    out << "))";
                                    break;
                                }
                                out << "((void *)sc_chain_prev(";
                                emitRawSrc();
                                out << "))";
                                break;
                            }
                            if (typed) {
                                // next(o: T) → 读链接字段并转为 T*
                                const Expr& c = *e.args[0];
                                out << "((" << mapType_(c.text);
                                for (int i = 0; i < c.castPtr + 1; i++) out << "*";
                                out << ")*(void **)((char *)(";
                                emitRawSrc();
                                out << ") + " << off << "))";
                                break;
                            }
                            out << "((void *)*(void **)((char *)(";
                            emitRawSrc();
                            out << ") + " << off << "))";
                            break;
                        }
                    }
                }
                // instanceOf(o, TypeName) → ((*(o)) == TypeName_hyper_impl)：O(1) 类型判定
                if (e.a->kind == Expr::Ident && e.a->text == "instanceOf"
                    && !localsT.count("instanceOf") && !globalsT.count("instanceOf")
                    && !funcs.count("instanceOf")) {
                    if (e.args.size() != 2 || e.args[1]->kind != Expr::Ident)
                        throw CompileError{"instanceOf(o, 类名) 需要 2 个实参且第二实参为类名", e.line};
                    // 不加外层冗余括号（== 优先级高于 && / || / 三元；一元 ! 自带操作数括号），
                    // 既保证语义优先级又避免 if 条件直接套等式的 -Wparentheses-equality 告警。
                    out << "*(";
                    emitExpr(*e.args[0], true);
                    out << ") == " << mapType_(e.args[1]->text) << "_hyper_impl";
                    break;
                }
                // 类型伪调用糖：T() → 堆构造 T__new()（malloc + 默认值 + init）
                if (const Decl* td = typeCallee(e)) {
                    // <atom>/<raw> 仅用于自动指针 T@ 目标构造（赋给 T@ 变量/成员，经 emitFatBind）；
                    // 此处是普通堆构造（赋给 T& 或表达式位）→ 标记无处安放，报错避免静默丢标记。
                    if (e.ctorAtom)
                        throw CompileError("atom 标记仅用于自动指针 T@ 目标构造，"
                                           "普通堆构造 T() 不支持", e.line);
                    if (e.ctorRaw)
                        throw CompileError("raw 标记仅用于自动指针 T@ 目标构造，"
                                           "普通堆构造 T() 不支持", e.line);
                    if (!e.futureId.empty()) {  // future<ID>(ctx?) → 打 id 标签 + 可选 ctx
                        out << mapType_(td->name) << "__new_tagged(" << scPfx(e.futureId) << ", ";
                        if (e.args.empty()) out << "(void *)0";
                        else emitExpr(*e.args[0], true);
                        out << ")";
                    } else if (!e.args.empty()) {
                        // T(args) → T__new_init(args)：分配 + init(args) 转发
                        const Decl* im = findMethod(td->name, "init");
                        if (!im || im->structCommon.fields.empty())
                            throw CompileError("类型 '" + td->name +
                                "' 的 init 无参数，不支持带参构造 T(...)", e.line);
                        if (e.args.size() != im->structCommon.fields.size())
                            throw CompileError("构造 '" + td->name +
                                "(...)' 实参个数与 init 形参不符", e.line);
                        out << mapType_(td->name) << "__new_init(";
                        for (size_t i = 0; i < e.args.size(); i++) {
                            if (i) out << ", ";
                            emitMethodArg(*e.args[i], &im->structCommon.fields[i]);
                        }
                        out << ")";
                    } else
                        out << mapType_(td->name) << "__new()";
                    break;
                }
                // print 关键字：C 风格日志输出（io 子项目 print，未被同名定义遮蔽时）
                if (e.a->kind == Expr::Ident && e.a->text == "print"
                    && !localsT.count("print") && !globalsT.count("print") && !funcs.count("print")) {
                    if (e.args.empty())
                        throw CompileError{"print 需要格式串实参", e.line};
                    out << "sc_print(";
                    for (size_t i = 0; i < e.args.size(); i++) {
                        if (i) out << ", ";
                        emitExpr(*e.args[i], true);
                    }
                    out << ")";
                    break;
                }
                // stringify 格式化关键字：stringify(值) → adt string；stringify(值, 缓存, 大小) → char*
                // （无参 string() 走上面的 T() 堆构造糖；被同名定义遮蔽时按普通调用）
                if (e.a->kind == Expr::Ident && e.a->text == "stringify" && !e.args.empty()
                    && !localsT.count("stringify") && !globalsT.count("stringify") && !funcs.count("stringify")) {
                    emitStrofCall(e);
                    break;
                }
                // 标签联合变体构造：T.Variant(payload) → ((T){ .tag = T__Variant, .u.Variant = payload })
                if (e.a->kind == Expr::Member) {
                    const Decl* td = nullptr;
                    if (const Field* vf = taggedCtorMember(*e.a, &td)) {
                        const bool hasPayload = !vf->type.name.empty() || vf->type.ptr > 0;
                        if (!hasPayload) {
                            if (!e.args.empty())
                                throw CompileError("标签联合变体 " + td->name + "." + vf->name +
                                                   " 无载荷，不能带实参构造", e.line);
                            out << "((" << mapType_(td->name) << "){ .tag = " << mapType_(td->name) << "__" << vf->name << " })";
                            break;
                        }
                        if (e.args.size() != 1)
                            throw CompileError("标签联合变体 " + td->name + "." + vf->name +
                                               " 需要且仅需要一个载荷实参", e.line);
                        out << "((" << mapType_(td->name) << "){ .tag = " << mapType_(td->name) << "__" << vf->name
                            << ", .u." << vf->name << " = ";
                        emitExpr(*e.args[0], true);
                        out << " })";
                        break;
                    }
                }
                // 顶层方法调用糖：o.m(...) / p->m(...) → T_m(&o/p, ...)
                if (e.a->kind == Expr::Member && !callableField(*e.a)) {
                    VType base;
                    if (exprVType(*e.a->a, base) && base.arr == 0 && base.ptr <= 1) {
                        // 堆专属类型 NAME& 的普通指针 .drop()：先调用户 drop（释放内部资源，
                        // 若有），再 sc_free 结构体块本身。无用户 drop → 仅 sc_free（默认析构）。
                        // 胖指针 NAME@ 由运行时 sc_fat_on_zero_d 释放，不在此拦截。
                        if (e.a->text == "drop" && e.args.empty() && !base.fat
                            && base.ptr == 1 && isHeapOnly(base.name)) {
                            const Decl* dm = findMethod(base.name, "drop");
                            out << "(";
                            if (dm) { out << scPfx(dm->name) << "("; emitExpr(*e.a->a); out << "), "; }
                            out << "sc_free(";
                            emitExpr(*e.a->a);
                            out << "))";
                            break;
                        }
                        // 维度调用糖：cls 实例 o.Dim(args) → T_hyper_impl(&o._class, SC_DIM_Dim, args)

                        if (classNames.count(base.name) && isDimCallName(e.a->text)) {
                            out << mapType_(base.name) << "_hyper_impl(&";
                            if (base.fat) { out << "("; emitFatBaseAsRaw(*e.a->a, base.name); out << ")->_class"; }
                            else if (e.a->op == ".") { out << "("; emitExpr(*e.a->a); out << ")._class"; }
                            else { out << "("; emitExpr(*e.a->a); out << ")->_class"; }
                            out << ", SC_DIM_" << e.a->text;
                            for (auto& a : e.args) { out << ", "; emitExpr(*a, true); }
                            out << ")";
                            break;
                        }
                        // 维度调用糖：object 动态接收者 ob.Dim(args) → (*ob)(ob, SC_DIM_Dim, args)
                        if (base.name == "object" && isDimCallName(e.a->text)) {
                            if (base.fat) {
                                // object@ 胖接收者：分派槽为擦除的 _class 槽指针 (object)(recv).p
                                out << "(*(object)("; emitExpr(*e.a->a); out << ").p)((object)(";
                                emitExpr(*e.a->a); out << ").p, SC_DIM_" << e.a->text;
                            } else {
                                out << "(*("; emitExpr(*e.a->a); out << "))(";
                                emitExpr(*e.a->a); out << ", SC_DIM_" << e.a->text;
                            }
                            for (auto& a : e.args) { out << ", "; emitExpr(*a, true); }
                            out << ")";
                            break;
                        }
                        if (const Decl* md = findMethod(base.name, e.a->text)) {
                            out << scPfx(md->name) << "(";   // 修饰名 T_m
                            if (base.fat) {
                                // 胖指针接收者：传底层堆对象指针 (T*)(recv).p（已是 T*，不取址）
                                emitFatBaseAsRaw(*e.a->a, base.name);
                            } else {
                                if (e.a->op == ".") out << "&";
                                emitExpr(*e.a->a);
                            }
                            for (size_t i = 0; i < e.args.size(); i++) {
                                out << ", ";
                                const Field* pf = i < md->structCommon.fields.size()
                                                ? &md->structCommon.fields[i] : nullptr;
                                emitMethodArg(*e.args[i], pf);
                            }
                            // 缺参 0 补全（接收者已占首参，后续皆需逗号）
                            for (size_t i = e.args.size(); i < md->structCommon.fields.size(); i++) {
                                out << ", ";
                                emitDefaultArg(md->structCommon.fields[i]);
                            }
                            // 显式调用含胖成员类型的 init：补隐藏 _self_own（胖接收者→其出边，
                            // 否则裸/值接收者→ SC_OWN_RAW 退化，见 §7.4）。
                            if (e.a->text == "init" && typeHasFatMember(base.name)) {
                                if (base.fat) {
                                    out << ", &((sc_ref *)(";
                                    emitExpr(*e.a->a, true);
                                    out << ").tar)->out";
                                } else {
                                    out << ", SC_OWN_RAW";
                                }
                            }
                            out << ")";
                            break;
                        }
                    }
                    // operand 序列化指令（read/write/nread/nwrite/nget/nset）：与类型无关的
                    // 原子指令不同，这些需按接收者标量宽度派发到 platform.h 的
                    // sc_<op>_s/_l/_ll（仅支持 2/4/8 字节标量接收者）。
                    {
                        static const std::unordered_set<std::string> kSerOps = {
                            "read", "write", "nread", "nwrite", "nget", "nset"};
                        if (kSerOps.count(e.a->text) && findMethod("operand", e.a->text)) {
                            const std::string& op = e.a->text;
                            VType rv;
                            bool ok = exprVType(*e.a->a, rv);
                            int ptr = rv.ptr - (e.a->op == "->" ? 1 : 0);
                            int sz = ok ? scalarByteSize(rv.name) : 0;
                            if (!ok || ptr != 0 || rv.arr != 0
                                || (sz != 2 && sz != 4 && sz != 8))
                                throw CompileError{"operand 序列化指令 " + op
                                    + " 仅支持 2/4/8 字节标量接收者", e.line};
                            const char* sfx = sz == 2 ? "s" : sz == 4 ? "l" : "ll";
                            // 接收者地址形态（&v / p）与值形态（v / *p）
                            auto emitAddr = [&] {
                                if (e.a->op == ".") out << "&";
                                out << "("; emitExpr(*e.a->a, true); out << ")";
                            };
                            auto emitVal = [&] {
                                if (e.a->op == ".") { out << "("; emitExpr(*e.a->a, true); out << ")"; }
                                else { out << "(*("; emitExpr(*e.a->a, true); out << "))"; }
                            };
                            if (op == "read" || op == "nread") {
                                if (e.args.size() != 1)
                                    throw CompileError{op + " 需要 1 个缓冲实参", e.line};
                                out << "sc_" << op << "_" << sfx << "(";
                                emitAddr(); out << ", "; emitExpr(*e.args[0], true); out << ")";
                                break;
                            }
                            if (op == "write" || op == "nwrite") {
                                if (e.args.size() != 1)
                                    throw CompileError{op + " 需要 1 个缓冲实参", e.line};
                                out << "sc_" << op << "_" << sfx << "(";
                                emitExpr(*e.args[0], true); out << ", "; emitVal(); out << ")";
                                break;
                            }
                            if (op == "nget") {
                                if (!e.args.empty())
                                    throw CompileError{"nget 不接受实参", e.line};
                                out << "sc_nget_" << sfx << "("; emitVal(); out << ")";
                                break;
                            }
                            // nset：v = sc_nset_X(网络序值)
                            if (e.args.size() != 1)
                                throw CompileError{"nset 需要 1 个网络序值实参", e.line};
                            out << "("; emitVal(); out << " = sc_nset_" << sfx << "(";
                            emitExpr(*e.args[0], true); out << "))";
                            break;
                        }
                    }
                    // operand 透传（platform.h 的 sc 侧）：接收者无同名方法时，把基础/任意
                    // 类型上的 . 操作透传为设备操作数通用指令 sc_<op>(&recv/recv, args...)。
                    // op.sc 的 operand 伪结构体登记可用指令集；C 侧 platform.h 提供同名
                    // sc_<op> 宏（类型无关，忽略入参/返回值类型）。
                    if (findMethod("operand", e.a->text)) {
                        out << "sc_" << e.a->text << "(";
                        if (e.a->op == ".") out << "&";   // 值接收者取址（透传指针，契合原子等指令）
                        out << "(";
                        emitExpr(*e.a->a, true);
                        out << ")";
                        for (auto& a : e.args) {
                            out << ", ";
                            emitExpr(*a, true);
                        }
                        out << ")";
                        break;
                    }
                }
                // 函数指针字段/变量/顶层函数调用（含缺参 0 补全）
                const Field* mf = callableField(*e.a);
                const std::vector<Field>* params = nullptr;
                if (mf) params = &mf->type.structCommon.fields;
                else if (e.a->kind == Expr::Ident && e.a->text != "this")
                    params = calleeParams(e.a->text);
                emitExpr(*e.a);
                out << "(";
                bool firstArg = true;
                // 每对象方法指针：按成员函数约定自动注入接收者为首参（. 取址 / -> 直传）
                if (mf && mf->type.fnKind == TypeRef::FncKind::MethodPtr
                    && e.a->kind == Expr::Member) {
                    if (e.a->op == ".") out << "&";
                    emitExpr(*e.a->a);
                    firstArg = false;
                }
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (!firstArg) out << ", ";
                    firstArg = false;
                    emitExpr(*e.args[i], true);
                }
                if (params)
                    for (size_t i = e.args.size(); i < params->size(); i++) {
                        if (!firstArg) out << ", ";
                        firstArg = false;
                        emitDefaultArg((*params)[i]);
                    }
                out << ")";
                break;
            }
            case Expr::Index:
                // 容器下标糖：t[key,...] → C_find__idx(&t, key,...)，即对容器实例执行 find，
                // 命中得元素节点 I&（用 : T& 下转回元素），未命中/出错为 nil。包装函数把出参
                // 回填与命中判定收进一次普通调用，避免 GNU 语句表达式（MSVC 不支持）。
                {
                    VType recv;
                    if (const Decl* fm = containerFindOf(e, &recv)) {
                        bool seen = false;
                        for (const Decl* h : indexHelperFinds) if (h == fm) { seen = true; break; }
                        if (!seen) indexHelperFinds.push_back(fm);
                        out << scPfx(fm->name) << "__idx(";
                        if (recv.ptr == 0) out << "&";                 // 值接收者自动取址
                        emitExpr(*e.a);
                        // 检索键（首键 e.b + 其余 e.args）逐一过 find 参数（自动 T&⟷I& 转换）
                        size_t keyi = 1;  // fields[0] 为 out，键从 fields[1] 起
                        auto emitKey = [&](const Expr& k) {
                            out << ", ";
                            const Field* pf = keyi < fm->structCommon.fields.size()
                                            ? &fm->structCommon.fields[keyi] : nullptr;
                            emitMethodArg(k, pf);
                            keyi++;
                        };
                        if (e.b) emitKey(*e.b);
                        for (auto& a : e.args) emitKey(*a);
                        out << ")";
                        break;
                    }
                }
                // --check=ptr：已知维度的栈数组下标 → SC_BOUNDCHK 越界校验；裸指针下标 → SC_PTRCHK nil 校验。
                if (g_ptrCheck) {
                    const std::vector<std::string>* dims =
                        (e.a && e.a->kind == Expr::Ident) ? knownDims(e.a->text) : nullptr;
                    if (dims && !dims->empty()) {
                        emitExpr(*e.a);
                        out << "[SC_BOUNDCHK(";
                        emitExpr(*e.b, true);
                        out << ", " << (*dims)[0] << ", \"" << ptrSite(e, "数组下标") << "\")]";
                        break;
                    }
                    VType bt;
                    if (e.a && exprVType(*e.a, bt) && bt.ptr > 0 && !bt.fat && bt.arr == 0) {
                        out << "SC_PTRCHK(";
                        emitExpr(*e.a);
                        out << ", \"" << ptrSite(e, "指针下标") << "\")[";
                        emitExpr(*e.b, true);
                        out << "]";
                        break;
                    }
                }
                emitExpr(*e.a);
                out << "[";
                emitExpr(*e.b, true);
                out << "]";
                break;
            case Expr::Member: {
                // 标签联合无载荷变体构造：T.Variant → ((T){ .tag = T__Variant })
                {
                    const Decl* td = nullptr;
                    if (const Field* vf = taggedCtorMember(e, &td)) {
                        if (!vf->type.name.empty() || vf->type.ptr > 0)
                            throw CompileError("标签联合变体 " + td->name + "." + vf->name +
                                               " 带载荷，构造须写 " + td->name + "." + vf->name +
                                               "(载荷)", e.line);
                        out << "((" << mapType_(td->name) << "){ .tag = " << mapType_(td->name) << "__" << vf->name << " })";
                        break;
                    }
                }
                // 胖指针 T@ 成员访问：p->field → ((T*)(p).p)->field（base 先解胖为裸 T*）
                {
                    std::string tt;
                    if (e.a && isFatExpr(*e.a, &tt)) {
                        emitFatBaseAsRaw(*e.a, tt);
                        out << "->" << e.text;
                        break;
                    }
                }
                // prev 上下文关键字（链表结构体）：边界安全前驱 → head 返回 NULL
                if (e.text == "prev") {
                    VType base;
                    if (exprVType(*e.a, base)) {
                        const Decl* sd = aggrOf(base.name);
                        if (sd && sd->linked) {
                            requireChain(e.line);
                            out << "((void *)sc_chain_prev(";
                            if (e.op == "->") emitExpr(*e.a);
                            else { out << "&("; emitExpr(*e.a, true); out << ")"; }
                            out << "))";
                            break;
                        }
                    }
                }
                // --check=ptr：裸指针成员访问 p->m → SC_PTRCHK(p, site)->m（nil 校验；
                // 胖指针已在上方走独立路径，prev/next 链表导航自带边界处理，均不重复拦）。
                if (g_ptrCheck && e.op == "->") {
                    out << "SC_PTRCHK(";
                    emitExpr(*e.a);
                    out << ", \"" << ptrSite(e, "成员访问") << "\")";
                } else {
                    emitExpr(*e.a);
                }
                std::string fn = e.text;
                if (e.text == "next") {
                    // 上下文关键字：链表结构体上 next 映射到内置 _next（rear 的 _next=NULL 自然终止）
                    VType base;
                    if (exprVType(*e.a, base)) {
                        const Decl* sd = aggrOf(base.name);
                        if (sd && sd->linked) fn = "_next";
                    }
                }
                out << e.op << fn;
                break;
            }
            case Expr::Sizeof: {
                // --check=mem：栈数组超额分配了尾哨兵，sizeof(数组变量) 须回报「逻辑大小」
                // （原维度×元素），否则 memset(buf,0,sizeof buf) 会抹掉尾哨兵造成误报。
                if (g_memCheck && e.a && e.a->kind == Expr::Ident) {
                    if (const MemCanaryVar* mc = findCanary(e.a->text)) {
                        out << "((" << canaryElems(mc->dims) << ") * sizeof(" << mc->elemTy << "))";
                        break;
                    }
                }
                out << "sizeof(";
                // 若内层是单纯标识符且是 sc 内置类型名，做类型映射再输出
                if (e.a && e.a->kind == Expr::Ident) {
                    const std::string mapped = mapType_(e.a->text);
                    if (mapped != e.a->text) { out << mapped; }
                    else emitExpr(*e.a, true);
                } else {
                    emitExpr(*e.a, true);
                }
                out << ")";
                break;
            }
            case Expr::Offsetof:
                out << "offsetof(" << mapType_(e.text) << ", " << e.op << ")";
                break;
            case Expr::Cast: {
                // print 格式覆盖 (expr: "%fmt") 仅能出现在 print 实参（在 emitPrintStmt
                // 中被拆解）；出现在其他表达式位置则是误用。
                if (e.castIsFmt)
                    throw CompileError{"格式说明符 (expr: \"%fmt\") 只能用于 print 实参", e.line};
                // 自动指针类型擦除/恢复强转（单层求值，经语句表达式只算一次操作数）：
                //   (e: T@)  恢复：裸 @（sc_afat）/T@ → sc_fat 视图（丢 dtor，前 24B 同构）
                //   (e: @)   擦除：T@（sc_fat）→ 裸 @（sc_afat），dtor 取 e 静态类型 T 的析构
                if (e.castFat) {
                    std::string srcT;
                    bool srcFat = isFatExpr(*e.a, &srcT);
                    // —— 瘦指针强转 (e: T*) / (e: *)：sc_thin{p,tar,dtor}（无 own）——
                    if (e.castThin) {
                        if (e.op.empty()) {
                            if (srcFat && srcT.empty()) { out << "("; emitExpr(*e.a, true); out << ")"; break; }
                            if (!srcFat) {
                                VType svt;
                                bool isPtr = exprVType(*e.a, svt) && (svt.ptr > 0 || svt.arr > 0);
                                out << "((sc_thin){" << (isPtr ? "(void *)(" : "(void *)(intptr_t)(");
                                emitExpr(*e.a, true);
                                out << "), (int32_t *)0, (void (*)(void *))0})";
                                break;
                            }
                            if (srcFat && srcT != "object" && classNames.count(srcT)) {
                                out << "sc_fat_as_thin_addoff(";
                                emitExpr(*e.a, true);
                                out << ", offsetof(" << mapType_(srcT)
                                    << ", _class), (void (*)(void *))sc_obj_drop)";
                                break;
                            }
                            std::string d = fatDtorArg(srcT);
                            out << "sc_fat_as_thin(";
                            emitExpr(*e.a, true);
                            out << ", " << (d.empty() ? "(void (*)(void *))0" : d) << ")";
                        } else {
                            // 还原为 T@（sc_fat 视图，own=RAW 借用）
                            if (classNames.count(e.op)) {
                                out << "sc_thin_as_fat_suboff(";
                                emitExpr(*e.a, true);
                                out << ", offsetof(" << mapType_(e.op)
                                    << ", _class), (void (*)(void *))sc_obj_drop)";
                            } else {
                                out << "sc_thin_as_fat(";
                                emitExpr(*e.a, true);
                                out << ")";
                            }
                        }
                        break;
                    }
                    if (e.op.empty()) {
                        // 擦除为裸 @
                        if (srcFat && srcT.empty()) { out << "("; emitExpr(*e.a, true); out << ")"; break; }  // 已是裸 @：恒等
                        // 源非胖（裸指针/整数）：装箱为非托管裸 @（p=值, tar=NULL, own=SC_OWN_RAW,
                        //   dtor=NULL）——纯类型擦除指针搬运，无 ARC 记账（解绑 no-op）。整数经
                        //   intptr_t 擦入指针槽（与 void& 擦除一致），指针/数组直取。
                        if (!srcFat) {
                            VType svt;
                            bool isPtr = exprVType(*e.a, svt) && (svt.ptr > 0 || svt.arr > 0);
                            out << "((sc_afat){" << (isPtr ? "(void *)(" : "(void *)(intptr_t)(");
                            emitExpr(*e.a, true);
                            out << "), (int32_t *)0, SC_OWN_RAW, (void (*)(void *))0})";
                            break;
                        }
                        // class 类型 T@ → object 风味擦除：.p 偏移到 &_class（+offsetof(T,_class)）、
                        // dtor 取通用蹦床 sc_obj_drop（运行时经 SC_DIM_DROP 动态派发析构）。使裸 @ 始终
                        // 携带派发能力，四向过桥对称：T@→@→object@ 亦无损（还原 object@ 直取 &_class）。
                        // object@ 源已是该形态（.p=&_class、dtor=sc_obj_drop），落入下方通用分支直透。
                        if (srcFat && srcT != "object" && classNames.count(srcT)) {
                            out << "sc_fat_as_afat_addoff(";
                            emitExpr(*e.a, true);
                            out << ", offsetof(" << mapType_(srcT)
                                << ", _class), (void (*)(void *))sc_obj_drop)";
                            break;
                        }
                        // object@ 源（已 object 风味）/ 非 class 类型（concrete 风味）：.p 原样，
                        // dtor 取静态析构（object→sc_obj_drop；普通 struct→T_drop/NULL）。
                        std::string d = fatDtorArg(srcT);
                        out << "sc_fat_as_afat(";
                        emitExpr(*e.a, true);
                        out << ", " << (d.empty() ? "(void (*)(void *))0" : d) << ")";
                    } else {
                        // 恢复为 T@（sc_fat 视图）。按源风味分流：
                        //   · 源为命名 object@ → 具体类 T@：.p 是擦除的 _class 槽，container_of
                        //     回算实体基址（_class 偏移≠0 时，如 ~ 链表前缀/<C,I> 容器前缀）。
                        //   · 源为命名 T@/object@（同名还原）：直透。
                        //   · 源为裸 @：风味已擦除，运行时按 dtor==sc_obj_drop 判：
                        //       object 风味（.p=&_class，凡 class 类型擦除均属此）→ 具体类还原减
                        //       offsetof(T,_class)、object@ 还原直取；
                        //       concrete 风味（.p=实体基址，仅非 class struct）→ 直接取。
                        if (srcFat && srcT == "object" && classNames.count(e.op)) {
                            out << "sc_fat_suboff(";
                            emitExpr(*e.a, true);
                            out << ", offsetof(" << mapType_(e.op) << ", _class))";
                            break;
                        }
                        if (srcFat && !srcT.empty()) { out << "("; emitExpr(*e.a, true); out << ")"; break; }
                        // —— 源为裸 @（srcT 空）——
                        if (e.op == "object") {
                            // 还原为 object@：要求该裸 @ 为 object 风味（.p 已是 &_class、
                            // dtor=sc_obj_drop）。凡 class 类型擦除均属此，故 class 源恒可还原。
                            // --check=ref：断言 dtor==sc_obj_drop，捕获「非 class struct 擦除的裸 @
                            // 误还原 object@」（concrete 风味无 _class 派发槽，object@ 无意义）。
                            out << "sc_afat_as_fat(";
                            if (g_refCheck) out << "__sc_afat_objck(";
                            emitExpr(*e.a, true);
                            if (g_refCheck) out << ", (void (*)(void *))sc_obj_drop)";
                            out << ")";
                            break;
                        }
                        if (classNames.count(e.op)) {
                            // 还原为具体类 T@：运行时按风味补偏移（object 风味 .p=&_class 须减偏移）。
                            out << "sc_afat_as_fat_suboff(";
                            emitExpr(*e.a, true);
                            out << ", offsetof(" << mapType_(e.op)
                                << ", _class), (void (*)(void *))sc_obj_drop)";
                            break;
                        }
                        // 还原为非类具体类型（普通 struct）：不可能是 object 风味，直接取 .p。
                        out << "sc_afat_as_fat(";
                        emitExpr(*e.a, true);
                        out << ")";
                    }
                    break;
                }
                // (inst: object) → &inst._class（类实例 → 类型擦除分派器槽地址）
                if (e.op == "object" && e.castPtr == 0) {
                    VType vt; bool ok = exprVType(*e.a, vt);
                    if (ok && vt.name == "object") { out << "("; emitExpr(*e.a, true); out << ")"; break; }
                    out << "(&";
                    if (ok && vt.fat) { out << "("; emitFatBaseAsRaw(*e.a, vt.name); out << ")->_class"; }
                    else if (ok && vt.ptr >= 1) { out << "("; emitExpr(*e.a, true); out << ")->_class"; }
                    else { out << "("; emitExpr(*e.a, true); out << ")._class"; }
                    out << ")";
                    break;
                }
                // (expr: type&) → ((T*)(expr))；含类型限定符 (expr: const T&) → ((const T*)(expr))
                // 源为自动指针（T@/object@/裸 @）→ 裸指针：取 .p（实体基址，零成本借用读，
                //   不改引用记账）。sc_fat/sc_afat 首成员即 p，故 (expr).p 通用。
                {
                    bool srcFat = isFatExpr(*e.a);
                    out << "((";
                    if (e.castConst) out << "const ";
                    if (e.castVolatile) out << "volatile ";
                    out << (e.op.empty() ? "void" : (e.castCBridge ? e.op : mapType_(e.op)));   // op 为空 → 裸 &/&&（void*/void**）；::T → C 域类型原样
                    for (int i = 0; i < e.castPtr; i++) out << "*";
                    if (e.castRestrict) out << " restrict";
                    out << ")(";
                    if (srcFat) { out << "("; emitExpr(*e.a, true); out << ").p"; }
                    else emitExpr(*e.a, true);
                    out << "))";
                }
                break;
            }
            case Expr::InitList: {
                // 数组 [..] 与结构体/联合 {..} 在 C 侧统一为花括号聚合初始化；
                // 指定成员 {name=expr} → C99 .name=expr
                out << "{";
                for (size_t i = 0; i < e.args.size(); i++) {
                    if (i) out << ", ";
                    if (i < e.initNames.size() && !e.initNames[i].empty())
                        out << "." << e.initNames[i] << " = ";
                    emitExpr(*e.args[i], true);
                }
                out << "}";
                break;
            }
            case Expr::FncLit: {
                // 匿名函数字面量（不捕获外层变量）：首次遇到时提升为顶层 static
                // 函数（定义写入 lambdaOut，在所有函数体前回填），表达式处输出函数名。
                auto it = lambdaNames.find(&e);
                if (it == lambdaNames.end()) {
                    std::string name = "sc__lambda_" + std::to_string(lambdaSeq++);
                    lambdaNames.emplace(&e, name);
                    emitLambdaDef(e, name);
                    out << name;
                } else {
                    out << it->second;
                }
                break;
            }
            // async E[, q]：异步驱动 rpc。
            //   无目标 ⇒ 登记进当前线程事件循环，立即返回 future&（libuv 协作式，调 X__async）。
            //   带队列 q ⇒ 非阻塞带回复：投递给 q，立即返回 promise&（mt-future）；消费者执行完后
            //     兑现，调用方 p->wait() 阻塞取结果。语句表达式求值（参数缓冲由 promise 堆拥有）：
            //     ({ struct W _ap = {0}; 填实参; q->async(q,(void(*)(void*))W_rpc,&_ap,sizeof(_ap)); })
            case Expr::Async: {
                // 裸 async（无操作数）：取当前 sync 驱动 rpc 的待应答调用，求值为 deferred&
                //   （rpc 延迟应答；rpc 体 return 不再自动应答，须之后 done s, result 兑现）。
                //   经 op 内核 op_deferred_current() 取（顺带标记「已领取」=转延迟），零 emit mt 符号。
                if (!e.a) {
                    out << "sc_op_deferred_current()";
                    break;
                }
                if (e.b) {                           // 带队列目标：非阻塞带回复，求值为 promise&
                    if (!e.a || e.a->kind != Expr::Call ||
                        !e.a->a || e.a->a->kind != Expr::Ident ||
                        !rpcs.count(e.a->a->text))
                        throw CompileError{"async<q> 期望 rpc 调用形式 name(args)", e.line};
                    const Decl* r = rpcs.at(e.a->a->text);
                    if (r->hasAwait)
                        throw CompileError{"async<q> 不能驱动协程 rpc '" + e.a->a->text +
                                           "'（含 await，请用无目标 async）", e.line};
                    if (r->structCommon.variadic)
                        throw CompileError{"async<q> 暂不支持可变参数 rpc：" + r->name, e.line};
                    const Expr& call = *e.a;
                    if (call.args.size() > r->structCommon.fields.size())
                        throw CompileError{"rpc 实参数量超出：" + r->name, e.line};
                    VType qvt;
                    if (!exprVType(*e.b, qvt) || qvt.name != "queue" || !aggrOf("queue"))
                        throw CompileError{"async<q> 的目标须为 queue 端点", e.line};
                    checkRpcOptKeys(e, false);   // <prio:N, delay:ms>（async 不支持 timeout）
                    const bool isPtr = qvt.ptr >= 1;
                    // 参数缓冲由 promise 堆拥有（async 内部 memcpy sizeof 字节）；复合字面量
                    // 地址生命期至本表达式所在块，调用期间有效（替代原 GNU 语句表达式）。
                    emitComBasePtr(*e.b, isPtr);
                    out << "->async(";
                    emitComBasePtr(*e.b, isPtr);
                    out << ", (void (*)(void *))" << scPfx(r->name) << "_rpc, &(struct " << scPfx(r->name) << "){";
                    bool firstInit = true;
                    for (size_t i = 0; i < call.args.size(); i++) {
                        const Field& f = r->structCommon.fields[i];
                        if (!firstInit) out << ", ";
                        firstInit = false;
                        out << "." << f.name << " = ";
                        emitExpr(*call.args[i], true);
                        if (!f.type.arrayDims.empty()) {   // 数组实参：额外装填 size（字节数）
                            out << ", ." << rpcArraySizeName(f) << " = ";
                            emitRpcArraySizeof(f);
                        }
                    }
                    if (firstInit) out << "0";   // 无实参：避免空初始化列表（C 不允许 {}）
                    out << "}, sizeof(struct " << scPfx(r->name) << "), ";
                    emitOptOr0(e.syncOpts, "prio", "int32_t"); out << ", ";
                    emitOptOr0(e.syncOpts, "delay", "int64_t");
                    out << ")";
                    break;
                }
                if (!e.syncOpts.empty())
                    throw CompileError{"无队列目标 async（事件循环 future&）不接受 <prio/delay> 选项", e.line};
                if (e.a && e.a->kind == Expr::Call && e.a->a && e.a->a->kind == Expr::Ident) {
                    const Expr& call = *e.a;
                    out << scPfx(call.a->text) << "__async(";
                    emitAsyncCallArgs(call);
                    out << ")";
                }
                break;
            }
            // sync E[, q]：同步驱动 rpc。
            //   无目标 ⇒ 当前线程直接执行（X(args) 包装：装填→worker X_rpc→取返回槽），替代裸 rpc()。
            //   带队列 q ⇒ 阻塞带回复：投递给 q，阻塞至消费者执行完，取回返回槽。语句表达式求值：
            //     ({ struct W _rp = {0}; 填实参; q->sync(q,(void(*)(void*))W_rpc,&_rp); _rp._; })
            case Expr::Sync: {
                if (!e.a || e.a->kind != Expr::Call ||
                    !e.a->a || e.a->a->kind != Expr::Ident ||
                    !rpcs.count(e.a->a->text))
                    throw CompileError{"sync 期望 rpc 调用形式 name(args)", e.line};
                const Decl* r = rpcs.at(e.a->a->text);
                if (r->hasAwait)
                    throw CompileError{"sync 不能驱动异步 rpc '" + e.a->a->text +
                                       "'（含 await，请用 async）", e.line};
                if (!e.b) {                          // 无目标：当前线程直接执行
                    if (!e.syncOpts.empty() || e.c)
                        throw CompileError{"无目标 sync（当前线程直接执行）不接受 <prio/delay/timeout> 选项与状态出参", e.line};
                    bool save = emitRpcCallOK;
                    emitRpcCallOK = true;
                    emitExpr(*e.a, true);
                    emitRpcCallOK = save;
                    break;
                }
                // 带队列目标：阻塞带回复
                if (r->structCommon.variadic)
                    throw CompileError{"sync<q> 暂不支持可变参数 rpc：" + r->name, e.line};
                const Expr& call = *e.a;
                if (call.args.size() > r->structCommon.fields.size())
                    throw CompileError{"rpc 实参数量超出：" + r->name, e.line};
                VType qvt;
                if (!exprVType(*e.b, qvt) || qvt.name != "queue" || !aggrOf("queue"))
                    throw CompileError{"sync<q> 的目标须为 queue 端点", e.line};
                checkRpcOptKeys(e, true);   // <prio:N, delay:ms, timeout:ms>
                const bool isPtr = qvt.ptr >= 1;
                out << "({ struct " << scPfx(r->name) << " _rp = {0}; ";
                for (size_t i = 0; i < call.args.size(); i++) {
                    const Field& f = r->structCommon.fields[i];
                    out << "_rp." << f.name << " = ";
                    emitExpr(*call.args[i], true); out << "; ";
                    if (!f.type.arrayDims.empty()) {   // 数组实参：额外装填 size（字节数）
                        out << "_rp." << rpcArraySizeName(f) << " = ";
                        emitRpcArraySizeof(f); out << "; ";
                    }
                }
                // 可选状态出参 &st（i4 左值地址）：仅在带 timeout 时有意义，接收 sync 返回码
                if (e.c) {
                    if (e.c->kind != Expr::Unary || e.c->op != "&" || !e.c->a)
                        throw CompileError{"sync 状态出参须为变量地址形式 &var（i4）", e.line};
                    if (!hasOpt(e.syncOpts, "timeout"))
                        throw CompileError{"sync 状态出参仅在带 <timeout:ms> 时有效", e.line};
                    emitExpr(*e.c->a, true); out << " = ";   // st = q->sync(...)
                }
                emitComBasePtr(*e.b, isPtr);
                out << "->sync(";
                emitComBasePtr(*e.b, isPtr);
                out << ", (void (*)(void *))" << scPfx(r->name) << "_rpc, &_rp, sizeof(_rp), ";
                emitOptOr0(e.syncOpts, "prio", "int32_t"); out << ", ";
                emitOptOr0(e.syncOpts, "delay", "int64_t"); out << ", ";
                emitOptOr0(e.syncOpts, "timeout", "int64_t");
                out << "); ";
                if (rpcHasRet(*r)) out << "_rp._; ";    // 返回槽（消费者已回填；超时为 0）
                else out << "(void)0; ";
                out << "})";
                break;
            }
            // await E：仅出现在异步 rpc 体内，由 emitAsyncStmts 在语句层处理；
            // 兜底（理论不可达）：发出其 future 操作数表达式。
            case Expr::Await:
                if (e.a) emitFutureExpr(*e.a);
                break;
        }
    }

    // ---------------- 语句 ----------------

    int fatTmpSeq = 0;  // 胖指针建头临时变量编号
    std::vector<size_t> fatBreakBoundary;     // break 目标处的 fatScopes 层数（循环/switch）
    std::vector<size_t> fatContinueBoundary;  // continue 目标处的 fatScopes 层数（循环）
    // goto 目标标签 → 其所在作用域索引（仅含「当前活动作用域链」上的标签：进域登记、退域注销）。
    // goto 跨域时据此从最内层清理到「标签所在作用域的子层」（含被跳过的胖根/final/栈哨兵）。
    std::unordered_map<std::string, size_t> labelDepth;

    // 当前活动作用域链上是否存在待清理项（胖根/借用栈对象/栈哨兵/final）。
    bool hasActiveCleanup() const {
        for (auto& v : fatScopes)       if (!v.empty()) return true;
        for (auto& v : fatArrayScopes)  if (!v.empty()) return true;
        for (auto& v : fatStackScopes)  if (!v.empty()) return true;
        for (auto& v : memCanaryScopes) if (!v.empty()) return true;
        for (auto& v : fatFinalScopes)  if (!v.empty()) return true;
        return false;
    }

    // 把表达式发到字符串（临时换出 out），用于构造 C 左值/own 子表达式
    std::string captureExpr(const Expr& e) {
        std::ostringstream tmp;
        std::swap(out, tmp);
        emitExpr(e, true);
        std::swap(out, tmp);
        return tmp.str();
    }

    // 胖指针左值信息：lv = sc_fat 左值 C 表达式；own = 持有者 out 指针表达式
    //   根变量（Ident）          → lv=名字, own=SC_OWN_ROOT
    //   胖 base 的胖成员 base->m → lv=((T*)(base).p)->m, own=&((sc_ref*)(base).tar)->out
    // 返回 false 表示非可记账胖左值。
    bool fatLhsInfo(const Expr& lhs, std::string& lv, std::string& own) {
        if (lhs.kind == Expr::Ident) {
            VType vt;
            if (!exprVType(lhs, vt) || !vt.fat) return false;
            lv = lhs.text;
            own = "SC_OWN_ROOT";
            return true;
        }
        // T@ 数组元素 arr[i]：base 为 T@ 数组（fat && arr>=1）⇒ 元素是独立根边（own=ROOT）。
        if (lhs.kind == Expr::Index && lhs.a) {
            VType at;
            if (exprVType(*lhs.a, at) && at.fat && at.arr >= 1) {
                lv = captureExpr(lhs);
                own = "SC_OWN_ROOT";
                return true;
            }
        }
        if (lhs.kind == Expr::Member && lhs.a) {
            std::string tt;
            if (!isFatExpr(lhs, nullptr)) return false;  // 成员本身须是 T@
            if (!isFatExpr(*lhs.a, &tt)) {
                // 经裸 base（含 this）写胖成员：仅允许 = nil 解绑（成员自带 own/tar，
                // 无需重算持有者 out，对有头/无头容器都安全）。own="" 标记裸 base。
                VType bt;
                if (!exprVType(*lhs.a, bt)) return false;
                std::string baseStr = captureExpr(*lhs.a);
                std::string acc = (bt.ptr > 0 || bt.arr > 0) ? "->" : ".";
                lv = "(" + baseStr + ")" + acc + lhs.text;
                own = "";   // 裸 base 标记：只可解绑，不可绑新边
                return true;
            }
            std::string baseStr = captureExpr(*lhs.a);
            lv = "((" + mapType_(tt) + " *)(" + baseStr + ").p)->" + lhs.text;
            own = "&((sc_ref *)(" + baseStr + ").tar)->out";
            return true;
        }
        return false;
    }

    // 自动指针 T@ 根变量声明：sc_fat 声明 + 按初值形态绑定，并登记进当前作用域。
    //   裸 @：sc_afat 声明（dtor 随句柄），登记 bare 标记供退块 sc_afat_unbind。
    //   瘦 @^：sc_thin 声明（dtor 随句柄、无 own），登记 thin 标记供退块 sc_thin_unbind。
    void emitFatVarInit(const Field& f, bool asConst, bool isStatic) {
        regVar(f);
        bool bare = f.type.fat && f.type.name.empty();
        bool thin = f.type.thin;
        if (!fatScopes.empty()) fatScopes.back().push_back({f.name, fatDtorArg(f.type.name), bare, thin});
        indent();
        if (isStatic) out << "static ";
        if (asConst) out << "const ";
        out << (thin ? "sc_thin " : (bare ? "sc_afat " : "sc_fat ")) << f.name << " = {0};\n";
        if (!f.init) return;
        emitFatBind(f.name, "SC_OWN_ROOT", *f.init, /*isInit*/ true, f.type.name, thin);
    }

    // T(args) 构造：校验实参与 init 形参个数匹配，输出逗号分隔实参列表（无括号，
    // 末尾不补逗号）。供胖目标带参构造 T@ = T(args) 各发射点复用。
    void emitCtorInitArgs(const Decl* td, const Expr& init) {
        const Decl* im = findMethod(td->name, "init");
        if (!im || im->structCommon.fields.empty())
            throw CompileError("类型 '" + td->name +
                "' 的 init 无参数，不支持带参构造 T(...)", init.line);
        if (init.args.size() != im->structCommon.fields.size())
            throw CompileError("构造 '" + td->name +
                "(...)' 实参个数与 init 形参不符", init.line);
        for (size_t i = 0; i < init.args.size(); i++) {
            if (i) out << ", ";
            emitMethodArg(*init.args[i], &im->structCommon.fields[i]);
        }
    }

    // 胖指针赋值（lv=左值, own=持有者 out 表达式）：
    //   T()   → 带头堆分配 + 绑新边（目标.in++、own.out++）
    //   调用  → 移动：结构体拷贝（入边守恒）；own 非 ROOT 时改挂并 own.out++
    //   胖左值→ 绑定：新增一条边（目标.in++、own.out++）
    //   nil   → 解绑（清空）
    // isInit=false 时先解绑旧边（§4.1 重新赋值必先拆旧边）。
    //   targetType=左值的目标静态类型 T（拆旧边时按其析构 drop 清理子成员，见 §5）。
    // 瘦指针 T@^（sc_thin）绑定：只统计目标入边 tar，dtor 随句柄，无 own 记账。
    //   nil → 仅解绑；T() → 带头堆分配 + sc_thin_bind；胖/瘦左值 → 拷 p/tar 绑新边。
    //   p/dtor 风味与裸 @ 一致（class → &_class + sc_obj_drop），故与 @ 互转无损。
    void emitThinBind(const std::string& lv, const Expr& init, bool isInit) {
        if (init.kind == Expr::Ident && init.text == "nil") {
            if (!isInit) emitFatUnbind(lv, "", false, /*thin*/ true);
            return;
        }
        if (!isInit) emitFatUnbind(lv, "", false, /*thin*/ true);
        // T()/T(args) → 带头分配 + 绑定（dtor 取静态类型析构）
        if (const Decl* td = typeCallee(init)) {
            std::string tmp = "_fat" + std::to_string(fatTmpSeq++);
            indent(); out << mapType_(td->name) << " *" << tmp << " = ";
            if (!init.args.empty()) {
                out << mapType_(td->name) << "__new_ref_init(";
                emitCtorInitArgs(td, init);
                out << ", " << ctorRefFlags(init) << ");\n";
            } else {
                out << mapType_(td->name) << "__new_ref(" << ctorRefFlags(init) << ");\n";
            }
            indent();
            if (classNames.count(td->name)) {
                out << "sc_thin_bind(&" << lv << ", (void *)&" << tmp << "->_class"
                    << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR)"
                    << ", (void (*)(void *))sc_obj_drop);\n";
            } else {
                std::string d = fatDtorArg(td->name);
                out << "sc_thin_bind(&" << lv << ", " << tmp
                    << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR), "
                    << (d.empty() ? "(void (*)(void *))0" : d) << ");\n";
            }
            return;
        }
        // 胖/瘦左值 → 拷 p/tar 绑新边（sc_fat/sc_afat/sc_thin 前 16B 同构）
        std::string st;
        if (isFatExpr(init, &st)) {
            std::string rhs = captureExpr(init);
            std::string d = st.empty() ? ("(" + rhs + ").dtor")
                          : (classNames.count(st) && st != "object" ? "(void (*)(void *))sc_obj_drop"
                             : (fatDtorArg(st).empty() ? "(void (*)(void *))0" : fatDtorArg(st)));
            std::string pexpr = (classNames.count(st) && st != "object")
                ? "(void *)&((" + mapType_(st) + " *)(" + rhs + ").p)->_class" : "(" + rhs + ").p";
            indent();
            out << "sc_thin_bind(&" << lv << ", " << pexpr << ", (sc_ref *)("
                << rhs << ").tar, " << d << ");\n";
            return;
        }
        indent(); out << lv << " = "; emitExpr(init, true); out << ";\n";
    }

    void emitFatBind(const std::string& lv, const std::string& own,
                     const Expr& init, bool isInit, const std::string& targetType = "",
                     bool thin = false) {
        // 胖左值类型名为空 ⇔ 裸 @（sc_afat，类型擦除）：解绑/绑定走 sc_afat_*，dtor 随句柄。
        bool bare = targetType.empty();
        std::string dtorArg = fatDtorArg(targetType);
        // 瘦 @^：sc_thin 路径（只统计 tar，无 own）。dtor 随句柄；nil/T()/胖左值三态。
        if (thin) { emitThinBind(lv, init, isInit); return; }
        // nil：仅解绑
        if (init.kind == Expr::Ident && init.text == "nil") {
            if (!isInit) emitFatUnbind(lv, dtorArg, bare);
            return;
        }
        if (!isInit) emitFatUnbind(lv, dtorArg, bare);
        // T() / T(args) → 带头分配（带参时转发 init 形参）+ 绑定
        if (const Decl* td = typeCallee(init)) {
            std::string tmp = "_fat" + std::to_string(fatTmpSeq++);
            indent();
            out << mapType_(td->name) << " *" << tmp << " = ";
            if (!init.args.empty()) {
                out << mapType_(td->name) << "__new_ref_init(";
                emitCtorInitArgs(td, init);
                out << ", " << ctorRefFlags(init) << ");\n";
            } else {
                out << mapType_(td->name) << "__new_ref("
                    << ctorRefFlags(init) << ");\n";
            }
            indent();
            // 裸 @ 目标：擦除类型。class 类型走 object 风味（p 偏移到 &_class、dtor=sc_obj_drop），
            // 使裸 @ 携带派发能力、可无损还原 object@；非 class struct 走 concrete 风味（p=实体基址、
            // dtor=T_drop/NULL）。两者均随句柄存 dtor。
            if (bare) {
                if (classNames.count(td->name)) {
                    out << "sc_afat_bind(&" << lv << ", (void *)&" << tmp << "->_class"
                        << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR), " << own
                        << ", (void (*)(void *))sc_obj_drop);\n";
                    return;
                }
                std::string d = fatDtorArg(td->name);
                out << "sc_afat_bind(&" << lv << ", " << tmp
                    << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR), " << own << ", "
                    << (d.empty() ? "(void (*)(void *))0" : d) << ");\n";
                return;
            }
            // object@ 目标：p 须为擦除的 _class 槽指针（非 T 实体基址，供 dim 派发 / sc_obj_drop）。
            std::string pexpr = tmp;
            if (targetType == "object" && classNames.count(td->name))
                pexpr = "(void *)&" + tmp + "->_class";
            out << "sc_fat_bind(&" << lv << ", " << pexpr
                << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR), " << own << ");\n";
            return;
        }
        // 普通调用返回 T@ → 移动（结构体拷贝，入边守恒；own 非 ROOT 改挂 + out++）
        if (init.kind == Expr::Call) {
            indent();
            out << lv << " = ";
            emitExpr(init, true);
            out << ";\n";
            if (own != "SC_OWN_ROOT") {
                indent(); out << lv << ".own = " << own << ";\n";
                indent(); out << "if (SC_OWN_REAL(" << own << ")) (*(" << own << "))++;\n";
            }
            return;
        }
        // & 取址传染（§4.2）：对胖目标子成员取址 → 结果也是胖指针，
        //   tar 来自最近胖 hop（共享其堆对象 in），own 由接收者位置定。
        if (init.kind == Expr::Unary && init.op == "&" && init.a) {
            if (const Expr* hop = fatHopOf(*init.a)) {
                std::string addr = "&(" + captureExpr(*init.a) + ")";
                std::string tarx = "(" + captureExpr(*hop) + ").tar";
                indent();
                out << "sc_fat_bind(&" << lv << ", " << addr
                    << ", (sc_ref *)" << tarx << ", " << own << ");\n";
                return;
            }
            // 4b：& 普通栈变量（无胖 base）→ tar 指向其注入的伴生 sc_ref 头（§4.2）
            if (const Expr* root = rootIdentOf(*init.a)) {
                if (fatBorrowVars.count(root->text)) {
                    std::string addr = "&(" + captureExpr(*init.a) + ")";
                    indent();
                    if (g_refCheck) {
                        // 开启检查：tar→伴生 sc_ref 头，退域断言悬挂
                        out << "sc_fat_bind(&" << lv << ", " << addr
                            << ", &__scref_" << root->text << ", " << own << ");\n";
                    } else {
                        // 默认构建：不注入栈头，借用按非追踪处理（tar=NULL，仅 own 记账）
                        out << "sc_fat_bind(&" << lv << ", " << addr
                            << ", (sc_ref *)0, " << own << ");\n";
                    }
                    return;
                }
            }
        }
        // 胖左值（Ident/Member/Index）→ 绑定一条新边
        std::string tt;
        if (isFatExpr(init, &tt)) {
            std::string rhs = captureExpr(init);
            // 裸 @ 目标：擦除绑定，按源风味分流：
            //   · 源已是裸 @（tt 空）→ p/dtor 随其句柄直透；
            //   · 源为具体类 T@（tt 为 class）→ object 风味：p 偏移到 &_class、dtor=sc_obj_drop；
            //   · 源为 object@（已 &_class）/ 非 class struct → p 原样，dtor 取静态析构。
            if (bare) {
                if (tt.empty()) {
                    indent();
                    out << "sc_afat_bind(&" << lv << ", (" << rhs << ").p, (sc_ref *)("
                        << rhs << ").tar, " << own << ", (" << rhs << ").dtor);\n";
                    return;
                }
                if (tt != "object" && classNames.count(tt)) {
                    indent();
                    out << "sc_afat_bind(&" << lv << ", (void *)&((" << mapType_(tt) << " *)(" << rhs
                        << ").p)->_class, (sc_ref *)(" << rhs << ").tar, " << own
                        << ", (void (*)(void *))sc_obj_drop);\n";
                    return;
                }
                std::string d = fatDtorArg(tt).empty() ? "(void (*)(void *))0" : fatDtorArg(tt);
                indent();
                out << "sc_afat_bind(&" << lv << ", (" << rhs << ").p, (sc_ref *)("
                    << rhs << ").tar, " << own << ", " << d << ");\n";
                return;
            }
            std::string pexpr = "(" + rhs + ").p";
            // object@ 目标：p 须为擦除的 _class 槽指针（dim 派发 / sc_obj_drop 均以之为入口）。
            //   源为具体类 T@ → &((T*)(rhs).p)->_class；源已是 object@ → 直取 (rhs).p（已是槽）。
            if (targetType == "object" && tt != "object" && classNames.count(tt))
                pexpr = "(void *)&((" + mapType_(tt) + " *)(" + rhs + ").p)->_class";
            indent();
            out << "sc_fat_bind(&" << lv << ", " << pexpr << ", (sc_ref *)("
                << rhs << ").tar, " << own << ");\n";
            return;
        }
        // 兜底（含 &expr 取址，Step4 实现）：暂作结构体拷贝
        indent();
        out << lv << " = ";
        emitExpr(init, true);
        out << ";\n";
    }

    // 含胖成员类型的 init 体内、经 this 给胖成员绑「新边」：own 取隐藏 _self_own，
    // 运行时按其是否「真实出边」分支（§7.4）：
    //   SC_OWN_REAL(_self_own) → 容器是带头 ref 对象：带头堆分配 + 记账绑定（owner.out++）；
    //   否则（SC_OWN_RAW，容器无头：栈值/裸堆/静态） → 退化为裸创建/裸别名，不追踪。
    // init 为 T() 构造或既有胖表达式（nil 已在上层按解绑处理）。
    void emitFatBindSelfOwn(const std::string& lv, const std::string& tt, const Expr& init) {
        // 重新赋值先拆旧边（own 字段保留，解绑用成员自带 own/tar）
        emitFatUnbind(lv, fatDtorArg(tt));
        const Decl* td = typeCallee(init);
        indent(); out << "if (SC_OWN_REAL(_self_own)) {\n";
        depth++;
        if (td) {
            std::string tmp = "_fat" + std::to_string(fatTmpSeq++);
            indent(); out << mapType_(td->name) << " *" << tmp << " = ";
            if (!init.args.empty()) {
                out << mapType_(td->name) << "__new_ref_init(";
                emitCtorInitArgs(td, init);
                out << ", " << ctorRefFlags(init) << ");\n";
            } else {
                out << mapType_(td->name) << "__new_ref("
                    << ctorRefFlags(init) << ");\n";
            }
            indent(); out << "sc_fat_bind(&" << lv << ", " << tmp
                << ", (sc_ref *)((char *)" << tmp << " - SC_REF_HDR), _self_own);\n";
        } else {
            std::string rhs = captureExpr(init);
            indent(); out << "sc_fat_bind(&" << lv << ", (" << rhs << ").p, (sc_ref *)("
                << rhs << ").tar, _self_own);\n";
        }
        depth--;
        indent(); out << "} else {\n";
        depth++;
        if (td) {
            indent(); out << "(" << lv << ").p = ";
            if (!init.args.empty()) {
                out << mapType_(td->name) << "__new_init(";
                emitCtorInitArgs(td, init);
                out << ");\n";
            } else {
                out << mapType_(td->name) << "__new();\n";
            }
        } else {
            std::string rhs = captureExpr(init);
            indent(); out << "(" << lv << ").p = (" << rhs << ").p;\n";
        }
        indent(); out << "(" << lv << ").tar = (int32_t *)0;\n";
        indent(); out << "(" << lv << ").own = SC_OWN_RAW;\n";
        depth--;
        indent(); out << "}\n";
    }

    // 半自动指针 T@1（单例指针）销毁旧对象：非 nil → drop（若有）+ sc_free。
    void emitAutoFreeDestroy(const std::string& name, const std::string& dropFn) {
        indent();
        out << "if (" << name << ") { ";
        if (!dropFn.empty()) out << dropFn << "(" << name << "); ";
        out << "sc_free(" << name << "); }\n";
    }

    // 半自动指针 T@1 赋值：先把 RHS 算入临时（避免自指/自销），与旧值不同且旧值非 nil 则
    // 销毁旧对象（drop + free），再存入新指针。覆盖语义即「设新值自动销毁旧对象」。
    void emitAutoFreeAssign(const AutoFreeVar& v, const Expr& rhs) {
        std::string tmp = "_af" + std::to_string(autoFreeTmpSeq++);
        indent(); out << "{ " << v.ctype << " *" << tmp << " = ";
        emitExpr(rhs, true);
        out << ";\n";
        depth++;
        indent(); out << "if (" << v.name << " != " << tmp << " && " << v.name << ") { ";
        if (!v.dropFn.empty()) out << v.dropFn << "(" << v.name << "); ";
        out << "sc_free(" << v.name << "); }\n";
        indent(); out << v.name << " = " << tmp << ";\n";
        depth--;
        indent(); out << "}\n";
    }

    // 胖指针赋值语句入口（左值任意：根变量 / 胖成员）。返回 false 表示非胖左值。
    bool emitFatAssignStmt(const Expr& lhs, const Expr& init, bool isInit) {
        std::string lv, own;
        if (!fatLhsInfo(lhs, lv, own)) return false;
        std::string tt;                  // 左值目标静态类型（拆旧边时据此调析构）
        isFatExpr(lhs, &tt);
        VType lvt; bool isThin = exprVType(lhs, lvt) && lvt.thin;
        if (own.empty()) {
            // 裸 base（含 this）胖成员：nil 解绑总是允许（成员自带 own/tar）。
            if (init.kind == Expr::Ident && init.text == "nil") {
                emitFatUnbind(lv, fatDtorArg(tt), false, isThin);
                return true;
            }
            // 含胖成员类型的 init 体内、经 this 绑新边：own 取隐藏 _self_own，运行时分支。
            if (curInFatInit && lhs.a && lhs.a->kind == Expr::Ident && lhs.a->text == "this") {
                emitFatBindSelfOwn(lv, tt, init);
                return true;
            }
            throw CompileError{"禁止经裸指针绑定自动指针 T@ 成员新边（仅 init 内经 this 绑定，或 = nil 解绑）", lhs.line};
        }
        emitFatBind(lv, own, init, isInit, tt, isThin);
        return true;
    }

    // 栈数组哨兵：逻辑元素数表达式 `(d0) * (d1) * ...`（一维即 `(d0)`）。
    static std::string canaryElems(const std::vector<std::string>& dims) {
        std::string s;
        for (size_t i = 0; i < dims.size(); i++) { if (i) s += " * "; s += "(" + dims[i] + ")"; }
        return s;
    }
    // 内层维度积表达式 `(d1) * (d2) * ...`（外层维度之外的各维），一维返回 "1"。
    static std::string canaryInner(const std::vector<std::string>& dims) {
        if (dims.size() <= 1) return "1";
        std::string s;
        for (size_t i = 1; i < dims.size(); i++) { if (i > 1) s += " * "; s += "(" + dims[i] + ")"; }
        return s;
    }

    // 活动作用域内（内层优先）查找已登记的栈数组 canary 变量，供 sizeof 回报逻辑大小用。
    const MemCanaryVar* findCanary(const std::string& name) const {
        for (auto sit = memCanaryScopes.rbegin(); sit != memCanaryScopes.rend(); ++sit)
            for (auto& mc : *sit)
                if (mc.name == name) return &mc;
        for (auto& mc : globalCanaries)
            if (mc.name == name) return &mc;
        return nullptr;
    }

    // 作用域退出：对本块内胖根变量逆序 unbind（skip = 被移动返回的变量名，跳过）
    void emitFatScopeCleanup(const std::vector<FatRoot>& scope, const std::string& skip) {
        for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
            if (it->name == skip) continue;
            emitFatUnbind(it->name, it->dtorArg, it->bare, it->thin);
        }
    }

    // 作用域退出两阶段（§5）：① 拆本块全部胖根边 ② 再对本块注入 ref 头的栈变量断言悬挂。
    // 其前另有 phase0：发出本块登记的 final 钩子（LIFO），先于拆边/断言执行。
    void emitScopeCleanupAt(size_t i, const std::string& skip) {
        emitFinalScope(i);                                       // phase0：final 钩子
        if (i < autoFreeScopes.size())                           // phase0.4：半自动指针 T@1 退域销毁
            for (auto it = autoFreeScopes[i].rbegin(); it != autoFreeScopes[i].rend(); ++it) {
                if (it->name == skip) continue;                  // 被移动返回的对象跳过
                emitAutoFreeDestroy(it->name, it->dropFn);
            }
        if (i < dropScopes.size())                               // phase0.5：栈值对象 RAII 自动 drop
            for (auto it = dropScopes[i].rbegin(); it != dropScopes[i].rend(); ++it) {
                if (it->name == skip) continue;                  // 被移动返回的对象跳过
                indent();
                out << it->dropFn << "(&" << it->name << ");\n";
            }
        emitFatScopeCleanup(fatScopes[i], skip);                  // phase1：拆边
        if (i < fatArrayScopes.size())                           // phase1b：T@ 数组逐元素拆边
            for (auto it = fatArrayScopes[i].rbegin(); it != fatArrayScopes[i].rend(); ++it) {
                // 多维：逐维嵌套 for 遍历全部标量元素，对每个 sc_fat 元素 unbind。
                indent();
                std::string sub;
                for (size_t d = 0; d < it->dims.size(); d++) {
                    std::string k = "_k" + std::to_string(d);
                    out << "for (size_t " << k << " = (" << it->dims[d] << "); "
                        << k << "-- > 0; ) ";
                    sub += "[" + k + "]";
                }
                if (it->dtorArg.empty())
                    out << "sc_fat_unbind(&" << it->name << sub << ");\n";
                else
                    out << "sc_fat_unbind_d(&" << it->name << sub << ", " << it->dtorArg << ");\n";
            }
        if (i < fatStackScopes.size())                           // phase2：栈对象断言（带 site）
            for (auto it = fatStackScopes[i].rbegin(); it != fatStackScopes[i].rend(); ++it) {
                indent();
                out << "sc_ref_check(&__scref_" << it->name << ", \"" << it->site << "\");\n";
            }
        if (i < memCanaryScopes.size())                          // phase3：栈数组尾哨兵校验
            for (auto it = memCanaryScopes[i].rbegin(); it != memCanaryScopes[i].rend(); ++it) {
                indent();
                std::string len = it->dims.size() == 1
                    ? "SC_CANARY_ELEMS(" + it->elemTy + ") * sizeof(" + it->elemTy + ")"
                    : "SC_CANARY";
                out << "sc_stack_canary_check((unsigned char*)" << it->name
                    << " + (" << canaryElems(it->dims) << ") * sizeof(" << it->elemTy << "), "
                    << len << ", "
                    << it->name << ", \"" << it->site << "\");\n";
            }
        // phase4：入口 main 函数体作用域（i==0）退出 → 注入全局/模块尾声 drop（最末）。
        if (inMainFunc && i == 0) emitMainEpilogue();
    }

    // final 钩子发出：本块登记的 final 块按 LIFO（逆源序）逐个发出 body，
    // 每块包一层 C 花括号隔离其局部声明。
    void emitFinalScope(size_t i) {
        if (i >= fatFinalScopes.size()) return;
        for (auto it = fatFinalScopes[i].rbegin(); it != fatFinalScopes[i].rend(); ++it) {
            indent(); out << "{\n";
            depth++;
            emitStmts((*it)->body);
            depth--;
            indent(); out << "}\n";
        }
    }

    // 跨全部活动作用域清理（return 时用，skip = 移动返回的变量）
    void emitFatReturnCleanup(const std::string& skip) {
        for (size_t i = fatScopes.size(); i-- > 0; )
            emitScopeCleanupAt(i, skip);
    }

    // 从最内层清理到 fromIdx 作用域（含）——break/continue 早退用。
    void emitFatCleanupTo(size_t fromIdx) {
        for (size_t i = fatScopes.size(); i-- > fromIdx; )
            emitScopeCleanupAt(i, "");
    }

    // 语句序列是否以终结语句（return/break/continue/goto）收尾（其后清理由它接管）
    static bool lastTerminates(const std::vector<StmtPtr>& stmts) {
        if (stmts.empty()) return false;
        switch (stmts.back()->kind) {
            case Stmt::ReturnS: case Stmt::BreakS:
            case Stmt::ContinueS: case Stmt::GotoS: return true;
            default: return false;
        }
    }

    void emitVarDecls(const std::vector<Field>& decls, bool asConst,
                      bool isStatic = false, bool isTls = false, bool externFwd = false) {
        for (auto& f : decls) {
            // C 桥接绑定 name:: type（let/var X:: T）：声明 extern，认领 C 侧已定义符号
            // （不分配存储、无初值）。let/var 之别仅作用于 sc 侧可变性检查，C 端恒为 extern T。
            if (f.cBridge) {
                regVar(f);
                indent();
                out << "extern ";
                emitDeclarator(f, false);
                out << ";\n";
                continue;
            }
            // 全局（非函数内、非 cBridge）变量的 C 名加 sc_ 前缀（sc 命名域）；
            // 局部/形参为块作用域，保持原名（cname 此时等于 f.name）。注册表键仍用原名 f.name。
            // 宏体内声明也须加前缀，与引用处（经 globalsT → scPfx，如 &ARGS_\name→&sc_ARGS_##name）
            // 保持一致，否则声明发裸名而引用发 sc_ 名 → 与手写头文件的同名 #define 逐 token 不符，
            // 触发 -Wmacro-redefined。唯一例外：名字以形参拼接符 `\` 开头（如 \b→##b），加前缀会
            // 产生前导 ## 破坏预处理器替换 → 此时保持原样。
            const bool spliceLead = inMacro && !f.name.empty() && f.name[0] == '\\';
            const std::string cname = (inFunc || spliceLead) ? f.name : scPfx(f.name);
            // 分身/切片句柄 var s: T[a, b]：展开为 struct T__project s = {a, b, NULL};
            if (f.type.project) {
                (inFunc ? projVarsL : projVarsG)[f.name] = f.type.name;
                indent();
                if (isTls) out << "static TLS ";
                else if (isStatic) out << "static ";
                out << "struct " << mapType_(f.type.name) << "__project " << cname << " = {";
                if (f.type.projectArgs)
                    for (size_t i = 0; i < f.type.projectArgs->size(); i++) {
                        if (i) out << ", ";
                        emitExpr(*(*f.type.projectArgs)[i], true);
                    }
                if (f.type.projectArgs && !f.type.projectArgs->empty()) out << ", ";
                out << "NULL};\n";
                continue;
            }
            // 自动指针 T@（胖指针根变量）：声明 sc_fat + 绑定（T()=新建 / 调用=移动 /
            // 胖左值=绑定借用）；登记入当前作用域，退域/return 处自动 unbind。
            if (f.type.fat && f.type.arrayDims.empty() && inFunc) {
                emitFatVarInit(f, asConst, isStatic);
                continue;
            }
            // 自动指针数组 T@（局部一维/多维）：声明 sc_fat 数组并零初始化；元素经下标赋值绑定，
            // 退域/return/break 处逐元素 unbind（整张引用图清理）。登记入当前 fat 数组作用域。
            if (f.type.fat && !f.type.arrayDims.empty() && inFunc) {
                regVar(f);
                indent();
                if (isStatic) out << "static ";
                if (asConst) out << "const ";
                out << "sc_fat " << f.name;
                for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
                out << " = {0};\n";
                if (!fatArrayScopes.empty())
                    fatArrayScopes.back().push_back({f.name, f.type.arrayDims, fatDtorArg(f.type.name)});
                continue;
            }
            // 全局 T@（文件作用域 var/let，标量或数组）：声明仍走下方通用路径（已验证
            // 正确发出 `static sc_fat NAME[..] = {0};`），但登记入 globalFatVars，由进程退出
            // destructor 钩子逐元素拆边（带 drop），释放其持有的系统资源。tls 为每线程存储期，
            // 单次 destructor 无法逐线程清理 → 不登记。
            if (f.type.fat && !inFunc && !isTls)
                globalFatVars.push_back({scPfx(f.name), f.type.arrayDims, fatDtorArg(f.type.name)});
            // --check=mem：函数内栈数组 → 超额分配尾哨兵 + 退域校验，捕获栈数组越界写。
            // 一维与多维均支持（多维仅外层超额若干「行」覆盖尾哨兵区，内层维度不变）；
            // 非 const/static/tls/分身/胖/内联/函数指针；尾哨兵紧贴有效元素就地拦截。
            if (g_memCheck && inFunc && !asConst && !isStatic && !isTls
                && !f.type.project && !f.type.fat && !f.type.hasInline
                && f.type.fnKind == TypeRef::FncKind::None
                && !f.type.arrayDims.empty()) {
                regVar(f);
                std::string base; int ptr; resolveType(f.type, base, ptr);
                std::string elemTy = base;
                for (int i = 0; i < ptr; i++) elemTy += "*";
                const auto& dims = f.type.arrayDims;
                const std::string& d0 = dims[0];
                indent();
                if (dims.size() == 1) {
                    out << elemTy << " " << f.name
                        << "[(" << d0 << ") + SC_CANARY_ELEMS(" << elemTy << ")]";
                } else {
                    // 多维：外层维度加 SC_CANARY_OUTER 行（覆盖尾哨兵区），内层维度原样保留。
                    out << elemTy << " " << f.name
                        << "[(" << d0 << ") + SC_CANARY_OUTER(" << elemTy
                        << ", " << canaryInner(dims) << ")]";
                    for (size_t i = 1; i < dims.size(); i++) out << "[" << dims[i] << "]";
                }
                if (f.init) { out << " = "; emitExpr(*f.init, true); }
                out << ";\n";
                indent();
                std::string len = dims.size() == 1
                    ? "SC_CANARY_ELEMS(" + elemTy + ") * sizeof(" + elemTy + ")"
                    : "SC_CANARY";
                out << "sc_stack_canary_fill((unsigned char*)" << f.name
                    << " + (" << canaryElems(dims) << ") * sizeof(" << elemTy << "), "
                    << len << ", "
                    << f.name << ");\n";
                if (!memCanaryScopes.empty())
                    memCanaryScopes.back().push_back({f.name, elemTy, dims, fatStackSite(f)});
                continue;
            }
            // --check=mem：全局（文件作用域）栈数组 → 同样超额分配尾哨兵，但填充/校验改由
            // constructor（启动）/destructor（退出）钩子托管（全局无退域时机）。一维与多维均支持；
            // 非 const/tls/分身/胖/内联/函数指针。捕获持续到退出的全局缓冲区上溢。
            if (g_memCheck && !inFunc && !asConst && !isTls
                && !f.type.project && !f.type.fat && !f.type.hasInline
                && f.type.fnKind == TypeRef::FncKind::None
                && !f.type.arrayDims.empty()) {
                regVar(f);
                std::string base; int ptr; resolveType(f.type, base, ptr);
                std::string elemTy = base;
                for (int i = 0; i < ptr; i++) elemTy += "*";
                const auto& dims = f.type.arrayDims;
                const std::string& d0 = dims[0];
                indent();
                if (isStatic) out << "static ";
                if (dims.size() == 1) {
                    out << elemTy << " " << cname
                        << "[(" << d0 << ") + SC_CANARY_ELEMS(" << elemTy << ")]";
                } else {
                    out << elemTy << " " << cname
                        << "[(" << d0 << ") + SC_CANARY_OUTER(" << elemTy
                        << ", " << canaryInner(dims) << ")]";
                    for (size_t i = 1; i < dims.size(); i++) out << "[" << dims[i] << "]";
                }
                if (f.init) { out << " = "; emitExpr(*f.init, true); }
                out << ";\n";
                globalCanaries.push_back({scPfx(f.name), elemTy, dims, fatStackSite(f)});
                continue;
            }
            // 无类型 var/let（var x: = 初值）：依据初值字面量推断默认类型；
            // 推断成功时跳过 emitDeclarator，按推断结果输出声明并登记轻量类型
            std::string infBase; int infPtr = 0; bool inferred = false;
            if (f.init && f.type.name.empty() && !f.type.hasInline
                && f.type.fnKind == TypeRef::FncKind::None
                && f.type.ptr == 0 && f.type.arrayDims.empty())
                inferred = inferLiteralType(*f.init, infBase, infPtr);

            if (inferred) (inFunc ? localsT : globalsT)[f.name] = VType{infBase, infPtr, 0};
            else regVar(f);
            // 宏体内 @ 导出变量：先发 extern 前置声明（外部链接 + 与手写 C ABI 头一致），
            //   随后的定义为非 static（外部链接）。供其它模块经注入的根接口头 extern 引用。
            if (externFwd) {
                indent();
                out << "extern ";
                if (inferred) {
                    if (asConst) out << "const ";
                    out << mapBase(infBase) << " ";
                    for (int i = 0; i < infPtr; i++) out << "*";
                    out << cname;
                } else emitDeclarator(f, asConst, cname.c_str());
                out << ";\n";
            }
            indent();
            if (isTls) out << "static TLS ";   // tls：必为 static（C 规范），TLS 宏见 platform.h
            else if (isStatic) out << "static ";
            if (inferred) {
                if (asConst) out << "const ";
                out << mapBase(infBase) << " ";
                for (int i = 0; i < infPtr; i++) out << "*";
                out << cname;
            } else emitDeclarator(f, asConst, cname.c_str());
            if (f.init) {
                out << " = ";
                emitExpr(*f.init, true);
            } else {
                const Decl* sd = aggrOf(f.type.name);
                if (sd && (sd->kind == Decl::StructD || sd->kind == Decl::UnionD)) {
                    // tls 为 static 存储期：初始化须常量表达式，不能调 __default()
                    if (!isTls && sd->kind == Decl::StructD && hasFieldDefaults(sd)) {
                        out << " = " << mapType_(sd->name) << "__default()";
                    } else {
                        out << " = {0}";
                    }
                }
            }
            out << ";\n";
            // cls 实例：安装分派器指针 _class（须早于 init，使 init 内类型查询/dim 调用可用）
            if (inFunc && f.type.ptr == 0 && f.type.arrayDims.empty()
                && !f.type.fat && !f.type.project && !f.type.hasInline) {
                if (const Decl* cd = aggrOf(f.type.name); cd && cd->isClass) {
                    indent();
                    out << f.name << "._class = " << mapType_(cd->name) << "_hyper_impl;\n";
                }
            }
            // 声明即构造：函数内无初值的结构变量，若类型有无参 init 方法则自动调用
            // （tls 除外：static 存储期只初始化一次，此处会每次进函数重执行）
            if (inFunc && !isTls && !f.init && f.type.ptr == 0 && f.type.arrayDims.empty()
                && !f.type.hasInline) {
                const Decl* im = findMethod(f.type.name, "init");
                if (im && im->structCommon.fields.empty()) {
                    indent();
                    out << scPfx(im->name) << "(&" << f.name
                        << (typeHasFatMember(f.type.name) ? ", SC_OWN_RAW" : "") << ");\n";
                }
            }
            // RAII 闭环：栈值对象（类型有 drop 方法）退域自动 drop，LIFO。被显式 drop 的
            // 变量（manualDropVars，move 语义）跳过，由用户接管，避免双重释放。
            if (inFunc && !isStatic && !isTls && f.type.ptr == 0 && f.type.arrayDims.empty()
                && !f.type.fat && !f.type.project && !f.type.hasInline
                && !manualDropVars.count(f.name)) {
                const Decl* dm = findMethod(f.type.name, "drop");
                if (dm && !dropScopes.empty())
                    dropScopes.back().push_back({f.name, scPfx(dm->name)});
            }
            // 半自动指针 T@1（单例指针）：物理普通指针，登记退域销毁（非 nil → drop + free），
            // 并入 autoFreeVars 供赋值拦截（设新值先销旧）。manualDropVars（move）则交用户接管。
            if (inFunc && !isStatic && !isTls && f.type.autoFree
                && !manualDropVars.count(f.name)) {
                std::string afBase; int afPtr; resolveType(f.type, afBase, afPtr);
                const Decl* dm = findMethod(f.type.name, "drop");
                AutoFreeVar v{f.name, afBase, dm ? scPfx(dm->name) : std::string()};
                if (!autoFreeScopes.empty()) autoFreeScopes.back().push_back(v);
                autoFreeVars[f.name] = v;
            }
            // Step4b：被 &var 借入胖指针的普通栈变量 → 注入伴生 sc_ref 头；
            // 退域两阶段清理（拆边后）对其 sc_ref_check，捕获借用比目标活得久的悬挂（§4.2/§7.3）。
            // 仅 --check=ref 开启时注入（默认构建省此开销，堆 ARC 不受影响）。
            if (g_refCheck && inFunc && !isStatic && !isTls && !f.type.fat
                && fatBorrowVars.count(f.name)) {
                indent();
                out << "sc_ref __scref_" << f.name << " = {0, 0, 0, 0};\n";
                if (!fatStackScopes.empty())
                    fatStackScopes.back().push_back({f.name, fatStackSite(f)});
            }
        }
    }

    // 栈悬挂断言的 who 文案：含源码定位（文件名:行）。
    std::string fatStackSite(const Field& f) const {
        std::string s = f.name;
        if (!g_refSrcFile.empty() && f.line > 0) {
            std::string base = std::filesystem::path(g_refSrcFile).filename().string();
            s += "@" + base + ":" + std::to_string(f.line);
        }
        return s;
    }

    // --check=ptr 守卫 site 文案：what@文件名:行（含源码定位，便于命中定位）。
    std::string ptrSite(const Expr& e, const char* what) const {
        std::string s = what;
        if (!g_refSrcFile.empty() && e.line > 0) {
            std::string base = std::filesystem::path(g_refSrcFile).filename().string();
            s += "@" + base + ":" + std::to_string(e.line);
        }
        return s;
    }

    // 标识符的编译期已知数组维度（局部优先，再全局）；非数组返回 nullptr。
    const std::vector<std::string>* knownDims(const std::string& name) const {
        if (inFunc) { auto it = varDimsL.find(name); if (it != varDimsL.end()) return &it->second; }
        auto it = varDimsG.find(name);
        return it != varDimsG.end() ? &it->second : nullptr;
    }

    // 记录变量的轻量类型（函数内→局部表，否则→全局表）
    void regVar(const Field& f) {
        VType vt{f.type.name, f.type.ptr, (int)f.type.arrayDims.size()};
        vt.fat = f.type.fat;
        vt.thin = f.type.thin;
        (inFunc ? localsT : globalsT)[f.name] = vt;
        if (!f.type.arrayDims.empty())
            (inFunc ? varDimsL : varDimsG)[f.name] = f.type.arrayDims;
        // 函数指针变量：额外记录内联签名（缺参补全查询用）
        if (f.type.fnKind != TypeRef::FncKind::None)
            (inFunc ? fnVarsL : fnVarsG)[f.name] = &f.type;
        // 分身/切片句柄参数/变量 T[...]：登记实体名，使 s._ 等句柄成员可解析
        if (f.type.project)
            (inFunc ? projVarsL : projVarsG)[f.name] = f.type.name;
    }

    void emitStmts(const std::vector<StmtPtr>& stmts) {
        fatScopes.emplace_back();
        fatArrayScopes.emplace_back();
        fatStackScopes.emplace_back();
        memCanaryScopes.emplace_back();
        fatFinalScopes.emplace_back();
        dropScopes.emplace_back();
        autoFreeScopes.emplace_back();
        // 本作用域直接子标签登记入 labelDepth（进域可见、退域注销），供 goto 跨域清理定位。
        size_t scopeIdx = fatScopes.size() - 1;
        for (auto& s : stmts)
            if (s->kind == Stmt::LabelS) labelDepth[s->text] = scopeIdx;
        for (auto& s : stmts) emitStmt(*s);
        // 块正常落出（非 return/break/... 收尾）：两阶段清理本块（拆胖边 + 栈对象断言）
        if (!lastTerminates(stmts)) emitScopeCleanupAt(fatScopes.size() - 1, "");
        for (auto& s : stmts)
            if (s->kind == Stmt::LabelS) labelDepth.erase(s->text);
        fatScopes.pop_back();
        fatArrayScopes.pop_back();
        fatStackScopes.pop_back();
        memCanaryScopes.pop_back();
        fatFinalScopes.pop_back();
        dropScopes.pop_back();
        autoFreeScopes.pop_back();
    }

    // 循环体：登记 break/continue 边界（= 体作用域层）后发出语句
    void emitLoopBody(const std::vector<StmtPtr>& stmts) {
        size_t boundary = fatScopes.size();  // emitStmts 即将 push 的体作用域索引
        fatBreakBoundary.push_back(boundary);
        fatContinueBoundary.push_back(boundary);
        emitStmts(stmts);
        fatContinueBoundary.pop_back();
        fatBreakBoundary.pop_back();
    }

    // for-in 糖：把 for name[: T&] in 集合 [revert][step][offset][num] 降解为 C 循环。
    // 集合按协议分类：① 值序列（范围 [a,b]/[a,b)、整数计数 n=[0,n)）；② 索引序列（静态数组、
    // 字符串按 '\0' 终止）；③ 链式序列（双向链 chain、<C,I> 投影容器，经 first/next 指针游标）；
    // ④ 序列索引协议（len + 整型 get/at：list/array，for v in seq[, i]）；⑤ 映射协议（first/next
    // 游标 + key_at/value_at：dict/bst/lru，for k, v in map）。后两类按方法约定探测，不绑库类型名。
    void emitForIn(const Stmt& s) {
        const int n = forSeq++;
        const std::string FI = "_fi" + std::to_string(n);   // 游标 / 索引
        const std::string FB = "_fb" + std::to_string(n);   // 基址指针（索引序列）
        const std::string FE = "_fe" + std::to_string(n);   // 终界 / 长度
        const std::string FR = "_fr" + std::to_string(n);   // 接收者指针（链式序列）
        const std::string FC = "_fc" + std::to_string(n);   // 计数（num 上限用）
        const std::string FLO = "_flo" + std::to_string(n); // 范围下界
        const std::string FHI = "_fhi" + std::to_string(n); // 范围上界

        auto emitOpt = [&](const ExprPtr& e, const char* def) {
            if (e) { out << "("; emitExpr(*e, true); out << ")"; }
            else out << def;
        };
        const bool hasNum = (bool)s.forNumE;

        // 集合分类
        enum Kind { KRange, KInt, KArray, KString, KChain, KContainer, KMap, KIndexSeq } kind = KRange;
        VType ct; bool ctok = false;
        if (s.forIsRange) kind = KRange;
        else {
            ctok = exprVType(*s.forColl, ct);
            const std::string cn = ctok ? resolveAliasName(ct.name) : "";
            if (ctok && cn == "char" && (ct.ptr >= 1 || ct.arr >= 1)) kind = KString;
            else if (ctok && ct.arr > 0) kind = KArray;
            else if (ctok && ct.arr == 0 && ct.ptr <= 1 && adtColls.count(cn)) kind = KContainer;
            else if (ctok && ct.arr == 0 && ct.ptr <= 1 && cn == "chain") kind = KChain;
            else if (ctok && ct.arr == 0 && ct.ptr <= 1 && aggrOf(cn) && isMapProto(cn)) kind = KMap;
            else if (ctok && ct.arr == 0 && ct.ptr <= 1 && aggrOf(cn) && idxGetterOf(cn)) kind = KIndexSeq;
            else if (ctok && ct.arr == 0 && ct.ptr == 0
                     && (scalarClass(ct.name) == 'i' || scalarClass(ct.name) == 'u')) kind = KInt;
            else throw CompileError{"for-in 不支持的集合类型", s.line};
        }

        // 循环变量类型（显式注解优先；否则按集合元素类型推断）
        std::string varName, varPtrName;  // varName=C 类型文本；varPtrName=取址后(cast)文本
        std::string vBase; int vPtr = 0;  // 用于 regVar
        bool cast = s.forVarHasType;
        if (cast) { vBase = s.forVarType.name; vPtr = s.forVarType.ptr; }

        // 集合维度与索引变量数量校验：数组维度=knownDims 大小，其余集合维度恒为 1。
        const std::vector<std::string>* dims =
            (kind == KArray && s.forColl->kind == Expr::Ident) ? knownDims(s.forColl->text) : nullptr;
        const size_t D = (kind == KArray && dims) ? dims->size() : 1;
        if (D == 1) {
            if (s.forIdxVars.size() > 1)
                throw CompileError{"for-in：一维集合至多 1 个索引变量", s.line};
        } else if (s.forIdxVars.size() != D) {
            throw CompileError{"for-in：" + std::to_string(D) + " 维数组需恰好 "
                               + std::to_string(D) + " 个索引变量", s.line};
        }
        // 索引变量发射：idxExpr 为该集合的「索引取值」C 表达式文本（可索引集合=真实下标，
        // 仅 next 迭代集合=递增计数）。仅一维分支调用；多维数组单独走嵌套循环路径。
        auto emitIdx = [&](const std::string& idxExpr) {
            if (!s.forIdxVars.empty()) {
                indent(); out << "int " << s.forIdxVars[0] << " = (int)(" << idxExpr << ");\n";
            }
        };

        indent(); out << "{\n"; depth++;

        // ---- 多维数组：N 层嵌套循环遍历全部标量元素（v=标量，索引变量=各维下标）----
        if (kind == KArray && D > 1) {
            if (s.forStepE || s.forOffsetE || s.forNumE)
                throw CompileError{"for-in：多维数组暂不支持 step/offset/num 选项", s.line};
            std::string elemBase = ct.name; int elemPtr = ct.ptr;
            std::string declTy = cast ? cTypeOf(vBase, vPtr) : cTypeOf(elemBase, elemPtr);
            if (!cast) { vBase = elemBase; vPtr = elemPtr; }
            std::vector<std::string> ids;
            for (size_t d = 0; d < D; d++) {
                std::string id = FI + "_" + std::to_string(d);
                ids.push_back(id);
                indent();
                out << "for (long " << id << " = "
                    << (s.forRevert ? "(" + (*dims)[d] + ") - 1" : std::string("0")) << "; "
                    << (s.forRevert ? id + " >= 0" : id + " < (" + (*dims)[d] + ")") << "; "
                    << (s.forRevert ? id + "--" : id + "++") << ") {\n";
                depth++;
            }
            for (size_t d = 0; d < D; d++) {
                indent(); out << "int " << s.forIdxVars[d] << " = (int)" << ids[d] << ";\n";
            }
            indent(); out << declTy << " " << s.forVar << " = ";
            if (cast && vPtr > 0) out << "(" << declTy << ")&";
            else if (cast) out << "(" << declTy << ")";
            emitExpr(*s.forColl, true);
            for (size_t d = 0; d < D; d++) out << "[" << ids[d] << "]";
            out << ";\n";
            emitForInBody(s, vBase, vPtr);
            for (size_t d = 0; d < D; d++) { depth--; indent(); out << "}\n"; }
            depth--; indent(); out << "}\n";
            return;
        }

        if (kind == KRange || kind == KInt) {
            // ---- 值序列：循环变量即迭代值 ----
            std::string vty = cast ? cTypeOf(vBase, vPtr) : "int";
            if (!cast) { vBase = "i4"; vPtr = 0; }
            indent(); out << vty << " " << FLO << " = ";
            if (kind == KInt) out << "0"; else emitExpr(*s.forRangeLo, true);
            out << ";\n";
            indent(); out << vty << " " << FHI << " = ";
            if (kind == KInt) { out << "("; emitExpr(*s.forColl, true); out << ")"; }
            else emitExpr(*s.forRangeHi, true);
            out << ";\n";
            const bool incl = (kind == KRange) ? s.forRangeIncl : false;  // 整数计数=半开
            indent(); out << "long " << FC << " = 0; (void)" << FC << ";\n";
            indent(); out << "for (" << vty << " " << FI << " = ";
            if (!s.forRevert) { out << FLO << " + "; emitOpt(s.forOffsetE, "0"); }
            else { out << FHI << (incl ? "" : " - 1") << " - "; emitOpt(s.forOffsetE, "0"); }
            out << "; ";
            if (!s.forRevert) out << FI << (incl ? " <= " : " < ") << FHI;
            else out << FI << " >= " << FLO;
            if (hasNum) { out << " && " << FC << " < "; emitOpt(s.forNumE, "0"); }
            out << "; " << FI << (s.forRevert ? " -= " : " += "); emitOpt(s.forStepE, "1");
            out << ", " << FC << "++) {\n";
            depth++;
            indent(); out << vty << " " << s.forVar << " = ";
            if (cast) { out << "(" << vty << ")"; }
            out << FI << ";\n";
            emitIdx(FI + " - " + FLO);          // 索引=0 基位置（revert 时倒序，v==coll[i] 恒等）
            emitForInBody(s, vBase, vPtr);
            depth--; indent(); out << "}\n";
        } else if (kind == KArray || kind == KString) {
            // ---- 索引序列：基址指针 + 整型下标 ----
            std::string elemBase; int elemPtr;
            if (kind == KString) { elemBase = "char"; elemPtr = 0; }
            else { elemBase = ct.name; elemPtr = ct.ptr; }
            std::string baseTy = cTypeOf(elemBase, elemPtr + 1);
            indent(); out << baseTy << " " << FB << " = "; emitExpr(*s.forColl, true); out << ";\n";
            indent(); out << "long " << FE << " = ";
            if (kind == KString) out << "0";
            else {
                const std::vector<std::string>* dims =
                    (s.forColl->kind == Expr::Ident) ? knownDims(s.forColl->text) : nullptr;
                if (!dims || dims->empty()) throw CompileError{"for-in 数组需编译期已知长度", s.line};
                out << (*dims)[0];
            }
            out << ";\n";
            if (kind == KString) {  // 扫描字符串长度（'\0' 终止）
                indent(); out << "while (" << FB << "[" << FE << "] != '\\0') " << FE << "++;\n";
            }
            indent(); out << "long " << FC << " = 0; (void)" << FC << ";\n";
            indent(); out << "for (long " << FI << " = ";
            if (!s.forRevert) { emitOpt(s.forOffsetE, "0"); }
            else { out << FE << " - 1 - "; emitOpt(s.forOffsetE, "0"); }
            out << "; ";
            out << (s.forRevert ? FI + " >= 0" : FI + " < " + FE);
            if (hasNum) { out << " && " << FC << " < "; emitOpt(s.forNumE, "0"); }
            out << "; " << FI << (s.forRevert ? " -= " : " += "); emitOpt(s.forStepE, "1");
            out << ", " << FC << "++) {\n";
            depth++;
            std::string declTy = cast ? cTypeOf(vBase, vPtr) : cTypeOf(elemBase, elemPtr);
            if (!cast) { vBase = elemBase; vPtr = elemPtr; }
            indent(); out << declTy << " " << s.forVar << " = ";
            if (cast && vPtr > 0) out << "(" << declTy << ")&" << FB << "[" << FI << "]";
            else if (cast)        out << "(" << declTy << ")" << FB << "[" << FI << "]";
            else                  out << FB << "[" << FI << "]";
            out << ";\n";
            // 数组（可索引）→ 真实下标 FI（revert 时倒序，v==coll[i] 恒等）；
            // 字符串（仅 next/'\0' 迭代）→ 递增计数 FC（0,1,2...，与 revert/offset 无关）。
            emitIdx(kind == KString ? FC : FI);
            emitForInBody(s, vBase, vPtr);
            depth--; indent(); out << "}\n";
        } else if (kind == KIndexSeq) {
            // ---- 序列索引协议：len + 整型下标 get/at 取值（list / array）----
            //   for v in seq [, i]：v=元素（list 为借用 @、array 为元素指针 &），i=下标。
            const Decl* mLen = findMethod(ct.name, "len");
            const Decl* mGet = idxGetterOf(ct.name);
            const TypeRef* grt = mGet->structCommon.type.get();
            const bool getFat = grt && grt->fat;              // get 返回 @/*（如 list）→ 借用句柄
            const bool getThin = grt && grt->thin;            // get 返回 （瘦指针）→ sc_thin（否则 sc_afat）
            const char* getHTy = getThin ? "sc_thin" : "sc_afat";
            std::string elemBase = grt ? grt->name : "";
            int elemPtr = grt ? grt->ptr : 1;
            std::string recvTy = cTypeOf(ct.name, 1);
            indent(); out << recvTy << " " << FR << " = ";
            if (ct.ptr == 0) out << "&";
            emitExpr(*s.forColl, true); out << ";\n";
            indent(); out << "long " << FE << " = (long)" << scPfx(mLen->name) << "(" << FR << ");\n";
            indent(); out << "long " << FC << " = 0; (void)" << FC << ";\n";
            indent(); out << "for (long " << FI << " = ";
            if (!s.forRevert) { emitOpt(s.forOffsetE, "0"); }
            else { out << FE << " - 1 - "; emitOpt(s.forOffsetE, "0"); }
            out << "; ";
            out << (s.forRevert ? FI + " >= 0" : FI + " < " + FE);
            if (hasNum) { out << " && " << FC << " < "; emitOpt(s.forNumE, "0"); }
            out << "; " << FI << (s.forRevert ? " -= " : " += "); emitOpt(s.forStepE, "1");
            out << ", " << FC << "++) {\n";
            depth++;
            // 循环变量：默认取 get 返回类型（@ → sc_afat 借用；& → 元素指针）；显式注解则下转。
            bool vFat;
            std::string declTy;
            if (cast) { declTy = cTypeOf(vBase, vPtr); vFat = false; }
            else {
                declTy = getFat ? getHTy : cTypeOf(elemBase, elemPtr);
                vBase = elemBase; vPtr = getFat ? 0 : elemPtr; vFat = getFat;
            }
            indent(); out << declTy << " " << s.forVar << " = ";
            if (cast && getFat && vPtr > 0)
                out << "(" << declTy << ")(" << scPfx(mGet->name) << "(" << FR << ", " << FI << ")).p";
            else if (cast)
                out << "(" << declTy << ")" << scPfx(mGet->name) << "(" << FR << ", " << FI << ")";
            else
                out << scPfx(mGet->name) << "(" << FR << ", " << FI << ")";
            out << ";\n";
            emitIdx(FI);                        // 可索引 → 真实下标 FI（revert 时倒序）
            emitForInBody(s, vBase, vPtr, vFat);
            depth--; indent(); out << "}\n";
        } else if (kind == KMap) {
            // ---- 映射协议：first/next 游标 + key_at/value_at（dict / bst / lru）----
            //   for k, v in map：k=键（默认 const void&，注解可下转），v=值（借用 @）。
            //   终止判据=value_at 返回空句柄（.p==NULL），与游标哨兵无关（dict -1 / bst·lru 0 通用）。
            if (s.forIdxVars.size() != 1)
                throw CompileError{"for-in 映射容器需 `k, v` 两个变量（键, 值）", s.line};
            if (s.forStepE || s.forOffsetE)
                throw CompileError{"for-in 映射暂不支持 step/offset 选项", s.line};
            const Decl* mFirst = findMethod(ct.name, s.forRevert ? "last" : "first");
            const Decl* mAdv   = findMethod(ct.name, s.forRevert ? "prev" : "next");
            const Decl* mKey   = findMethod(ct.name, "key_at");
            const Decl* mVal   = findMethod(ct.name, "value_at");
            if (!mFirst || !mAdv)
                throw CompileError{"for-in 映射缺少 " + std::string(s.forRevert ? "last/prev" : "first/next") + " 方法", s.line};
            std::string recvTy = cTypeOf(ct.name, 1);
            indent(); out << recvTy << " " << FR << " = ";
            if (ct.ptr == 0) out << "&";
            emitExpr(*s.forColl, true); out << ";\n";
            indent(); out << "long " << FC << " = 0; (void)" << FC << ";\n";
            indent(); out << "for (int64_t " << FI << " = " << scPfx(mFirst->name) << "(" << FR << "); ; "
                          << FI << " = " << scPfx(mAdv->name) << "(" << FR << ", " << FI << "), " << FC << "++) {\n";
            depth++;
            // value 变量（forIdxVars[0]）：借用句柄（value_at 返回瘦指针则 sc_thin，否则 sc_afat），
            // 空句柄即终止；不参与作用域回收。
            const TypeRef* vrt = mVal->structCommon.type.get();
            const char* valHTy = (vrt && vrt->thin) ? "sc_thin" : "sc_afat";
            indent(); out << valHTy << " " << s.forIdxVars[0] << " = " << scPfx(mVal->name)
                          << "(" << FR << ", " << FI << ");\n";
            indent(); out << "if (" << s.forIdxVars[0] << ".p == (void *)0) break;\n";
            if (hasNum) { indent(); out << "if (" << FC << " >= "; emitOpt(s.forNumE, "0"); out << ") break;\n"; }
            // key 变量（forVar）：默认 const void*，显式注解 name: T& 则下转。
            std::string kBase; int kPtr; std::string kty;
            if (cast) { kBase = vBase; kPtr = vPtr; kty = cTypeOf(kBase, kPtr); }
            else { kBase = ""; kPtr = 1; kty = "const void *"; }
            indent(); out << kty << " " << s.forVar << " = (" << kty << ")"
                          << scPfx(mKey->name) << "(" << FR << ", " << FI << ");\n";
            // 登记两变量轻量类型 → 发体 → 还原（k=键指针、v=裸 @ 借用）。
            auto& tbl = inFunc ? localsT : globalsT;
            bool hk = tbl.count(s.forVar); VType ok; if (hk) ok = tbl[s.forVar];
            tbl[s.forVar] = VType{kBase, kPtr, 0, false};
            bool hv = tbl.count(s.forIdxVars[0]); VType ov; if (hv) ov = tbl[s.forIdxVars[0]];
            tbl[s.forIdxVars[0]] = VType{"", 0, 0, true};
            emitLoopBody(s.body);
            if (hv) tbl[s.forIdxVars[0]] = ov; else tbl.erase(s.forIdxVars[0]);
            if (hk) tbl[s.forVar] = ok; else tbl.erase(s.forVar);
            depth--; indent(); out << "}\n";
        } else {
            // ---- 链式序列：first/next 游标遍历（chain / 容器）----
            const bool isChain = (kind == KChain);
            const Decl* mFirst = findMethod(ct.name, s.forRevert ? "last" : "first");
            if (!mFirst) throw CompileError{"for-in：集合缺少 " + std::string(s.forRevert ? "last" : "first") + " 方法", s.line};
            const Decl* mAdv = isChain ? nullptr : findMethod(ct.name, s.forRevert ? "prev" : "next");
            if (!isChain && !mAdv) throw CompileError{"for-in：容器缺少 " + std::string(s.forRevert ? "prev" : "next") + " 方法（revert 需 last/prev）", s.line};
            // 接收者指针
            std::string recvTy = cTypeOf(ct.name, 1);
            indent(); out << recvTy << " " << FR << " = ";
            if (ct.ptr == 0) out << "&";
            emitExpr(*s.forColl, true); out << ";\n";
            // 游标起点
            indent(); out << "void *" << FI << " = (void *)" << scPfx(mFirst->name) << "(" << FR << ");\n";
            // 前进表达式生成器（next/prev）
            auto advExpr = [&]() {
                if (isChain) {
                    // chain：前进读注入的 _next（偏移 sizeof(void*)，链尾为 nil）；
                    // 逆序用边界安全前驱 chain_prev（链头 _prev 指向 rear，须经契约判定 → nil）。
                    if (!s.forRevert) out << "((void *)*(void **)((char *)" << FI << " + sizeof(void *)))";
                    else { requireChain(s.line); out << "((void *)sc_chain_prev(" << FI << "))"; }
                } else {
                    out << "((void *)" << scPfx(mAdv->name) << "(" << FR << ", " << FI << "))";
                }
            };
            // offset 跳过
            if (s.forOffsetE) {
                indent(); out << "{ long _fo" << n << " = "; emitOpt(s.forOffsetE, "0");
                out << "; while (_fo" << n << "-- > 0 && " << FI << " != (void *)0) " << FI << " = ";
                advExpr(); out << "; }\n";
            }
            indent(); out << "long " << FC << " = 0; (void)" << FC << ";\n";
            // 循环变量：默认取 first 返回类型（chain=void&，容器=I&）；显式注解则下转
            std::string declTy;
            if (cast) declTy = cTypeOf(vBase, vPtr);
            else {
                const TypeRef* rt = mFirst->structCommon.type.get();
                std::string rb = rt ? rt->name : "";
                int rp = rt ? rt->ptr : 1;
                declTy = cTypeOf(rb, rp);
                vBase = rb; vPtr = rp;
            }
            if (s.forStepE) {
                // step 多步前进含内层循环，不能置于 for-increment（那须 GNU 语句表达式，
                //   MSVC 不支持）——改写为 while：前进置于「下一轮迭代前」（首轮标志跳过），
                //   与 for 完全等价，保持 break/continue 语义（continue→循环顶→前进，break→退出）。
                indent(); out << "int _ff" << n << " = 1;\n";
                indent(); out << "while (1) {\n";
                depth++;
                indent(); out << "if (!_ff" << n << ") { long _fk" << n << " = "; emitOpt(s.forStepE, "1");
                out << "; while (_fk" << n << "-- > 0 && " << FI << " != (void *)0) " << FI << " = ";
                advExpr(); out << "; " << FC << "++; }\n";
                indent(); out << "_ff" << n << " = 0;\n";
                indent(); out << "if (!(" << FI << " != (void *)0";
                if (hasNum) { out << " && " << FC << " < "; emitOpt(s.forNumE, "0"); }
                out << ")) break;\n";
                indent(); out << declTy << " " << s.forVar << " = (" << declTy << ")" << FI << ";\n";
                emitIdx(FC);
                emitForInBody(s, vBase, vPtr);
                depth--; indent(); out << "}\n";
            } else {
                indent(); out << "for (; " << FI << " != (void *)0";
                if (hasNum) { out << " && " << FC << " < "; emitOpt(s.forNumE, "0"); }
                out << "; " << FI << " = "; advExpr(); out << ", " << FC << "++) {\n";
                depth++;
                indent(); out << declTy << " " << s.forVar << " = (" << declTy << ")" << FI << ";\n";
                emitIdx(FC);                        // 链/容器（仅 next 迭代）→ 递增计数 0,1,2...
                emitForInBody(s, vBase, vPtr);
                depth--; indent(); out << "}\n";
            }
        }
        depth--; indent(); out << "}\n";
    }

    // for-in 体：登记循环变量轻量类型（供体内方法糖/成员访问解析），发出体语句，再还原。
    void emitForInBody(const Stmt& s, const std::string& vBase, int vPtr, bool vFat = false) {
        auto& tbl = inFunc ? localsT : globalsT;
        bool had = tbl.count(s.forVar);
        VType old; if (had) old = tbl[s.forVar];
        tbl[s.forVar] = VType{vBase, vPtr, 0, vFat};
        // 索引/坐标变量：登记为 i4（整型计数）
        std::vector<std::pair<bool, VType>> savedIdx;
        for (auto& iv : s.forIdxVars) {
            bool h = tbl.count(iv);
            savedIdx.push_back({h, h ? tbl[iv] : VType{}});
            tbl[iv] = VType{"i4", 0, 0};
        }
        emitLoopBody(s.body);
        for (size_t i = 0; i < s.forIdxVars.size(); i++) {
            if (savedIdx[i].first) tbl[s.forIdxVars[i]] = savedIdx[i].second;
            else tbl.erase(s.forIdxVars[i]);
        }
        if (had) tbl[s.forVar] = old; else tbl.erase(s.forVar);
    }

    // 分身/切片句柄变量名 → 实体类型 T 名（非句柄返回空串）
    std::string projEntityOf(const std::string& name) const {
        if (inFunc) { auto it = projVarsL.find(name); if (it != projVarsL.end()) return it->second; }
        auto it = projVarsG.find(name);
        return it != projVarsG.end() ? it->second : std::string{};
    }

    // 实体类型 ent 上名为 name 的「每对象方法指针」字段（MethodPtr）。
    // 分身 alloc/free 既可是类方法（findMethod），也可是 MethodPtr 字段（com 等设备相关每对象实现）。
    const Field* methodPtrField(const std::string& ent, const std::string& name) const {
        const Decl* sd = aggrOf(ent);
        if (!sd) return nullptr;
        for (auto& f : sd->structCommon.fields)
            if (f.name == name && f.type.fnKind == TypeRef::FncKind::MethodPtr) return &f;
        return nullptr;
    }

    // ---- com 通讯链（<< 发 / >> 收，同步形态）----
    // 一次 com io 操作：target=数据目标（lvalue），send=true 为 <<（write 发），否则 >>（read 收）。
    struct ComOp { const Expr* target; bool send; };

    // 若 e 是以 com（设备通讯端点）为最左操作数的 << / >> 链，返回 com 基址表达式，
    // 并按从左到右顺序填充 ops；否则返回 nullptr（让 << / >> 退化为普通位移）。
    // 形如 ((com >> a) << b) >> c：左结合，自顶向下剥离，最左操作数须为 com 基类型。
    const Expr* comChain(const Expr& e, std::vector<ComOp>& ops) const {
        if (e.kind != Expr::Binary || (e.op != "<<" && e.op != ">>")) return nullptr;
        std::vector<ComOp> rev;
        const Expr* cur = &e;
        while (cur->kind == Expr::Binary && (cur->op == "<<" || cur->op == ">>")) {
            rev.push_back({cur->b.get(), cur->op == "<<"});
            cur = cur->a.get();
        }
        VType vt;
        if (!exprVType(*cur, vt) || vt.name != "com" || !aggrOf("com")) return nullptr;
        ops.assign(rev.rbegin(), rev.rend());
        return cur;
    }

    // 发出 com 通讯链（同步形态）：
    //   · 目标为 com[...] 句柄（limit 分身）：com >> s → 框架读流程 limit_read(&com, s._)；
    //   · 目标为普通变量 v：com << v / com >> v → 直接调 com 的 write/read 每对象方法指针。
    //       com << v  →  _scsz = sizeof(v); com.write(&com, &v, &_scsz);
    //       com >> v  →  _scsz = sizeof(v); com.read (&com, &v, &_scsz);
    // 接收者按值/指针自动取址注入（与 MethodPtr 调用约定一致）。size 为收发字节数（in/out）。
    void emitComChain(const Expr& base, const std::vector<ComOp>& ops) {
        VType vt;
        exprVType(base, vt);
        const bool isPtr = vt.ptr >= 1;
        indent(); out << "{\n";
        depth++;
        bool declaredSz = false;
        for (auto& o : ops) {
            // rpc 形态：发（<<）目标为 rpc 调用 rpc(args)；收（>>）目标为裸 rpc 名 rpc。
            if (const Decl* r = comRpcTarget(o.target, o.send)) {
                if (o.send) emitComRpcSend(base, isPtr, *r, o.target->args);
                else        emitComRpcRecv(base, isPtr, *r);
                continue;
            }
            // 误用诊断：rpc 名/调用方向不符
            if (const Decl* r = comRpcTarget(o.target, !o.send)) {
                (void)r;
                if (o.send) throw CompileError{"com << rpc 发送需带参数：com << rpc(参数...)", o.target->line};
                else        throw CompileError{"com >> rpc 收端不接受实参，请写 com >> rpc", o.target->line};
            }
            if (o.target->kind == Expr::Call)
                throw CompileError{"com 通讯的回调形态（<< / >> 接非 rpc 调用）暂未实现", o.target->line};
            // 目标为 com[...] 句柄（limit 分身）→ 框架读流程驱动
            if (o.target->kind == Expr::Ident && projEntityOf(o.target->text) == "com") {
                if (o.send)
                    throw CompileError{"com[...] 句柄仅用于 >> 读流程，不支持 << 写", o.target->line};
                indent(); out << "sc_limit_read(";
                if (isPtr) emitExpr(base, true);
                else { out << "&("; emitExpr(base, true); out << ")"; }
                out << ", " << o.target->text << "._);\n";
                continue;
            }
            // 普通变量：直接 write/read（同步收发 sizeof 字节）
            if (!declaredSz) { indent(); out << "uint32_t _scsz;\n"; declaredSz = true; }
            const char* method = o.send ? "write" : "read";
            indent();
            out << "_scsz = sizeof(";
            emitExpr(*o.target, true);
            out << "); ";
            emitExpr(base, true);
            out << (isPtr ? "->" : ".") << method << "(";
            if (isPtr) emitExpr(base, true);
            else { out << "&("; emitExpr(base, true); out << ")"; }
            out << ", (void *)&(";
            emitExpr(*o.target, true);
            out << "), &_scsz);\n";
        }
        depth--;
        indent(); out << "}\n";
    }

    // com 收发的 rpc 形态识别：
    //   发（send=true）：target 为 rpc 调用 `rpc(args)`（Call，callee 为 rpc 名）；
    //   收（send=false）：target 为裸 rpc 名 `rpc`（Ident，名为 rpc）。
    // 返回对应 rpc Decl，否则 nullptr。
    const Decl* comRpcTarget(const Expr* t, bool send) const {
        std::string nm;
        if (send) {
            if (t->kind == Expr::Call && t->a && t->a->kind == Expr::Ident) nm = t->a->text;
        } else {
            if (t->kind == Expr::Ident) nm = t->text;
        }
        if (nm.empty()) return nullptr;
        auto it = rpcs.find(nm);
        return it != rpcs.end() ? it->second : nullptr;
    }

    // 发出「接收者地址」实参（com*）：指针直接传，值取址。
    void emitComBasePtr(const Expr& base, bool isPtr) {
        if (isPtr) emitExpr(base, true);
        else { out << "&("; emitExpr(base, true); out << ")"; }
    }

    // ---- queue 投递链（<< 投递，整体打包）----
    // 若 e 是以 queue（消息队列端点）为最左操作数的 << 链，返回 queue 基址表达式，
    // 并按从左到右顺序填充 targets（每个为一个 rpc 调用 work(args)）；否则返回 nullptr。
    // 与 com 不同：queue 仅支持 << 投递（>> 报错），每个目标整体打包成一条消息 post。
    const Expr* queueChain(const Expr& e, std::vector<const Expr*>& targets) const {
        if (e.kind != Expr::Binary || (e.op != "<<" && e.op != ">>")) return nullptr;
        std::vector<const Expr*> rev;
        bool sawRecv = false;
        const Expr* cur = &e;
        while (cur->kind == Expr::Binary && (cur->op == "<<" || cur->op == ">>")) {
            if (cur->op == ">>") sawRecv = true;
            rev.push_back(cur->b.get());
            cur = cur->a.get();
        }
        VType vt;
        if (!exprVType(*cur, vt) || vt.name != "queue" || !aggrOf("queue")) return nullptr;
        if (sawRecv)
            throw CompileError{"queue 仅支持 << 投递，不支持 >> 读取", e.line};
        targets.assign(rev.rbegin(), rev.rend());
        return cur;
    }

    // 发出 queue 投递链：每个目标 rpc 调用 work(args) 整体打包成一条消息 post。
    //   q << work(a, b)  →  { struct work _rp = {0}; _rp.x = a; ...;
    //                         q->post(q, (void (*)(void *))work_rpc, &_rp, sizeof(_rp), 0, 0); }
    // 接收者按值/指针自动取址注入（emitComBasePtr，与协议指针调用约定一致）。
    void emitQueueChain(const Expr& base, const std::vector<const Expr*>& targets) {
        VType vt;
        exprVType(base, vt);
        const bool isPtr = vt.ptr >= 1;
        for (const Expr* t : targets) {
            if (t->kind != Expr::Call || !t->a || t->a->kind != Expr::Ident)
                throw CompileError{"queue << 的目标必须是 rpc 调用：q << work(参数...)", t->line};
            auto it = rpcs.find(t->a->text);
            if (it == rpcs.end())
                throw CompileError{"queue << 的目标必须是 rpc：" + t->a->text, t->line};
            const Decl* r = it->second;
            if (r->hasAwait)
                throw CompileError{"queue << 暂不支持异步 rpc：" + r->name, t->line};
            if (r->structCommon.variadic)
                throw CompileError{"queue << 暂不支持可变参数 rpc：" + r->name, t->line};
            if (t->args.size() > r->structCommon.fields.size())
                throw CompileError{"rpc 实参数量超出：" + r->name, t->line};
            indent(); out << "{\n"; depth++;
            indent(); out << "struct " << scPfx(r->name) << " _rp = {0};\n";
            for (size_t i = 0; i < t->args.size(); i++) {
                const Field& f = r->structCommon.fields[i];
                indent(); out << "_rp." << f.name << " = ";
                emitExpr(*t->args[i], true); out << ";\n";
                if (!f.type.arrayDims.empty()) {       // 数组实参：额外装填 size（字节数）
                    indent(); out << "_rp." << rpcArraySizeName(f) << " = ";
                    emitRpcArraySizeof(f); out << ";\n";
                }
            }
            indent();
            emitComBasePtr(base, isPtr);
            out << "->post(";
            emitComBasePtr(base, isPtr);
            out << ", (void (*)(void *))" << scPfx(r->name) << "_rpc, &_rp, sizeof(_rp), 0, 0);\n";
            depth--; indent(); out << "}\n";
        }
    }

    // com << rpc(args)（发）：装填 rpc 参数结构体（跳过返回槽 _），逐参数字段序列化 write。
    //   普通标量/指针：write(&_rp.f, sizeof)；数组：write(_rp.f, f_size)；
    //   com[...] 参数：句柄无法序列化 → 报错。
    void emitComRpcSend(const Expr& base, bool isPtr, const Decl& r,
                        const std::vector<ExprPtr>& args) {
        if (r.hasAwait)
            throw CompileError{"com 收发暂不支持异步 rpc：" + r.name, r.line};
        if (r.structCommon.variadic)
            throw CompileError{"com << rpc 暂不支持可变参数 rpc：" + r.name, r.line};
        if (args.size() > r.structCommon.fields.size())
            throw CompileError{"rpc 实参数量超出：" + r.name, r.line};
        indent(); out << "{\n"; depth++;
        indent(); out << "struct " << scPfx(r.name) << " _rp = {0};\n";
        // 装填实参（与 run 一致；数组额外填 size）
        for (size_t i = 0; i < args.size(); i++) {
            const Field& f = r.structCommon.fields[i];
            if (f.type.project)
                throw CompileError{"com << rpc 的 com[...] 参数不支持序列化发送：" + f.name, r.line};
            indent(); out << "_rp." << f.name << " = "; emitExpr(*args[i], true); out << ";\n";
            if (!f.type.arrayDims.empty()) {
                indent(); out << "_rp." << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f); out << ";\n";
            }
        }
        indent(); out << "uint32_t _scsz;\n";
        // 逐参数字段 write（声明顺序，与收端对称）
        for (auto& f : r.structCommon.fields) {
            if (f.type.project)
                throw CompileError{"com << rpc 的 com[...] 参数不支持序列化发送：" + f.name, r.line};
            indent();
            if (!f.type.arrayDims.empty()) {
                out << "_scsz = (uint32_t)_rp." << rpcArraySizeName(f) << "; ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "write(";
                emitComBasePtr(base, isPtr);
                out << ", (void *)(_rp." << f.name << "), &_scsz);\n";
            } else {
                out << "_scsz = sizeof(_rp." << f.name << "); ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "write(";
                emitComBasePtr(base, isPtr);
                out << ", (void *)&(_rp." << f.name << "), &_scsz);\n";
            }
        }
        depth--; indent(); out << "}\n";
    }

    // com >> rpc（收）：从 com 逐参数字段读入 rpc 参数结构体（跳过返回槽 _），然后触发 rpc。
    //   普通标量/指针：read(&_rp.f, sizeof)；
    //   数组：栈上开等长后备缓冲、_rp.f 指向它，read 进缓冲；
    //   com[...] 参数：以本 com 为本体绑定句柄（alloc + _self），走框架读流程 limit_read。
    // 读毕调用 worker `rpc_rpc(&_rp)`（不取返回值）。
    void emitComRpcRecv(const Expr& base, bool isPtr, const Decl& r) {
        if (r.hasAwait)
            throw CompileError{"com 收发暂不支持异步 rpc：" + r.name, r.line};
        if (r.structCommon.variadic)
            throw CompileError{"com >> rpc 暂不支持可变参数 rpc：" + r.name, r.line};
        indent(); out << "{\n"; depth++;
        indent(); out << "struct " << scPfx(r.name) << " _rp = {0};\n";
        // 数组后备缓冲 + com[...] 句柄上下文初始化
        for (auto& f : r.structCommon.fields) {
            if (!f.type.arrayDims.empty()) {
                const std::string bk = "_rp_" + f.name;
                indent(); emitArrayBacking(f, bk); out << ";\n";
                indent(); out << "_rp." << f.name << " = " << bk << ";\n";
                indent(); out << "_rp." << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f); out << ";\n";
            } else if (f.type.project) {
                // 句柄上下文（alloc 参数）从 projectArgs 初始化
                const std::vector<Field>* alParams = projectAllocParams(f.type.name);
                if (f.type.projectArgs && alParams) {
                    size_t n = std::min(f.type.projectArgs->size(), alParams->size());
                    for (size_t i = 0; i < n; i++) {
                        indent(); out << "_rp." << f.name << "." << (*alParams)[i].name << " = ";
                        emitExpr(*(*f.type.projectArgs)[i], true); out << ";\n";
                    }
                }
            }
        }
        indent(); out << "uint32_t _scsz;\n";
        // 逐参数字段 read
        for (auto& f : r.structCommon.fields) {
            if (!f.type.arrayDims.empty()) {
                indent();
                out << "_scsz = (uint32_t)_rp." << rpcArraySizeName(f) << "; ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "read(";
                emitComBasePtr(base, isPtr);
                out << ", (void *)(_rp." << f.name << "), &_scsz);\n";
            } else if (f.type.project) {
                // 绑定句柄到本 com（alloc + _self 回指）+ 框架读流程
                const std::vector<Field>* alParams = projectAllocParams(f.type.name);
                indent(); out << "_rp." << f.name << "._ = ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "alloc(";
                emitComBasePtr(base, isPtr);
                if (alParams) for (auto& p : *alParams)
                    out << ", _rp." << f.name << "." << p.name;
                out << ");\n";
                indent(); out << "_rp." << f.name << "._->_self = ";
                emitComBasePtr(base, isPtr); out << ";\n";
                indent(); out << "sc_limit_read(";
                emitComBasePtr(base, isPtr);
                out << ", _rp." << f.name << "._);\n";
            } else {
                indent();
                out << "_scsz = sizeof(_rp." << f.name << "); ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "read(";
                emitComBasePtr(base, isPtr);
                out << ", (void *)&(_rp." << f.name << "), &_scsz);\n";
            }
        }
        indent(); out << scPfx(r.name) << "_rpc(&_rp);\n";
        depth--; indent(); out << "}\n";
    }

    // 分身实体 ent 的 alloc 参数列表（句柄上下文字段，= alloc 去隐式 this 后的形参）。
    const std::vector<Field>* projectAllocParams(const std::string& ent) const {
        const Decl* al = findMethod(ent, "alloc");
        if (al) return &al->structCommon.fields;
        const Field* alF = methodPtrField(ent, "alloc");
        return alF ? &alF->type.structCommon.fields : nullptr;
    }

    // rpc 数组形参的栈上后备缓冲声明：`T nm[d1][d2]...`（按声明的完整数组类型）。
    void emitArrayBacking(const Field& f, const std::string& nm) {
        std::string base; int ptr; resolveType(f.type, base, ptr);
        out << base << " ";
        for (int i = 0; i < ptr; i++) out << "*";
        out << nm;
        for (auto& d : f.type.arrayDims) out << "[" << d << "]";
    }


    // 分身/切片句柄赋值语法糖：
    //   s = nil  →  if (s._) { T_free(s._->_self, s._); s._ = NULL; }
    //   s = 本体 →  s._ = T_alloc(&本体, s.p1, s.p2, ...); s._->_self = &本体;
    // alloc/free 可为类方法（T_xxx(&recv,...)）或 MethodPtr 字段（recv.xxx(&recv,...)）。
    void emitProjectAssign(const std::string& s, const std::string& ent, const Expr& rhs) {
        const bool isNil = rhs.kind == Expr::Ident && rhs.text == "nil";
        const Decl* fr = findMethod(ent, "free");
        const Decl* al = findMethod(ent, "alloc");
        const Field* frF = fr ? nullptr : methodPtrField(ent, "free");
        const Field* alF = al ? nullptr : methodPtrField(ent, "alloc");
        if (isNil) {
            indent(); out << "if (" << s << "._) { ";
            // free 接收者 = 本体 = s._->_self
            if (fr) out << scPfx(fr->name) << "(" << s << "._->_self, " << s << "._); ";
            else if (frF) out << s << "._->_self->free(" << s << "._->_self, " << s << "._); ";
            out << s << "._ = NULL; }\n";
            return;
        }
        indent();
        out << s << "._ = ";
        const std::vector<Field>* alParams = al ? &al->structCommon.fields
                                          : (alF ? &alF->type.structCommon.fields : nullptr);
        if (al) out << scPfx(al->name) << "(&";           // 类方法：T_alloc(&本体, ...)
        else if (alF) { emitExpr(rhs, true); out << ".alloc(&"; }  // 字段：本体.alloc(&本体, ...)
        else out << "(&";
        emitExpr(rhs, true);
        if (alParams) for (auto& p : *alParams) out << ", " << s << "." << p.name;
        out << ");\n";
        indent();
        out << s << "._->_self = &";
        emitExpr(rhs, true);
        out << ";\n";
    }

    // 若表达式是「已登记 inl 块名的调用」，返回其 InlineDefS，否则 nullptr
    const Stmt* asInlineCall(const Expr* e) const {
        if (e && e->kind == Expr::Call && e->a && e->a->kind == Expr::Ident) {
            auto it = inlineDefs.find(e->a->text);
            if (it != inlineDefs.end()) return it->second;
        }
        return nullptr;
    }

    // inl 块返回类型的 C 类型串（值 inl 用于提升临时声明；void inl 返回空串）
    std::string inlRetCType(const Stmt& def) const {
        const auto& rt = def.decl->structCommon.type;
        if (!rt || (rt->name.empty() && rt->ptr == 0 && !rt->fat)) return "";
        VType vt{rt->name, rt->ptr, (int)rt->arrayDims.size()};
        vt.fat = rt->fat;
        return cTypeOf(vt.name, vt.ptr);
    }

    // 把表达式渲染为 C 代码串（用于捕获 inl 值上下文的赋值目标 lhs）
    std::string renderExprStr(const Expr& e) {
        std::ostringstream tmp;
        std::swap(tmp, out);
        emitExpr(e, true);
        std::swap(tmp, out);
        return tmp.str();
    }

    // inl 真内联块展开：调用点原地展开为 { 形参临时=实参; 块体; 尾标签:; }
    //   · 形参在展开处求值一次到同名块级临时（绑定一次，非文本多次求值）；
    //   · 块体经 emitStmts 输出，共享外层栈帧（前置局部可见可改）；
    //   · void 上下文（lhs 空）：块体裸 return → goto 尾标签（早退本块）；
    //   · 值上下文（lhs 非空）：块体 return v → lhs=v; goto 尾标签；
    //   · 递归（直接/互相）经 inlineActive 栈检测报错（语义层亦有静态检测）。
    void emitInlineExpand(const Stmt& def, const std::vector<ExprPtr>& args,
                          const std::string& name, const std::string& lhs) {
        const auto& params = def.decl->structCommon.fields;
        for (auto& a : inlineActive)
            if (a == name)
                throw CompileError{"inl 递归展开：内联块 '" + name +
                                   "' 直接或间接调用自身（内联块不可递归）", def.line};
        if (args.size() != params.size())
            throw CompileError{"inl 块 '" + name + "' 需 " +
                               std::to_string(params.size()) + " 个实参，得到 " +
                               std::to_string(args.size()), def.line};

        std::string endLabel = "__sc_inl_end_" + std::to_string(inlineSeq++);

        indent(); out << "{\n";
        depth++;
        // 先提升实参内嵌套的 inl 调用（其临时须先于形参绑定发射），使形参绑定处可替换。
        std::vector<const Expr*> argPtrs;
        for (auto& a : args) if (a) argPtrs.push_back(a.get());
        bool argHoist = inlHoistBegin(argPtrs);
        // 形参绑定：
        //   · 字面值实参 → 直接文本代入（快路径，无临时；块体引用形参处替换为该文本）；
        //   · 其余实参   → 同名块级临时 T p = 实参（在此求值一次，绑定一次）。
        //  代入表在块体发射期间生效（保存/恢复外层，支持内联体内再嵌套展开）。
        std::unordered_map<std::string, std::string> newParamSub;
        // 先把所有非字面值实参在“形参尚未声明”时算入唯一临时 _inla*（以外层代入表渲染）。
        // 否则嵌套展开中，内层形参与外层同名变量在 C 作用域内相互遮蔽，会生成 `T b = b;`
        // 之类的自初始化（右值读到刚声明、未初始化的内层 b），运行期即野指针 → 段错误。
        std::vector<std::string> argTmp(params.size());
        for (size_t i = 0; i < params.size(); i++) {
            const Expr& a = *args[i];
            if (inlArgLiteral(a)) continue;
            std::string tn = "_inla" + std::to_string(inlTmpSeq++);
            indent();
            emitDeclarator(params[i], false, tn.c_str());
            out << " = ";
            emitExpr(a, true);                  // 此刻内层形参均未入作用域，引用的是外层同名变量
            out << ";\n";
            argTmp[i] = std::move(tn);
        }
        for (size_t i = 0; i < params.size(); i++) {
            const Field& p = params[i];
            const Expr& a = *args[i];
            if (inlArgLiteral(a)) {
                newParamSub[p.name] = renderExprStr(a);   // 以当前(外层)代入表渲染实参文本
            } else {
                indent();
                emitDeclarator(p);
                out << " = " << argTmp[i] << ";\n";       // 从外层已算好的临时绑定，避免自遮蔽
                regVar(p);                      // 登记，使块体引用形参可解析
            }
        }
        if (argHoist) inlHoistEnd();            // 实参绑定完毕，撤实参提升替换层
        // 展开块体：入展开上下文（帧栈 + 递归栈 + 形参代入表），emitStmts 自管其内层作用域清理
        auto savedParamSub = std::move(inlParamSub);
        inlParamSub = std::move(newParamSub);
        inlineFrames.push_back({endLabel, lhs});
        inlineActive.push_back(name);
        emitStmts(def.decl->body);
        inlineActive.pop_back();
        inlineFrames.pop_back();
        inlParamSub = std::move(savedParamSub);
        // 尾标签：return 早退目标（label 后接空语句，兼容其后无语句）
        indent(); out << endLabel << ":;\n";
        depth--;
        indent(); out << "}\n";
    }

    // 实参是否字面值（形参绑定快路径：直接文本代入，无需临时）
    static bool inlArgLiteral(const Expr& e) {
        return e.kind == Expr::IntLit || e.kind == Expr::FloatLit ||
               e.kind == Expr::CharLit || e.kind == Expr::StrLit;
    }

    // var/let x = name(args)：若初值是 inl 值调用，先声明 x（无初值），再展开块把
    //   return v 赋给 x。要求 inl 值调用独占单声明（语义层已强制）。返回 true=已处理。
    bool emitInlineInitVar(const std::vector<Field>& decls) {
        if (decls.size() != 1) return false;
        const Field& f = decls[0];
        const Stmt* def = f.init ? asInlineCall(f.init.get()) : nullptr;
        if (!def) return false;
        // 先声明目标变量（非 const，便于块内赋值；sc let 的不可变性由语义层保证）
        indent();
        emitDeclarator(f);
        out << ";\n";
        regVar(f);
        // 值上下文展开：块内 return v → f.name = v; goto label
        emitInlineExpand(*def, f.init->args, f.init->a->text, f.name);
        return true;
    }

    void emitStmt(const Stmt& s) {
        // 行号映射：指定了源文件时输出 #line 指令（调试器断点/单步/堆栈
        // 直接落在 .sc 源码）；否则输出注释供人工对照
        if (!inMacro && s.line > 0) {
            if (!srcFile.empty()) {
                out << "#line " << s.line << " \"" << srcFile << "\"\n";
            } else {
                indent();
                out << "/* line " << s.line << " */\n";
            }
        }

        // inl 值提升：无条件求值位置内嵌套的值 inl 调用先展开到自动临时 _inlN，
        // 原位以临时名参与外层表达式。作用域覆盖整个语句发射（RAII 保证各 break 分支亦清理）。
        struct InlHoistScope {
            CGen* cg; bool active;
            ~InlHoistScope() { if (active) cg->inlHoistEnd(); }
        } _ihs{ this, beginInlHoistFor(s) };
        (void)_ihs;

        switch (s.kind) {
            case Stmt::ExprS:
                // inl 真内联块调用：
                //   · 裸 name(args)          → void 上下文展开（lhs 空）
                //   · lhs = name(args)       → 值上下文展开（return v → lhs=v; goto）
                if (s.expr) {
                    if (const Stmt* def = asInlineCall(s.expr.get())) {
                        emitInlineExpand(*def, s.expr->args, s.expr->a->text, "");
                        break;
                    }
                    if (s.expr->kind == Expr::Binary && s.expr->op == "=") {
                        if (const Stmt* def = asInlineCall(s.expr->b.get())) {
                            std::string lhs = renderExprStr(*s.expr->a);
                            emitInlineExpand(*def, s.expr->b->args,
                                             s.expr->b->a->text, lhs);
                            break;
                        }
                    }
                }

                // com 通讯链（同步形态）：com << v（发）/ com >> v（收）→ 直接 write/read
                if (s.expr && s.expr->kind == Expr::Binary &&
                    (s.expr->op == "<<" || s.expr->op == ">>")) {
                    std::vector<ComOp> ops;
                    if (const Expr* base = comChain(*s.expr, ops)) {
                        emitComChain(*base, ops);
                        break;
                    }
                    // queue 投递链：q << work(参数) → 整体打包 post（左操作数类型分派）
                    std::vector<const Expr*> qtargets;
                    if (const Expr* qbase = queueChain(*s.expr, qtargets)) {
                        emitQueueChain(*qbase, qtargets);
                        break;
                    }
                }
                // 分身/切片句柄赋值语法糖：s = 本体 / s = nil
                if (s.expr && s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                    s.expr->a && s.expr->a->kind == Expr::Ident) {
                    const std::string ent = projEntityOf(s.expr->a->text);
                    if (!ent.empty()) { emitProjectAssign(s.expr->a->text, ent, *s.expr->b); break; }
                }
                // 半自动指针 T@1 赋值：先销毁旧对象（非 nil → drop + free）再存新指针
                if (s.expr && s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                    s.expr->a && s.expr->a->kind == Expr::Ident) {
                    auto afit = autoFreeVars.find(s.expr->a->text);
                    if (afit != autoFreeVars.end()) {
                        emitAutoFreeAssign(afit->second, *s.expr->b);
                        break;
                    }
                }
                // 胖指针赋值：p = ... / base->m = ...（先拆旧边再绑新边，§4.1）
                if (s.expr && s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                    s.expr->a && isFatExpr(*s.expr->a)) {
                    if (emitFatAssignStmt(*s.expr->a, *s.expr->b, /*isInit*/ false)) break;
                }
                // 丢弃返回 T@ 的调用：移动来的入边无新主 → 丢弃点自动解绑临时（§7.7）
                if (s.expr && s.expr->kind == Expr::Call && isFatExpr(*s.expr)) {
                    std::string tt;
                    isFatExpr(*s.expr, &tt);              // 临时目标类型（解绑即析构清理子成员）
                    std::string dtorArg = fatDtorArg(tt);
                    std::string tmp = "_fatd" + std::to_string(fatTmpSeq++);
                    indent(); out << "{ sc_fat " << tmp << " = ";
                    emitExpr(*s.expr, true);
                    if (dtorArg.empty())
                        out << "; sc_fat_unbind(&" << tmp << "); }\n";
                    else
                        out << "; sc_fat_unbind_d(&" << tmp << ", " << dtorArg << "); }\n";
                    break;
                }
                {
                    bool seq = seqHoistBegin(*s.expr);
                    indent();
                    emitExpr(*s.expr, true);
                    out << ";\n";
                    if (seq) seqHoistEnd();
                }
                break;
            case Stmt::VarS:
                if (emitInlineInitVar(s.decls)) break;
                emitVarDecls(s.decls, false, inMacro && !s.exported, false, inMacro && s.exported); break;
            case Stmt::LetS:
                if (emitInlineInitVar(s.decls)) break;
                emitVarDecls(s.decls, true, inMacro && !s.exported, false, inMacro && s.exported); break;
            case Stmt::TlsS: emitVarDecls(s.decls, false, false, true); break;
            case Stmt::ReturnS:
                // inl 内联块展开中：return 早退本块（不返回外层函数）。
                //   · void 上下文（帧 lhs 空）：裸 return → goto 尾标签；
                //   · 值上下文（帧 lhs 非空）：return v → lhs=v; goto 尾标签。
                //   语义层已保证 void inl 只裸 return、值 inl 必带值 return。
                if (!inlineFrames.empty()) {
                    const auto& fr = inlineFrames.back();
                    indent();
                    if (s.expr && !fr.lhs.empty()) {
                        out << fr.lhs << " = ";
                        emitExpr(*s.expr, true);
                        out << "; goto " << fr.endLabel << ";\n";
                    } else {
                        out << "goto " << fr.endLabel << ";\n";
                    }
                    break;
                }
                if (curRpc) {
                    indent();
                    // rpc 实际函数：返回值写入结构体首个默认成员 _
                    if (s.expr && rpcHasRet(*curRpc)) {
                        out << "_p->_ = ";
                        emitExpr(*s.expr, true);
                        out << "; return;\n";
                    } else out << "return;\n";
                    break;
                }
                {
                    // 自动指针：是否有待清理胖根变量
                    bool anyFat = false;
                    for (auto& sc : fatScopes) if (!sc.empty()) { anyFat = true; break; }
                    // 自动指针 T@ 数组：任意活动作用域登记了 T@ 数组 → return 前须逐元素拆边
                    bool anyFatArr = false;
                    for (auto& fa : fatArrayScopes) if (!fa.empty()) { anyFatArr = true; break; }
                    // final 钩子：任意活动作用域登记了 final → return 也须先发出
                    bool anyFinal = false;
                    for (auto& fc : fatFinalScopes) if (!fc.empty()) { anyFinal = true; break; }
                    // --check=mem 栈数组尾哨兵：任意活动作用域登记了栈数组 → return 前须校验
                    bool anyCanary = false;
                    for (auto& mc : memCanaryScopes) if (!mc.empty()) { anyCanary = true; break; }
                    // RAII：任意活动作用域登记了栈值对象 → return 前须自动 drop
                    bool anyDrop = false;
                    for (auto& dc : dropScopes) if (!dc.empty()) { anyDrop = true; break; }
                    // 半自动指针 @1（单例指针）：任意活动作用域登记了 → return 前须销毁（非 nil → drop + free）
                    bool anyAutoFree = false;
                    for (auto& ac : autoFreeScopes) if (!ac.empty()) { anyAutoFree = true; break; }
                    // return p（p 为胖左值）= 移动：跳过该变量的 unbind，入边随返回值移交
                    // return v（v 为栈值对象）= 移动：跳过其自动 drop，所有权移交调用方
                    // return (t: @) / (t: T@)：擦除/还原 cast 同样把胖左值移交返回值，
                    //   须穿透 cast 取内层胖标识符，否则尾部清理会 unbind 掉它致返回值悬垂。
                    std::string skip;
                    const Expr* mv = s.expr.get();
                    if (mv && mv->kind == Expr::Cast && mv->a
                        && isFatExpr(*mv) && mv->a->kind == Expr::Ident && isFatExpr(*mv->a))
                        mv = mv->a.get();
                    if (mv && mv->kind == Expr::Ident && isFatExpr(*mv))
                        skip = mv->text;
                    else if (mv && mv->kind == Expr::Ident)
                        skip = mv->text;
                    if (!anyFat && !anyFatArr && !anyFinal && !anyCanary && !anyDrop
                        && !anyAutoFree
                        && !(inMainFunc && (mainHasTeardown || hasGcanaryHook() || hasGfatHook()))) {
                        bool seq = s.expr ? seqHoistBegin(*s.expr) : false;
                        indent();
                        out << "return";
                        if (s.expr) { out << " "; emitExpr(*s.expr, true); }
                        out << ";\n";
                        if (seq) seqHoistEnd();
                        break;
                    }
                    // 有清理：先把返回值算入临时（braced 块隔离 _ret），清理后再返回，
                    // 避免清理过程释放掉返回值所依赖的对象。
                    indent(); out << "{\n";
                    depth++;
                    if (s.expr) {
                        bool seq = seqHoistBegin(*s.expr);
                        indent();
                        if (curFnSig) emitRetType(*curFnSig); else out << "intptr_t";
                        out << " _ret = ";
                        emitExpr(*s.expr, true);
                        out << ";\n";
                        if (seq) seqHoistEnd();
                        emitFatReturnCleanup(skip);
                        indent(); out << "return _ret;\n";
                    } else {
                        emitFatReturnCleanup(skip);
                        indent(); out << "return;\n";
                    }
                    depth--;
                    indent(); out << "}\n";
                }
                break;
            case Stmt::BreakS:
                if (!fatBreakBoundary.empty())
                    emitFatCleanupTo(fatBreakBoundary.back());
                indent(); out << "break;\n";
                break;
            case Stmt::ContinueS:
                if (!fatContinueBoundary.empty())
                    emitFatCleanupTo(fatContinueBoundary.back());
                indent(); out << "continue;\n";
                break;
            case Stmt::IfS:
                indent();
                out << "if (";
                emitScalarized(*s.expr);
                out << ") {\n";
                depth++; emitStmts(s.body); depth--;
                indent(); out << "}";
                if (!s.elseBody.empty()) {
                    // else if 折叠
                    if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                        out << " else ";
                        emitElseIf(*s.elseBody[0]);
                    } else {
                        out << " else {\n";
                        depth++; emitStmts(s.elseBody); depth--;
                        indent(); out << "}\n";
                    }
                } else out << "\n";
                break;
            case Stmt::RetCallS: {
                // ret 调用语法糖：首次出现自动声明函数级 $（_sc_ret），随后复用。
                if (!retDollarDeclared) {
                    indent(); out << "int32_t _sc_ret;\n";   // ret == i4
                    retDollarDeclared = true;
                }
                if (s.retOp == "!!") {
                    // !! f() → _sc_ret = f(); if (_sc_ret != 0) assert(false);
                    indent(); out << "_sc_ret = ";
                    emitExpr(*s.expr, true);
                    out << ";\n";
                    indent(); out << "if (_sc_ret != 0) assert(false);\n";
                    break;
                }
                // ! f()  → if (((_sc_ret = f())) != 0)  { body }（失败 $!=ok 进块）
                // OP f() → if (((_sc_ret = f())) OP 0)   { body }（OP ∈ > < >= <=）
                indent(); out << "if (";
                if (s.retOp == "!") {
                    out << "((_sc_ret = ";
                    emitExpr(*s.expr, true);
                    out << ")) != 0";
                } else {
                    out << "((_sc_ret = ";
                    emitExpr(*s.expr, true);
                    out << ") " << s.retOp << " 0)";
                }
                out << ") {\n";
                depth++; emitStmts(s.body);
                if (s.retProp) {
                    // 错误传播糖 ?：体后向上层 return $（void 函数则 return;）
                    const auto& rt = curFnSig ? curFnSig->structCommon.type : nullptr;
                    const bool retVoid = !rt || (rt->name.empty() && rt->ptr == 0);
                    indent();
                    out << (retVoid ? "return;\n" : "return _sc_ret;\n");
                }
                depth--;
                indent(); out << "}\n";
                break;
            }
            case Stmt::WhileS:
                indent();
                out << "while (";
                emitScalarized(*s.expr);
                out << ") {\n";
                depth++; emitLoopBody(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::DoWhileS:
                indent(); out << "do {\n";
                depth++; emitLoopBody(s.body); depth--;
                indent(); out << "} while (";
                emitScalarized(*s.expr);
                out << ");\n";
                break;
            case Stmt::ForS:
                if (s.forIn) { emitForIn(s); break; }
                indent();
                out << "for (";
                if (s.forInit) emitExpr(*s.forInit, true);
                out << "; ";
                if (s.forCond) emitExpr(*s.forCond, true);
                out << "; ";
                if (s.forStep) emitExpr(*s.forStep, true);
                out << ") {\n";
                depth++; emitLoopBody(s.body); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::CaseS: {
                // 标签联合解构：case 作用于 def T: @( ... ) 值 → 按 tag 安全分发并绑定载荷
                VType sv;
                const Decl* tu = exprVType(*s.expr, sv) ? taggedUnionOf(sv.name) : nullptr;
                if (tu) { emitTaggedCase(s, *tu); break; }
                indent();
                out << "switch (";
                emitExpr(*s.expr, true);
                out << ") {\n";
                depth++;
                fatBreakBoundary.push_back(fatScopes.size());  // switch 也是 break 目标
                for (auto& arm : s.caseArms) {
                    if (arm.labels.empty()) {
                        indent(); out << "default:\n";
                    } else {
                        for (auto& lab : arm.labels) {
                            indent();
                            out << "case ";
                            emitExpr(*lab, true);
                            out << ":\n";
                        }
                    }
                    indent(); out << "{\n";
                    depth++;
                    emitStmts(arm.body);
                    if (!arm.through) {
                        indent(); out << "break;\n";
                    }
                    depth--;
                    indent(); out << "}\n";
                }
                fatBreakBoundary.pop_back();
                depth--;
                indent(); out << "}\n";
                break;
            }
            case Stmt::GotoS: {
                // goto 跨域清理：目标标签若在当前活动作用域链上，则从最内层清理到「标签
                // 所在作用域的子层」（含被跳过的胖根/final/栈哨兵；回跳同时拆解标签体使其重入干净）。
                auto it = labelDepth.find(s.text);
                if (it != labelDepth.end()) {
                    emitFatCleanupTo(it->second + 1);
                } else if (hasActiveCleanup()) {
                    // 目标不在活动链上（跨分支/跳入更深作用域）：无法安全清理被跳过的自动指针/final。
                    throw CompileError{
                        "goto 跨非包含作用域跳转：目标标签 '" + s.text +
                        "' 不在当前活动作用域链上，存在待清理的自动指针/final/栈哨兵，"
                        "无法保证内存安全（请避免跨分支或跳入更深作用域的 goto）",
                        s.line};
                }
                indent(); out << "goto " << s.text << ";\n";
                break;
            }
            case Stmt::LabelS:
                // 标签后接空语句，使「标签紧跟声明」合法（C 中 label 后须为语句，声明非语句）。
                indent(); out << s.text << ":;\n";
                depth++; emitStmts(s.body); depth--;
                break;
            case Stmt::DeclS:
                // 宏体内可定义函数（fnc）：emit 为函数定义（#define 续行）；其余为内嵌类型
                if (s.decl->kind == Decl::FuncD || s.decl->kind == Decl::FuncTypeD)
                    emitFunc(*s.decl);
                else
                    emitTypeDecl(*s.decl);
                break;
            case Stmt::MixS:
                // mix 宏展开（函数体内）：展开为 C 宏调用，宏体自含分号，不再包裹
                indent();
                if (s.expr) emitExpr(*s.expr);
                out << "\n";
                break;
            case Stmt::InlineDefS:
                // inl 真内联块定义：仅登记进函数域块表，定义处不产码；调用点 name(args)
                //   由 ExprS 分支拦截并原地展开（emitInlineExpand）。
                inlineDefs[s.text] = &s;
                break;
            case Stmt::FinalS:
                // final 钩子：登记入当前作用域，退出点（emitScopeCleanupAt phase0）发出 body。
                if (!fatFinalScopes.empty()) fatFinalScopes.back().push_back(&s);
                break;
            case Stmt::RunS:
                emitRunStmt(s);
                break;
            case Stmt::DoneS:
                emitDoneStmt(s);
                break;
            case Stmt::FormS:
                emitFormStmt(s);
                break;
            case Stmt::BackS:
                emitBackStmt(s);
                break;
            case Stmt::PrintS:
                emitPrintStmt(s);
                break;
            case Stmt::AssertS:
                emitAssertStmt(s);
                break;
        }
    }

    // run 语句 → 装填 rpc 参数结构体 + thread_run 调用
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    // thread_run 在 op_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // run 语句 → 装填 rpc 参数结构体 + 线程原语调用（第二参按类型静态分派）
    //   run worker(a, b), &t →
    //   { struct worker _rp = {0}; _rp.x = a; ...;
    //     thread_run((void (*)(void *))worker_rpc, &_rp, sizeof(_rp), (thread **)(&t)); }
    //   run worker(a, b), p（p 为 pool& 接口协议，值则自动取址）→
    //     p->run(p, (void (*)(void *))worker_rpc, &_rp, sizeof(_rp));
    // thread_run 在 op_impl 中实现：单次 alloc(sizeof(thread)+sizeof(参数)+实现私有区)，
    // 参数 memcpy 到 thread 对象紧随位置；出参为空时 detach 自释放。
    // pool 经协议指针派发（零 emit mt 符号）：参数拷贝入任务节点，调用点无需保活。
    void emitRunStmt(const Stmt& s) {
        const Expr& call = *s.expr;
        auto it = rpcs.find(call.a->text);
        if (it == rpcs.end())
            throw CompileError{"run 的目标必须是 rpc: " + call.a->text, s.line};
        const Decl* r = it->second;
        if (call.args.size() > r->structCommon.fields.size())
            throw CompileError{"rpc 实参数量超出: " + r->name + " 期望至多 " +
                               std::to_string(r->structCommon.fields.size()) + " 个", s.line};
        // thread 类型属语言内核（op.sc 默认导入），detach/joinable 形态无需 inc mt.sc。
        // run<target, stack:N, prio:M> 选项：透传给 C（stack=u4 栈字节数，prio=u1 优先级）。
        //   键在此校验；值为任意表达式，运行时求值并强转（u4/u1），不做编译期范围校验。
        bool hasOpts = !s.runOpts.empty();
        for (auto& o : s.runOpts) {
            if (o.first != "stack" && o.first != "prio")
                throw CompileError{"run 选项未知键：'" + o.first +
                                   "'（当前仅支持 stack、prio）", s.line};
        }
        // <target> 类型分派：pool（接口协议，对象/指针）→ 入池；queue → 留待下一轮；其余报错
        bool toPool = false;
        bool poolPtr = false;
        if (s.runTarget) {
            VType vt;
            if (exprVType(*s.runTarget, vt) && vt.arr == 0 && vt.ptr <= 1) {
                const Decl* sd = aggrOf(vt.name);
                if (sd && sd->name == "pool") { toPool = true; poolPtr = (vt.ptr == 1); }
                else if (sd && sd->name == "queue")
                    throw CompileError{"run<queue>（线程绑定消息队列为消费者）暂未实现，留待下一轮；"
                                       "如需向队列投递任务请用 sync<q>/async<q>", s.line};
            }
            if (!toPool)
                throw CompileError{"run<target> 的目标须为 pool（入池）", s.line};
        }
        // pool 工作线程为预创建，逐任务的 stack/prio 不适用 → 显式报错
        if (toPool && hasOpts)
            throw CompileError{"run 选项（stack/prio）不适用于 pool 目标", s.line};
        indent(); out << "{\n";
        depth++;
        indent(); out << "struct " << scPfx(r->name) << " _rp = {0};\n";
        for (size_t i = 0; i < call.args.size(); i++) {
            const Field& f = r->structCommon.fields[i];
            indent();
            out << "_rp." << f.name << " = ";
            emitExpr(*call.args[i], true);
            out << ";\n";
            if (!f.type.arrayDims.empty()) {       // 数组实参：额外装填 size（字节数）
                indent();
                out << "_rp." << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f);
                out << ";\n";
            }
        }
        indent();
        if (toPool) {
            // 经 pool 协议指针派发：PTR->run(PTR, fn, &_rp, sizeof(_rp))
            //   PTR：pool& 直传，pool 值取址（仿 com 的 c->close(c)），零 emit mt 符号
            emitComBasePtr(*s.runTarget, poolPtr);
            out << "->run(";
            emitComBasePtr(*s.runTarget, poolPtr);
            out << ", (void (*)(void *))" << scPfx(r->name) << "_rpc, &_rp, sizeof(_rp));\n";
        } else {
            out << "sc_thread_run((void (*)(void *))" << scPfx(r->name) << "_rpc, &_rp, sizeof(_rp), ";
            if (s.forInit) {
                out << "(sc_thread **)(";
                emitExpr(*s.forInit, true);
                out << ")";
            } else out << "NULL";
            out << ", ";
            emitOptOr0(s.runOpts, "stack", "uint32_t"); out << ", ";
            emitOptOr0(s.runOpts, "prio", "uint8_t");
            out << ");\n";
        }
        depth--;
        indent(); out << "}\n";
    }

    // done 语句 → 按操作数类型多态分派（future 单线程异步 / deferred rpc 延迟应答）：
    //   future&：done f[, result]      → future_done(f, <result 擦除 void*>);
    //   deferred&：done s[, result]    → s->respond(s, &临时, sizeof)；按返回类型宽度写回返回槽。
    // future 结果擦除为 void*（指针直转，标量经 intptr_t 往返，与 f.get(): T& 对应）；
    // deferred 结果按其静态类型原值写入调用方返回槽（偏移 0，与即时应答原地写 _ 同布局），
    // 经协议指针 s->respond 派发——零 emit mt 符号。result 省略=无结果。
    void emitDoneStmt(const Stmt& s) {
        VType tv;
        bool isDeferred = exprVType(*s.expr, tv) && tv.name == "deferred" && tv.arr == 0;
        if (isDeferred) {
            indent();
            if (!s.forInit) {
                // 无结果：respond(s, NULL, 0)（rpc 无返回值）
                out << "("; emitExpr(*s.expr, true); out << ")->respond(";
                emitExpr(*s.expr, true); out << ", NULL, 0);\n";
                return;
            }
            // 有结果：按 result 静态类型存临时再 respond，宽度 = sizeof(返回类型)
            VType rv;
            bool ok = exprVType(*s.forInit, rv);
            out << "{ ";
            if (ok) {
                out << cTypeOf(rv.name, rv.ptr) << " _dv = (";
            } else {
                out << "intptr_t _dv = (intptr_t)(";   // 兜底：无法静态推断时按 intptr_t 宽度
            }
            emitExpr(*s.forInit, true);
            out << "); (";
            emitExpr(*s.expr, true); out << ")->respond(";
            emitExpr(*s.expr, true); out << ", &_dv, sizeof(_dv)); }\n";
            return;
        }
        indent();
        out << "sc_future_done(";
        emitExpr(*s.expr, true);     // future&（指针）
        out << ", ";
        if (!s.forInit) {
            out << "NULL";
        } else {
            VType vt;
            bool isPtr = exprVType(*s.forInit, vt) && (vt.ptr > 0 || vt.arr > 0);
            out << (isPtr ? "(void *)(" : "(void *)(intptr_t)(");
            emitExpr(*s.forInit, true);
            out << ")";
        }
        out << ");\n";
    }

    // form 语句 → token_form(t, v, 0, ctx, exec) —— 给 form token 灌初值（@）并升格为 form 主，
    //   可选第三参 ctx（&n 侧车）绑定为节点私有上下文、第四参 exec 挂节点处理钩子（无则 NULL）。
    //   初值须为 @；form 不带 tag，标签恒 0。
    void emitFormStmt(const Stmt& s) {
        indent();
        out << "sc_token_form(";
        emitExpr(*s.expr, true);     // tok&（句柄指针）
        out << ", ";
        emitExpr(*s.forInit, true);  // @ 初值
        out << ", 0, ";
        if (s.forCond) { out << "(void*)"; emitExpr(*s.forCond, true); }  // 可选侧车 ctx
        else out << "(void*)0";
        out << ", ";
        if (s.forStep) { out << "(sc_token_exec)"; emitExpr(*s.forStep, true); }  // 可选节点钩子 exec
        else out << "(sc_token_exec)0";
        out << ");\n";
    }

    // back t[, seed]：反向遍历（反向传播骨架）→ token_back(t, seed, 0)。
    //   有 seed：擦除为 *（瘦）灌入起点；无 seed：传空瘦句柄（运行时不灌种子，保留 t 当前值）。
    void emitBackStmt(const Stmt& s) {
        indent();
        out << "sc_token_back(";
        emitExpr(*s.expr, true);     // tok&（句柄指针）
        out << ", ";
        if (s.forInit) emitExpr(*s.forInit, true);   // 瘦句柄梯度种子
        else           out << "(sc_thin){(void *)0, (int32_t *)0, (void (*)(void *))0}";  // 空瘦：无种子
        out << ", 0);\n";
    }

    //   python 风格拼接：字符串字面量→格式串纯文本（% 转义为 %%）；其余实参按静态
    //   类型自动补 printf 说明符并作可变参数（必要的 64 位说明符经 inttypes 宏跨平台）；
    //   (expr: "%fmt") 显式格式覆盖（值原样传入，不再自动加 cast）。
    //   <chn> 通道 u1 透传给 C print 首参（默认 0）。
    void emitPrintStmt(const Stmt& s) {
        // <chn> 为 string 变量时：路由到 string_printf —— 把格式化文本「追加进该串」而非
        // 输出到 stdout（无时间戳/级别/通道修饰，等价直接调用 string 的 printf）。其余
        // 拼接糖/格式自动补全逻辑与 print 完全一致，仅调用目标不同。
        // 通道名的 C 化：与 ident 发射一致——局部/形参原名；sc 域全局/枚举常量/宏/函数/rpc
        // 加 sc_ 前缀（如日志级别枚举 <E> → sc_E）；数字字面量/外部 C 名原样。
        auto chnName = [&](const std::string& c) -> std::string {
            if (localsT.count(c)) return c;
            if (globalsT.count(c) || funcs.count(c) || rpcs.count(c) ||
                enumConsts.count(c) || macroNames.count(c)) return scPfx(c);
            return c;
        };
        std::string strChn;   // 非空=string 通道目标（已规整为 string* 实参文本）
        if (s.printChn != "0") {
            const VType* cvt = nullptr;
            { auto a = localsT.find(s.printChn);
              if (a != localsT.end()) cvt = &a->second;
              else { auto b = globalsT.find(s.printChn); if (b != globalsT.end()) cvt = &b->second; } }
            if (cvt && cvt->arr == 0 && resolveAliasName(cvt->name) == "string" && aggrOf("string")) {
                const std::string cn = chnName(s.printChn);
                if (cvt->fat)           strChn = "((string *)(" + cn + ").p)";  // string@ 胖指针解 .p
                else if (cvt->ptr >= 1) strChn = cn;                            // string& 裸指针直传
                else                    strChn = "&(" + cn + ")";               // string 值取址（理论上不会出现）
            }
        }

        // 括号形式 print(...) = C printf 兼容模式：实参原样传递（首参为格式串）
        if (s.printCompat) {
            indent();
            if (!strChn.empty()) out << "sc_string_printf(" << strChn;
            else                 out << "sc_print((uint8_t)(" << chnName(s.printChn) << ")";
            for (auto& argp : s.printArgs) {
                out << ", ";
                emitExpr(*argp, true);
            }
            out << ");\n";
            return;
        }
        std::string fmtExpr;   // 相邻 C 串字面量/宏拼成的格式实参
        std::string lit;       // 当前累积的串字面量内文（已转义）
        auto flush = [&]() {
            if (!fmtExpr.empty()) fmtExpr += " ";
            fmtExpr += "\"" + lit + "\"";
            lit.clear();
        };
        struct PV { const Expr* e; std::string pre, post; };
        std::vector<PV> pvs;

        for (auto& argp : s.printArgs) {
            const Expr& arg = *argp;
            // 字符串字面量 → 纯文本（% 转义）
            if (arg.kind == Expr::StrLit) {
                std::string inner = arg.text.size() >= 2
                    ? arg.text.substr(1, arg.text.size() - 2) : std::string();
                for (char ch : inner) { if (ch == '%') lit += "%%"; else lit += ch; }
                continue;
            }
            // 显式格式覆盖 (expr: "%fmt") → 格式串原样追加，值原样传入
            if (arg.kind == Expr::Cast && arg.castIsFmt) {
                std::string inner = arg.op.size() >= 2
                    ? arg.op.substr(1, arg.op.size() - 2) : std::string();
                lit += inner;
                pvs.push_back({arg.a.get(), "", ""});
                continue;
            }
            // 其余 → 按静态类型自动选择说明符
            VType vt;
            if (!exprVType(arg, vt))
                throw CompileError{"print 无法推断实参类型，请用 (expr: \"%fmt\") 指定格式", s.line};
            std::string nm = resolveAliasName(vt.name);
            char cls = scalarClass(vt.name);
            std::string pre, post, spec, macro;
            if (vt.arr == 0 && vt.ptr == 0 && nm == "string" && aggrOf("string")) {
                spec = "s"; pre = "sc_string_cstr(&("; post = "))";      // adt string 值
            } else if (vt.arr == 0 && vt.ptr == 1 && nm == "string" && aggrOf("string")) {
                spec = "s"; pre = "sc_string_cstr("; post = ")";          // adt string 指针
            } else if (cls == 'c' && (vt.ptr >= 1 || vt.arr >= 1)) {
                spec = "s";                                            // char& / char[] 字符串
            } else if (vt.ptr >= 1 || vt.arr >= 1) {
                spec = "p"; pre = "(void *)("; post = ")";             // 其余指针/数组
            } else {
                switch (cls) {
                    case 'i':
                        if (nm == "i8") { macro = "PRId64"; pre = "(int64_t)("; post = ")"; }
                        else { spec = "d"; pre = "(int)("; post = ")"; }
                        break;
                    case 'u':
                        if (nm == "u8") { macro = "PRIu64"; pre = "(uint64_t)("; post = ")"; }
                        else { spec = "u"; pre = "(unsigned)("; post = ")"; }
                        break;
                    case 'f': spec = "f"; pre = "(double)("; post = ")"; break;
                    case 'b': spec = "d"; pre = "(int)("; post = ")"; break;
                    case 'c': spec = "c"; pre = "(int)("; post = ")"; break;
                    default:
                        throw CompileError{"print 无法为类型 '" +
                            (vt.name.empty() ? std::string("(无类型)") : vt.name) +
                            "' 自动选择格式，请用 (expr: \"%fmt\") 指定", s.line};
                }
            }
            if (!macro.empty()) { lit += "%"; flush(); fmtExpr += " " + macro; }
            else lit += "%" + spec;
            pvs.push_back({&arg, pre, post});
        }
        if (!lit.empty() || fmtExpr.empty()) flush();

        indent();
        if (!strChn.empty()) out << "sc_string_printf(" << strChn << ", " << fmtExpr;
        else                 out << "sc_print((uint8_t)(" << chnName(s.printChn) << "), " << fmtExpr;
        for (auto& pv : pvs) {
            out << ", " << pv.pre;
            emitExpr(*pv.e, true);
            out << pv.post;
        }
        out << ");\n";
    }

    // ---------------- 单元测试（--test）----------------
    static bool isCmpOp(const std::string& op) {
        return op == "==" || op == "!=" || op == "<" || op == ">"
            || op == "<=" || op == ">=";
    }

    // assert 失败诊断用源码文件名（优先 #line 的 srcFile，回退 ref site / 占位）。
    std::string testSrcName() const {
        if (!srcFile.empty()) return srcFile;
        if (!g_refSrcFile.empty()) return g_refSrcFile;
        return "test";
    }

    // 普通函数调用 f(...) 的返回类型解析（assert 值回显专用；exprVType 出于
    // 保护 print golden 仅对 T@ 胖返回解析，此处补足标量返回类型用于诊断显示）。
    bool assertCallVType(const Expr& e, VType& vt) const {
        if (e.kind != Expr::Call || !e.a || e.a->kind != Expr::Ident) return false;
        auto fit = funcs.find(e.a->text);
        if (fit == funcs.end()) return false;
        const Decl* sig = fit->second;
        if (!sig->funcTypeName.empty()) {
            auto ft = funcTypes.find(sig->funcTypeName);
            if (ft != funcTypes.end()) sig = ft->second;
        }
        const auto& rt = sig->structCommon.type;
        if (!rt || (rt->name.empty() && rt->ptr == 0)) return false;
        vt = {rt->name, rt->ptr, (int)rt->arrayDims.size()};
        vt.fat = rt->fat;
        return true;
    }

    // 单个标量/指针/串值的 printf 说明符（spec 或 macro 二选一）与 cast 前后缀。
    // 取自 print 的按静态类型自动格式逻辑；无法格式化（结构体等）→ false。
    bool autoFmtScalar(const Expr& arg, std::string& spec, std::string& macro,
                       std::string& pre, std::string& post) const {
        VType vt;
        if (!exprVType(arg, vt) && !assertCallVType(arg, vt)) return false;
        std::string nm = resolveAliasName(vt.name);
        char cls = scalarClass(vt.name);
        spec.clear(); macro.clear(); pre.clear(); post.clear();
        if (vt.arr == 0 && vt.ptr == 0 && nm == "string" && aggrOf("string")) {
            spec = "s"; pre = "sc_string_cstr(&("; post = "))";
        } else if (vt.arr == 0 && vt.ptr == 1 && nm == "string" && aggrOf("string")) {
            spec = "s"; pre = "sc_string_cstr("; post = ")";
        } else if (cls == 'c' && (vt.ptr >= 1 || vt.arr >= 1)) {
            spec = "s";
        } else if (vt.ptr >= 1 || vt.arr >= 1) {
            spec = "p"; pre = "(void *)("; post = ")";
        } else {
            switch (cls) {
                case 'i':
                    if (nm == "i8") { macro = "PRId64"; pre = "(int64_t)("; post = ")"; }
                    else { spec = "d"; pre = "(int)("; post = ")"; }
                    break;
                case 'u':
                    if (nm == "u8") { macro = "PRIu64"; pre = "(uint64_t)("; post = ")"; }
                    else { spec = "u"; pre = "(unsigned)("; post = ")"; }
                    break;
                case 'f': spec = "f"; pre = "(double)("; post = ")"; break;
                case 'b': spec = "d"; pre = "(int)("; post = ")"; break;
                case 'c': spec = "c"; pre = "(int)("; post = ")"; break;
                default: return false;
            }
        }
        return true;
    }

    // 比较断言失败时回显单侧操作数的值（不可自动格式化则静默跳过该行）。
    void emitAssertValue(const char* label, const Expr& e) {
        std::string spec, macro, pre, post;
        if (!autoFmtScalar(e, spec, macro, pre, post)) return;
        indent();
        if (!macro.empty())
            out << "printf(\"  #   " << label << " = %\" " << macro << " \"\\n\", ";
        else
            out << "printf(\"  #   " << label << " = %" << spec << "\\n\", ";
        out << pre;
        emitExpr(e, true);
        out << post << ");\n";
    }

    // assert 语句 → if (!(表达式)) { 记录失败头 + 可选值显示 + 中止当前用例 }
    void emitAssertStmt(const Stmt& s) {
        indent(); out << "if (!(";
        emitExpr(*s.expr, true);
        out << ")) {\n";
        depth++;
        indent();
        out << "sc__fail_head(" << cStrLit(testSrcName()) << ", " << s.line << ", "
            << cStrLit(s.text) << ", ";
        if (s.assertMsg) emitExpr(*s.assertMsg, true);
        else out << "(const char *)0";
        out << ");\n";
        // 比较表达式（== != < > <= >=）：回显左右操作数实际值，便于定位
        if (s.expr->kind == Expr::Binary && isCmpOp(s.expr->op)) {
            emitAssertValue("左值", *s.expr->a);
            emitAssertValue("右值", *s.expr->b);
        }
        indent(); out << "sc__fail_done();\n";
        depth--;
        indent(); out << "}\n";
    }

    // tst 用例 → static void <cname>(void)（仿 emitFunc 的作用域设置；可见本单元非导出符号）
    void emitTestFunc(const Decl& d, const std::string& cname) {
        if (!srcFile.empty() && d.line > 0)
            out << "#line " << d.line << " \"" << srcFile << "\"\n";
        out << "static void " << cname << "(void) {\n";
        localsT.clear(); fnVarsL.clear(); varDimsL.clear(); projVarsL.clear();
        inFunc = true;
        retDollarDeclared = false;
        curFnSig = nullptr;
        curMethodOwner.clear();
        curInFatInit = false;
        preScanFatBorrows(d.body);
        scanManualDrops(d.body);
        depth++;
        emitStmts(d.body);
        depth--;
        inFunc = false;
        out << "}\n\n";
    }

    // 测试运行时：setjmp 隔离 + TAP 风格报告辅助（须早于测试函数体，供 assert 调用）。
    void emitTestRuntime() {
        out << "/* --- sc 单元测试运行时（--test 注入） --- */\n"
            << "#include <setjmp.h>\n"
            << "static jmp_buf sc__test_jmp;\n"
            << "static int sc__test_failed;\n"
            << "static int sc__test_no;\n"
            << "static int sc__pass_total;\n"
            << "static int sc__fail_total;\n"
            << "static int sc__skip_total;\n"
            << "static const char *sc__test_name;\n"
            << "static void sc__fail_head(const char *file, int line, const char *expr, const char *msg) {\n"
            << "    printf(\"not ok %d - %s\\n\", sc__test_no, sc__test_name);\n"
            << "    printf(\"  # %s:%d: assert %s\\n\", file, line, expr);\n"
            << "    if (msg && msg[0]) printf(\"  #   %s\\n\", msg);\n"
            << "    sc__test_failed = 1;\n"
            << "}\n"
            << "static void sc__fail_done(void) { longjmp(sc__test_jmp, 1); }\n"
            << "static void sc__run_case(const char *name, void (*fn)(void), int skip) {\n"
            << "    sc__test_no++;\n"
            << "    sc__test_name = name;\n"
            << "    sc__test_failed = 0;\n"
            << "    if (skip) { printf(\"ok %d - %s  # SKIP\\n\", sc__test_no, name); sc__skip_total++; return; }\n"
            << "    if (setjmp(sc__test_jmp) == 0) fn();\n"
            << "    if (sc__test_failed) sc__fail_total++;\n"
            << "    else { printf(\"ok %d - %s\\n\", sc__test_no, name); sc__pass_total++; }\n"
            << "}\n\n";
    }

    // 测试 runner main：模块 init → 逐个 tst 用例 → 模块 drop → 汇总；失败数即退出码。
    void emitTestRunnerMain() {
        out << "int main(int argc, char **argv) {\n";
        out << "    (void)argc; (void)argv;\n";
        for (auto& t : depTokens) out << "    sc_mod_" << t << "_init();\n";
        for (auto& ci : gClassInstalls) out << "    " << ci.first << "._class = " << ci.second << "_hyper_impl;\n";
        for (auto& g : gInits)    out << "    " << g.fn << "(&" << g.var << (g.ownArg.empty() ? "" : ", " + g.ownArg) << ");\n";
        emitTokRegistrations("    ");
        for (auto& tc : testCases)
            out << "    sc__run_case(\"" << tc.d->name << "\", &" << tc.cname
                << ", " << (tc.d->testSkip ? 1 : 0) << ");\n";
        for (auto it = gDrops.rbegin(); it != gDrops.rend(); ++it)
            out << "    " << it->fn << "(&" << it->var << ");\n";
        for (auto it = depTokens.rbegin(); it != depTokens.rend(); ++it)
            out << "    sc_mod_" << *it << "_drop();\n";
        out << "    printf(\"1..%d\\n\", sc__pass_total + sc__fail_total + sc__skip_total);\n";
        out << "    printf(\"==> 测试通过 %d，失败 %d，跳过 %d\\n\", "
               "sc__pass_total, sc__fail_total, sc__skip_total);\n";
        out << "    return sc__fail_total ? 1 : 0;\n";
        out << "}\n";
    }

    // 对象 → &(对象)，指针 → 原样（按轻量类型推断；不可推断时默认按对象取地址）
    void emitAutoAddr(const Expr& e) {
        VType vt;
        bool isPtr = exprVType(e, vt) && vt.ptr > 0 && vt.arr == 0;
        if (!isPtr) out << "&(";
        emitExpr(e, true);
        if (!isPtr) out << ")";
    }

    void emitElseIf(const Stmt& s) { // "} else " 之后接 if，不缩进首行
        out << "if (";
        emitScalarized(*s.expr);
        out << ") {\n";
        depth++; emitStmts(s.body); depth--;
        indent(); out << "}";
        if (!s.elseBody.empty()) {
            if (s.elseBody.size() == 1 && s.elseBody[0]->kind == Stmt::IfS) {
                out << " else ";
                emitElseIf(*s.elseBody[0]);
            } else {
                out << " else {\n";
                depth++; emitStmts(s.elseBody); depth--;
                indent(); out << "}\n";
            }
        } else out << "\n";
    }

    // ---------------- 类型定义 ----------------
    void emitFieldList(const std::vector<Field>& fields) {
        depth++;
        for (auto& f : fields) {
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        depth--;
    }

    // 收集 sc 内按值（非指针、非内联）直接引用的命名聚合体依赖
    void collectValueDeps(const StructCommon& sc, std::vector<const Decl*>& deps) {
        for (auto& f : sc.fields) {
            if (f.type.ptr == 0 && !f.type.hasInline) {
                if (const Decl* dep = aggrOf(f.type.name)) deps.push_back(dep);
            }
            if (f.type.hasInline) collectValueDeps(f.type.structCommon, deps);
        }
    }

    // 输出聚合体完整定义，先递归前移其按值依赖（定义顺序无关）。
    // 标记先于递归，天然防御非法的相互按值包含（由语义阶段另行报错）。
    void emitAggrWithDeps(const Decl& d) {
        if (emittedAggr.count(&d)) return;
        emittedAggr.insert(&d);
        std::vector<const Decl*> deps;
        collectValueDeps(d.structCommon, deps);
        for (auto* dep : deps)
            if ((dep->kind == Decl::StructD || dep->kind == Decl::UnionD)
                && !dep->external && !dep->isRpc)
                emitAggrWithDeps(*dep);
        emitTypeDecl(d);
    }

    // 分身/切片句柄结构体 T__project：def T: <S> {} 的 T[...] 类型展开。
    // 字段 = T.alloc 去掉隐式 this 后的形参（句柄上下文，存切片参数初值）+ S* _（nil=未绑定）。
    // 实体 T 可为外部 @def（结构体来自模块头）；此时 T__project 仍由本工程生成，
    // 但不重复发出外部的 S 定义（S 同样由其模块头提供）。
    void emitProjectTypedef(const Decl& d) {
        if (!emittedProject.insert(&d).second) return;
        const Decl* al = findMethod(d.name, "alloc");
        const Field* alF = al ? nullptr : methodPtrField(d.name, "alloc");
        const std::vector<Field>* alParams = al ? &al->structCommon.fields
                                  : (alF ? &alF->type.structCommon.fields : nullptr);
        if (const Decl* sDecl = aggrOf(d.projectSelf))
            if (!sDecl->external) emitAggrWithDeps(*sDecl);
        indent();
        out << "typedef struct " << mapType_(d.name) << "__project {\n";
        depth++;
        if (alParams) for (auto& p : *alParams) {
            indent(); emitDeclarator(p); out << ";\n";
        }
        indent(); out << mapType_(d.projectSelf) << " *_;\n";
        depth--;
        indent();
        out << "} " << mapType_(d.name) << "__project;\n\n";
    }

    // 标签联合 def T: @( v1 / v2:payload / ... )：展开为带隐藏 tag 的安全和类型。
    //   typedef struct T { enum { T__v1, T__v2, ... } tag; union { payload... } u; } T;
    //   无载荷变体不占 union 成员；全部无载荷时省略 union（退化为带名枚举的标量）。
    void emitTaggedUnion(const Decl& d) {
        indent();
        out << "typedef struct " << mapType_(d.name) << " {\n";
        depth++;
        // tag 枚举：变体常量名 T__Variant（C 枚举常量泄漏到外层作用域，供构造/解构引用）
        indent();
        out << "enum {";
        for (size_t i = 0; i < d.structCommon.fields.size(); i++)
            out << (i ? ", " : " ") << mapType_(d.name) << "__" << d.structCommon.fields[i].name;
        out << " } tag;\n";
        // 载荷 union：仅含有载荷的变体
        bool anyPayload = false;
        for (auto& f : d.structCommon.fields)
            if (!f.type.name.empty() || f.type.ptr > 0) { anyPayload = true; break; }
        if (anyPayload) {
            indent(); out << "union {\n";
            depth++;
            for (auto& f : d.structCommon.fields) {
                if (f.type.name.empty() && f.type.ptr == 0) continue;  // 无载荷变体跳过
                indent();
                emitDeclarator(f);
                out << ";\n";
            }
            depth--;
            indent(); out << "} u;\n";
        }
        depth--;
        indent();
        out << "} " << mapType_(d.name) << ";\n\n";
    }

    // 标签联合解构 case：先将被解构值绑定到临时量（避免重复求值），按 tag 分发，
    // 每个 Variant as x 分支把当前变体载荷拷贝到只读视图 x。无 default 时强制穷尽。
    void emitTaggedCase(const Stmt& s, const Decl& tu) {
        // 预校验：标签合法、绑定仅用于有载荷单变体、穷尽性
        std::set<std::string> covered;
        bool hasDefault = false;
        for (auto& arm : s.caseArms) {
            if (arm.labels.empty()) { hasDefault = true; continue; }
            for (auto& lab : arm.labels) {
                if (lab->kind != Expr::Ident)
                    throw CompileError("标签联合 case 分支标签须为变体名", arm.line);
                const Field* vf = taggedVariant(&tu, lab->text);
                if (!vf)
                    throw CompileError("'" + lab->text + "' 不是标签联合 " + tu.name + " 的变体", arm.line);
                if (!covered.insert(lab->text).second)
                    throw CompileError("标签联合变体 " + tu.name + "." + lab->text + " 分支重复", arm.line);
            }
            if (!arm.binding.empty()) {
                if (arm.labels.size() != 1)
                    throw CompileError("标签联合 case 绑定 'as' 仅适用于单变体分支", arm.line);
                const Field* vf = taggedVariant(&tu, arm.labels[0]->text);
                if (vf->type.name.empty() && vf->type.ptr == 0)
                    throw CompileError("变体 " + tu.name + "." + arm.labels[0]->text +
                                       " 无载荷，不能用 'as' 绑定", arm.line);
            }
        }
        if (!hasDefault && covered.size() < tu.structCommon.fields.size()) {
            std::string missing;
            for (auto& f : tu.structCommon.fields)
                if (!covered.count(f.name))
                    missing += (missing.empty() ? "" : ", ") + f.name;
            throw CompileError("标签联合 " + tu.name + " 的 case 解构不穷尽，缺少变体: " + missing +
                               "（补齐分支或添加 default ':'）", s.line);
        }

        std::string tmp = "_case" + std::to_string(fatTmpSeq++);
        indent();
        out << mapType_(tu.name) << " " << tmp << " = ";
        emitExpr(*s.expr, true);
        out << ";\n";
        indent();
        out << "switch (" << tmp << ".tag) {\n";
        depth++;
        fatBreakBoundary.push_back(fatScopes.size());
        for (auto& arm : s.caseArms) {
            if (arm.labels.empty()) {
                indent(); out << "default:\n";
            } else {
                for (auto& lab : arm.labels) {
                    indent();
                    out << "case " << mapType_(tu.name) << "__" << lab->text << ":\n";
                }
            }
            indent(); out << "{\n";
            depth++;
            // 载荷绑定 Variant as x：拷贝当前变体载荷到只读视图 x
            bool restore = false, hadSaved = false;
            VType savedVT;
            if (!arm.binding.empty()) {
                const Field* vf = taggedVariant(&tu, arm.labels[0]->text);
                std::string base; int ptr; resolveType(vf->type, base, ptr);
                indent();
                out << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
                out << arm.binding << " = " << tmp << ".u." << arm.labels[0]->text << ";\n";
                auto it = localsT.find(arm.binding);
                if (it != localsT.end()) { hadSaved = true; savedVT = it->second; }
                localsT[arm.binding] = {vf->type.name, vf->type.ptr,
                                        (int)vf->type.arrayDims.size()};
                restore = true;
            }
            emitStmts(arm.body);
            if (restore) {
                if (hadSaved) localsT[arm.binding] = savedVT;
                else localsT.erase(arm.binding);
            }
            if (!arm.through) { indent(); out << "break;\n"; }
            depth--;
            indent(); out << "}\n";
        }
        fatBreakBoundary.pop_back();
        depth--;
        indent(); out << "}\n";
    }

    // 宏体拼写翻译：sc 的 \ 粘贴 → C 的 ##；`name` 串化 → C 的 #name。
    // 跳过字符串/字符字面量内部（其中的 \ 与 ` 原样保留，避免误伤 "\n" 等）。
    static std::string macroSpellToC(const std::string& s) {
        std::string r;
        for (size_t i = 0; i < s.size();) {
            char c = s[i];
            if (c == '"' || c == '\'') {                // 跳过字面量
                char q = c; r += c; i++;
                while (i < s.size()) {
                    if (s[i] == '\\' && i + 1 < s.size()) { r += s[i]; r += s[i + 1]; i += 2; continue; }
                    r += s[i];
                    if (s[i] == q) { i++; break; }
                    i++;
                }
                continue;
            }
            if (c == '\\') { r += "##"; i++; continue; }  // token 粘贴
            if (c == '`') {                               // `name` 串化
                size_t j = i + 1; std::string inner;
                while (j < s.size() && s[j] != '`') inner += s[j++];
                r += "#"; r += inner;
                i = (j < s.size()) ? j + 1 : j;
                continue;
            }
            r += c; i++;
        }
        return r;
    }

    // def 宏 → C #define
    //   对象宏 def NAME: = value      → #define NAME value
    //   函数宏 def name: p1,... \n body → #define name(p1,...) \ <body 逐句续行>
    void emitMacroDef(const Decl& d) {
        if (d.macroObject) {
            std::ostringstream save = std::move(out); out = std::ostringstream();
            if (d.expr) emitExpr(*d.expr, true);
            std::string val = out.str(); out = std::move(save);
            out << "#define " << scPfx(d.name) << " " << macroSpellToC(val) << "\n";
            return;
        }
        // 函数宏头部：name(p1, p2, ..., ...)
        std::string header = "#define " + scPfx(d.name) + "(";
        bool first = true;
        for (auto& p : d.structCommon.fields) {
            if (!first) header += ", ";
            header += p.name; first = false;
        }
        if (d.structCommon.variadic) header += first ? "..." : ", ...";
        header += ")";
        // 宏体：逐句 emit 到暂存流，再以反斜线续行拼接
        std::ostringstream save = std::move(out); out = std::ostringstream();
        int saveDepth = depth; depth = 1;
        bool saveMacro = inMacro; inMacro = true;
        for (auto& s : d.body) emitStmt(*s);
        inMacro = saveMacro; depth = saveDepth;
        std::string body = macroSpellToC(out.str()); out = std::move(save);
        out << header;
        std::string line;
        auto flush = [&](const std::string& ln) {
            if (ln.find_first_not_of(" \t") == std::string::npos) return;  // 跳过空白行
            out << " \\\n" << ln;
        };
        for (char c : body) {
            if (c == '\n') { flush(line); line.clear(); }
            else line += c;
        }
        flush(line);
        out << "\n";
    }

    // 顶层 mix → 宏调用展开（产生声明；宏体自含分号，不再包裹）
    void emitMixExpand(const Decl& d) {
        if (d.expr) { emitExpr(*d.expr, true); out << "\n"; }
    }

    void emitTypeDecl(const Decl& d) {
        switch (d.kind) {
            case Decl::EnumD:
                indent();
                out << "typedef enum { /* base: " << mapBase(d.structCommon.type->name) << " */\n";
                depth++;
                for (size_t i = 0; i < d.structCommon.fields.size(); i++) {
                    indent();
                    out << scPfx(d.structCommon.fields[i].name);
                    if (d.structCommon.fields[i].init) {
                        out << " = ";
                        emitExpr(*d.structCommon.fields[i].init, true);
                    }
                    if (i + 1 < d.structCommon.fields.size()) out << ",";
                    out << "\n";
                }
                depth--;
                indent();
                out << "} " << mapType_(d.name) << ";\n\n";
                break;
            case Decl::StructD:
            case Decl::UnionD:
                // 标签联合 def T: @( ... )：tag 枚举 + 载荷 union 的安全和类型
                if (d.kind == Decl::UnionD && d.tagged) {
                    emitTaggedUnion(d);
                    break;
                }
                indent();
                out << "typedef " << (d.kind == Decl::UnionD ? "union" : "struct")
                    << " " << mapType_(d.name) << " {\n";
                structOwnerTag = (d.kind == Decl::UnionD ? "union " : "struct ") + mapType_(d.name);
                emitFieldList(d.structCommon.fields);
                structOwnerTag.clear();
                indent();
                out << "} " << mapType_(d.name) << ";\n\n";
                if (d.kind == Decl::StructD && hasFieldDefaults(&d)) {
                    indent();
                    out << "static inline " << mapType_(d.name) << " " << mapType_(d.name) << "__default(void) {\n";
                    depth++;
                    indent(); out << mapType_(d.name) << " _v = {0};\n";
                    for (auto& f : d.structCommon.fields) {
                        if (!f.init) continue;
                        indent();
                        out << "_v." << f.name << " = ";
                        emitExpr(*f.init, true);
                        out << ";\n";
                    }
                    indent(); out << "return _v;\n";
                    depth--;
                    indent(); out << "}\n\n";
                }
                // 分身/切片句柄结构体 T__project：def T: <S> {} 的 T[...] 类型展开。
                if (d.kind == Decl::StructD && !d.projectSelf.empty())
                    emitProjectTypedef(d);
                break;
            case Decl::AliasD: {
                std::string base; int ptr;
                resolveType(*d.structCommon.type, base, ptr);
                indent();
                out << "typedef " << base << " ";
                for (int i = 0; i < ptr; i++) out << "*";
                out << mapType_(d.name) << ";\n\n";
                break;
            }
            case Decl::FuncTypeD: {
                if (d.cImpl) break;  // C 实现的接口：不生成 typedef，在 pass 1 中生成 extern 声明
                indent();
                out << "typedef ";
                emitRetType(d);
                out << " (*" << mapType_(d.name) << ")(";
                emitParams(d.structCommon.fields, d.structCommon.variadic);
                out << ");\n\n";
                break;
            }
            default:
                throw CompileError{"内部错误：非类型定义", d.line};
        }
    }

    // ---------------- 函数 ----------------
    void emitRetType(const Decl& d) {
        const auto& rt = d.structCommon.type;
        if (!rt || (rt->name.empty() && rt->ptr == 0 && !rt->fat)) {
            out << "void"; // 空指针 / 省略返回类型 = void
            return;
        }
        // 返回自动指针：T@ → sc_fat；裸 @（name 空，类型擦除）→ sc_afat（带 dtor 槽）
        if (rt->fat) { out << (rt->thin ? "sc_thin" : (rt->name.empty() ? "sc_afat" : "sc_fat")); return; }
        std::string base; int ptr;
        resolveType(*rt, base, ptr);
        out << base;
        for (int i = 0; i < ptr; i++) out << " *";
    }

    void emitParams(const std::vector<Field>& params, bool variadic = false) {
        if (params.empty() && !variadic) { out << "void"; return; }
        for (size_t i = 0; i < params.size(); i++) {
            if (i) out << ", ";
            emitDeclarator(params[i]);
        }
        if (variadic) out << (params.empty() ? "..." : ", ...");
    }

    // 函数签名（实现预定义类型的函数，从函数类型表展开签名）
    void emitFuncSig(const Decl& d) {
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it == funcTypes.end())
                throw CompileError{"未定义的函数类型: " + d.funcTypeName, d.line};
            sig = it->second;
        }
        emitRetType(*sig);
        // 宏体内以形参拼接构造的函数名（含 splice `\`，如 def_vec 的 Vec_\N\_push）为合成名：
        // 编译器不入 funcs 表，外部调用点按展开后的裸名（Vec_int_push）发射；且同宏内 typedef
        // 类型名（Vec_\N）亦裸名。故此处函数名亦须保持裸名，三者一致，避免定义 sc_ 前缀而调用/
        // 类型裸名的错配。非宏体或无 splice 的普通函数照常加 sc_ 前缀（sc 命名域）。
        const bool spliceFn = inMacro && d.name.find('\\') != std::string::npos;
        out << " " << (spliceFn ? d.name : scPfx(d.name)) << "(";
        if (!d.methodOwner.empty()) {
            out << mapType_(d.methodOwner) << " *_this";
            if (!sig->structCommon.fields.empty() || sig->structCommon.variadic) {
                out << ", ";
                emitParams(sig->structCommon.fields, sig->structCommon.variadic);
            }
            // 含胖成员类型的 init：尾随隐藏 _self_own（持有者出边上下文），见 §7.4。
            if (d.methodName == "init" && typeHasFatMember(d.methodOwner))
                out << ", int32_t *_self_own";
        } else {
            emitParams(sig->structCommon.fields, sig->structCommon.variadic);
        }
        out << ")";
    }

    void emitFunc(const Decl& d) {
        // 函数定义行映射回 .sc 源码（函数序言断点落在 fnc 行）；宏体内禁用 #line（#define 内不可含）
        if (!inMacro && !srcFile.empty() && d.line > 0)
            out << "#line " << d.line << " \"" << srcFile << "\"\n";
        if (d.isRpc) { if (d.hasAwait) emitAsyncRpc(d); else emitRpcWorker(d); return; }
        if (d.name != "main" && shouldStaticize(d)) out << "static ";
        emitFuncSig(d);
        out << " {\n";
        // 函数作用域：注册参数类型（含预定义函数类型展开的签名）
        localsT.clear();
        fnVarsL.clear();
        varDimsL.clear();
        projVarsL.clear();
        inlineDefs.clear();     // inl 内联块为函数域，跨函数不可见
        inFunc = true;
        retDollarDeclared = false;
        const Decl* sig = &d;
        if (!d.funcTypeName.empty()) {
            auto it = funcTypes.find(d.funcTypeName);
            if (it != funcTypes.end()) sig = it->second;
        }
        for (auto& p : sig->structCommon.fields) regVar(p);
        curFnSig = sig;
        curMethodOwner = d.methodOwner;   // 方法体内 this 的类型来源（非方法为空）
        curInFatInit = (d.methodName == "init" && typeHasFatMember(d.methodOwner));
        preScanFatBorrows(d.body);   // Step4b：预扫描被 & 借出的栈变量，决定注入 ref 头
        scanManualDrops(d.body);     // RAII：预扫描显式 drop 的变量（move 语义抑制自动 drop）
        depth++;
        // 入口 main：序言注入全局/模块 init；inMainFunc 使 scope0 清理注入尾声 drop。
        const bool isMain = (d.name == "main" && !d.external);
        if (isMain) { inMainFunc = true; emitMainPrologue(); }
        emitStmts(d.body);
        if (isMain) inMainFunc = false;
        depth--;
        inFunc = false;
        curFnSig = nullptr;
        curMethodOwner.clear();
        curInFatInit = false;
        out << "}\n\n";
    }

    // ---------------- 类机制：运行时序言与分派器 ----------------
    // tril / object / sc_hyper 类型定义 + 全局类 id 枚举 + 全局维度选择子枚举。
    void emitClassRuntimePrelude() {
        if (!usesClassRt) return;
        out << "/* 类机制运行时（cls / dim / object） */\n";
        out << "#include <stdarg.h>\n#include <stdio.h>\n#include <string.h>\n";
        // 类型/枚举：头文件模式由工程级 class.h 提供（已于文件头 #include，跨单元一致编号，
        //   且模块头可见 tril/sc_hyper/object）；内联模式则就地输出（单文件自包含）。
        if (!classHeaderName.empty()) {
            out << "\n";
            return;
        }
        out << "typedef int8_t tril;\n";
        out << "#define SC_TRIL_NEG ((tril)-1)\n";
        out << "#define SC_TRIL_UNK ((tril)0)\n";
        out << "#define SC_TRIL_POS ((tril)1)\n";
        out << "typedef tril (*sc_hyper)(void *, uint32_t, ...);\n";
        out << "typedef sc_hyper *object;\n";
        if (!classNames.empty()) {
            out << "enum { SC_CLS_NONE = 0";
            int i = 1;
            for (auto& n : classNames) out << ", SC_CLS_" << n << " = " << i++;
            out << " };\n";
        }
        out << "enum { SC_DIM_CLS_ID = 0, SC_DIM_REF = 1, SC_DIM_DROP = 2"
               ", SC_DIM_OBJ_KEY = 3, SC_DIM_OBJ_NAME = 4, SC_DIM_RLT_KEY = 5, SC_DIM_RLT_NAME = 6";
        std::vector<std::pair<int, std::string>> ds;
        for (auto& kv : dimSelectors) ds.push_back({kv.second, kv.first});
        std::sort(ds.begin(), ds.end());
        for (auto& p : ds) out << ", SC_DIM_" << p.second << " = " << p.first;
        out << " };\n\n";
    }

    // 维度参数从可变实参提取的 C 类型（含默认实参提升：小整数→int，float→double）。
    std::string dimVaType(const Field& f) {
        if (f.type.ptr > 0 || f.type.fat || f.type.project)
            return cTypeOf(f.type.name, f.type.ptr > 0 ? f.type.ptr : 1);
        std::string n = resolveAliasName(f.type.name);
        if (n == "f4") return "double";
        if (n == "i1" || n == "i2" || n == "u1" || n == "u2" ||
            n == "bool" || n == "tril" || n == "char")
            return "int";
        return cTypeOf(f.type.name, 0);
    }

    // 单条维度 case：提取实参为具名局部，va_end，再发出维度体（this→_this，curMethodOwner=T）。
    void emitDimCase(const Decl& dim) {
        out << "    case SC_DIM_" << dim.methodName << ": {\n";
        localsT.clear(); fnVarsL.clear(); varDimsL.clear(); projVarsL.clear();
        inFunc = true; retDollarDeclared = false;
        const Decl* sig = &dim;
        for (auto& p : sig->structCommon.fields) regVar(p);
        curFnSig = sig;
        curMethodOwner = dim.methodOwner;
        curInFatInit = false;
        for (auto& p : sig->structCommon.fields) {
            out << "        ";
            emitDeclarator(p);
            std::string vt = dimVaType(p);
            std::string dt = cTypeOf(p.type.name, p.type.ptr);
            out << " = ";
            if (p.type.ptr == 0 && vt != dt) out << "(" << dt << ")";
            out << "va_arg(_va, " << vt << ");\n";
        }
        out << "        va_end(_va);\n";
        preScanFatBorrows(dim.body);
        scanManualDrops(dim.body);
        depth = 2;
        emitStmts(dim.body);
        depth = 0;
        out << "        return SC_TRIL_UNK;\n";  // 安全网：维度体未显式 return tril
        out << "    }\n";
        inFunc = false; curFnSig = nullptr; curMethodOwner.clear(); curInFatInit = false;
    }

    // cls 的分派器：T_hyper_impl(void *_slot, uint32_t _dim, ...) → switch(_dim)。
    // _slot 指向对象内 _class 槽，container_of 回算出 _this。每个 case 自含 va_end+return。
    void emitDispatcher(const Decl& c) {
        const std::string& T = c.name;
        const std::string Tc = mapType_(c.name);   // C 符号名（含 sc_ 前缀）；T 保留原名用于 SC_CLS_/查表
        out << "tril " << Tc << "_hyper_impl(void *_slot, uint32_t _dim, ...) {\n";
        out << "    " << Tc << " *_this = (" << Tc << " *)((char *)_slot - offsetof("
            << Tc << ", _class));\n";
        out << "    (void)_this;\n";
        out << "    va_list _va; va_start(_va, _dim);\n";
        out << "    switch (_dim) {\n";
        out << "    case SC_DIM_CLS_ID: { int32_t *_id = va_arg(_va, int32_t *); "
            << "va_end(_va); *_id = SC_CLS_" << T << "; return SC_TRIL_POS; }\n";
        // REF（保留）：object@ 取目标 sc_ref 头——派发器知 offsetof(T,_class)，由 container_of
        //   回算的 _this 减 SC_REF_HDR 即头（堆对象头紧贴实体）。类型擦除后唯一能定位头的途径。
        out << "    case SC_DIM_REF: { sc_ref **_h = va_arg(_va, sc_ref **); "
            << "va_end(_va); *_h = (sc_ref *)((char *)_this - SC_REF_HDR); return SC_TRIL_POS; }\n";
        // DROP（保留）：object@ 入边归零时经派发器调本类析构（类型擦除后静态查不到 T_drop）。
        if (const Decl* dm = findMethod(T, "drop"))
            out << "    case SC_DIM_DROP: { va_end(_va); " << scPfx(dm->name)
                << "(_this); return SC_TRIL_POS; }\n";
        bool hasKey = false, hasName = false, hasRltKey = false, hasRltName = false;
        auto it = classDims.find(T);
        if (it != classDims.end())
            for (auto* dim : it->second) {
                if (dim->methodName == "OBJ_KEY")  hasKey = true;
                if (dim->methodName == "OBJ_NAME") hasName = true;
                if (dim->methodName == "RLT_KEY")  hasRltKey = true;
                if (dim->methodName == "RLT_NAME") hasRltName = true;
            }
        // OBJ_KEY 默认：存在 obj_key 字段则取字段值，否则取对象基址。
        if (!hasKey) {
            out << "    case SC_DIM_OBJ_KEY: { void **_k = va_arg(_va, void **); va_end(_va); *_k = ";
            if (classHasField(c, "obj_key"))
                out << "(void *)(uintptr_t)_this->obj_key";
            else
                out << "(void *)_this";
            out << "; return SC_TRIL_POS; }\n";
        }
        // OBJ_NAME 默认：存在 obj_name 字段则取字段值（%s），否则 snprintf "<类名>@<地址>"。
        if (!hasName) {
            out << "    case SC_DIM_OBJ_NAME: { char *_b = va_arg(_va, char *); "
                << "int32_t _cap = va_arg(_va, int); va_end(_va); ";
            if (classHasField(c, "obj_name"))
                out << "snprintf(_b, (size_t)_cap, \"%s\", _this->obj_name); ";
            else
                out << "snprintf(_b, (size_t)_cap, \"" << T << "@%p\", (void *)_this); ";
            out << "return SC_TRIL_POS; }\n";
        }
        // RLT_KEY 默认：取自身与另一对象的 key（经 OBJ_KEY，尊重覆盖），比大小返回三态。
        if (!hasRltKey)
            out << "    case SC_DIM_RLT_KEY: { object _other = va_arg(_va, object); va_end(_va); "
                << "void *_ka = (void *)0, *_kb = (void *)0; "
                << Tc << "_hyper_impl(_slot, SC_DIM_OBJ_KEY, &_ka); "
                << "if (_other) (*_other)(_other, SC_DIM_OBJ_KEY, &_kb); "
                << "if ((uintptr_t)_ka < (uintptr_t)_kb) return SC_TRIL_NEG; "
                << "if ((uintptr_t)_ka > (uintptr_t)_kb) return SC_TRIL_POS; "
                << "return SC_TRIL_UNK; }\n";
        // RLT_NAME 默认：取自身与另一对象的 name（经 OBJ_NAME，尊重覆盖），strcmp 返回三态。
        if (!hasRltName)
            out << "    case SC_DIM_RLT_NAME: { object _other = va_arg(_va, object); va_end(_va); "
                << "char _na[256], _nb[256]; _na[0] = 0; _nb[0] = 0; "
                << Tc << "_hyper_impl(_slot, SC_DIM_OBJ_NAME, _na, (int32_t)sizeof(_na)); "
                << "if (_other) (*_other)(_other, SC_DIM_OBJ_NAME, _nb, (int32_t)sizeof(_nb)); "
                << "int _r = strcmp(_na, _nb); "
                << "if (_r < 0) return SC_TRIL_NEG; if (_r > 0) return SC_TRIL_POS; "
                << "return SC_TRIL_UNK; }\n";
        if (it != classDims.end())
            for (auto* dim : it->second) emitDimCase(*dim);
        out << "    default: va_end(_va); return SC_TRIL_UNK;\n";
        out << "    }\n";
        out << "}\n\n";
    }

    // 提升匿名函数字面量为顶层 static 函数，定义追加到 lambdaOut。
    // 保存/恢复外层函数的代码生成状态（输出流 + 局部符号表 + 缩进），可重入。
    void emitLambdaDef(const Expr& e, const std::string& name) {
        // 保存外层状态
        auto savedLocalsT  = std::move(localsT);
        auto savedFnVarsL  = std::move(fnVarsL);
        auto savedVarDimsL = std::move(varDimsL);
        const bool savedInFunc = inFunc;
        const bool savedInMain = inMainFunc;   // lambda 体非 main：禁用尾声注入
        const int  savedDepth  = depth;
        auto savedMethodOwner = std::move(curMethodOwner);   // lambda 体无 this
        const bool savedInFatInit = curInFatInit;
        std::ostringstream savedOut = std::move(out);
        out = std::ostringstream();

        // 干净的函数作用域
        localsT.clear(); fnVarsL.clear(); varDimsL.clear(); projVarsL.clear();
        curMethodOwner.clear();
        curInFatInit = false;
        inFunc = true;
        inMainFunc = false;
        retDollarDeclared = false;
        depth = 0;
        auto savedManualDrops = std::move(manualDropVars);   // RAII：lambda 独立预扫描
        auto savedDropScopes  = std::move(dropScopes);
        auto savedAutoFreeScopes = std::move(autoFreeScopes);
        auto savedAutoFreeVars   = std::move(autoFreeVars);
        auto savedInlineDefs = std::move(inlineDefs);        // inl 块为外层函数域，lambda 不继承
        dropScopes.clear();
        autoFreeScopes.clear();
        autoFreeVars.clear();
        inlineDefs.clear();
        scanManualDrops(e.fncBody);

        Decl sigDecl;                                   // 仅承载返回类型给 emitRetType
        sigDecl.structCommon.type = e.fncSig.type;
        out << "static ";
        emitRetType(sigDecl);
        out << " " << name << "(";
        emitParams(e.fncSig.fields, e.fncSig.variadic);
        out << ") {\n";
        for (auto& p : e.fncSig.fields) regVar(p);
        depth++;
        emitStmts(e.fncBody);
        depth--;
        out << "}\n\n";

        lambdaOut << out.str();                         // 收集定义（嵌套 lambda 已先行追加）

        // 恢复外层状态
        out = std::move(savedOut);
        localsT  = std::move(savedLocalsT);
        fnVarsL  = std::move(savedFnVarsL);
        varDimsL = std::move(savedVarDimsL);
        inFunc = savedInFunc;
        inMainFunc = savedInMain;
        depth  = savedDepth;
        curMethodOwner = std::move(savedMethodOwner);
        curInFatInit = savedInFatInit;
        manualDropVars = std::move(savedManualDrops);
        dropScopes     = std::move(savedDropScopes);
        autoFreeScopes = std::move(savedAutoFreeScopes);
        autoFreeVars   = std::move(savedAutoFreeVars);
        inlineDefs     = std::move(savedInlineDefs);
    }

    // ---------------- rpc：伪形参函数糖 ----------------
    // rpc add: i4, a: i4, b: i4 展开为三件套：
    //   struct add { int32_t _; int32_t a; int32_t b; };  // 同名参数结构体
    //   void add_rpc(struct add *_p);                      // 实际函数
    //   static inline int32_t add(int32_t a, int32_t b);   // 调用包装
    // 结构体仅用 tag（不 typedef）：C 中 struct tag 与函数名分属不同
    // 命名空间，故二者可同名，调用形式与 fnc 完全一致。

    // 是否有返回值（与 fnc 一致：空指针 / 省略返回类型 = void 无返回值）
    static bool rpcHasRet(const Decl& d) {
        const auto& rt = d.structCommon.type;
        return rt && (!rt->name.empty() || rt->ptr > 0);
    }

    // sync/async/run 选项块：按 key 在 opts 中查表达式并 emit（带强转）；缺省 emit "(cast)0"。
    //   值为任意表达式（运行时求值），故不做编译期范围校验，直接括号包裹后 emit。
    void emitOptOr0(const std::vector<std::pair<std::string, ExprPtr>>& opts,
                    const char* key, const char* cast) {
        for (auto& kv : opts) {
            if (kv.first == key) {
                out << "(" << cast << ")("; emitExpr(*kv.second, true); out << ")";
                return;
            }
        }
        out << "(" << cast << ")0";
    }
    // opts 中是否含某键。
    static bool hasOpt(const std::vector<std::pair<std::string, ExprPtr>>& opts, const char* key) {
        for (auto& kv : opts) if (kv.first == key) return true;
        return false;
    }
    // 校验 sync/async 选项键：仅允许 prio/delay（与 timeout，仅 sync）。
    void checkRpcOptKeys(const Expr& e, bool allowTimeout) {
        for (auto& kv : e.syncOpts) {
            if (kv.first == "prio" || kv.first == "delay") continue;
            if (kv.first == "timeout") {
                if (!allowTimeout)
                    throw CompileError{"async<q, ...> 暂不支持 timeout 选项（仅 sync 有限超时，见 P5c）", e.line};
                continue;
            }
            throw CompileError{"sync/async 选项仅支持 prio/delay" +
                               std::string(allowTimeout ? "/timeout" : "") +
                               "，未知键：" + kv.first, e.line};
        }
    }

    // rpc 数组形参 → 结构体两字段：指针（数组退化）+ size（字节数）。
    // 名为 a 的数组形参映射为 `T (*a)[剩余维]` 与 `size_t a_size`，
    // 装填时传数组地址与其 sizeof，便于 C 传输层据 size 序列化数组。
    // 数组形参的伴生 size 字段名。
    static std::string rpcArraySizeName(const Field& f) { return f.name + "_size"; }

    // 数组形参在结构体内的指针字段声明：首维退化为指针，保留其余维度，
    // 使 worker 体内 a[i][j] 正常索引。1 维 → `T *name`；多维 → `T (*name)[d2]...`。
    void emitRpcArrayPtr(const Field& f) {
        std::string base; int ptr;
        resolveType(f.type, base, ptr);
        const auto& dims = f.type.arrayDims;
        const std::string nm = (f.name == "this" ? "_this" : f.name);
        out << base << " ";
        for (int i = 0; i < ptr; i++) out << "*";
        if (dims.size() <= 1) {                    // 1 维：普通指针
            out << "*" << nm;
        } else {                                   // 多维：指向数组的指针
            out << "(*" << nm << ")";
            for (size_t i = 1; i < dims.size(); i++) out << "[" << dims[i] << "]";
        }
    }

    // 数组形参完整类型的 sizeof：`sizeof(T [*..][d1][d2]...)`（按声明静态求值）。
    void emitRpcArraySizeof(const Field& f) {
        std::string base; int ptr;
        resolveType(f.type, base, ptr);
        out << "sizeof(" << base;
        for (int i = 0; i < ptr; i++) out << " *";
        for (auto& dim : f.type.arrayDims) out << "[" << dim << "]";
        out << ")";
    }

    // 同名参数结构体：返回槽 _ 为首个默认成员（C 侧可用 _ 访问）
    void emitRpcStruct(const Decl& d) {
        out << "struct " << scPfx(d.name) << " {\n";
        depth++;
        if (rpcHasRet(d)) {
            indent();
            emitRetType(d);
            out << " _;\n";
        } else if (d.structCommon.fields.empty() && !d.hasAwait) {
            indent();
            out << "char _;\n";  // C 不允许空结构体：占位
        }
        // 异步 rpc：追加状态机隐藏字段 + 把跨 await 存活的局部提升到帧
        if (d.hasAwait) {
            indent(); out << "sc_future *_ret;\n";   // 本次调用的结果 future
            indent(); out << "int _state;\n";     // 状态机当前段
            indent(); out << "sc_future *_fut;\n";   // 当前正在 await 的 future
        }
        for (auto& f : d.structCommon.fields) {
            if (!f.type.arrayDims.empty()) {       // 数组形参 → <T*, size> 两字段
                indent(); emitRpcArrayPtr(f); out << ";\n";
                indent(); out << "size_t " << rpcArraySizeName(f) << ";\n";
                continue;
            }
            indent();
            emitDeclarator(f);
            out << ";\n";
        }
        if (d.hasAwait) {                          // 提升的局部变量
            for (auto& f : collectAsyncLocals(d)) {
                indent(); emitDeclarator(*f); out << ";\n";
            }
            // 【F】com<<>>rpc 序列化：每个 op 一个堆 rpc 参数槽（跨 await 存活）
            std::vector<const Decl*> crpcs = collectComRpcStmts(d);
            for (size_t i = 0; i < crpcs.size(); i++) {
                indent(); out << "struct " << scPfx(crpcs[i]->name) << " *_crpc" << i << ";\n";
            }
        }
        depth--;
        out << "};\n";
    }

    // 实际函数签名：void name_rpc(struct name *_p)
    void emitRpcWorkerSig(const Decl& d) {
        out << "void " << scPfx(d.name) << "_rpc(struct " << scPfx(d.name) << " *_p)";
    }

    // 调用包装：装填结构体 → 执行实际函数 → 取返回槽
    void emitRpcWrapper(const Decl& d) {
        out << "static inline ";
        emitRetType(d);
        out << " " << scPfx(d.name) << "(";
        emitParams(d.structCommon.fields, d.structCommon.variadic);
        out << ") {\n";
        depth++;
        indent(); out << "struct " << scPfx(d.name) << " _p = {0};\n";
        for (auto& f : d.structCommon.fields) {
            const std::string arg = (f.name == "this" ? "_this" : f.name);
            indent();
            out << "_p." << f.name << " = " << arg << ";\n";
            if (!f.type.arrayDims.empty()) {       // 数组：额外装填 size（字节数）
                indent();
                out << "_p." << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f);
                out << ";\n";
            }
        }
        indent(); out << scPfx(d.name) << "_rpc(&_p);\n";
        if (rpcHasRet(d)) { indent(); out << "return _p._;\n"; }
        depth--;
        out << "}\n\n";
    }

    // rpc 接口三件套：结构体 + 实际函数原型 + 调用包装
    // workerStatic：本模块定义且未导出时 static；仅声明/导出时 extern
    void emitRpcInterface(const Decl& d, bool workerStatic) {
        // com[...] 句柄参数 → 先保证其 T__project typedef 已发出（结构体按值内含）。
        for (auto& f : d.structCommon.fields)
            if (f.type.project)
                if (const Decl* ent = aggrOf(f.type.name))
                    if (!ent->projectSelf.empty()) emitProjectTypedef(*ent);
        emitRpcStruct(d);
        if (workerStatic) out << "static ";
        emitRpcWorkerSig(d);
        out << ";\n";
        if (d.hasAwait) {
            // 异步 rpc：无同步包装，改发启动器原型 future* X__async(参数...)
            if (workerStatic) out << "static ";
            out << "sc_future *" << scPfx(d.name) << "__async(";
            emitParams(d.structCommon.fields, d.structCommon.variadic);
            out << ");\n";
        } else {
            emitRpcWrapper(d);
        }
    }

    // rpc 实际函数体：参数引用由 emitExpr/ReturnS 改写为 _p->xxx
    void emitRpcWorker(const Decl& d) {
        if (shouldStaticize(d)) out << "static ";
        emitRpcWorkerSig(d);
        out << " {\n";
        localsT.clear();
        fnVarsL.clear();
        varDimsL.clear();
        projVarsL.clear();
        inFunc = true;
        retDollarDeclared = false;
        for (auto& p : d.structCommon.fields) regVar(p);
        curRpc = &d;
        rpcParams.clear();
        for (auto& p : d.structCommon.fields) rpcParams.insert(p.name);
        depth++;
        emitStmts(d.body);
        depth--;
        curRpc = nullptr;
        rpcParams.clear();
        inFunc = false;
        out << "}\n\n";
    }

    // ================= 异步 rpc（含 await）→ 状态机（stackless coroutine）=================
    // 含 await 的 rpc 编译为：
    //   struct X { 返回槽 _; future* _ret; int _state; future* _fut; 参数...; 提升局部...; };
    //   future* X__async(参数...)   启动器：建帧 + 造 _ret + 装参 + 首次驱动 → 返回 _ret
    //   void    X_rpc(struct X* _p) 状态机：switch(_state) 跳转，await 点切段、让出
    //
    // 控制流支持：await 可出现在 if/while/do-while/经典 for 体内（含嵌套）。原理（protothreads/
    //   Duff's device）：所有局部均提升到帧 _p->name，函数体内无 C 局部声明，故顶部
    //   switch(_state){case N: goto _sN;} 可从函数顶 goto 跳进循环/分支体内的 _sN: 标签
    //   （合法 C：goto 跳入块允许，因无被跳过的初始化）。跳入后续跑、重评循环条件，语义正确。
    //   暂不支持：for-in / case 内 await（带 await 时报明确错误，不静默丢弃）。
    //   await 形态：await E / var x:T = await E / x = await E / com<<>> 收发糖。

    // 单条语句「直接」（不含嵌套体）的 await 让出点数量。
    int directAwaits(const Stmt& s) {
        int n = 0;
        if (s.kind == Stmt::VarS || s.kind == Stmt::LetS) {
            for (auto& f : s.decls)
                if (f.init && f.init->kind == Expr::Await) n++;
        } else if (s.kind == Stmt::ExprS && s.expr) {
            if (s.expr->kind == Expr::Await) n++;
            else if (s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                     s.expr->b && s.expr->b->kind == Expr::Await) n++;
            else if (s.expr->kind == Expr::Binary &&
                     (s.expr->op == "<<" || s.expr->op == ">>")) {
                std::vector<ComOp> ops;            // com 收发链：每个 op 至少一个 await 点
                if (comChain(*s.expr, ops)) {
                    for (auto& o : ops) {
                        // 【F】rpc 序列化：每参数字段一个让出点；【D】/【E】各一个
                        if (const Decl* r = comRpcTarget(o.target, o.send))
                            n += (int)r->structCommon.fields.size();
                        else
                            n += 1;
                    }
                }
            }
        }
        return n;
    }

    // 深度检测：语句子树（含 for-in/case 等所有嵌套体）是否含 await。
    bool hasAwaitDeep(const Stmt& s) {
        if (directAwaits(s) > 0) return true;
        for (auto& c : s.body)     if (hasAwaitDeep(*c)) return true;
        for (auto& c : s.elseBody) if (hasAwaitDeep(*c)) return true;
        for (auto& arm : s.caseArms)
            for (auto& c : arm.body) if (hasAwaitDeep(*c)) return true;
        return false;
    }

    // 递归收集需提升到帧的局部变量（var/let 声明），按名去重（首见保留）。
    // 进入受支持的控制流体（if/while/do-while/经典 for）；不进 for-in/case（其局部留作 C 局部）。
    void collectAsyncLocalsList(const std::vector<StmtPtr>& stmts,
                                std::vector<const Field*>& locals,
                                std::unordered_set<std::string>& seen) {
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            if (s.kind == Stmt::VarS || s.kind == Stmt::LetS)
                for (auto& f : s.decls)
                    if (seen.insert(f.name).second) locals.push_back(&f);
            switch (s.kind) {
                case Stmt::IfS:
                    collectAsyncLocalsList(s.body, locals, seen);
                    collectAsyncLocalsList(s.elseBody, locals, seen);
                    break;
                case Stmt::WhileS:
                case Stmt::DoWhileS:
                    collectAsyncLocalsList(s.body, locals, seen);
                    break;
                case Stmt::ForS:
                    if (!s.forIn) collectAsyncLocalsList(s.body, locals, seen);
                    break;
                default: break;
            }
        }
    }
    std::vector<const Field*> collectAsyncLocals(const Decl& d) {
        std::vector<const Field*> locals;
        std::unordered_set<std::string> seen;
        collectAsyncLocalsList(d.body, locals, seen);
        return locals;
    }

    // 递归统计 await 让出点数量（= 状态机段数 - 1）。遍历序须与 emitAsyncStmt 发射序一致。
    int countAsyncAwaitsList(const std::vector<StmtPtr>& stmts) {
        int n = 0;
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            n += directAwaits(s);
            switch (s.kind) {
                case Stmt::IfS:
                    n += countAsyncAwaitsList(s.body);
                    n += countAsyncAwaitsList(s.elseBody);
                    break;
                case Stmt::WhileS:
                case Stmt::DoWhileS:
                    n += countAsyncAwaitsList(s.body);
                    break;
                case Stmt::ForS:
                    if (!s.forIn) n += countAsyncAwaitsList(s.body);
                    break;
                default: break;
            }
        }
        return n;
    }
    int countAsyncAwaits(const Decl& d) { return countAsyncAwaitsList(d.body); }

    // 收集 rpc 体顶层的「com<<>>rpc 序列化」op（按出现顺序），供帧注入 _crpcN 槽。
    // 顺序与 emitComAwait 内 comRpcIdx 递增一致（同序遍历 statements→ops）。
    // 结构体定义阶段调用时变量环境未建立，故临时按 d 注册参数/异步局部（与函数体一致），
    // 使 comChain 的 base 类型判定可用；用毕复原。
    std::vector<const Decl*> collectComRpcStmts(const Decl& d) {
        auto savedLocalsT  = localsT;   auto savedFnVarsL  = fnVarsL;
        auto savedVarDimsL = varDimsL;  auto savedProjVarsL = projVarsL;
        const bool savedInFunc = inFunc;
        localsT.clear(); fnVarsL.clear(); varDimsL.clear(); projVarsL.clear();
        inFunc = true;
        for (auto& p : d.structCommon.fields) regVar(p);
        for (auto& f : collectAsyncLocals(d)) regVar(*f);

        std::vector<const Decl*> v;
        collectComRpcStmtsList(d.body, v);

        localsT  = std::move(savedLocalsT);  fnVarsL  = std::move(savedFnVarsL);
        varDimsL = std::move(savedVarDimsL); projVarsL = std::move(savedProjVarsL);
        inFunc = savedInFunc;
        return v;
    }

    // 递归收集 com<<>>rpc 序列化 op 的目标 rpc（DFS 序须与 emitAsyncStmt 发射的 comRpcIdx 递增一致）。
    void collectComRpcStmtsList(const std::vector<StmtPtr>& stmts, std::vector<const Decl*>& v) {
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            if (s.kind == Stmt::ExprS && s.expr &&
                s.expr->kind == Expr::Binary && (s.expr->op == "<<" || s.expr->op == ">>")) {
                std::vector<ComOp> ops;
                if (comChain(*s.expr, ops))
                    for (auto& o : ops)
                        if (const Decl* r = comRpcTarget(o.target, o.send)) v.push_back(r);
            }
            switch (s.kind) {
                case Stmt::IfS:
                    collectComRpcStmtsList(s.body, v);
                    collectComRpcStmtsList(s.elseBody, v);
                    break;
                case Stmt::WhileS:
                case Stmt::DoWhileS:
                    collectComRpcStmtsList(s.body, v);
                    break;
                case Stmt::ForS:
                    if (!s.forIn) collectComRpcStmtsList(s.body, v);
                    break;
                default: break;
            }
        }
    }

    // 发出 async 调用/await rpc 的实参（位置参数，逐个求值）。
    void emitAsyncCallArgs(const Expr& call) {
        for (size_t i = 0; i < call.args.size(); i++) {
            if (i) out << ", ";
            emitExpr(*call.args[i], true);
        }
    }

    // 发出一个"产生 future"的表达式：
    //   - 对含 await 的 rpc 调用 → 改写为启动器 name__async(args)
    //   - 否则（叶子原语 delay / 自定义 fnc bg_square 等）→ 原样发出
    void emitFutureExpr(const Expr& e) {
        if (e.kind == Expr::Call && e.a && e.a->kind == Expr::Ident) {
            auto it = rpcs.find(e.a->text);
            if (it != rpcs.end() && it->second->hasAwait) {
                out << scPfx(e.a->text) << "__async(";   // 启动器 sc 域前缀（与定义 §6243/6791 一致）
                emitAsyncCallArgs(e);
                out << ")";
                return;
            }
        }
        bool save = emitRpcCallOK;   // await 上下文：放行（叶子原语 fnc 非 rpc 不受限，此为兜底）
        emitRpcCallOK = true;
        emitExpr(e, true);
        emitRpcCallOK = save;
    }

    // 发出 (T)future_get(_p->_fut)：指针类型直接强转；标量经 intptr_t 还原。
    void emitFutureGetCast(const std::string& base, int ptr) {
        if (ptr > 0) {
            out << "(" << base << " ";
            for (int i = 0; i < ptr; i++) out << "*";
            out << ")sc_future_get(_p->_fut)";
        } else {
            out << "(" << base << ")(intptr_t)sc_future_get(_p->_fut)";
        }
    }

    // 一个 await 点：发起 → 登记本帧为 waiter → 已就绪则续跑、否则让出。
    // target 非空时（var x = await E / x = await E），恢复后把结果写回 target。
    void emitAwaitPoint(const Expr& futureExpr, const std::string* target,
                        const std::string& tBase, int tPtr, const Decl& d) {
        int st = ++asyncState;
        indent(); out << "_p->_fut = "; emitFutureExpr(futureExpr); out << ";\n";
        indent(); out << "if (sc_future_await(_p->_fut, _p, (void (*)(void *))"
                      << scPfx(d.name) << "_rpc)) goto _s" << st << ";\n";
        indent(); out << "_p->_state = " << st << "; return;\n";
        indent(); out << "_s" << st << ": ;\n";
        if (target) {
            indent(); out << *target << " = "; emitFutureGetCast(tBase, tPtr); out << ";\n";
        }
    }

    // 异步 com 收发的一个 await 点（按右操作数形态分形）：
    //   【D】普通变量 v   →  com_read_async/com_write_async（io 直接填充/读取 v）；
    //   【E】com[...] 句柄 →  com_limit_read_async（框架确定读循环，遇 again 挂起续读）；
    //   【F】rpc 调用/名   →  逐参数字段 await 序列化收发（每字段一个让出点）。
    void emitComAwait(const Expr& base, const ComOp& o, const Decl& d) {
        VType vt; exprVType(base, vt);
        const bool isPtr = vt.ptr >= 1;
        // 【F】rpc 形态：发（<<）目标为 rpc 调用、收（>>）目标为裸 rpc 名
        if (const Decl* r = comRpcTarget(o.target, o.send)) {
            if (o.send) emitComRpcSendAsync(base, isPtr, *r, o.target->args, d);
            else        emitComRpcRecvAsync(base, isPtr, *r, d);
            return;
        }
        if (const Decl* r = comRpcTarget(o.target, !o.send)) {
            (void)r;
            if (o.send) throw CompileError{"com << rpc 发送需带参数：com << rpc(参数...)", o.target->line};
            else        throw CompileError{"com >> rpc 收端不接受实参，请写 com >> rpc", o.target->line};
        }
        if (o.target->kind == Expr::Call)
            throw CompileError{"com 通讯的回调形态（<< / >> 接非 rpc 调用）暂未实现", o.target->line};
        // 【E】com[...] 句柄（limit 分身）→ 框架读流程异步驱动（仅 >>）
        if (o.target->kind == Expr::Ident && projEntityOf(o.target->text) == "com") {
            if (o.send)
                throw CompileError{"com[...] 句柄仅用于 >> 读流程，不支持 << 写", o.target->line};
            int st = ++asyncState;
            indent(); out << "_p->_fut = sc_com_limit_read_async(";
            emitComBasePtr(base, isPtr);
            out << ", "; emitExpr(*o.target, true); out << "._);\n";
            indent(); out << "if (sc_future_await(_p->_fut, _p, (void (*)(void *))"
                          << scPfx(d.name) << "_rpc)) goto _s" << st << ";\n";
            indent(); out << "_p->_state = " << st << "; return;\n";
            indent(); out << "_s" << st << ": ;\n";
            return;
        }
        // 【D】普通变量：发起 com_read_async/com_write_async，io 直接填充/读取 target
        int st = ++asyncState;
        const char* fn = o.send ? "sc_com_write_async" : "sc_com_read_async";
        indent(); out << "_p->_fut = " << fn << "(";
        emitComBasePtr(base, isPtr);
        out << ", (void *)&("; emitExpr(*o.target, true); out << "), sizeof(";
        emitExpr(*o.target, true); out << "));\n";
        indent(); out << "if (sc_future_await(_p->_fut, _p, (void (*)(void *))"
                      << scPfx(d.name) << "_rpc)) goto _s" << st << ";\n";
        indent(); out << "_p->_state = " << st << "; return;\n";
        indent(); out << "_s" << st << ": ;\n";
    }

    // 异步 com io await 点（单一收发）：FN(base, dataPtr, size) → future → await 握手。
    void emitComIoAwait(const Expr& base, bool isPtr, bool send,
                        const std::string& dataPtr, const std::string& sizeExpr,
                        const Decl& d) {
        int st = ++asyncState;
        const char* fn = send ? "sc_com_write_async" : "sc_com_read_async";
        indent(); out << "_p->_fut = " << fn << "(";
        emitComBasePtr(base, isPtr);
        out << ", " << dataPtr << ", " << sizeExpr << ");\n";
        indent(); out << "if (sc_future_await(_p->_fut, _p, (void (*)(void *))"
                      << scPfx(d.name) << "_rpc)) goto _s" << st << ";\n";
        indent(); out << "_p->_state = " << st << "; return;\n";
        indent(); out << "_s" << st << ": ;\n";
    }

    // 【F】发· com << rpc(实参...)（异步）：把 rpc 参数堆分配进帧槽 _p->_crpcN（跨 await
    // 存活），先装填（无让出），再逐字段 com_write_async await 写出，末了释放该槽。
    void emitComRpcSendAsync(const Expr& base, bool isPtr, const Decl& r,
                             const std::vector<ExprPtr>& args, const Decl& d) {
        if (r.hasAwait)
            throw CompileError{"com 收发暂不支持异步 rpc：" + r.name, r.line};
        if (r.structCommon.variadic)
            throw CompileError{"com << rpc 暂不支持可变参数 rpc：" + r.name, r.line};
        if (args.size() > r.structCommon.fields.size())
            throw CompileError{"rpc 实参数量超出：" + r.name, r.line};
        const std::string rp = "_p->_crpc" + std::to_string(comRpcIdx++);
        indent(); out << rp << " = (struct " << scPfx(r.name) << " *)sc_chunk0(sizeof(struct "
                      << scPfx(r.name) << "));\n";
        for (size_t i = 0; i < args.size(); i++) {
            const Field& f = r.structCommon.fields[i];
            if (f.type.project)
                throw CompileError{"com << rpc 的 com[...] 参数不支持序列化发送：" + f.name, r.line};
            indent(); out << rp << "->" << f.name << " = "; emitExpr(*args[i], true); out << ";\n";
            if (!f.type.arrayDims.empty()) {
                indent(); out << rp << "->" << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f); out << ";\n";
            }
        }
        for (auto& f : r.structCommon.fields) {
            if (f.type.project)
                throw CompileError{"com << rpc 的 com[...] 参数不支持序列化发送：" + f.name, r.line};
            if (!f.type.arrayDims.empty())
                emitComIoAwait(base, isPtr, true, "(void *)(" + rp + "->" + f.name + ")",
                               "(uint32_t)" + rp + "->" + rpcArraySizeName(f), d);
            else
                emitComIoAwait(base, isPtr, true, "(void *)&(" + rp + "->" + f.name + ")",
                               "sizeof(" + rp + "->" + f.name + ")", d);
        }
        indent(); out << "sc_recycle(" << rp << ");\n";
    }

    // 【F】收· com >> rpc（异步）：堆分配帧槽 _p->_crpcN，初始化数组后备/句柄上下文（无
    // 让出），逐字段 await 读入（句柄字段走 com_limit_read_async 框架读流程），读毕触发
    // worker rpc_rpc，再释放该槽与数组后备。
    void emitComRpcRecvAsync(const Expr& base, bool isPtr, const Decl& r, const Decl& d) {
        if (r.hasAwait)
            throw CompileError{"com 收发暂不支持异步 rpc：" + r.name, r.line};
        if (r.structCommon.variadic)
            throw CompileError{"com >> rpc 暂不支持可变参数 rpc：" + r.name, r.line};
        const std::string rp = "_p->_crpc" + std::to_string(comRpcIdx++);
        indent(); out << rp << " = (struct " << scPfx(r.name) << " *)sc_chunk0(sizeof(struct "
                      << scPfx(r.name) << "));\n";
        // 数组后备（堆）+ com[...] 句柄上下文初始化（无让出）
        for (auto& f : r.structCommon.fields) {
            if (!f.type.arrayDims.empty()) {
                indent(); out << rp << "->" << f.name << " = sc_chunk0(";
                emitRpcArraySizeof(f); out << ");\n";
                indent(); out << rp << "->" << rpcArraySizeName(f) << " = ";
                emitRpcArraySizeof(f); out << ";\n";
            } else if (f.type.project) {
                const std::vector<Field>* alParams = projectAllocParams(f.type.name);
                if (f.type.projectArgs && alParams) {
                    size_t n = std::min(f.type.projectArgs->size(), alParams->size());
                    for (size_t i = 0; i < n; i++) {
                        indent(); out << rp << "->" << f.name << "." << (*alParams)[i].name << " = ";
                        emitExpr(*(*f.type.projectArgs)[i], true); out << ";\n";
                    }
                }
            }
        }
        // 逐字段 await 读
        for (auto& f : r.structCommon.fields) {
            if (!f.type.arrayDims.empty()) {
                emitComIoAwait(base, isPtr, false, "(void *)(" + rp + "->" + f.name + ")",
                               "(uint32_t)" + rp + "->" + rpcArraySizeName(f), d);
            } else if (f.type.project) {
                // 绑定句柄到本 com（alloc + _self 回指），再 await 框架读流程
                const std::vector<Field>* alParams = projectAllocParams(f.type.name);
                indent(); out << rp << "->" << f.name << "._ = ";
                emitExpr(base, true); out << (isPtr ? "->" : ".") << "alloc(";
                emitComBasePtr(base, isPtr);
                if (alParams) for (auto& p : *alParams)
                    out << ", " << rp << "->" << f.name << "." << p.name;
                out << ");\n";
                indent(); out << rp << "->" << f.name << "._->_self = ";
                emitComBasePtr(base, isPtr); out << ";\n";
                int st = ++asyncState;
                indent(); out << "_p->_fut = sc_com_limit_read_async(";
                emitComBasePtr(base, isPtr);
                out << ", " << rp << "->" << f.name << "._);\n";
                indent(); out << "if (sc_future_await(_p->_fut, _p, (void (*)(void *))"
                              << scPfx(d.name) << "_rpc)) goto _s" << st << ";\n";
                indent(); out << "_p->_state = " << st << "; return;\n";
                indent(); out << "_s" << st << ": ;\n";
            } else {
                emitComIoAwait(base, isPtr, false, "(void *)&(" + rp + "->" + f.name + ")",
                               "sizeof(" + rp + "->" + f.name + ")", d);
            }
        }
        indent(); out << scPfx(r.name) << "_rpc(" << rp << ");\n";
        for (auto& f : r.structCommon.fields)
            if (!f.type.arrayDims.empty()) { indent(); out << "sc_recycle(" << rp << "->" << f.name << ");\n"; }
        indent(); out << "sc_recycle(" << rp << ");\n";
    }

    // 完成：写返回槽 → 释放帧 → future_done 唤醒上游（return）。
    void emitAsyncComplete(const Stmt& ret, const Decl& d) {
        const bool hasRet = rpcHasRet(d);
        if (hasRet && ret.expr) {
            indent(); out << "_p->_ = "; emitExpr(*ret.expr, true); out << ";\n";
        }
        std::string res = "NULL";
        if (hasRet) {
            std::string base; int ptr; resolveType(*d.structCommon.type, base, ptr);
            res = ptr > 0 ? "(void *)(_p->_)" : "(void *)(intptr_t)(_p->_)";
        }
        indent();
        out << "{ sc_future *_r = _p->_ret; void *_res = " << res
            << "; sc_recycle(_p); sc_future_done(_r, _res); return; }\n";
    }

    // 发出异步 rpc 体（语句序列；await 点切段，支持 if/while/do-while/经典 for 内 await）。
    void emitAsyncStmts(const Decl& d) {
        emitAsyncStmtList(d.body, d);
        // 无显式 return 的 void 异步 rpc：补完成
        if (d.body.empty() || d.body.back()->kind != Stmt::ReturnS) {
            Stmt fake; fake.kind = Stmt::ReturnS;
            emitAsyncComplete(fake, d);
        }
    }

    // 递归发出异步语句序列。遍历序须与 countAsyncAwaits/collectComRpcStmts 一致（状态号对齐）。
    void emitAsyncStmtList(const std::vector<StmtPtr>& stmts, const Decl& d) {
        for (auto& sp : stmts) {
            const Stmt& s = *sp;
            if (s.line > 0 && !srcFile.empty())
                out << "#line " << s.line << " \"" << srcFile << "\"\n";
            emitAsyncStmt(s, d);
        }
    }

    // 发出单条异步语句。受支持控制流（if/while/do-while/经典 for）发真实 C 结构并递归其体；
    // await 点（emitAwaitPoint/emitComAwait）内联发 _sN: 标签，供顶部 switch goto 跳入。
    void emitAsyncStmt(const Stmt& s, const Decl& d) {
        switch (s.kind) {
            case Stmt::VarS: case Stmt::LetS:
                for (auto& f : s.decls) {
                    std::string base; int ptr; resolveType(f.type, base, ptr);
                    std::string tgt = "_p->" + f.name;
                    if (f.init && f.init->kind == Expr::Await)
                        emitAwaitPoint(*f.init->a, &tgt, base, ptr, d);
                    else if (f.init) {           // 普通局部（已提升到帧）：赋值
                        indent(); out << tgt << " = "; emitExpr(*f.init, true); out << ";\n";
                    }
                }
                break;
            case Stmt::ExprS:
                if (s.expr && s.expr->kind == Expr::Binary &&
                    (s.expr->op == "<<" || s.expr->op == ">>")) {  // com 收发（异步形态）
                    std::vector<ComOp> ops;
                    if (const Expr* base = comChain(*s.expr, ops)) {
                        for (auto& o : ops) emitComAwait(*base, o, d);
                        break;
                    }
                }
                if (s.expr && s.expr->kind == Expr::Await) {     // 独立 await E
                    emitAwaitPoint(*s.expr->a, nullptr, "", 0, d);
                } else if (s.expr && s.expr->kind == Expr::Binary && s.expr->op == "=" &&
                           s.expr->b && s.expr->b->kind == Expr::Await) {  // x = await E
                    std::string tgt; std::string base = "void"; int ptr = 0;
                    if (s.expr->a->kind == Expr::Ident) {
                        tgt = "_p->" + s.expr->a->text;
                        auto it = localsT.find(s.expr->a->text);
                        if (it != localsT.end()) { base = mapType_(it->second.name); ptr = it->second.ptr; }
                    }
                    emitAwaitPoint(*s.expr->b->a, &tgt, base, ptr, d);
                } else {                                          // 普通语句（printf 等）
                    indent(); emitExpr(*s.expr, true); out << ";\n";
                }
                break;
            case Stmt::ReturnS:
                emitAsyncComplete(s, d);
                break;
            case Stmt::IfS: {
                indent(); out << "if (";
                emitScalarized(*s.expr);
                out << ") {\n";
                depth++; emitAsyncStmtList(s.body, d); depth--;
                indent(); out << "}";
                if (!s.elseBody.empty()) {
                    out << " else {\n";
                    depth++; emitAsyncStmtList(s.elseBody, d); depth--;
                    indent(); out << "}\n";
                } else out << "\n";
                break;
            }
            case Stmt::WhileS:
                indent(); out << "while (";
                emitScalarized(*s.expr);
                out << ") {\n";
                depth++; emitAsyncStmtList(s.body, d); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::DoWhileS:
                indent(); out << "do {\n";
                depth++; emitAsyncStmtList(s.body, d); depth--;
                indent(); out << "} while (";
                emitScalarized(*s.expr);
                out << ");\n";
                break;
            case Stmt::ForS:
                if (s.forIn) {
                    if (hasAwaitDeep(s))
                        throw CompileError{"async rpc 暂不支持 for-in 循环体内 await；"
                                           "请改用 while 循环", s.line};
                    emitStmt(s);   // 无 await：按同步路径发出
                    break;
                }
                indent(); out << "for (";
                if (s.forInit) emitExpr(*s.forInit, true);
                out << "; ";
                if (s.forCond) emitExpr(*s.forCond, true);
                out << "; ";
                if (s.forStep) emitExpr(*s.forStep, true);
                out << ") {\n";
                depth++; emitAsyncStmtList(s.body, d); depth--;
                indent(); out << "}\n";
                break;
            case Stmt::BreakS:
                indent(); out << "break;\n";
                break;
            case Stmt::ContinueS:
                indent(); out << "continue;\n";
                break;
            default:
                // for-in/case 及其它语句：含 await 则明确报错（绝不静默丢弃）；否则按同步路径发出。
                if (hasAwaitDeep(s))
                    throw CompileError{"async rpc 暂不支持该语句内 await（仅 if/while/do-while/"
                                       "经典 for 支持嵌套 await）", s.line};
                emitStmt(s);
                break;
        }
    }


    // 启动器：future* X__async(参数...) { 建帧 + 造 _ret + 装参 + 首次驱动 → 返回 _ret }
    void emitAsyncLauncher(const Decl& d) {
        if (shouldStaticize(d)) out << "static ";
        out << "sc_future *" << scPfx(d.name) << "__async(";
        emitParams(d.structCommon.fields, d.structCommon.variadic);
        out << ") {\n";
        depth++;
        indent(); out << "struct " << scPfx(d.name) << " *_p = (struct " << scPfx(d.name)
                      << " *)sc_chunk0(sizeof(struct " << scPfx(d.name) << "));\n";
        indent(); out << "_p->_state = 0;\n";
        indent(); out << "_p->_ret = sc_future_new();\n";
        for (auto& f : d.structCommon.fields) {
            const std::string arg = (f.name == "this" ? "_this" : f.name);
            indent(); out << "_p->" << f.name << " = " << arg << ";\n";
        }
        indent(); out << scPfx(d.name) << "_rpc(_p);\n";
        indent(); out << "return _p->_ret;\n";
        depth--;
        out << "}\n\n";
    }

    // 异步 rpc：启动器 + 状态机两段定义。
    void emitAsyncRpc(const Decl& d) {
        std::vector<const Field*> locals = collectAsyncLocals(d);
        localsT.clear(); fnVarsL.clear(); varDimsL.clear();
        inFunc = true;
        retDollarDeclared = false;
        for (auto& p : d.structCommon.fields) regVar(p);
        for (auto& f : locals) regVar(*f);
        curRpc = &d;
        rpcParams.clear();
        for (auto& p : d.structCommon.fields) rpcParams.insert(p.name);
        for (auto& f : locals) rpcParams.insert(f->name);

        emitAsyncLauncher(d);

        if (shouldStaticize(d)) out << "static ";
        out << "void " << scPfx(d.name) << "_rpc(struct " << scPfx(d.name) << " *_p) {\n";
        depth++;
        int nstates = countAsyncAwaits(d) + 1;
        indent(); out << "switch (_p->_state) {\n";
        depth++;
        for (int i = 0; i < nstates; i++) { indent(); out << "case " << i << ": goto _s" << i << ";\n"; }
        depth--;
        indent(); out << "}\n";
        indent(); out << "_s0: ;\n";
        asyncState = 0;
        comRpcIdx = 0;
        emitAsyncStmts(d);
        depth--;
        out << "}\n\n";

        curRpc = nullptr;
        rpcParams.clear();
        inFunc = false;
    }

    //   inc stdio.h    → #include <stdio.h>
    //   inc "my.h"     → #include "my.h"
    //   inc <stdio.h>  → #include <stdio.h>（原样）
    // platform.h 已带入的标准头（stdio/stdlib/string 等）会被跳过，避免重复引入。
    void emitInclude(const Decl& d) {
        const std::string& h = d.name;
        if (endsWith(h, ".sc")) {
            const std::string key = d.origin.empty() ? h : d.origin;
            // 带手写 C ABI 头的子项目模块（<root>/<name>/<name>.sc + 同目录 <name>.h，
            // 如 builtins 的 adt/io）：直接引用其手写头，路径含根目录名以明确归属
            // （<root>/<name>/<name>.h，如 "builtins/adt/adt.h"），随 -I <root的上级>
            // 可见，无需生成/复制内部 scm_<token>.h。
            const std::filesystem::path p(key);
            const std::string stem = p.stem().string();
            if (p.has_parent_path() && p.parent_path().filename() == stem &&
                std::filesystem::exists(p.parent_path() / (stem + ".h"))) {
                // 相对项目根的模块头路径（含分组，如 "builtins/adt/adt.h"、
                // "templates/.scenv/modules/wsi/wsi.h"），随 -I <项目根> 可见；支持任意分组层级。
                out << "#include \"" << headerBackedPath(p.parent_path(), stem)
                    << "\"\n";
                return;
            }
            out << "#include \"" << moduleHeaderName(key) << "\"\n";
            return;
        }
        // platform.h 已带入的标准 C 头：跳过（避免与文件头部 #include "platform.h" 重复）
        {
            static const std::unordered_set<std::string> kPlatformHdrs = {
                "stdint.h", "stddef.h", "stdbool.h", "stdarg.h",
                "stdio.h", "stdlib.h", "string.h", "time.h",
                "assert.h", "inttypes.h",
            };
            std::string bare = h;
            if (!bare.empty() && (bare.front() == '<' || bare.front() == '"'))
                bare = bare.substr(1, bare.size() - 2);
            if (kPlatformHdrs.count(bare)) return;
        }
        if (!h.empty() && (h[0] == '"' || h[0] == '<')) out << "#include " << h << "\n";
        else out << "#include <" << h << ">\n";
    }

    // 为所有结构/联合生成前置 typedef 声明：
    //   typedef struct X X;
    //   typedef union U U;
    // 目的：让结构/函数定义顺序与源码解耦，先使用再定义也可通过 C 编译。
    void emitForwardAggrDecls(bool exportedOnly = false) {
        std::unordered_set<std::string> emitted;
        for (auto& d : prog.decls) {
            if (d->kind != Decl::StructD && d->kind != Decl::UnionD) continue;
            if (exportedOnly && !d->exported) continue;
            if (d->external) continue;  // 外部模块类型由其模块头提供
            // 头支撑单元：自身 @导出聚合的前置/完整声明均由手写头提供，不重复前向声明。
            //   op 单元：自身全部聚合（含非导出机制类型）均由 op.h 提供，整体跳过。
            if (unitOpModule) continue;
            if (unitHeaderBacked && d->exported) continue;
            // 泛型自包含实例聚合：头文件模式下前向 typedef 由 generic.h 提供，避免重复。
            if (!genericHeaderName.empty() && d->genericInst && isSelfContainedInstance(*d))
                continue;
            if (!emitted.insert(d->name).second) continue;
            // 标签联合展开为 struct（带 tag + union），前向声明须与定义一致用 struct
            const bool asUnion = d->kind == Decl::UnionD && !d->tagged;
            // 本函数在类型注册表（aggrs）填充前即被调用，mapType_ 尚无法识别为 sc 聚合；
            // 此处 decls 均为非外部的 sc 域聚合，直接 scPfx 加前缀。
            out << "typedef " << (asUnion ? "union" : "struct")
                << " " << scPfx(d->name) << " " << scPfx(d->name) << ";\n";
        }
        if (!emitted.empty()) out << "\n";
    }

    // ---------------- 主流程：两遍扫描输出 ----------------
    // 第一遍：类型定义 + 全局变量 + 函数原型声明（forward declaration）
    // 第二遍：函数体实现
    // 这样做的目的是支持函数间的任意引用顺序（包括递归/互递归）

    // 模块 token：取路径文件名 stem（去最后扩展名），净化为合法 C 标识符片段。
    // 不可用绝对路径（golden 快照须可移植；emit-c 模式 srcFile 为相对路径而项目模式为
    // canonical）——stem 在「引用方（inc 名）」与「被引方（自身 srcFile）」两侧一致。
    // 空输入（stdout emit-c 无 srcFile）返回空串：此时本单元不产出自身 sc_mod 定义。
    static std::string modStemToken(const std::string& raw) {
        if (raw.empty()) return "";
        std::string s = raw;
        auto isDelim = [](char c){ return c == '"' || c == '<' || c == '>' || c == '\''; };
        while (!s.empty() && isDelim(s.front())) s.erase(s.begin());
        while (!s.empty() && isDelim(s.back())) s.pop_back();
        std::string stem = std::filesystem::path(s).stem().string();
        std::string t;
        for (unsigned char c : stem) t += std::isalnum(c) ? (char)c : '_';
        return t;
    }

    // 采集本单元生命周期信息：入口判定、直接 .sc 依赖、自有全局对象 init/drop。
    // 须在 methods/aliases 注册表填充后调用（findMethod 依赖之）。
    void collectLifecycle() {
        for (auto& d : prog.decls)
            if (d->kind == Decl::FuncD && !d->external && !d->isRpc && d->name == "main")
                isEntryUnit = true;
        // 测试目标：合成 runner main 取代用户 main，本单元即入口（不再产出自身 sc_mod_*）。
        if (g_testMode) isEntryUnit = true;
        modToken = modStemToken(srcFile);

        auto endsSc = [](const std::string& s) {
            return s.size() >= 3 && s.compare(s.size() - 3, 3, ".sc") == 0;
        };
        std::set<std::string> seen;
        for (auto& d : prog.decls) {
            if (d->kind != Decl::IncD || !endsSc(d->origin)) continue;
            std::string tok = modStemToken(d->name);
            if (tok == modToken) continue;              // 不自调
            if (seen.insert(tok).second) depTokens.push_back(tok);
        }

        // 自有全局对象：仅可变（var，非 let/tls/const）标量结构变量。
        //   init：类型有零参 init 方法 → 注入构造（不论是否显式初值）。显式初值先按聚合
        //         初始化赋字段，init 随后作为构造钩子运行，语义同 C++ 成员初值 + 构造体。
        //   drop：类型有 drop 方法 → 注入析构（不论是否显式初值，对象存在即析构）。
        for (auto& d : prog.decls) {
            if (d->external || d->kind != Decl::VarD) continue;
            // tok 句柄（isTok VarD）：登记 tok_bind 绑定（句柄本身为 tok* 指针，不入 gInits）。
            if (d->isTok && !d->structCommon.fields.empty()) {
                tokBinds.push_back({d->structCommon.fields.front().name, d->tokId, d->tokFn});
                continue;
            }
            for (auto& f : d->structCommon.fields) {
                if (f.type.ptr != 0 || !f.type.arrayDims.empty() || f.type.hasInline
                    || f.type.fat || f.type.project
                    || f.type.fnKind != TypeRef::FncKind::None)
                    continue;
                // 全局 cls 实例：登记 _class 安装（早于 init）。
                if (const Decl* cd = aggrOf(f.type.name); cd && cd->isClass)
                    gClassInstalls.push_back({scPfx(f.name), mapType_(cd->name)});
                const Decl* im = findMethod(f.type.name, "init");
                if (im && im->structCommon.fields.empty())
                    gInits.push_back({scPfx(f.name), scPfx(im->name),
                                      typeHasFatMember(f.type.name) ? "SC_OWN_RAW" : ""});
                const Decl* dm = findMethod(f.type.name, "drop");
                if (dm) gDrops.push_back({scPfx(f.name), scPfx(dm->name), ""});
            }
        }
        // dep 依赖关系：解析每个依赖项 id → 已绑定 tok 句柄变量名（同单元内声明序在前）。
        {
            std::unordered_map<std::string, std::string> idToVar;
            for (auto& tb : tokBinds) idToVar[tb.id] = tb.var;
            for (auto& d : prog.decls) {
                if (d->external || d->kind != Decl::DepD) continue;
                DepReg dr;
                dr.all = d->depAll;
                dr.loop = d->depLoop;
                dr.fn = d->tokFn;
                for (auto& it : d->depItems) {
                    auto m = idToVar.find(it.second);
                    if (m == idToVar.end())
                        throw CompileError{"dep 依赖项 \"" + it.second +
                            "\" 未声明对应 tok（同单元内须先 tok 声明）", d->line};
                    dr.vars.push_back(m->second);
                }
                for (auto& it : d->depTargets) {         // map 目标项：同样须为同单元已声明 tok
                    auto m = idToVar.find(it.second);
                    if (m == idToVar.end())
                        throw CompileError{"dep map 目标 \"" + it.second +
                            "\" 未声明对应 tok（同单元内须先 tok 声明）", d->line};
                    dr.targetVars.push_back(m->second);
                }
                depRegs.push_back(std::move(dr));
            }
        }
        // 图烘焙（baked constants）：对 dep…map 边建有向图，调 builtins/tok/graph 规范接口算每个
        //   token 的依赖图深度（源=0，多前驱取最长路），回填 tokBinds[i].depth。结果在
        //   emitTokRegistrations 中作为字面量常量随 token_set_depth 写入——运行时零图遍历
        //   （lightmap 式预计算：编译期算贵的，运行时只查表）。无 map 的 token 深度恒 0。
        {
            std::unordered_map<std::string, int> vidx;   // token id → 顶点编号（首见序）
            std::vector<int> eu, ev;                     // 边表：eu[i] -> ev[i]
            auto vid = [&](const std::string& id) -> int {
                auto it = vidx.find(id);
                if (it != vidx.end()) return it->second;
                int n = (int)vidx.size();
                vidx.emplace(id, n);
                return n;
            };
            for (auto& d : prog.decls) {
                if (d->external || d->kind != Decl::DepD || d->depTargets.empty()) continue;
                if (d->depLoop) continue;                // loop 边成环：排除出 depth 图（否则整图退化全 0）
                for (auto& s : d->depItems)
                    for (auto& t : d->depTargets) {
                        eu.push_back(vid(s.second));
                        ev.push_back(vid(t.second));
                    }
            }
            if (!eu.empty()) {
                sc_graph g;
                g.nv = (int)vidx.size();
                g.ne = (int)eu.size();
                g.eu = eu.data();
                g.ev = ev.data();
                std::vector<int> depth((size_t)g.nv, 0);
                sc_graph_depth(&g, depth.data());        // 环已由语义分析拦截，此处必为 DAG
                std::vector<int> crit((size_t)g.nv, 0), slack((size_t)g.nv, 0);
                sc_graph_critical(&g, crit.data(), slack.data());  // 关键路径 + 松弛（同一 map DAG）
                std::vector<int> indeg((size_t)g.nv, 0), outdeg((size_t)g.nv, 0);
                sc_graph_degree(&g, indeg.data(), outdeg.data());  // 扇入/扇出度（枢纽识别）
                std::vector<int> reach((size_t)g.nv, 0);
                sc_graph_reach(&g, reach.data());        // 可达下游数（脏标记影响范围）
                std::vector<int> bwidth((size_t)g.nv, 0);
                sc_graph_batches(&g, nullptr, bwidth.data(), nullptr);  // 拓扑波次并行宽度（接 MT）
                std::vector<int> ckpt((size_t)g.nv, 0), domsz((size_t)g.nv, 0);
                sc_graph_dominators(&g, ckpt.data(), domsz.data());     // 支配检查点 + 子树规模（缓存边界）
                for (auto& tb : tokBinds) {
                    auto it = vidx.find(tb.id);
                    if (it == vidx.end()) continue;
                    size_t v    = (size_t)it->second;
                    tb.inMap    = true;
                    tb.depth    = depth[v];
                    tb.critical = crit[v];
                    tb.slack    = slack[v];
                    tb.fanin    = indeg[v];
                    tb.fanout   = outdeg[v];
                    tb.reach    = reach[v];
                    tb.batchWidth = bwidth[v];
                    tb.checkpoint = ckpt[v];
                    tb.domSize  = domsz[v];
                }
            }
        }
        // SCC 烘焙（baked constants）：对 dep loop 受控反馈环的 源→目标 边建有向图，调 sc_graph_scc
        //   （Tarjan）算每个 token 的强连通簇编号 + 簇大小，回填 tokBinds[i].sccId/sccSize。多顶点簇
        //   （或自环）即反馈簇——结果随 token_set_scc 烘焙为字面量，运行时 token_loop_run 据此 O(1) 驱动
        //   迭代。loop 边走此独立路径，与 all/any/map 的 depth 烘焙互不干扰。
        {
            std::unordered_map<std::string, int> vidx;   // token id → 顶点编号（首见序，仅 loop 边参与）
            std::vector<std::string> vids;               // 顶点 → token id
            std::vector<int> eu, ev;                     // loop 边表：eu[i] -> ev[i]
            auto vid = [&](const std::string& id) -> int {
                auto it = vidx.find(id);
                if (it != vidx.end()) return it->second;
                int n = (int)vidx.size();
                vidx.emplace(id, n);
                vids.push_back(id);
                return n;
            };
            for (auto& d : prog.decls) {
                if (d->external || d->kind != Decl::DepD || !d->depLoop) continue;
                for (auto& s : d->depItems)
                    for (auto& t : d->depTargets) {
                        int u = vid(s.second), v = vid(t.second);
                        eu.push_back(u);
                        ev.push_back(v);
                    }
            }
            if (!eu.empty()) {
                sc_graph g;
                g.nv = (int)vidx.size();
                g.ne = (int)eu.size();
                g.eu = eu.data();
                g.ev = ev.data();
                std::vector<int> comp((size_t)g.nv, 0);
                int ncomp = 0;
                sc_graph_scc(&g, comp.data(), &ncomp);
                std::vector<int> compSize((size_t)ncomp, 0);   // 各簇成员数
                for (int v = 0; v < g.nv; v++) compSize[(size_t)comp[v]]++;
                // 自环（源==目标）令该单点簇也成反馈：标记成员数 < 2 但有自环的簇为反馈（簇大小补到 2）
                std::vector<char> compSelf((size_t)ncomp, 0);
                for (int e = 0; e < g.ne; e++)
                    if (eu[(size_t)e] == ev[(size_t)e]) compSelf[(size_t)comp[eu[(size_t)e]]] = 1;
                for (auto& tb : tokBinds) {
                    auto it = vidx.find(tb.id);
                    if (it == vidx.end()) continue;
                    int c = comp[(size_t)it->second];
                    int sz = compSize[(size_t)c];
                    if (sz < 2 && compSelf[(size_t)c]) sz = 2;  // 自环单点簇也视为反馈
                    tb.sccId = c;
                    tb.sccSize = sz;
                }
            }
        }
        mainHasTeardown = isEntryUnit && (!gDrops.empty() || !depTokens.empty());
    }

    // 第一遍原型区：依赖模块 sc_mod_* 前向声明 + 本库模块自身 sc_mod_* 前向声明。
    // 库模块自身 sc_mod 仅在有真实模块 token（srcFile 非空）时产出。
    void emitLifecycleDecls() {
        bool any = false;
        for (auto& t : depTokens) {
            out << "void sc_mod_" << t << "_init(void); void sc_mod_" << t << "_drop(void);\n";
            any = true;
        }
        if (!isEntryUnit && !modToken.empty()) {
            out << "void sc_mod_" << modToken << "_init(void); void sc_mod_"
                << modToken << "_drop(void);\n";
            any = true;
        }
        for (auto& dr : depRegs) {                       // dep follow 蹦床前向声明（main/init 引用在前）
            const std::string fn = scPfx(dr.fn);
            std::string tramp = fn.substr(0, fn.size() - 7) + "_tramp";
            out << "static int " << tramp << "(sc_token **, int, int, void *);\n";
            any = true;
        }
        if (any) out << "\n";
    }

    // 库模块（无 main）：定义 sc_mod_<token>_init/drop。幂等（菱形依赖只执行一次）：
    //   init：先递归各直接依赖 init，再构造自有全局（源序）。
    //   drop：先析构自有全局（逆序），再递归各直接依赖 drop（逆序）。
    void emitLifecycleDefs() {
        if (isEntryUnit || modToken.empty()) return;
        out << "void sc_mod_" << modToken << "_init(void) {\n";
        out << "    static int _done = 0; if (_done) return; _done = 1;\n";
        // 退化路径：本模块启动钩子（栈哨兵填充）须先于自有全局构造。
        if (hasGcanaryHook()) {
            out << "#if !SC_HAVE_AUTO_HOOKS\n";
            out << "    __sc_gcanary_init();\n";
            out << "#endif\n";
        }
        for (auto& t : depTokens) out << "    sc_mod_" << t << "_init();\n";
        for (auto& ci : gClassInstalls) out << "    " << ci.first << "._class = " << ci.second << "_hyper_impl;\n";
        for (auto& g : gInits)    out << "    " << g.fn << "(&" << g.var << (g.ownArg.empty() ? "" : ", " + g.ownArg) << ");\n";
        emitTokRegistrations("    ");
        out << "}\n";
        out << "void sc_mod_" << modToken << "_drop(void) {\n";
        out << "    static int _done = 0; if (_done) return; _done = 1;\n";
        for (auto it = gDrops.rbegin(); it != gDrops.rend(); ++it)
            out << "    " << it->fn << "(&" << it->var << ");\n";
        for (auto it = depTokens.rbegin(); it != depTokens.rend(); ++it)
            out << "    sc_mod_" << *it << "_drop();\n";
        // 退化路径：本模块退出钩子（哨兵校验 / 全局 T@ 拆边）最后运行。
        if (hasGcanaryHook() || hasGfatHook()) {
            out << "#if !SC_HAVE_AUTO_HOOKS\n";
            if (hasGcanaryHook()) out << "    __sc_gcanary_fini();\n";
            if (hasGfatHook())    out << "    __sc_gfat_fini();\n";
            out << "#endif\n";
        }
        out << "}\n\n";
    }

    // main 序言：递归各直接依赖模块 init，再构造入口自有全局（源序）。
    void emitMainPrologue() {
        // 控制台 UTF-8：须先于任何输出——Windows 控制台默认代码页为本地 ANSI（简体中文=
        //   GBK/936），而 sc 输出一律 UTF-8 字节，不切码页则中文乱码（非 Windows 空操作）。
        indent(); out << "SC_CONSOLE_UTF8();\n";
        // 退化路径：栈哨兵填充须先于任何全局构造（捕获构造期溢出）。
        if (hasGcanaryHook()) {
            out << "#if !SC_HAVE_AUTO_HOOKS\n";
            indent(); out << "__sc_gcanary_init();\n";
            out << "#endif\n";
        }
        for (auto& t : depTokens) { indent(); out << "sc_mod_" << t << "_init();\n"; }
        for (auto& ci : gClassInstalls) { indent(); out << ci.first << "._class = " << ci.second << "_hyper_impl;\n"; }
        for (auto& g : gInits)    { indent(); out << g.fn << "(&" << g.var << (g.ownArg.empty() ? "" : ", " + g.ownArg) << ");\n"; }
        std::string pad((size_t)depth * 4, ' ');
        emitTokRegistrations(pad);
    }

    // 分布式 token 注册：tok_bind 绑定各句柄（combine 回调或 NULL），随后 tok_depend
    //   注册各依赖关系（门逻辑 + follow 蹦床）。绑定先于依赖（依赖引用已绑定句柄）。
    void emitTokRegistrations(const std::string& pad) {
        for (auto& tb : tokBinds) {
            const std::string v = scPfx(tb.var);
            out << pad << v << " = sc_token_bind(\"";
            for (char c : tb.id) {                       // 最小转义：引号 / 反斜杠
                if (c == '"' || c == '\\') out << '\\';
                out << c;
            }
            out << "\", " << (tb.fn.empty() ? "NULL" : "(sc_token_combine)" + scPfx(tb.fn)) << ");\n";
            if (tb.depth)                                // 烘焙：依赖图深度（编译期常量）写入句柄；深度 0（源/非图）省略
                out << pad << "sc_token_set_depth(" << v << ", " << tb.depth << ");\n";
            if (tb.slack >= 0)                           // 烘焙：关键路径标志 + 松弛（编译期常量）；仅在 map DAG 中的 token（slack>=0）发出
                out << pad << "sc_token_set_crit(" << v << ", " << tb.critical << ", " << tb.slack << ");\n";
            if (tb.inMap) {                              // 烘焙：map DAG 图度量（编译期常量）——扇入扇出 / 影响范围 / 波次宽度 / 支配检查点
                out << pad << "sc_token_set_degree(" << v << ", " << tb.fanin << ", " << tb.fanout << ");\n";
                out << pad << "sc_token_set_reach(" << v << ", " << tb.reach << ");\n";
                out << pad << "sc_token_set_batch(" << v << ", " << tb.batchWidth << ");\n";
                out << pad << "sc_token_set_dom(" << v << ", " << tb.checkpoint << ", " << tb.domSize << ");\n";
            }
            if (tb.sccSize > 1)                          // 烘焙：SCC 反馈簇划分（编译期常量）写入句柄；非反馈（簇大小≤1）省略
                out << pad << "sc_token_set_scc(" << v << ", " << tb.sccId << ", " << tb.sccSize << ");\n";
        }
        for (size_t i = 0; i < depRegs.size(); ++i) {
            auto& dr = depRegs[i];
            const std::string fn = scPfx(dr.fn);
            std::string tramp = fn.substr(0, fn.size() - 7) + "_tramp";  // _follow → _tramp
            if (dr.targetVars.empty()) {                 // 无 map 目标：原 token_depend（ABI 不变）
                out << pad << "{ sc_token *_deps" << i << "[] = {";
                for (size_t j = 0; j < dr.vars.size(); ++j)
                    out << (j ? ", " : " ") << scPfx(dr.vars[j]);
                out << " }; sc_token_depend(_deps" << i << ", " << dr.vars.size() << ", "
                    << (dr.all ? 1 : 0) << ", " << tramp << ", NULL); }\n";
            } else {                                     // map/loop：_deps = 源 ++ 目标；token_depend_map / token_depend_loop
                out << pad << "{ sc_token *_deps" << i << "[] = {";
                bool first = true;
                for (auto& v : dr.vars)        { out << (first ? " " : ", ") << scPfx(v); first = false; }
                for (auto& v : dr.targetVars)  { out << (first ? " " : ", ") << scPfx(v); first = false; }
                out << " }; " << (dr.loop ? "sc_token_depend_loop" : "sc_token_depend_map")
                    << "(_deps" << i << ", " << dr.vars.size() << ", "
                    << dr.targetVars.size() << ", " << (dr.all ? 1 : 0) << ", " << tramp << ", NULL); }\n";
            }
        }
    }

    // dep follow 蹦床：把运行时 token_follow 通用签名打包为 follow 体的 dep_in& 上下文
    //   （toks=依赖项数组 / count=个数 / active=触发动作码），调用合成的 follow 函数
    //   （返回 bool→下次门逻辑）。前向声明在 emitLifecycleDecls 产出。
    //   C 类型名经 mapType_ 由 sc 名（token/dep_in）加 sc_ 前缀，与 op.h 定义对齐。
    void emitDepTrampolines() {
        const std::string cTok = mapType_("token");
        const std::string cDep = mapType_("dep_in");
        for (auto& dr : depRegs) {
            const std::string fn = scPfx(dr.fn);
            std::string tramp = fn.substr(0, fn.size() - 7) + "_tramp";
            out << "static int " << tramp << "(" << cTok << " **_ts, int _n, int _acting, void *_ctx) {\n";
            out << "    " << cDep << " _self; _self.toks = _ts; _self.count = _n; _self.active = _acting; _self.ctx = _ctx;\n";
            out << "    return (int)" << fn << "(&_self);\n}\n";
        }
        if (!depRegs.empty()) out << "\n";
    }
    // main 尾声：析构入口自有全局（逆序），再递归各直接依赖模块 drop（逆序）。
    // 由 emitScopeCleanupAt（i==0 且 inMainFunc）在所有现有清理 phase 之后发出。
    void emitMainEpilogue() {
        for (auto it = gDrops.rbegin(); it != gDrops.rend(); ++it) {
            indent(); out << it->fn << "(&" << it->var << ");\n";
        }
        for (auto it = depTokens.rbegin(); it != depTokens.rend(); ++it) {
            indent(); out << "sc_mod_" << *it << "_drop();\n";
        }
        // 退化路径：进程退出钩子（哨兵校验 / 全局 T@ 拆边）在所有用户析构之后运行。
        if (hasGcanaryHook() || hasGfatHook()) {
            out << "#if !SC_HAVE_AUTO_HOOKS\n";
            if (hasGcanaryHook()) { indent(); out << "__sc_gcanary_fini();\n"; }
            if (hasGfatHook())    { indent(); out << "__sc_gfat_fini();\n"; }
            out << "#endif\n";
        }
    }

    // 退化路径（platform.h SC_HAVE_AUTO_HOOKS==0）：本单元是否产出对应启动/退出钩子。
    bool hasGcanaryHook() const { return g_memCheck && !globalCanaries.empty(); }
    bool hasGfatHook()    const { return !globalFatVars.empty(); }

    // 退化路径：钩子函数前置声明（具名 static，定义在文件末尾），供 main / sc_mod_* 显式
    // 调用。以 #if !SC_HAVE_AUTO_HOOKS 包裹——GCC/Clang/MSVC 由平台机制自动触发，不产出。
    void emitHookFwdDecls() {
        if (!hasGcanaryHook() && !hasGfatHook()) return;
        out << "#if !SC_HAVE_AUTO_HOOKS\n";
        if (hasGcanaryHook()) {
            out << "static void __sc_gcanary_init(void);\n";
            out << "static void __sc_gcanary_fini(void);\n";
        }
        if (hasGfatHook()) out << "static void __sc_gfat_fini(void);\n";
        out << "#endif\n\n";
    }

    std::string run() {
        // 头支撑单元自检：srcFile 形如 <root>/<stem>/<stem>.sc 且同目录有手写 <stem>.h。
        if (!srcFile.empty()) {
            std::filesystem::path p(srcFile);
            const std::string stem = p.stem().string();
            if (endsWith(p.string(), ".sc") && p.has_parent_path() &&
                p.parent_path().filename() == stem &&
                std::filesystem::exists(p.parent_path() / (stem + ".h"))) {
                // 引用手写头的相对项目根路径（如 "builtins/adt/adt.h"、
                // "templates/.scenv/modules/wsi/wsi.h"），随 -I <项目根> 可见；支持任意分组层级。
                headerBackedInclude = headerBackedPath(p.parent_path(), stem);
                unitHeaderBacked = true;
            }
            // op：默认导入的语言运行时模块（builtins/op.sc + 同目录 op.h），其手写头
            //   op.h 已由 platform.h 默认带入，故按头支撑单元处理（跳过自身 @导出类型内联，
            //   由 op.h 提供）——但无需再发 #include（platform.h 已含），headerBackedInclude 留空。
            else if (stem == "op" && p.has_parent_path() &&
                     std::filesystem::exists(p.parent_path() / "op.h")) {
                unitOpModule = true;       // op 全部自有类型/原型均由 op.h 提供，整体跳过
            }
        }

        // 标准 C 头统一由 builtins/platform.h 提供（该目录默认在 -I 路径），
        // 同时带入 TLS 宏等跨平台适配
        out << "/* 由 scc 生成，请勿手工修改 */\n"
            << "#include \"platform.h\"\n";

        // 头支撑单元：紧随 platform.h 引入本模块手写 C ABI 头，提供本单元 @导出类型的
        // 唯一定义（下方第一遍跳过其内联），并带入仅头部宏供拼接的 <stem>_impl.c 使用。
        // op 例外：其 op.h 已由 platform.h 默认带入，headerBackedInclude 留空、不重复发出。
        if (unitHeaderBacked && !headerBackedInclude.empty())
            out << "#include \"" << headerBackedInclude << "\"\n";


        // future<ID> 聚合枚举 future_id：头文件模式下由独立 type.h 提供，.c 在此 #include
        //（无依赖，置于最前）。内联模式则随 decls 就地输出，无需此包含。
        if (!typeHeaderName.empty() && !prog.futureIds.empty())
            out << "#include \"" << typeHeaderName << "\"\n";

        // cls/dim 全局类型与选择子：头文件模式下由独立 class.h 提供（tril/sc_hyper/object
        //   类型 + SC_CLS_/SC_DIM_ 跨单元一致编号）。须先于用户 inc 头引入，使引用 _class
        //   字段/object 形参的模块头可见这些类型。内联模式则在运行时序言就地输出。
        if (!classHeaderName.empty() && computeUsesClassRuntime())
            out << "#include \"" << classHeaderName << "\"\n";

        // 泛型实例自包含类型：头文件模式下由工程级 generic.h 提供（跨单元去重、一致），
        //   须先于用户 inc 头引入，使引用实例类型（含按值/指针）的模块头可见其定义/前向。
        //   内联模式则随 decls 就地输出。
        if (!genericHeaderName.empty() && unitHasGenericInst())
            out << "#include \"" << genericHeaderName << "\"\n";

        // 用户 inc 引入的头文件
        for (auto& d : prog.decls)
            if (d->kind == Decl::IncD) emitInclude(*d);
        // 根模块导出接口头：作为末位 include（集成单元全局定义/操作对本依赖单元可见）
        if (!rootPreludeHeader.empty())
            out << "#include \"" << rootPreludeHeader << "\"\n";
        out << "\n";

        // 最简策略：默认为所有结构/联合输出前置声明，消除定义顺序依赖
        emitForwardAggrDecls();

        // 收集函数类型、聚合类型、顶层函数与方法注册表（含外部模块合并声明，供语法糖识别）
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            else if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD) aliases[d->name] = d->structCommon.type->name;
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
            else if ((d->kind == Decl::FuncD ||
                      (d->kind == Decl::FuncTypeD && d->cImpl)) && !d->isRpc)
                funcs[d->name] = d.get();  // 顶层函数（含 @fnc name:: C 实现自由函数）：
                                           // 缺参补全查签名 + 调用点走 sc 域前缀（与 emitFuncSig
                                           // 原型、impl.c 的 sc_ 前缀一致）
            if (d->isRpc) rpcs[d->name] = d.get();  // run 语句目标查询
        }

        // 静态全局对象 / 模块级生命周期采集（依赖 methods/aliases 注册表已填充）
        collectLifecycle();

        // 类机制采集：cls 类、各 dim（分配全局选择子，源序）。
        for (auto& d : prog.decls) {
            if (d->kind == Decl::StructD && d->isClass) {
                classNames.insert(d->name);
                if (!d->external) classDecls.push_back(d.get());
                else externalClassNames.insert(d->name);
            }
        }
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncD && d->isDim) {
                if (d->methodName == "CLS_ID")
                    throw CompileError{"CLS_ID 维度由编译器生成，不可自定义", d->line};
                classDims[d->methodOwner].push_back(d.get());
                (void)dimId(d->methodName);    // 注册全局选择子（保留名映射到 0/1/2）
            }
        }
        usesClassRt = !classDecls.empty() || unitUsesClassTypes();

        // 单元测试目标：收集 tst 用例并分配 C 函数名（源序）。
        if (g_testMode) {
            int i = 0;
            for (auto& d : prog.decls)
                if (d->kind == Decl::TestD && !d->external)
                    testCases.push_back({d.get(),
                        "sc_test_" + (modToken.empty() ? std::string("u") : modToken)
                        + "_" + std::to_string(i++)});
        }

        // 预扫描 T() 伪调用（仅本单元代码，外部合并声明不扫），顺带标记 run/print/string
        heapNews.clear();
        for (auto& d : prog.decls) {
            if (d->external) continue;
            for (auto& f : d->structCommon.fields) if (f.init) scanExprForNew(*f.init);
            for (auto& s : d->body) scanStmtForNew(*s);
        }

        // 枚举类型名集合（string 格式化按整数）
        for (auto& d : prog.decls)
            if (d->kind == Decl::EnumD) {
                enums.insert(d->name);
                for (auto& f : d->structCommon.fields) enumConsts.insert(f.name);
            }

        // sc 域宏名集合（与 emitMacroDef 发 #define 的范围一致：非 :: 桥接、非泛型模板）：
        //   引用/调用处经 scPfx 加前缀，与其 #define 名对齐。
        for (auto& d : prog.decls)
            if (d->kind == Decl::MacroD && !d->cImpl && d->macroTypeParams.empty())
                macroNames.insert(d->name);

        // 容器类型名集合：def T: <C, I> 的 C（含外部模块声明），供 t[key,...] → find 糖识别
        for (auto& d : prog.decls)
            if (d->kind == Decl::StructD && !d->adtColl.empty())
                adtColls.insert(resolveAliasName(d->adtColl));

        // 自动指针 T@：收集被当作胖目标的类型名（决定生成 T__new_ref 带头分配辅助）
        for (auto& d : prog.decls) {
            for (auto& f : d->structCommon.fields) noteFatType(f.type);
            if (d->structCommon.type) noteFatType(*d->structCommon.type);
            if (!d->external)
                for (auto& s : d->body) collectFatTypesStmt(*s);
        }

        // print 关键字：已属语言内核（op.sc 默认导入声明 @fnc print::，原型由 op.h
        // 默认带入，运行时 op_impl.c 始终链接），无需 inc io.sc，亦无需在此声明。
        // stringify 格式化关键字：依赖 adt string（返回类型）；选项类型 stringify_t
        if (usesStrof && !funcs.count("stringify")) {
            if (!aggrOf("string"))
                throw CompileError{"stringify(...) 格式化依赖内置 string，请先 inc adt.sc", usesStrof};
        }

        // run 语句线程原语：thread 类型与 thread_run 已属语言内核（op.h 默认带入，
        // op_impl.c 始终链接），无需在此声明。pool 亦为 op 层接口协议（vtable 见 op.h
        // 默认带入），入池经协议指针派发——语言内核零 emit mt 符号，无需在此声明。

        // ADT 容器（def T: <C, I> {}）接口完备性校验：
        // 容器 C 与元素节点 I 必须已定义，且 C 具备必备成员函数 insert/remove/find/first/next
        // （last/prev 可选）。导航语义经容器方法实现，元素 T 在 C 视角下零偏移重解释为 I。
        for (auto& d : prog.decls) {
            if (d->kind != Decl::StructD || d->adtItem.empty()) continue;
            const std::string sig = " <" + d->adtColl + ", " + d->adtItem + ">";
            const Decl* coll = aggrOf(d->adtColl);
            if (!coll)
                throw CompileError{"ADT 容器类型 " + d->adtColl + " 未定义（def "
                                   + d->name + ":" + sig + "）", d->line};
            if (!aggrOf(d->adtItem))
                throw CompileError{"ADT 元素节点类型 " + d->adtItem + " 未定义（def "
                                   + d->name + ":" + sig + "）", d->line};
            for (const char* req : {"insert", "remove", "find", "first", "next"})
                if (!findMethod(coll->name, req))
                    throw CompileError{"ADT 容器 " + d->adtColl + " 缺少必备成员函数 "
                                       + req + "（def " + d->name + ":" + sig
                                       + " 要求 insert/remove/find/first/next）", d->line};
        }

        // 分身/切片（def T: <S> {}）接口完备性校验：
        // 分身类型 S 必须已定义，且实体 T 须具备 alloc/free 成员函数
        // （alloc: 隐式 this=T*, 余参=切片参数, 返回 S&；free: fnc: S&）。
        for (auto& d : prog.decls) {
            if (d->kind != Decl::StructD || d->projectSelf.empty()) continue;
            const std::string sig = " <" + d->projectSelf + ">";
            if (!aggrOf(d->projectSelf))
                throw CompileError{"分身/切片类型 " + d->projectSelf + " 未定义（def "
                                   + d->name + ":" + sig + "）", d->line};
            for (const char* req : {"alloc", "free"})
                if (!findMethod(d->name, req) && !methodPtrField(d->name, req))
                    throw CompileError{"分身/切片实体 " + d->name + " 缺少必备成员函数 "
                                       + req + "（def " + d->name + ":" + sig
                                       + " 要求 alloc/free）", d->line};
        }

        // 类机制运行时序言（tril/object/sc_hyper 类型 + 全局类 id / 维度选择子枚举）
        emitClassRuntimePrelude();
        // object@ 通用析构蹦床前置声明（fatDtorArg("object") 在函数体内引用，定义在文件末）
        if (usesClassRt) out << "static void sc_obj_drop(void *);\n";
        // 每个 cls 的分派器原型（构造点安装 _class 指针、object 强转、dim 调用均引用之）
        for (auto* c : classDecls)
            out << "tril " << mapType_(c->name) << "_hyper_impl(void *, uint32_t, ...);\n";
        // 外部（其它单元定义）cls 类：分派器在彼单元导出，本单元 extern 引用（跨单元
        //   instanceOf / dim 调用 / object 强转 / _class 安装）。
        for (auto& n : externalClassNames)
            out << "extern tril " << mapType_(n) << "_hyper_impl(void *, uint32_t, ...);\n";
        if (!classDecls.empty() || !externalClassNames.empty()) out << "\n";

        // 第一遍：类型、全局变量、函数原型（外部模块声明不参与输出，由模块头提供）
        for (auto& d : prog.decls) {
            // future_id 聚合枚举：头文件模式下由 type.h 提供（已 #include），不再内联
            if (d->genTypeHeader && !typeHeaderName.empty()) continue;
            if (d->external && d->kind != Decl::IncD) {
                // 外部 @def 结构体由模块头提供；但其分身句柄 T__project 仍需本工程生成。
                if (d->kind == Decl::StructD && !d->projectSelf.empty())
                    emitProjectTypedef(*d);
                // 外部导出的全局变量（如 mod 单例实例）：虽不在本单元发出定义（由模块头
                //   extern 提供），但其类型须登记进 globalsT，跨模块方法调用糖才能解析接收者
                //   类型（instance.method() → Type_method(&instance)）。
                if (d->kind == Decl::VarD || d->kind == Decl::LetD)
                    for (auto& f : d->structCommon.fields) regVar(f);
                continue;
            }
            // 头支撑单元：自身 @导出类型由手写头提供完整定义，跳过内联（避免与拼接的
            // <stem>_impl.c 经手写头带入的定义重复）。分身句柄 T__project 仍需本工程生成。
            if (unitHeaderBacked && d->exported &&
                (d->kind == Decl::StructD || d->kind == Decl::UnionD ||
                 d->kind == Decl::EnumD   || d->kind == Decl::AliasD)) {
                if (d->kind == Decl::StructD && !d->projectSelf.empty())
                    emitProjectTypedef(*d);
                continue;
            }
            // op 单元：自身全部类型/ :: 接口原型均由 op.h（经 platform.h）提供，整体跳过
            //   （含非 @导出的机制内部类型）；分身句柄（如 com__project）仅消费单元需要、
            //   在各消费单元就地生成，op 自身不发出（其 op_impl.c 直接用 op.h 的 limit/com）。
            if (unitOpModule) {
                if (d->kind == Decl::StructD || d->kind == Decl::UnionD ||
                    d->kind == Decl::EnumD   || d->kind == Decl::AliasD)
                    continue;
                if (d->kind == Decl::FuncTypeD && !d->isRpc &&
                    (d->cImpl || !d->methodOwner.empty()))
                    continue;   // :: 接口原型由 op.h 提供
            }
            // 泛型自包含实例类型：头文件模式下由工程级 generic.h 提供（跨单元一致、去重），
            //   本单元跳过内联定义（其成员函数仍随下方 FuncD 以 static 就地发出）。
            //   非自包含实例（按值内嵌用户聚合等）仍内联，generic.h 仅给前向 typedef。
            if (!genericHeaderName.empty() && d->genericInst &&
                isSelfContainedInstance(*d) &&
                (d->kind == Decl::StructD || d->kind == Decl::UnionD ||
                 d->kind == Decl::EnumD   || d->kind == Decl::AliasD))
                continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::StructD: case Decl::UnionD:
                    emitAggrWithDeps(*d);  // 惰性前移按值依赖，定义顺序无关
                    break;
                case Decl::FuncTypeD:
                    // rpc 仅声明：接口三件套，实际函数在外部（C 侧）实现
                    if (d->isRpc) emitRpcInterface(*d, false);
                    // C 实现接口（fnc name:: 或成员 fnc m::）：extern 原型，实现在 C 侧
                    else if (d->cImpl || !d->methodOwner.empty()) {
                        out << "extern ";
                        emitFuncSig(*d); out << ";\n";
                    }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD:
                    emitVarDecls(d->structCommon.fields, false, shouldStaticize(*d));
                    break;
                case Decl::LetD:
                    emitVarDecls(d->structCommon.fields, true, shouldStaticize(*d));
                    break;
                case Decl::TlsD:
                    emitVarDecls(d->structCommon.fields, false, false, true);  // 始终 static TLS
                    break;
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, shouldStaticize(*d)); break; }
                    if (d->isDim) break;  // 维度：折叠进分派器，无独立原型/定义
                    if (d->name != "main") {
                        if (shouldStaticize(*d)) out << "static ";
                        emitFuncSig(*d);
                        out << ";\n";
                    }
                    break;
                case Decl::IncD: break;  // 已在顶部输出
                case Decl::AddD: break;  // 构建指令，不产生 C 输出
                case Decl::TestD: break;  // tst 用例：普通编译忽略；测试模式由第二遍 + runner 处理
                case Decl::MacroD:
                    if (!d->macroTypeParams.empty()) break;  // 泛型宏模板：已单态化为具体声明，不输出 #define
                    if (d->cImpl) break;  // C 宏桥接（def name::）：宏实现在 C 头中，不生成 #define
                    emitMacroDef(*d);    // def 宏 → #define
                    break;
                case Decl::MixD:
                    if (d->macroConsumed) break;  // 泛型 mix：已展开为具体声明，不再输出宏调用
                    emitMixExpand(*d);   // 顶层 mix → 宏调用展开（声明）
                    break;
                case Decl::DepD: break;  // dep 依赖关系：无独立声明，注册经模块 init 注入
            }
        }
        out << "\n";
        // 模块级生命周期前向声明（依赖模块 sc_mod_* + 本库模块自身 sc_mod_*）：
        // 须早于 main 函数体（其序言/尾声调用依赖模块 sc_mod_*）与本模块 sc_mod 定义。
        emitLifecycleDecls();
        // 单元测试运行时（--test）：setjmp 隔离 + TAP 报告辅助，须早于测试函数体。
        if (g_testMode) emitTestRuntime();
        // 堆构造辅助函数（T() 伪调用糖使用）
        emitNewHelpers();
        // 第二遍：函数定义先写入暂存流（string 格式化调用点按需登记格式化请求），
        // 随后回填支撑代码（原语/格式化器/包装）再拼接函数体
        std::ostringstream mainOut = std::move(out);
        out = std::ostringstream();
        for (auto& d : prog.decls) {
            if (d->kind != Decl::FuncD || d->external) continue;
            if (d->isDim) continue;  // 维度：折叠进分派器（emitDispatcher）
            // 测试模式：屏蔽用户 main（由合成 runner main 取代）
            if (g_testMode && !d->isRpc && d->name == "main") continue;
            // 经 add 内联的函数：#line 映射回被 add 的源文件（调试断点/单步落在原 .sc）
            if (!srcFile.empty() && !d->inlinedFrom.empty()) {
                const std::string save = srcFile;
                srcFile = lineSafePath(d->inlinedFrom);
                emitFunc(*d);
                srcFile = save;
            } else {
                emitFunc(*d);
            }
        }
        // 测试模式：tst 用例 → static 测试函数
        if (g_testMode)
            for (auto& tc : testCases) emitTestFunc(*tc.d, tc.cname);
        std::string funcsPart = out.str();
        out = std::move(mainOut);
        emitSofHelpers();
        emitIndexHelpers();      // 容器下标糖 _find__idx 包装（在 find 原型之后、函数体之前）
        emitHookFwdDecls();      // 退化路径：启动/退出钩子前置声明（供 main/sc_mod 显式调用）
        out << lambdaOut.str();   // 提升的匿名函数定义（在函数体前，确保被引用前已定义）
        out << funcsPart;
        emitDepTrampolines();     // dep follow 蹦床定义（在合成 follow 函数体之后）
        // 类机制分派器定义（每个 cls 一个 T_hyper_impl，折叠其全部 dim + 保留维度）
        if (usesClassRt) {
            // object@ 通用析构蹦床：入边归零时 sc_fat_on_zero_d 静态拿不到擦除类型的析构，
            //   故转交派发器 SC_DIM_DROP（其内 container_of 回算 _this 调本类 T_drop）。_p=_class 槽。
            out << "static void sc_obj_drop(void *_p) {\n"
                << "    if (!_p) return;\n"
                << "    sc_hyper _h = *(sc_hyper *)_p;\n"
                << "    if (_h) _h(_p, SC_DIM_DROP);\n"
                << "}\n\n";
        }
        for (auto* c : classDecls) emitDispatcher(*c);
        // 库模块（无 main）：sc_mod_<token>_init/drop 定义（在方法/全局声明之后）。
        emitLifecycleDefs();
        // 测试 runner main（--test）：串起模块 init / 各 tst 用例 / drop，失败数即退出码。
        if (g_testMode) emitTestRunnerMain();
        // --check=mem：全局栈数组尾哨兵的启动填充 / 退出校验钩子（在全局声明之后）。
        if (g_memCheck && !globalCanaries.empty()) {
            auto canaryLen = [](const MemCanaryVar& mc) -> std::string {
                return mc.dims.size() == 1
                    ? "SC_CANARY_ELEMS(" + mc.elemTy + ") * sizeof(" + mc.elemTy + ")"
                    : std::string("SC_CANARY");
            };
            out << "\nSC_CONSTRUCTOR(__sc_gcanary_init) {\n";
            for (auto& mc : globalCanaries)
                out << "    sc_stack_canary_fill((unsigned char*)" << mc.name
                    << " + (" << canaryElems(mc.dims) << ") * sizeof(" << mc.elemTy << "), "
                    << canaryLen(mc) << ", " << mc.name << ");\n";
            out << "}\n";
            out << "SC_DESTRUCTOR(__sc_gcanary_fini) {\n";
            for (auto& mc : globalCanaries)
                out << "    sc_stack_canary_check((unsigned char*)" << mc.name
                    << " + (" << canaryElems(mc.dims) << ") * sizeof(" << mc.elemTy << "), "
                    << canaryLen(mc) << ", " << mc.name << ", \"" << mc.site << "\");\n";
            out << "}\n";
        }
        // 全局 T@（标量/数组）退出清理：进程退出（main 返回后）逐个 / 逐元素拆边，
        // in→0 触发 drop（释放系统资源），与「全局根指针域退出自动拆」语义一致。
        if (!globalFatVars.empty()) {
            out << "\nSC_DESTRUCTOR(__sc_gfat_fini) {\n";
            for (auto& gv : globalFatVars) {
                out << "    ";
                std::string sub;
                for (size_t d = 0; d < gv.dims.size(); d++) {   // 数组：逐维 for 遍历全部标量元素
                    std::string k = "_k" + std::to_string(d);
                    out << "for (size_t " << k << " = (" << gv.dims[d] << "); "
                        << k << "-- > 0; ) ";
                    sub += "[" + k + "]";
                }
                if (gv.dtorArg.empty())
                    out << "sc_fat_unbind(&" << gv.name << sub << ");\n";
                else
                    out << "sc_fat_unbind_d(&" << gv.name << sub << ", " << gv.dtorArg << ");\n";
            }
            out << "}\n";
        }
        return out.str();
    }

    // ---------------- 头文件生成（@导出对象） ----------------
    // 导出类型 → 完整 typedef；导出变量/常量 → extern；导出函数 → 原型
    std::string runHeader(const std::string& guard) {
        bool any = false;
        for (auto& d : prog.decls) if (d->exported && !d->external) { any = true; break; }
        if (!any) return "";

        out << "/* 由 scc 生成，请勿手工修改 —— @导出对象声明 */\n"
            << "#ifndef " << guard << "\n"
            << "#define " << guard << "\n\n"
            << "#include \"platform.h\"\n";

        // 模块若使用类机制，导出的 cls 结构含 _class(sc_hyper) 字段、导出函数可能以 object
        //   为形参，故头文件须先引入 class.h 提供 tril/sc_hyper/object 类型与全局选择子。
        if (computeUsesClassRuntime())
            out << "#include \"class.h\"\n";

        // 模块导出签名可能引用泛型实例类型（如 fnc fill(v: Vec_int&)）。自包含实例完整定义
        //   与全部实例前向 typedef 由工程级 generic.h 提供，头文件引入它即可让导出原型可编译。
        if (unitHasGenericInst())
            out << "#include \"generic.h\"\n";
        out << "\n";

        // 头文件同样先输出导出结构/联合的前置声明，减少声明顺序耦合
        emitForwardAggrDecls(true);

        // 类型注册表：导出签名可能引用聚合/别名/枚举/类/函数类型（含跨模块 external 类型，
        //   如 op 的 token）。须在发射签名前填充，令 mapType_ 正确加 sc_ 前缀——否则 external
        //   类型走「外部 C 类型」透传落裸名（如 token），与其模块头的 sc_ 前缀定义（sc_token）
        //   不符，消费单元编译报「unknown type name 'token'」。与 run() 的注册表填充对齐。
        for (auto& d : prog.decls) {
            if (d->kind == Decl::FuncTypeD && !d->isRpc && !d->cImpl && d->methodOwner.empty())
                funcTypes[d->name] = d.get();
            else if (d->kind == Decl::StructD || d->kind == Decl::UnionD)
                aggrs[d->name] = d.get();
            else if (d->kind == Decl::AliasD && d->structCommon.type)
                aliases[d->name] = d->structCommon.type->name;
            else if (d->kind == Decl::EnumD) enums.insert(d->name);
            if (d->kind == Decl::StructD && d->isClass) classNames.insert(d->name);
            if (!d->methodOwner.empty() && !d->isRpc)
                methods[d->methodOwner][d->methodName] = d.get();
        }

        for (auto& d : prog.decls) {
            if (!d->exported) continue;
            // 外部声明（other 模块的类型/符号）由其模块头提供，导出头不重复；但 @inc 例外——
            //   sc 模块 inc 被标 external，@inc 的意图正是把该依赖头传播进本导出头（如 path 的
            //   path_stack 内嵌 proto 的 sc_proto 字段，须让消费单元先见到 proto.h）。
            if (d->external && d->kind != Decl::IncD) continue;
            switch (d->kind) {
                case Decl::EnumD: case Decl::StructD:
                case Decl::UnionD: case Decl::AliasD:
                    emitTypeDecl(*d);
                    break;
                case Decl::FuncTypeD:
                    if (d->isRpc) emitRpcInterface(*d, false);
                    else if (d->cImpl || !d->methodOwner.empty()) {
                        out << "extern ";
                        emitFuncSig(*d); out << ";\n";
                    }
                    else emitTypeDecl(*d);
                    break;
                case Decl::VarD: emitExternVars(d->structCommon.fields, false); break;
                case Decl::LetD: emitExternVars(d->structCommon.fields, true); break;
                case Decl::TlsD: break;  // tls 不可导出（parser 已拦截）
                case Decl::FuncD:
                    if (d->isRpc) { emitRpcInterface(*d, false); break; }
                    emitFuncSig(*d);
                    out << ";\n";
                    break;
                case Decl::IncD:
                    emitInclude(*d);
                    break;
                case Decl::AddD: break;  // 构建指令，不产生 C 输出
                case Decl::TestD: break;  // tst 用例不导出
                case Decl::MacroD: case Decl::MixD: break;  // 宏/展开：不入导出头
                case Decl::DepD: break;  // dep 不可导出（parser 已拦截）
            }
        }
        out << "\n#endif /* " << guard << " */\n";
        return out.str();
    }

    // extern 变量声明（头文件用：不带初值）
    void emitExternVars(const std::vector<Field>& decls, bool asConst) {
        for (auto& f : decls) {
            indent();
            out << "extern ";
            // 顶层导出全局的 C 名加 sc_ 前缀（sc 命名域），与其定义（emitVarDecls 非宏体路径
            // 亦经 scPfx）及消费单元引用（经 globalsT → scPfx）一致；cBridge（name:: T）认领
            // C 侧既有符号，保持裸名。
            std::string cname = f.cBridge ? f.name : scPfx(f.name);
            emitDeclarator(f, asConst, cname.c_str());
            out << ";\n";
        }
    }
};

} // namespace

// 模块 token 的机器无关基串：模块位于项目根（g_projectRoot = builtins 上级）之下时，
//   取「相对项目根」的路径作基串（如 /abs/repo/examples/x.sc → examples/x.sc），
//   使生成的 scm_<token>.h 名与 include guard 不含机器绝对前缀（跨机/远程回归稳定）。
//   项目根未知、或模块逃出根（相对路径以 ".." 起）时回退原字符串（保唯一性）。
//   外部链接（供 main.cpp 复用同一份实现，避免与 codegen 端 token 归一化漂移）。
static std::string moduleTokenBase(const std::string& s) {
    namespace fs = std::filesystem;
    if (!g_projectRoot.empty()) {
        std::error_code ec;
        fs::path rel = fs::relative(fs::path(s), fs::path(g_projectRoot), ec);
        if (!ec && !rel.empty()) {
            const std::string r = rel.generic_string();
            if (r != "." && r.rfind("..", 0) != 0) return r;  // 未逃出项目根
        }
    }
    return s;
}

std::string moduleFileToken(const std::string& s) {
    const std::string base = moduleTokenBase(s);
    std::string out = "scm_";
    for (unsigned char ch : base) out += std::isalnum(ch) ? (char)ch : '_';
    return out;
}

void setRefCheck(bool on) { g_refCheck = on; }
bool getRefCheck() { return g_refCheck; }
void setMemCheck(bool on) { g_memCheck = on; }
bool getMemCheck() { return g_memCheck; }
void setPtrCheck(bool on) { g_ptrCheck = on; }
bool getPtrCheck() { return g_ptrCheck; }
void setRefSrcFile(const std::string& path) { g_refSrcFile = path; }
void setProjectRoot(const std::string& path) { g_projectRoot = path; }
void setTestMode(bool on) { g_testMode = on; }
bool getTestMode() { return g_testMode; }

std::string emitC(const Program& prog, const std::string& srcFile) {
    CGen g(prog);
    g.srcFile = lineSafePath(srcFile);
    return g.run();
}

std::string emitC(const Program& prog, const std::string& srcFile,
                  const std::string& stringifyHeaderName, std::string* stringifyHeaderOut,
                  const std::string& rootPreludeHeader) {
    CGen g(prog);
    g.srcFile = lineSafePath(srcFile);
    g.sofHeaderName = stringifyHeaderName;
    g.rootPreludeHeader = rootPreludeHeader;  // 根模块导出接口头（末位 include）；空=禁用
    if (!prog.futureIds.empty()) g.typeHeaderName = "type.h";  // 文件模式：.c #include "type.h"
    g.classHeaderName = "class.h";  // 工程/文件模式：cls/dim 选择子由共享 class.h 提供（跨单元一致）
    g.genericHeaderName = "generic.h";  // 工程/文件模式：泛型实例类型由共享 generic.h 提供（跨单元一致）
    std::string c = g.run();
    if (stringifyHeaderOut) *stringifyHeaderOut = g.sofHeaderOut;
    return c;
}

std::string emitCHeader(const Program& prog, const std::string& guardName) {
    CGen g(prog);
    return g.runHeader(guardName);
}

// future<ID> 聚合枚举头 type.h：转译/构建管线在工程输出同级落盘，各 .c #include 它。
std::string emitFutureIdHeader(const std::vector<std::string>& ids) {
    if (ids.empty()) return "";
    std::string s = "/* 由 scc 生成：future<ID> 聚合事件枚举，请勿手工修改 */\n"
                    "#ifndef SCC_TYPE_H\n#define SCC_TYPE_H\n\n"
                    "typedef enum { /* base: int32_t */\n";
    for (size_t i = 0; i < ids.size(); i++) {
        s += "    " + scPfx(ids[i]);
        if (i + 1 < ids.size()) s += ",";
        s += "\n";
    }
    s += "} " + scPfx("future_id") + ";\n\n#endif\n";
    return s;
}

// cls/dim 全局选择子头 class.h：转译/构建管线在工程输出同级落盘，各使用类机制的 .c
//   #include 它。跨所有单元取类名/维度名并集后由此生成，保证 SC_CLS_<T>/SC_DIM_<Name>
//   在不同单元取同一编号（同名类/维度跨单元一致）。
std::string emitClassHeader(const std::vector<std::string>& clsNames,
                            const std::vector<std::string>& dimNames) {
    std::string s = "/* 由 scc 生成：cls 类 id 与 dim 维度选择子（跨单元一致），请勿手工修改 */\n"
                    "#ifndef SCC_CLASS_H\n#define SCC_CLASS_H\n\n";
    // 类机制运行时类型：tril / sc_hyper / object（跨模块共享，供模块头引用 _class 字段
    //   与 object 形参；故置于 class.h 而非各 .c 中段序言，确保 inc 头先于其使用可见）。
    s += "#include <stdint.h>\n";
    s += "typedef int8_t tril;\n";
    s += "#define SC_TRIL_NEG ((tril)-1)\n";
    s += "#define SC_TRIL_UNK ((tril)0)\n";
    s += "#define SC_TRIL_POS ((tril)1)\n";
    s += "typedef tril (*sc_hyper)(void *, uint32_t, ...);\n";
    s += "typedef sc_hyper *object;\n\n";
    // 全局类 id 枚举（SC_CLS_NONE=0，各类从 1 起，首见序）
    s += "enum { SC_CLS_NONE = 0";
    int ci = 1;
    for (auto& n : clsNames) s += ", SC_CLS_" + n + " = " + std::to_string(ci++);
    s += " };\n";
    // 全局维度选择子枚举（保留 0..6，用户维度从 7 起，首见序；跳过保留名）
    s += "enum { SC_DIM_CLS_ID = 0, SC_DIM_REF = 1, SC_DIM_DROP = 2, "
         "SC_DIM_OBJ_KEY = 3, SC_DIM_OBJ_NAME = 4, SC_DIM_RLT_KEY = 5, SC_DIM_RLT_NAME = 6";
    int di = 7;
    for (auto& n : dimNames) {
        if (n == "CLS_ID" || n == "OBJ_KEY" || n == "OBJ_NAME" ||
            n == "RLT_KEY" || n == "RLT_NAME" || n == "REF" || n == "DROP")
            continue;  // 保留维度已占 0..6
        s += ", SC_DIM_" + n + " = " + std::to_string(di++);
    }
    s += " };\n\n#endif\n";
    return s;
}

// 程序是否需要类机制运行时（供工程管线判定是否写出共享 class.h）。
bool programUsesClassRuntime(const Program& prog) {
    CGen g(prog);
    return g.computeUsesClassRuntime();
}

// 程序是否含泛型单态化实例（供工程管线判定是否写出共享 generic.h）。
bool programHasGenericInst(const Program& prog) {
    for (auto& d : prog.decls) if (d->genericInst) return true;
    return false;
}

// 泛型实例类型头 generic.h：转译/构建管线在工程输出同级落盘，各含实例的单元 .c 与模块头
//   #include 它。跨所有单元收集泛型单态化产物：全部实例聚合的前向 typedef + 自包含实例
//   （仅基本类型/指针字段）的完整定义（按类型名去重），保证实例类型跨模块一致可见
//   （导出签名引用、按值/指针传递）。无任何实例则返回空串（不落盘）。
std::string emitGenericHeader(const std::vector<const Program*>& progs) {
    std::set<std::string> seenFwd, seenDef;
    std::string fwd, defs;
    bool any = false;
    for (const Program* p : progs) {
        if (!programHasGenericInst(*p)) continue;
        any = true;
        CGen g(*p);
        g.appendGenericFragment(seenFwd, seenDef, fwd, defs);
    }
    if (!any) return "";
    std::string s = "/* 由 scc 生成：泛型实例类型（跨单元一致、去重），请勿手工修改 */\n"
                    "#ifndef SCC_GENERIC_H\n#define SCC_GENERIC_H\n\n"
                    "#include \"platform.h\"\n\n";
    s += fwd;
    if (!fwd.empty()) s += "\n";
    s += defs;
    s += "#endif\n";
    return s;
}
