// ============================================================
// cheaders.cpp —— C 头文件外部描述符采集（libclang 动态加载 + 退化文本匹配）
// ============================================================
// libclang 通过 dlopen 在运行时按需加载（编译期不依赖 libclang），未指定或
// 加载失败时退化为头文件文本标识符匹配。详见 cheaders.h。
// ============================================================
#include "cheaders.h"
#include "host_compat.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <unordered_map>

namespace {

// unused 符号逐个列出的总数阈值：超过则只统计计数，不灌入 AST（避免 windows.h
// 这类聚合头产出数千节点撑爆树/JSON）。
constexpr int kUnusedListThreshold = 64;

bool endsWithSc(const std::string& s) {
    return s.size() >= 3 && s.compare(s.size() - 3, 3, ".sc") == 0;
}

// macOS：若调用方未提供 sysroot/目标三元组，自动探测 SDK 路径（否则 libclang
// 找不到系统头 <stdio.h> 等，只能枚举到预定义宏）。交叉编译（显式 -target/
// --sysroot/-isysroot）时不注入本机 SDK，避免污染目标平台头解析。其它平台返回空。
std::vector<std::string> autoSysrootArgs(const std::vector<std::string>& given) {
    // 已显式指定 sysroot 或目标三元组 → 尊重调用方，不自动注入
    for (auto& a : given)
        if (a == "-isysroot" || a.rfind("-isysroot", 0) == 0 ||
            a == "-target"   || a.rfind("--target", 0) == 0 ||
            a == "--sysroot" || a.rfind("--sysroot", 0) == 0)
            return {};
#ifdef __APPLE__
    if (FILE* p = popen("xcrun --show-sdk-path 2>/dev/null", "r")) {
        std::string path;
        char buf[1024];
        while (fgets(buf, sizeof buf, p)) path += buf;
        pclose(p);
        while (!path.empty() && (path.back() == '\n' || path.back() == '\r' ||
                                 path.back() == ' '))
            path.pop_back();
        if (!path.empty()) return {"-isysroot", path};
    }
#endif
    return {};
}

// 去掉头名两侧的 <> 或 ""，返回裸头名
std::string stripDelims(const std::string& raw) {
    if (raw.size() >= 2 &&
        ((raw.front() == '<' && raw.back() == '>') ||
         (raw.front() == '"' && raw.back() == '"')))
        return raw.substr(1, raw.size() - 2);
    return raw;
}

// 读取头文件，抽取其中全部标识符（退化文本匹配用）
bool readHeaderIdents(const std::filesystem::path& p,
                      std::unordered_set<std::string>& idents) {
    std::ifstream f(p, std::ios::binary);
    if (!f) return false;
    std::string s((std::istreambuf_iterator<char>(f)),
                  std::istreambuf_iterator<char>());
    for (size_t i = 0, n = s.size(); i < n;) {
        unsigned char ch = (unsigned char)s[i];
        if (std::isalpha(ch) || ch == '_') {
            size_t j = i + 1;
            while (j < n && (std::isalnum((unsigned char)s[j]) || s[j] == '_')) j++;
            idents.insert(s.substr(i, j - i));
            i = j;
        } else {
            i++;
        }
    }
    return true;
}

// ---------------- libclang 最小 ABI 声明 ----------------
// 仅声明用到的类型与函数，避免编译期依赖 clang-c 头文件。
extern "C" {
typedef struct { const void* data; unsigned private_flags; } CXString;
typedef struct { int kind; int xdata; const void* data[3]; } CXCursor;
typedef struct { const void* ptr_data[2]; unsigned int_data; } CXSourceLocation;
typedef void* CXIndex;
typedef void* CXTranslationUnit;
typedef struct { const char* Filename; const char* Contents; unsigned long Length; } CXUnsavedFile;
}

// CXChildVisitResult
enum { CXChildVisit_Continue = 1 };
// 用到的 CXCursorKind（ABI 稳定值）
enum {
    CK_Struct = 2, CK_Union = 3, CK_Enum = 5,
    CK_Function = 8, CK_Var = 9, CK_Typedef = 20, CK_Macro = 501,
};
// CXTranslationUnit_DetailedPreprocessingRecord(0x01) 取宏 | SkipFunctionBodies(0x40) 提速
constexpr unsigned kParseOptions = 0x01u | 0x40u;

typedef CXIndex (*fn_createIndex)(int, int);
typedef void (*fn_disposeIndex)(CXIndex);
typedef CXTranslationUnit (*fn_parseTU)(CXIndex, const char*, const char* const*,
                                        int, CXUnsavedFile*, unsigned, unsigned);
typedef void (*fn_disposeTU)(CXTranslationUnit);
typedef CXCursor (*fn_tuCursor)(CXTranslationUnit);
typedef unsigned (*fn_visitChildren)(CXCursor, int (*)(CXCursor, CXCursor, void*), void*);
typedef int (*fn_cursorKind)(CXCursor);
typedef CXString (*fn_cursorSpelling)(CXCursor);
typedef const char* (*fn_getCString)(CXString);
typedef void (*fn_disposeString)(CXString);
typedef CXSourceLocation (*fn_cursorLocation)(CXCursor);
typedef void (*fn_presumedLocation)(CXSourceLocation, CXString*, unsigned*, unsigned*);

struct Clang {
    void* h = nullptr;
    fn_createIndex    createIndex    = nullptr;
    fn_disposeIndex   disposeIndex   = nullptr;
    fn_parseTU        parseTU        = nullptr;
    fn_disposeTU      disposeTU      = nullptr;
    fn_tuCursor       tuCursor       = nullptr;
    fn_visitChildren  visitChildren  = nullptr;
    fn_cursorKind     cursorKind     = nullptr;
    fn_cursorSpelling cursorSpelling = nullptr;
    fn_getCString     getCString     = nullptr;
    fn_disposeString  disposeString  = nullptr;
    fn_cursorLocation cursorLocation = nullptr;
    fn_presumedLocation presumedLoc  = nullptr;

    bool load(const std::string& path) {
        h = host::dlOpen(path.c_str());
        if (!h) return false;
        createIndex    = (fn_createIndex)    host::dlSym(h, "clang_createIndex");
        disposeIndex   = (fn_disposeIndex)   host::dlSym(h, "clang_disposeIndex");
        parseTU        = (fn_parseTU)        host::dlSym(h, "clang_parseTranslationUnit");
        disposeTU      = (fn_disposeTU)      host::dlSym(h, "clang_disposeTranslationUnit");
        tuCursor       = (fn_tuCursor)       host::dlSym(h, "clang_getTranslationUnitCursor");
        visitChildren  = (fn_visitChildren)  host::dlSym(h, "clang_visitChildren");
        cursorKind     = (fn_cursorKind)     host::dlSym(h, "clang_getCursorKind");
        cursorSpelling = (fn_cursorSpelling) host::dlSym(h, "clang_getCursorSpelling");
        getCString     = (fn_getCString)     host::dlSym(h, "clang_getCString");
        disposeString  = (fn_disposeString)  host::dlSym(h, "clang_disposeString");
        cursorLocation = (fn_cursorLocation) host::dlSym(h, "clang_getCursorLocation");
        presumedLoc    = (fn_presumedLocation) host::dlSym(h, "clang_getPresumedLocation");
        return createIndex && disposeIndex && parseTU && disposeTU && tuCursor &&
               visitChildren && cursorKind && cursorSpelling && getCString &&
               disposeString && cursorLocation && presumedLoc;
    }
    ~Clang() { if (h) host::dlClose(h); }
};

struct CollectCtx {
    Clang* c;
    std::vector<std::pair<std::string, int>>* out;  // name, Decl::Kind
};

// 将 clang cursor 类别映射为 Decl::Kind；非目标类别返回 -1
int mapKind(int ck) {
    switch (ck) {
        case CK_Function: return Decl::FuncTypeD;  // 外部函数：仅签名
        case CK_Typedef:  return Decl::AliasD;
        case CK_Struct:   return Decl::StructD;
        case CK_Union:    return Decl::UnionD;
        case CK_Enum:     return Decl::EnumD;
        case CK_Var:      return Decl::VarD;
        case CK_Macro:    return Decl::LetD;       // 宏当作常量类展示
        default:          return -1;
    }
}

int visitTopLevel(CXCursor cursor, CXCursor /*parent*/, void* data) {
    auto* ctx = static_cast<CollectCtx*>(data);
    int dk = mapKind(ctx->c->cursorKind(cursor));
    if (dk < 0) return CXChildVisit_Continue;
    // 过滤编译器预定义符号（预定义宏/命令行定义）：其 presumed 文件名为空或形如
    // "<built-in>"/"<command line>"，不属于任何真实头文件，统计它们会严重失真。
    CXSourceLocation loc = ctx->c->cursorLocation(cursor);
    CXString fnameS;
    unsigned line = 0, col = 0;
    ctx->c->presumedLoc(loc, &fnameS, &line, &col);
    const char* fn = ctx->c->getCString(fnameS);
    const bool builtin = !fn || !*fn || fn[0] == '<';
    ctx->c->disposeString(fnameS);
    if (builtin) return CXChildVisit_Continue;
    CXString sp = ctx->c->cursorSpelling(cursor);
    const char* cs = ctx->c->getCString(sp);
    if (cs && *cs) ctx->out->emplace_back(std::string(cs), dk);
    ctx->c->disposeString(sp);
    return CXChildVisit_Continue;
}

// 用 libclang 枚举单个 C 头的全部顶层符号；成功返回 true
bool clangEnumerate(Clang& c, const std::string& bare, bool quoted,
                    const std::filesystem::path& baseDir,
                    const std::vector<std::string>& extraArgs,
                    std::vector<std::pair<std::string, int>>& out) {
    CXIndex idx = c.createIndex(0, 0);  // displayDiagnostics=0：静默
    if (!idx) return false;
    const std::string inc = quoted ? ("#include \"" + bare + "\"\n")
                                   : ("#include <" + bare + ">\n");
    CXUnsavedFile uf{"scc_probe.c", inc.c_str(), (unsigned long)inc.size()};
    std::vector<std::string> argStore;
    argStore.push_back("-I" + baseDir.string());  // 供 inc "局部头" 解析
    for (auto& a : extraArgs) argStore.push_back(a);
    std::vector<const char*> argv;
    argv.reserve(argStore.size());
    for (auto& a : argStore) argv.push_back(a.c_str());
    CXTranslationUnit tu = c.parseTU(idx, "scc_probe.c", argv.data(),
                                     (int)argv.size(), &uf, 1, kParseOptions);
    if (!tu) { c.disposeIndex(idx); return false; }
    CollectCtx ctx{&c, &out};
    c.visitChildren(c.tuCursor(tu), visitTopLevel, &ctx);
    c.disposeTU(tu);
    c.disposeIndex(idx);
    return true;
}

DeclPtr makeSym(const std::string& name, int kind, const std::string& origin,
                bool used, int line) {
    auto d = std::make_unique<Decl>();
    d->kind = (Decl::Kind)kind;
    d->name = name;
    d->external = true;
    d->origin = origin;
    d->used = used;
    d->line = line;
    return d;
}

} // namespace

bool tryLoadLibclang(const std::string& path) {
    if (path.empty()) return false;
    void* h = host::dlOpen(path.c_str());
    if (!h) return false;
    host::dlClose(h);
    return true;
}

std::string detectLibclang() {
    namespace fs = std::filesystem;
    std::vector<std::string> cands;

#if defined(__APPLE__)
    // Xcode / CommandLineTools 活动开发者目录
    if (FILE* p = popen("xcode-select -p 2>/dev/null", "r")) {
        std::string dev;
        char buf[512];
        while (fgets(buf, sizeof buf, p)) dev += buf;
        pclose(p);
        while (!dev.empty() && (dev.back() == '\n' || dev.back() == '\r' ||
                                dev.back() == ' '))
            dev.pop_back();
        if (!dev.empty()) {
            cands.push_back(dev + "/usr/lib/libclang.dylib");
            cands.push_back(dev +
                "/Toolchains/XcodeDefault.xctoolchain/usr/lib/libclang.dylib");
        }
    }
    cands.push_back("/Library/Developer/CommandLineTools/usr/lib/libclang.dylib");
    cands.push_back("/opt/homebrew/opt/llvm/lib/libclang.dylib");  // Homebrew (arm64)
    cands.push_back("/usr/local/opt/llvm/lib/libclang.dylib");     // Homebrew (x86_64)
    const char* soname = "libclang.dylib";
#elif defined(_WIN32)
    cands.push_back("C:/Program Files/LLVM/bin/libclang.dll");
    cands.push_back("C:/Program Files (x86)/LLVM/bin/libclang.dll");
    const char* soname = "libclang.dll";
#else  // Linux / *nix
    cands.push_back("/usr/lib/libclang.so");
    cands.push_back("/usr/lib64/libclang.so");
    cands.push_back("/usr/local/lib/libclang.so");
    // 常见多架构目录与 llvm-* 工具链目录里扫描 libclang.so*
    static const char* dirs[] = {
        "/usr/lib", "/usr/lib64", "/usr/local/lib",
        "/usr/lib/x86_64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
    };
    for (const char* d : dirs) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) continue;
        for (auto& e : fs::directory_iterator(d, ec)) {
            const std::string nm = e.path().filename().string();
            if (nm.rfind("libclang.so", 0) == 0) cands.push_back(e.path().string());
        }
    }
    for (const char* base : {"/usr/lib", "/usr/lib64"}) {
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;
        for (auto& e : fs::directory_iterator(base, ec)) {
            if (!e.is_directory(ec)) continue;
            const std::string nm = e.path().filename().string();
            if (nm.rfind("llvm", 0) != 0) continue;  // llvm-14、llvm 等
            cands.push_back((e.path() / "lib" / "libclang.so").string());
            cands.push_back((e.path() / "lib" / "libclang.so.1").string());
        }
    }
    const char* soname = "libclang.so";
#endif

    // 1. 显式候选路径：存在且可加载即用
    for (auto& c : cands)
        if (fs::exists(c) && tryLoadLibclang(c)) return c;

    // 2. 交由动态链接器按 soname 搜索（DYLD/LD_LIBRARY_PATH 等）
    for (const char* sn : {soname, "libclang.so.1"})
        if (tryLoadLibclang(sn)) return sn;

    return {};
}

void gatherCHeaderDescriptors(Program& prog,
                              const std::filesystem::path& baseDir,
                              const ClangOptions& opt,
                              const std::unordered_set<std::string>& refs) {

    // 收集 C 头 inc（名字不以 .sc 结尾的 IncD）
    std::vector<Decl*> cincs;
    for (auto& d : prog.decls)
        if (d->kind == Decl::IncD && !endsWithSc(d->name)) cincs.push_back(d.get());
    if (cincs.empty()) return;

    Clang clang;
    const bool haveClang = !opt.libPath.empty() && clang.load(opt.libPath);

    // 合并调用方参数与（macOS）自动探测的 SDK sysroot
    std::vector<std::string> clangArgs = opt.args;
    for (auto& a : autoSysrootArgs(opt.args)) clangArgs.push_back(a);

    std::vector<DeclPtr> synthesized;
    for (Decl* inc : cincs) {
        const std::string raw = inc->name;
        const bool quoted = !raw.empty() && raw.front() == '"';
        const std::string bare = stripDelims(raw);
        inc->external = true;       // 纳入外部描述符体系
        inc->origin = bare;         // 分组键（与合成描述符的 origin 一致）

        // ---- 来源 1：libclang 枚举全集 ----
        if (haveClang) {
            std::vector<std::pair<std::string, int>> syms;
            if (clangEnumerate(clang, bare, quoted, baseDir, clangArgs, syms)) {
                std::unordered_map<std::string, int> uniq;  // 去重（首个类别胜出）
                for (auto& s : syms) uniq.emplace(s.first, s.second);
                const int total = (int)uniq.size();
                const bool listAll = total <= kUnusedListThreshold;
                inc->externDeclared = total;
                inc->externAnalyzed = true;
                for (auto& [name, kind] : uniq) {
                    const bool used = refs.count(name) > 0;
                    if (!used && !listAll) continue;  // 降噪：大头只列已用
                    synthesized.push_back(makeSym(name, kind, bare, used, inc->line));
                }
                continue;
            }
            // clang 解析失败 → 落到退化匹配
        }

        // ---- 来源 2：退化文本匹配 ----
        std::filesystem::path hp;
        for (const auto& cand : {baseDir / bare, std::filesystem::path(bare)}) {
            if (std::filesystem::exists(cand) && std::filesystem::is_regular_file(cand)) {
                hp = cand;
                break;
            }
        }
        std::unordered_set<std::string> idents;
        const bool resolved = !hp.empty() && readHeaderIdents(hp, idents);
        inc->externDeclared = -1;        // 退化无法枚举全集 → 总数未知
        inc->externAnalyzed = resolved;  // 读到文件才允许"导入未使用"警告
        if (resolved) {
            for (const auto& name : refs)
                if (idents.count(name))   // 退化未知类别，统一按函数签名展示
                    synthesized.push_back(makeSym(name, Decl::FuncTypeD, bare, true, inc->line));
        }
    }

    for (auto& d : synthesized) prog.decls.push_back(std::move(d));
}
