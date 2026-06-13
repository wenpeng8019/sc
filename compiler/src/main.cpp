// ============================================================
// scc 编译器 —— 主入口
// ============================================================
// 整个编译流水线： lex → parse → emit [→ cc 编译 → 执行]
//   lex:  源码字符串 → Token 序列
//   parse: Token 序列 → Program AST 树
//   emit:  Program → 输出（C源码 / AST JSON / 规范化sc源码）
//
// 五种运行模式：
//   默认      → 编译+执行：转 C 后直接用系统 C 编译器编译并运行，
//              不保存中间文件，整体效果类似解释器
//              C 编译器选择：$SCC_CC > $CC > gcc
//   --build  → 构建产物：编译链接为持久产物，按 -o 后缀决定类型
//              （可执行文件 / .a 静态库 / .so|.dylib 动态库）
//   --emit-c → emitC()       sc源码转译为C源码
//   --ast    → emitAstJson() AST结构导出为JSON树
//   --emit-sc → emitSc()     从AST再生规范化sc源码
//
// 所有编译错误通过 CompileError 异常传播，在此统一捕获并格式化输出。
// ============================================================
#include "ast_json.h"
#include "codegen_c.h"
#include "codegen_sc.h"
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <map>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static void usage() {
    std::cerr << "用法: scc <input.sc | -> [选项] [-- 程序参数...]\n"
              << "  默认：转 C 后直接编译并执行（类似解释器，不保存中间文件）\n"
              << "        C 编译器优先级：环境变量 SCC_CC > CC > 当前目录 .sc 配置文件 > gcc\n"
              << "        .sc 配置文件格式：key = value，每行一项（# 行注释）：\n"
              << "          cc     = clang          # C 编译器（环境变量 SCC_CC/CC 优先）\n"
              << "          cflags = -O2 -Wall      # 编译选项（SCC_CFLAGS 优先）\n"
              << "          ldflags = -framework Cocoa  # 链接选项（SCC_LDFLAGS 优先）\n"
              << "          inc    = /opt/inc:vendor/inc  # 头文件搜索路径，':' 分隔 → -I（SCC_INC 优先）\n"
              << "          lib    = /opt/lib:vendor/lib  # 库搜索路径，':' 分隔 → -L（SCC_LIB 优先）\n"
              << "          libs   = m pthread       # 链接库名，空格/逗号分隔 → -l（SCC_LIBS 优先）\n"
              << "          adt    = my_adt.c        # adt 自定义实现（SCC_ADT/--adt 优先）\n"
              << "        交叉编译配置项（同样可用环境变量 SCC_* 或 --target 目标档覆盖）：\n"
              << "          cc/ar/objcopy = <工具>   # 交叉工具链程序（SCC_CC/SCC_AR/SCC_OBJCOPY 优先）\n"
              << "          target_flags = -mcpu=... # 目标机器选项，同时进入编译与链接（SCC_TARGET_FLAGS）\n"
              << "          sysroot = /path/sysroot  # 目标 sysroot → --sysroot（SCC_SYSROOT 优先）\n"
              << "          triple  = aarch64-linux-gnu # 目标三元组，驱动平台表（SCC_TARGET_TRIPLE 优先）\n"
              << "          threads = -lpthread / none  # 线程库链接选项，显式覆盖平台表（SCC_THREADS）\n"
              << "          debug   = dsymutil / none   # 链接后调试打包步骤，覆盖平台表（SCC_DEBUG）\n"
              << "          freestanding = 1         # 裸机目标（无托管运行时；SCC_FREESTANDING 优先）\n"
              << "          platforms = plat.tbl     # 外置平台表文件（SCC_PLATFORMS 优先；行 pattern:threads:debug）\n"
              << "          run     = qemu-arm -L .. # 运行包装器/模拟器（SCC_RUN 优先；run 模式用）\n"
              << "  -l <名>    追加链接库（可重复；-lm 写法也支持，与配置的 libs 合并）\n"
              << "  --adt <x>  adt 自定义实现（.c/.o/.a，照 builtins/adt/adt.h 契约实现）；\n"
              << "             未指定时 inc adt.sc 自动链接内置默认实现 builtins/adt/adt_impl.c\n"
              << "  --target <file>  加载交叉编译目标档（key=value，同 .sc 配置语法；SCC_TARGET 亦可）\n"
              << "  --builtins <dir> 目标适配 builtins 目录（最高优先级，替换默认库实现）\n"
              << "  --build    构建产物模式：编译链接为持久产物，应用与 run 相同的工具链配置\n"
              << "             产物类型按 -o 后缀决定：.a → 静态库（ar rcs）；\n"
              << "             .so/.dylib → 动态库（-shared，单元编译附加 -fPIC）；\n"
              << "             .bin/.hex → 裸机镜像（objcopy 转 raw/Intel-HEX）；其余（含 .elf）→ 可执行文件\n"
              << "             构建库且存在 @导出对象时，额外生成同名 .h 头文件\n"
              << "             -o 缺省为输入文件名去 .sc 后缀（stdin 输入必须指定 -o）\n"
              << "  --emit-c   转译为 C 源码（配合 -o 输出到文件，缺省 stdout；\n"
              << "             存在 @导出对象且指定 -o 时，额外生成同名 .h 头文件；不受以上编译配置影响）\n"
              << "  --ast      输出 AST JSON 树\n"
              << "  --emit-sc  从 AST 再生成规范化 sc 源码\n"
              << "  -o <file>  输出文件（--build/--emit-c/--ast/--emit-sc 模式下有效）\n"
              << "             裸 -o 不带值时按输入文件名 + 模式后缀推导，写入输入文件所在目录：\n"
              << "             --emit-c→.c  --ast→.json  --emit-sc→.out.sc  --build→无后缀\n"
              << "  '-' 表示从 stdin 读入；'--' 之后的参数传递给被执行的程序\n";
}

// 从文件读取全部文本；失败返回空串
static std::string readWholeFile(const std::filesystem::path& p) {
    std::ifstream fin(p);
    if (!fin) return {};
    std::ostringstream ss;
    ss << fin.rdbuf();
    return ss.str();
}

// 写文本到文件；成功返回 true，失败返回 false
static bool writeTextFile(const std::filesystem::path& p, const std::string& content) {
    std::ofstream f(p);
    if (!f) {
        std::cerr << "错误: 无法写入文件 " << p << "\n";
        return false;
    }
    f << content;
    return true;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

///////////////////////////////////////////////////////////////////////////////

// ---------------- 工具链扩展配置 ----------------
// 环境变量优先，未设置时取 .sc 配置文件同名键：
//   SCC_CFLAGS / cflags    额外编译选项（空格分隔）
//   SCC_LDFLAGS / ldflags  额外链接选项（空格分隔）
//   SCC_INC / inc          头文件搜索路径，':' 分隔（类似 PATH）→ 逐项 -I
//   SCC_LIB / lib          库搜索路径，':' 分隔 → 逐项 -L
//   SCC_LIBS / libs        链接库名，空格或逗号分隔 → 逐项 -l

struct ToolConfig {
    std::string cflags;   // 编译阶段附加选项（含 -I 展开）
    std::string ldflags;  // 链接阶段附加选项（含 -L/-l 展开）
    std::string adtImpl;  // adt 自定义实现（--adt/SCC_ADT/.sc 配置 adt；空=内置默认实现）

    // ---- 交叉编译扩展 ----
    std::string machine;     // 目标机器选项（target_flags + --sysroot）：同时进入编译与链接
    std::string ar = "ar";   // 静态库归档器（SCC_AR/ar；交叉如 arm-none-eabi-ar）
    std::string objcopy;     // 目标文件转换器（SCC_OBJCOPY/objcopy；产 .bin/.hex）
    std::string runner;      // 运行包装器（SCC_RUN/run；如 "qemu-arm -L <sysroot>"，空=直接执行）
    std::string triple;      // 目标三元组（SCC_TARGET_TRIPLE/triple；空=本机）
    std::string threadsLib;  // 线程库链接选项（平台表/显式 threads 解析，如 "-lpthread"）
    std::string debugTool;   // 链接后调试打包步骤（"dsymutil" / "none"）
    bool freestanding = false; // 裸机档：目标无托管运行时（SCC_FREESTANDING/freestanding=1）
    bool crossRun = false;   // 目标平台与本机不同族 → 不能直接 exec（须 runner 或 --build）
};

// 目标档（--target 文件）键值表：configValue 在环境变量之后、./.sc 配置之前回退到此。
// 优先级：环境变量 SCC_* > --target 目标档 > ./.sc 配置 > 内置默认
static std::map<std::string, std::string> g_profile;

// --builtins 指定的目标适配 builtins 目录（最高优先级；空=按默认搜索）
static std::filesystem::path g_builtinsOverride;

// 读取当前目录下的 .sc 配置文件，返回指定 key 的值（未配置返回空串）
// 格式：每行 key = value，'#' 开头为注释，键值两侧空白忽略
//   cc = clang
static std::string readConfig(const std::string& key) {
    std::ifstream fin(".sc");
    if (!fin) return "";
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    std::string line;
    while (std::getline(fin, line)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        if (trim(l.substr(0, eq)) == key) return trim(l.substr(eq + 1));
    }
    return "";
}

// 获取配置项：环境变量 > --target 目标档 > ./.sc 配置文件
static std::string configValue(const char* env, const char* key) {
    const char* v = std::getenv(env);
    if (v && *v) return v;
    auto it = g_profile.find(key);
    if (it != g_profile.end() && !it->second.empty()) return it->second;
    return readConfig(key);
}

// 加载 --target 目标档（与 .sc 配置同语法：每行 key = value，'#' 注释）；失败即退出
static void loadProfile(const std::string& path) {
    std::ifstream fin(path);
    if (!fin) {
        std::cerr << "错误: 无法打开目标档 " << path << "\n";
        std::exit(1);
    }
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    std::string line;
    while (std::getline(fin, line)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        g_profile[trim(l.substr(0, eq))] = trim(l.substr(eq + 1));
    }
}

static std::vector<std::string> splitBy(const std::string& s, const char* seps) {
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s) {
        if (std::strchr(seps, ch)) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else cur += ch;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// ---------------- 平台表（target triple → 工具链行为）----------------
// 自动化决策的基础：线程库链接选项、链接后调试打包步骤。
//   threads：线程实现所需链接选项（如 "-lpthread"，空表示内置/无需）
//   debug：链接后调试步骤（"dsymutil" 或 "none"）
//   known：该三元组是否被平台表覆盖（未覆盖且未显式声明则报错）
struct PlatProps { std::string threads; std::string debug; bool known = false; };

// 本机三元组（按 scc 自身编译期宿主宏推断的平台族）
static std::string hostTriple() {
#if defined(__APPLE__)
    return "apple-darwin";
#elif defined(_WIN32)
    return "w64-mingw32";
#elif defined(__linux__)
    return "linux-gnu";
#else
    return "unknown";
#endif
}

// 平台族（用于判定 host≠target：族不同即不能在本机直接运行）
static std::string platformFamily(const std::string& triple) {
    auto has = [&](const char* s) { return triple.find(s) != std::string::npos; };
    // 先判宿主 OS（注意 arm-linux-gnueabihf 含 "eabi" 却是托管 Linux，须先于裸机判定）
    if (has("darwin") || has("apple")) return "darwin";
    if (has("mingw") || has("windows") || has("win32") || has("msvc")) return "windows";
    if (has("linux")) return "linux";
    if (has("none") || has("eabi") || has("elf")) return "bare";  // 裸机 *-none-eabi/*-elf
    return "unknown";
}

// 内置平台表：按三元组族返回工具链行为
static PlatProps builtinPlatform(const std::string& triple) {
    const std::string fam = platformFamily(triple);
    if (fam == "darwin")  return {"", "dsymutil", true};
    if (fam == "windows") return {"", "none", true};
    if (fam == "bare")    return {"", "none", true};   // 裸机：无托管线程/调试打包
    if (fam == "linux")   return {"-lpthread", "none", true};
    return {"", "none", false};                         // 未知：交由外置表/显式声明
}

// 外置平台表（配置优先）：SCC_PLATFORMS/platforms 指向的文件，每行
//   pattern : threads : debug
// pattern 为三元组子串匹配（如 "linux-musl"），命中即覆盖内置表。
static PlatProps externalPlatform(const std::string& triple) {
    const std::string path = configValue("SCC_PLATFORMS", "platforms");
    if (path.empty()) return {"", "none", false};
    std::ifstream fin(path);
    if (!fin) return {"", "none", false};
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r");
        if (a == std::string::npos) return std::string();
        size_t b = s.find_last_not_of(" \t\r");
        return s.substr(a, b - a + 1);
    };
    std::string line;
    while (std::getline(fin, line)) {
        std::string l = trim(line);
        if (l.empty() || l[0] == '#') continue;
        auto parts = splitBy(l, ":");
        if (parts.empty()) continue;
        const std::string pat = trim(parts[0]);
        if (pat.empty() || triple.find(pat) == std::string::npos) continue;
        PlatProps p;
        p.known = true;
        p.threads = parts.size() > 1 ? trim(parts[1]) : "";
        p.debug   = parts.size() > 2 ? trim(parts[2]) : "none";
        if (p.threads == "none") p.threads.clear();
        return p;
    }
    return {"", "none", false};
}

// 选择工具程序：环境变量 > 目标档/.sc 配置 > 缺省值（空缺省表示可留空）
static std::string pickTool(const char* env, const char* key, const char* def) {
    std::string v = configValue(env, key);
    return v.empty() ? std::string(def ? def : "") : v;
}

// 汇总所有扩展配置为两段命令行片段；extraLibs 来自命令行 -l
static ToolConfig loadToolConfig(const std::vector<std::string>& extraLibs,
                                 const std::string& adtOpt = "") {
    ToolConfig tc;
    const std::string cflags = configValue("SCC_CFLAGS", "cflags");
    if (!cflags.empty()) tc.cflags += " " + cflags;
    for (auto& p : splitBy(configValue("SCC_INC", "inc"), ":"))
        tc.cflags += " -I " + p;
    const std::string ldflags = configValue("SCC_LDFLAGS", "ldflags");
    if (!ldflags.empty()) tc.ldflags += " " + ldflags;
    for (auto& p : splitBy(configValue("SCC_LIB", "lib"), ":"))
        tc.ldflags += " -L " + p;
    for (auto& l : splitBy(configValue("SCC_LIBS", "libs"), " ,"))
        tc.ldflags += " -l" + l;
    for (auto& l : extraLibs)
        tc.ldflags += " -l" + l;
    tc.adtImpl = !adtOpt.empty() ? adtOpt : configValue("SCC_ADT", "adt");

    // ---- 交叉编译：工具程序 + 目标机器选项 + 平台行为解析 ----
    tc.ar      = pickTool("SCC_AR", "ar", "ar");
    tc.objcopy = pickTool("SCC_OBJCOPY", "objcopy", "objcopy");
    tc.runner  = configValue("SCC_RUN", "run");

    // 目标机器选项（target_flags + sysroot）：同时进入编译与链接两步
    const std::string tflags = configValue("SCC_TARGET_FLAGS", "target_flags");
    if (!tflags.empty()) tc.machine += " " + tflags;
    const std::string sysroot = configValue("SCC_SYSROOT", "sysroot");
    if (!sysroot.empty()) tc.machine += " --sysroot=" + sysroot;

    const std::string fs = configValue("SCC_FREESTANDING", "freestanding");
    tc.freestanding = (fs == "1" || fs == "true" || fs == "yes");

    // 平台行为：显式声明 > 外置表 > 内置表
    tc.triple = configValue("SCC_TARGET_TRIPLE", "triple");
    const std::string host = hostTriple();
    const std::string effective = tc.triple.empty() ? host : tc.triple;
    if (platformFamily(effective) == "bare") tc.freestanding = true;

    PlatProps pp = externalPlatform(effective);          // 外置表优先
    if (!pp.known) pp = builtinPlatform(effective);      // 内置表兜底
    const std::string thOpt  = configValue("SCC_THREADS", "threads");  // 显式声明最高
    const std::string dbgOpt = configValue("SCC_DEBUG", "debug");
    if (!thOpt.empty())  { pp.threads = (thOpt == "none" ? "" : thOpt); pp.known = true; }
    if (!dbgOpt.empty()) { pp.debug = dbgOpt; pp.known = true; }

    // 未知目标且未显式声明：要求用户声明（裸机档天然 freestanding 免此校验）
    if (!tc.triple.empty() && !pp.known && !tc.freestanding) {
        std::cerr << "错误: 未知目标三元组 '" << tc.triple
                  << "'，平台表未覆盖；请在目标档声明 threads/debug，"
                     "或提供平台表（SCC_PLATFORMS/platforms）\n";
        std::exit(1);
    }
    tc.threadsLib = pp.threads;
    tc.debugTool  = pp.known ? pp.debug : "none";

    // host≠target（平台族不同）：不能在本机直接运行
    tc.crossRun = !tc.triple.empty() &&
                  platformFamily(tc.triple) != platformFamily(host);
    return tc;
}

#ifdef SCC_EMBED_BUILTINS
// ---------------- 内嵌 builtins（发行版变体）----------------
// CMake -DSCC_EMBED_BUILTINS=ON 时，builtins 的 .sc/.h 与预编译 adt.a
// 经 cmake/embed_builtins.cmake 生成的表内嵌进二进制；首次使用释放到
// ~/.cache/scc/builtins-<内容哈希>（已存在且大小一致则复用，内容变化
// 自动换目录），使 scc 单二进制发行无需携带 builtins 目录。
struct EmbeddedFile { const char* path; const unsigned char* data; size_t size; };
extern const EmbeddedFile g_embeddedBuiltins[];
extern const size_t g_embeddedBuiltinsCount;
extern const char* const g_embeddedBuiltinsHash;

static std::filesystem::path embeddedBuiltinsDir() {
    static const std::filesystem::path cached = [] {
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path base;
        if (const char* home = std::getenv("HOME"))
            base = fs::path(home) / ".cache" / "scc";
        else
            base = fs::temp_directory_path(ec) / "scc-cache";
        const fs::path dir = base / ("builtins-" + std::string(g_embeddedBuiltinsHash));
        for (size_t i = 0; i < g_embeddedBuiltinsCount; i++) {
            const auto& f = g_embeddedBuiltins[i];
            const fs::path p = dir / f.path;
            if (fs::is_regular_file(p, ec) && fs::file_size(p, ec) == f.size) continue;
            fs::create_directories(p.parent_path(), ec);
            std::ofstream out(p, std::ios::binary);
            if (!out.write(reinterpret_cast<const char*>(f.data),
                           static_cast<std::streamsize>(f.size)))
                return fs::path{};  // 释放失败：禁用内嵌目录（不影响其余搜索路径）
        }
        return dir;
    }();
    return cached;
}
#endif

///////////////////////////////////////////////////////////////////////////////

// 从指定路径开始向上搜索 builtins 目录
static std::filesystem::path findBuiltinsDir(const std::filesystem::path& start) {
    for (auto p = start; !p.empty(); ) {
        auto cand = p / "builtins";
        if (std::filesystem::exists(cand) && std::filesystem::is_directory(cand)) return cand;
        auto parent = p.parent_path();
        if (parent == p) break;  // 已到根目录（"/" 的 parent 仍是 "/"），防止死循环
        p = parent;
    }
    return {};
}

// builtins 根目录根级头文件默认加入编译 -I：platform.h 等
// + 搜索顺序同模块解析：输入目标文件所在目录向上 → cwd(当前目录)向上 → SCC_BUILTINS → 内嵌释放目录
static void addBuiltinsInclude(ToolConfig& tc, const std::string& input) {

    namespace fs = std::filesystem; fs::path b;

    // --builtins 指定的目标适配目录：最高优先级（交叉/裸机用适配实现替换默认库）
    if (!g_builtinsOverride.empty()) b = g_builtinsOverride;

    // 根据输入目标文件所在目录向上搜索 builtins 目录；
    if (b.empty() && input != "-") {
        std::error_code ec;
        const fs::path abs = fs::absolute(fs::path(input), ec);
        if (!ec) b = findBuiltinsDir(abs.parent_path());
    }

    // 从当前目录向上搜索 builtins 目录；
    if (b.empty()) b = findBuiltinsDir(fs::current_path());

    // 环境变量 SCC_BUILTINS 指定的目录（优先级高于内嵌资源释放目录）；
    if (b.empty())
        if (const char* envB = std::getenv("SCC_BUILTINS")) b = envB;

    // 内嵌资源释放目录（优先级最低）：仅在 SCC_EMBED_BUILTINS 定义时启用
#ifdef SCC_EMBED_BUILTINS
    if (b.empty()) b = embeddedBuiltinsDir();
#endif

    if (!b.empty()) {
        tc.cflags += " -I " + b.string();
        // builtins 根的上级目录：使生成代码中带根名的引用（如
        // #include "builtins/adt/adt.h"）可解析
        const fs::path parent = b.parent_path();
        if (!parent.empty() && parent != b) tc.cflags += " -I " + parent.string();
    }
}

// 生成合法的 C 标识符作为模块文件名/头文件名 token（如输入路径 "foo/bar-baz.sc" → token "scm_foo_bar_baz"）
static std::string moduleFileToken(const std::string& s) {
    std::string out = "scm_";
    for (unsigned char ch : s) out += std::isalnum(ch) ? (char)ch : '_';
    return out;
}

// 生成合法的 C 预处理器宏名作为 include guard（如输入路径 "foo/bar-baz.sc" → guard "FOO_BAR_BAZ_SC_H"）
static std::string guardFromHeaderName(const std::string& hname) {
    std::string g;
    for (char ch : hname) g += std::isalnum((unsigned char)ch) ? (char)std::toupper(ch) : '_';
    return g;
}

///////////////////////////////////////////////////////////////////////////////

// 产物类型由输出文件名后缀决定
enum class OutKind { Exe, StaticLib, SharedLib, Bin, Hex };

static OutKind outputKind(const std::string& out) {
    if (endsWith(out, ".a")) return OutKind::StaticLib;
    if (endsWith(out, ".so") || endsWith(out, ".dylib")) return OutKind::SharedLib;
    if (endsWith(out, ".bin")) return OutKind::Bin;
    if (endsWith(out, ".hex")) return OutKind::Hex;
    return OutKind::Exe;  // 含 .elf：交叉/裸机可执行
}

// 选择系统 C 编译器：环境变量 SCC_CC > CC > 目标档/.sc 配置 cc 项 > 缺省 gcc
static std::string pickCC() {
    const char* cc = std::getenv("SCC_CC");
    if (cc && *cc) return cc;
    cc = std::getenv("CC");
    if (cc && *cc) return cc;
    auto it = g_profile.find("cc");
    if (it != g_profile.end() && !it->second.empty()) return it->second;
    std::string conf = readConfig("cc");
    if (!conf.empty()) return conf;
    return "gcc";
}

// 把 .o 列表合成最终产物：可执行（链接）/ 静态库（ar rcs）/ 动态库（-shared）/ 裸机镜像（objcopy）
static int linkOutput(OutKind kind,
                      const std::vector<std::filesystem::path>& objects,
                      const std::string& output,
                      const ToolConfig& tc) {
    // 裸机镜像 .bin/.hex：先链接临时 .elf，再 objcopy 转换
    if (kind == OutKind::Bin || kind == OutKind::Hex) {
        const std::string elf = output + ".elf";
        std::string cmd = pickCC() + " -g" + tc.machine;
        for (auto& o : objects) cmd += " " + o.string();
        cmd += " -o " + elf + tc.ldflags;
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << "错误: 构建产物失败（" << cmd << "）\n";
            return 1;
        }
        const char* fmt = (kind == OutKind::Bin) ? "binary" : "ihex";
        std::string oc = tc.objcopy + " -O " + fmt + " " + elf + " " + output;
        int rc = std::system(oc.c_str()) == 0 ? 0 : 1;
        if (rc != 0) std::cerr << "错误: objcopy 转换失败（" << oc << "）\n";
        std::filesystem::remove(elf);
        return rc;
    }
    std::string cmd;
    if (kind == OutKind::StaticLib) {
        cmd = tc.ar + " rcs " + output;
        for (auto& o : objects) cmd += " " + o.string();
    } else {
        cmd = pickCC() + " -g" + tc.machine;
        if (kind == OutKind::SharedLib) cmd += " -shared";
        for (auto& o : objects) cmd += " " + o.string();
        cmd += " -o " + output + tc.ldflags;
    }
    if (std::system(cmd.c_str()) != 0) {
        std::cerr << "错误: 构建产物失败（" << cmd << "）\n";
        return 1;
    }
    // 调试信息打包（由目标平台决定）：macOS 把 .o 中的 debug map 打成 .dSYM，
    // 使 lldb 能映射回 .sc 源码（交叉/裸机目标 debugTool=none，跳过）
    if (tc.debugTool == "dsymutil" && kind != OutKind::StaticLib)
        std::system(("dsymutil " + output + " >/dev/null 2>&1").c_str());
    return 0;
}

// ----------------------------------------------------------------------------

// 编译 C 源码为产物
static int buildSource(const std::string& csrc,
                       const std::string& output,
                       const ToolConfig& tc) {
    const OutKind kind = outputKind(output);

    // 裸机镜像 .bin/.hex：先链接临时 .elf，再 objcopy 转换为原始/Intel-HEX 镜像
    if (kind == OutKind::Bin || kind == OutKind::Hex) {
        char tmpl[] = "/tmp/scc_img_XXXXXX";
        int fd = mkstemp(tmpl);
        if (fd < 0) { std::cerr << "错误: 无法创建临时文件\n"; return 1; }
        close(fd);
        const std::string elf = std::string(tmpl) + ".elf";
        std::string cmd = pickCC() + " -g" + tc.machine + tc.cflags
                        + " -x c - -o " + elf + tc.ldflags;
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) { std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n"; unlink(tmpl); return 1; }
        fwrite(csrc.data(), 1, csrc.size(), pipe);
        if (pclose(pipe) != 0) {
            std::cerr << "错误: C 编译失败（" << cmd << "）\n";
            unlink(tmpl); std::filesystem::remove(elf); return 1;
        }
        const char* fmt = (kind == OutKind::Bin) ? "binary" : "ihex";
        std::string oc = tc.objcopy + " -O " + fmt + " " + elf + " " + output;
        int rc = std::system(oc.c_str()) == 0 ? 0 : 1;
        if (rc != 0) std::cerr << "错误: objcopy 转换失败（" << oc << "）\n";
        unlink(tmpl); std::filesystem::remove(elf);
        return rc;
    }

    // 构建可执行文件：单命令编译+链接直达输出
    if (kind == OutKind::Exe) {
        std::string cmd = pickCC() + " -g" + tc.machine + tc.cflags + " -x c - -o " + output + tc.ldflags;
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) { 
            std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n";
             return 1; 
        }
        fwrite(csrc.data(), 1, csrc.size(), pipe);
        if (pclose(pipe) != 0) { 
            std::cerr << "错误: C 编译失败（" << cmd << "）\n";
            return 1; 
        }
        return 0;
    }

    // 库：先编译临时 .o，再 ar / -shared 合成
    char tmpTemplate[] = "/tmp/scc_build_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) { std::cerr << "错误: 无法创建临时目录\n"; return 1; }
    const std::filesystem::path tmpDir(dirC);
    const std::filesystem::path obj = tmpDir / "unit.o";
    std::string cmd = pickCC() + " -g" + tc.machine + tc.cflags
                    + (kind == OutKind::SharedLib ? " -fPIC" : "")
                    + " -x c - -c -o " + obj.string();
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) {
        std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n";
        std::filesystem::remove_all(tmpDir);
        return 1;
    }
    fwrite(csrc.data(), 1, csrc.size(), pipe);
    int rc = pclose(pipe) != 0 ? 1 : 0;
    if (rc != 0) std::cerr << "错误: C 编译失败（" << cmd << "）\n";
    if (rc == 0) rc = linkOutput(kind, {obj}, output, tc);
    std::filesystem::remove_all(tmpDir);
    return rc;
}

// 编译 C 源码为（临时）可执行文件并运行；程序参数透传，运行结束产物删除
static int compileAndRunSource(const std::string& csrc,
                               const std::vector<std::string>& progArgs,
                               const ToolConfig& tc) {

    // 1. 创建临时可执行文件路径
    char bin[] = "/tmp/scc_run_XXXXXX";
    int fd = mkstemp(bin);
    if (fd < 0) { 
        std::cerr << "错误: 无法创建临时文件\n";
        return 1; 
    }
    close(fd);

    // 2. 通过管道把 C 源码送给编译器，不落盘中间 .c 文件
    // 添加 -g 标志生成调试符号，便于 gdb/lldb 进行源代码级调试
    // 单命令编译+链接：machine（目标机器选项）/cflags 与 ldflags 都附加
    std::string cmd = pickCC() + " -g" + tc.machine + tc.cflags + " -x c - -o " + bin + tc.ldflags;
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) { 
        std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n"; unlink(bin);
        return 1; 
    }
    fwrite(csrc.data(), 1, csrc.size(), pipe);
    int crc = pclose(pipe);
    if (crc != 0) {
        std::cerr << "错误: C 编译失败（" << cmd << "）\n";
        unlink(bin);
        return 1;
    }

    // 3. fork+exec 运行产物，透传程序参数，避免 shell 转义问题
    //    交叉目标：本机无法直接执行，须经 runner（模拟器）包装；既无 runner
    //    又是跨平台族目标则报错，引导改用 --build 或配置 run。
    if (tc.crossRun && tc.runner.empty()) {
        std::cerr << "错误: 交叉目标无法在本机直接运行；请改用 --build 生成产物，"
                     "或配置 run = <模拟器>（如 qemu-arm -L <sysroot>）\n";
        unlink(bin);
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) { std::cerr << "错误: fork 失败\n"; unlink(bin); return 1; }
    if (pid == 0) {
        std::vector<char*> argv;
        // runner（如 "qemu-arm -L /sysroot"）拆成前缀 argv，再接产物与程序参数
        std::vector<std::string> runParts = splitBy(tc.runner, " ");
        for (auto& r : runParts) argv.push_back(const_cast<char*>(r.c_str()));
        argv.push_back(bin);
        for (auto& a : progArgs) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        if (!runParts.empty()) execvp(argv[0], argv.data());
        else execv(bin, argv.data());
        _exit(127);  // exec 失败
    }

    int st = 0;
    waitpid(pid, &st, 0);
    unlink(bin);                // 4. 清理临时产物
    if (WIFSIGNALED(st)) {
        std::cerr << "错误: 程序被信号 " << WTERMSIG(st) << " 终止\n";
        return 128 + WTERMSIG(st);
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

///////////////////////////////////////////////////////////////////////////////

static std::string stripDelims(const std::string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '<' && s.back() == '>'))
            return s.substr(1, s.size() - 2);
    }
    return s;
}

// 模块路径解析：根据 @inc 声明的原始字符串和当前单元目录，生成候选路径列表并返回第一个存在的路径
// + 搜索顺序：输入文件目录向上 → cwd 向上 → SCC_BUILTINS → 内嵌释放目录；
//   路径解析支持缺省 .sc 后缀（如 @inc "foo" 会搜索 foo 和 foo.sc）
static std::filesystem::path 
resolveModulePath(const std::string& raw, const std::filesystem::path& baseDir) {
    const std::string target = stripDelims(raw);
    const std::filesystem::path rel(target);
    const auto builtins = !baseDir.empty() ? findBuiltinsDir(baseDir) : std::filesystem::path{};
    const auto cwdBuiltins = findBuiltinsDir(std::filesystem::current_path());
    std::vector<std::filesystem::path> candidates = {
        rel,
        baseDir / rel,
    };
    if (!endsWith(target, ".sc")) {
        candidates.push_back(rel.string() + ".sc");
        candidates.push_back(baseDir / (rel.string() + ".sc"));
    }
    // builtins 搜索：直接路径 + 子项目形态 builtins/<名>/<名>.sc（如 adt/adt.sc）
    auto pushBuiltins = [&](const std::filesystem::path& b) {
        if (b.empty()) return;
        candidates.push_back(b / rel);
        const std::string stem = rel.stem().string();
        candidates.push_back(b / stem / (stem + ".sc"));
        if (!endsWith(target, ".sc")) candidates.push_back(b / (rel.string() + ".sc"));
    };
    pushBuiltins(g_builtinsOverride);  // --builtins 目标适配目录：最高优先级
    pushBuiltins(builtins);
    if (cwdBuiltins != builtins) pushBuiltins(cwdBuiltins);
    if (const char* envB = std::getenv("SCC_BUILTINS"))
        pushBuiltins(std::filesystem::path(envB));
#ifdef SCC_EMBED_BUILTINS
    pushBuiltins(embeddedBuiltinsDir());  // 内嵌资源释放目录（优先级最低）
#endif
    for (auto& c : candidates)
        if (!c.empty() && std::filesystem::exists(c) && std::filesystem::is_regular_file(c))
            return std::filesystem::weakly_canonical(c);
    return {};
}

struct UnitInfo {
    std::filesystem::path path;
    Program prog;
    std::vector<std::filesystem::path> deps;
};

// 解析单元依赖：扫描 @inc 声明，解析模块路径，标记 external 和 origin，返回直接依赖列表
static std::vector<std::filesystem::path> 
resolveUnitDeps(Program& prog, const std::filesystem::path& srcPath) {

    std::vector<std::filesystem::path> deps;
    const auto baseDir = srcPath.has_parent_path() ? srcPath.parent_path()
                                                   : std::filesystem::current_path();
    for (auto& d : prog.decls) {
        if (d->kind != Decl::IncD) continue;
        auto modPath = resolveModulePath(d->name, baseDir);
        if (!modPath.empty() && endsWith(modPath.string(), ".sc")) {
            d->external = true;
            d->origin = modPath.string();
            prog.externSymbols.push_back(modPath.string());
            deps.push_back(modPath);
        } else {
            d->external = false;
        }
    }
    // 合并直接依赖的 @导出声明（external 标记）：供跨模块语法糖
    //（方法调用/声明即构造/方法字段）识别，不参与本单元代码生成
    for (auto& dep : deps) {
        const std::string text = readWholeFile(dep);
        if (text.empty()) continue;
        try {
            Program mp = parse(lex(text));
            for (auto& md : mp.decls) {
                if (!md->exported) continue;
                md->external = true;
                md->origin = dep.string();
                prog.decls.push_back(std::move(md));
            }
        } catch (...) {
            // 依赖解析失败：留待该单元自身编译时报错
        }
    }
    return deps;
}

// 从项目入口文件加载模块图，递归解析依赖，检查语义；成功返回 true，失败返回 false 和错误信息
static bool loadUnitGraph(const std::filesystem::path& srcPath,
                          std::unordered_map<std::string, UnitInfo>& units,
                          std::unordered_set<std::string>& visiting,
                          std::string& errMsg) {

    // 循环依赖检测：已加载的模块直接返回，正在访问的模块再次访问则报错
    const auto canon = std::filesystem::weakly_canonical(srcPath);
    const std::string key = canon.string();
    if (units.find(key) != units.end()) return true;
    if (!visiting.insert(key).second) {
        errMsg = "检测到循环模块依赖: " + key;
        return false;
    }

    // 读取源文件
    const std::string src = readWholeFile(canon);
    if (src.empty()) {
        errMsg = "无法读取模块文件: " + key;
        visiting.erase(key);
        return false;
    }

    UnitInfo u;
    u.path = canon;
    u.prog = parse(lex(src));                   // 解析源代码为 AST
    u.deps = resolveUnitDeps(u.prog, canon);    // 先合并依赖导出声明（external）
    semanticCheck(u.prog);                      // 再检查：导入类型/方法可见

    // 递归加载依赖模块
    for (auto& dep : u.deps) {
        if (!loadUnitGraph(dep, units, visiting, errMsg)) {
            visiting.erase(key);
            return false;
        }
    }

    // 加载成功：记录单元信息，标记访问完成
    units[key] = std::move(u);
    visiting.erase(key);
    return true;
}

// 把模块图中所有单元生成 .c/.h 并编译为 .o
// + extraCFlags 用于追加单元级编译选项（如动态库的 -fPIC）；成功返回 0
//   extraLd 返回子项目需要的额外链接选项（如 Linux 上 m 的 -lpthread）
static int compileUnitsToObjects(std::unordered_map<std::string, UnitInfo>& units,
                                 const ToolConfig& tc,
                                 const std::string& extraCFlags,
                                 const std::filesystem::path& tmpDir,
                                 std::vector<std::filesystem::path>& objects,
                                 std::string* extraLd = nullptr) {
    struct UnitArtifact {
        std::filesystem::path cpath;
        std::filesystem::path hpath;
        std::filesystem::path opath;
        std::filesystem::path srcDir;  // 源 .sc 所在目录（解析 inc "local.h"）
    };
    std::vector<UnitArtifact> arts;

    // 第一阶段：为每个模块生成 .c/.h 单元
    for (auto& kv : units) {
        const std::string token = moduleFileToken(kv.first);
        const std::string hname = token + ".h";
        const std::filesystem::path cpath = tmpDir / (token + ".c");
        const std::filesystem::path hpath = tmpDir / hname;
        const std::filesystem::path opath = tmpDir / (token + ".o");

        // stringify 关键字：按类型生成的 JSON 格式化器写入独立 <token>_stringify.h，
        // 由本单元 .c 在类型定义之后 #include（-I tmpDir 使其可见）
        const std::string sofName = token + "_stringify.h";
        std::string sofSrc;
        const std::string csrc = emitC(kv.second.prog, kv.first, sofName, &sofSrc);  // 带 #line 源码映射
        if (!writeTextFile(cpath, csrc)) return 1;
        if (!sofSrc.empty() && !writeTextFile(tmpDir / sofName, sofSrc)) return 1;

        std::string hsrc = emitCHeader(kv.second.prog, guardFromHeaderName(hname));
        if (hsrc.empty()) {
            const std::string guard = guardFromHeaderName(hname);
            hsrc = "#ifndef " + guard + "\n#define " + guard + "\n#endif\n";
        }
        if (!writeTextFile(hpath, hsrc)) return 1;
        
        arts.push_back({cpath, hpath, opath, kv.second.path.parent_path()});
    }

    // 第二阶段：统一编译所有 .c -> .o
    for (auto& a : arts) {
        // 添加 -g 标志生成调试符号；-I 源目录使 inc "local.h" 可被找到
        std::string ccCmd = pickCC() + " -g" + tc.machine + tc.cflags + extraCFlags + " -I " + tmpDir.string();
        if (!a.srcDir.empty()) ccCmd += " -I " + a.srcDir.string();
        ccCmd += " -c " + a.cpath.string() + " -o " + a.opath.string();
        if (std::system(ccCmd.c_str()) != 0) {
            std::cerr << "错误: C 单元编译失败（" << ccCmd << "）\n";
            return 1;
        }
        objects.push_back(a.opath);
    }

    // 第三阶段：builtins 子项目实现自动参与链接
    //   子项目形态 <目录>/x/x.sc，同目录存在 x_impl.c（自动编译，
    //   -I 自身目录 + -I 上级目录使 "x.h"/"platform.h" 可见）或
    //   x.a（内嵌发行版释放的预编译库，直接链接）；两者皆无则跳过。
    //   adt 特例：--adt <x.c|x.o|x.a> 可替换默认实现。
    for (auto& kv : units) {
        const std::filesystem::path p(kv.first);
        const std::string stem = p.stem().string();
        if (p.extension() != ".sc" || p.parent_path().filename() != stem) continue;
        const std::filesystem::path dir = p.parent_path();

        std::filesystem::path impl;
        if (stem == "adt" && !tc.adtImpl.empty()) {
            impl = tc.adtImpl;
            if (!std::filesystem::exists(impl)) {
                std::cerr << "错误: adt 实现文件不存在: " << impl.string() << "\n";
                return 1;
            }
        } else {
            impl = dir / (stem + "_impl.c");
            if (!std::filesystem::exists(impl)) impl = dir / (stem + ".a");
            if (!std::filesystem::exists(impl)) continue;  // 非子项目实现形态
        }
        // 线程库由目标平台表决定（Linux=-lpthread；macOS/Windows/裸机=空）
        if (stem == "m" && extraLd && !tc.threadsLib.empty()
            && extraLd->find(tc.threadsLib) == std::string::npos)
            *extraLd += " " + tc.threadsLib;
        if (impl.extension() == ".c") {
            const std::filesystem::path obj = tmpDir / (stem + "_impl.o");
            std::string ccCmd = pickCC() + " -g" + tc.machine + tc.cflags + extraCFlags
                + " -I " + dir.string()
                + " -I " + dir.parent_path().string()
                + " -c " + impl.string() + " -o " + obj.string();
            if (std::system(ccCmd.c_str()) != 0) {
                std::cerr << "错误: " << stem << " 实现编译失败（" << ccCmd << "）\n";
                return 1;
            }
            objects.push_back(obj);
        } else {
            objects.push_back(impl);  // .o/.a 直接参与链接
        }
    }
    return 0;
}

// 从项目入口文件加载，编译为目标产物（不执行、产物保留）
static int buildProject(const std::filesystem::path& rootPath,
                        const std::string& output,
                        const ToolConfig& tc) {

    // 1. 加载模块图
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    // 2. 创建临时目录
    char tmpTemplate[] = "/tmp/scc_units_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) {
        std::cerr << "错误: 无法创建临时目录\n";
        return 1;
    }
    const std::filesystem::path tmpDir(dirC);

    const OutKind kind = outputKind(output);

    // 3. 编译所有单元为对象文件到临时目录
    std::vector<std::filesystem::path> objects;
    std::string extraLd;
    int rc = compileUnitsToObjects(units, tc,
                                   kind == OutKind::SharedLib ? " -fPIC" : "",
                                   tmpDir, objects, &extraLd);
    if (rc == 0) {

        // 4. 链接所有对象文件为最终产物
        ToolConfig tcLink = tc;
        tcLink.ldflags += extraLd;
        rc = linkOutput(kind, objects, output, tcLink);
    }

    // 5. 清理临时产物
    std::filesystem::remove_all(tmpDir);
    return rc;
}

// 从项目入口文件加载，编译为（临时）可执行文件并运行；程序参数透传，运行结束产物删除
static int compileAndRunProject(const std::filesystem::path& rootPath,
                                const std::vector<std::string>& progArgs,
                                const ToolConfig& tc) {

    // 1. 加载模块图
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    // 2. 编译所有单元为对象文件，产物放在临时目录；成功返回 0，失败清理后退出
    char tmpTemplate[] = "/tmp/scc_units_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) {
        std::cerr << "错误: 无法创建临时目录\n";
        return 1;
    }
    const std::filesystem::path tmpDir(dirC);
    std::vector<std::filesystem::path> objects;
    std::string extraLd;
    if (compileUnitsToObjects(units, tc, "", tmpDir, objects, &extraLd) != 0) {
        std::filesystem::remove_all(tmpDir);
        return 1;
    }

    // 3. 链接所有对象文件为临时可执行文件，添加 -g 以保留调试符号；成功返回 0，失败清理后退出
    const std::filesystem::path bin = tmpDir / "run.out";
    std::string linkCmd = pickCC() + " -g" + tc.machine;        // -g 保留调试符号；machine 目标机器选项
    for (auto& o : objects) linkCmd += " " + o.string();        // 构造要链接的所有对象文件列表
    linkCmd += " -o " + bin.string() + tc.ldflags + extraLd;
    if (std::system(linkCmd.c_str()) != 0) {
        std::cerr << "错误: 链接失败（" << linkCmd << "）\n";
        std::filesystem::remove_all(tmpDir);
        return 1;
    }

    // 4. fork+exec 运行产物，透传程序参数，避免 shell 转义问题；运行结束清理临时目录
    //    交叉目标须经 runner（模拟器）包装；跨平台族且无 runner 则报错
    if (tc.crossRun && tc.runner.empty()) {
        std::cerr << "错误: 交叉目标无法在本机直接运行；请改用 --build 生成产物，"
                     "或配置 run = <模拟器>（如 qemu-arm -L <sysroot>）\n";
        std::filesystem::remove_all(tmpDir);
        return 1;
    }
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "错误: fork 失败\n";
        std::filesystem::remove_all(tmpDir);
        return 1;
    }
    if (pid == 0) {
        std::vector<char*> argv;
        std::string binS = bin.string();
        std::vector<std::string> runParts = splitBy(tc.runner, " ");
        for (auto& r : runParts) argv.push_back(const_cast<char*>(r.c_str()));
        argv.push_back(const_cast<char*>(binS.c_str()));
        for (auto& a : progArgs) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        if (!runParts.empty()) execvp(argv[0], argv.data());
        else execv(binS.c_str(), argv.data());
        _exit(127);  // exec 失败
    }

    int st = 0;
    waitpid(pid, &st, 0);
    std::filesystem::remove_all(tmpDir);    // 5. 清理临时产物
    if (WIFSIGNALED(st)) {
        std::cerr << "错误: 程序被信号 " << WTERMSIG(st) << " 终止\n";
        return 128 + WTERMSIG(st);
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

///////////////////////////////////////////////////////////////////////////////

int main(int argc, char** argv) {

    // 环境变量 SCC_TARGET 指定的目标档先加载（命令行 --target 后加载可覆盖其键值）
    if (const char* envT = std::getenv("SCC_TARGET")) {
        if (*envT) loadProfile(envT);
    }

    // ---- 1. 解析命令行参数 ----
    std::string input, output, mode = "run";  // 默认：编译+执行
    std::vector<std::string> progArgs;        // '--' 后透传给被执行程序的参数
    std::vector<std::string> cmdLibs;         // -l 指定的链接库名
    std::string adtOpt;                       // --adt 指定的 adt 自定义实现
    bool bareO = false;                       // -o 未带值（按输入文件名+模式后缀推导）
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--") {                                     // 其后全部为程序参数
            for (i++; i < argc; i++) progArgs.push_back(argv[i]);
            break;
        }
        else if (a == "-o") {
            // -o 后跟非选项值则为输出文件名；缺省（最后一个参数 / 后接选项 / 后接 '--'）
            // 视为 bareO，最后按输入文件名 + 模式后缀推导输出路径
            if (i + 1 < argc && argv[i + 1][0] != '-' && std::string(argv[i + 1]) != "--")
                output = argv[++i];
            else bareO = true;
        }
        else if (a == "-l" && i + 1 < argc) cmdLibs.push_back(argv[++i]);  // -l m
        else if (a == "--adt" && i + 1 < argc) adtOpt = argv[++i];  // adt 自定义实现
        else if (a == "--target" && i + 1 < argc) loadProfile(argv[++i]);  // 交叉编译目标档
        else if (a == "--builtins" && i + 1 < argc)                 // 目标适配 builtins 目录
            g_builtinsOverride = argv[++i];
        else if (a.size() > 2 && a.compare(0, 2, "-l") == 0)
            cmdLibs.push_back(a.substr(2));                  // -lm 写法
        else if (a == "--build") mode = "build";             // 构建产物模式
        else if (a == "--emit-c") mode = "c";                // 转译 C 模式
        else if (a == "--ast") mode = "ast";                 // AST JSON 模式
        else if (a == "--emit-sc") mode = "sc";              // 再生 sc 模式
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (input.empty()) input = a;                   // 第一个非选项参数 = 输入文件
        else { usage(); return 1; }
    }
    if (input.empty()) { usage(); return 1; }

    // 裸 -o（未指定文件名）：按输入文件名 + 模式后缀推导输出路径，写入输入文件所在目录
    //   --emit-c → .c     --ast → .json     --emit-sc → .out.sc     --build → 无后缀
    //   run 模式：忽略 bareO（无中间产物）；stdin 输入：要求显式 -o <file>
    if (bareO && output.empty() && input != "-") {
        const std::filesystem::path ip = input;
        const std::string stem = ip.stem().string();
        std::string name;
        if (mode == "c")        name = stem + ".c";
        else if (mode == "ast") name = stem + ".json";
        else if (mode == "sc")  name = stem + ".out.sc";
        else if (mode == "build") name = stem;
        if (!name.empty()) {
            const auto dir = ip.has_parent_path() ? ip.parent_path() : std::filesystem::path(".");
            output = (dir / name).string();
        }
    }

    // ---- 2. 读取源码（文件或 stdin）----
    std::stringstream ss;
    if (input == "-") {
        ss << std::cin.rdbuf();   // stdin 模式：管道输入
    } else {
        std::ifstream fin(input);
        if (!fin) {
            std::cerr << "错误: 无法打开文件 " << input << "\n";
            return 1;
        }
        ss << fin.rdbuf();        // 将整个文件读入内存 stringstream
    }
    std::string src = ss.str();

    // ---- 3. 编译流水线 + 输出 ----
    try {

        auto prog = parse(lex(src));                                // 词法分析(源码 → token 流) -> 语法分析(token 流 → AST 程序树)
        if (input != "-")                                           // 对于目标工程文件输入
            resolveUnitDeps(prog, std::filesystem::path(input));    // + 记录当前单元的模块依赖信息（不展开源码，合并依赖导出声明）
        semanticCheck(prog);                                        // 语义检查：类型/方法可见性、@导出对象合法性等

        // 3c. 代码生成：根据 mode 选择后端（run 模式也先生成 C）
        std::string sofHeaderSrc;  // --emit-c -o 模式下 stringify 格式化器（同级 stringify.h）
        auto c = mode == "ast" ? emitAstJson(prog)                  // AST→JSON
               : mode == "sc"  ? emitSc(prog)                       // AST→规范化sc
               : (mode == "c" && !output.empty())                   // --emit-c 到文件：分离 stringify.h
                     ? emitC(prog, "", "stringify.h", &sofHeaderSrc)
                     : emitC(prog);                                 // run/stdout：内联自包含

        // 3d. run 模式：不保存中间文件，直接编译并执行（run/build 模式应用工具链扩展配置）
        if (mode == "run") {
            
            ToolConfig tc = loadToolConfig(cmdLibs, adtOpt);
            addBuiltinsInclude(tc, input);                      // builtins 根级别（platform.h 等）头文件默认可见
            
            if (input == "-") return compileAndRunSource(c, progArgs, tc);
            return compileAndRunProject(std::filesystem::path(input), progArgs, tc);
        }

        // 3d'. build 模式：编译链接为持久产物（类型按 -o 后缀决定）
        if (mode == "build") {

            ToolConfig tc = loadToolConfig(cmdLibs, adtOpt);
            addBuiltinsInclude(tc, input);                      // builtins 根级别（platform.h 等）头文件默认可见

            std::string out = output;
            if (out.empty()) {
                if (input == "-") {
                    std::cerr << "错误: stdin 输入的构建模式必须用 -o 指定输出文件\n";
                    return 1;
                }
                out = std::filesystem::path(input).stem().string();  // 默认使用（去除 .sc 后缀的）输入文件名
            }
            int rc = input == "-" ? buildSource(c, out, tc)
                                  : buildProject(std::filesystem::path(input), out, tc);
            if (rc != 0) return rc;

            // 构建库时：存在 @导出对象则生成同名 .h 接口头文件（取根模块导出）
            if (outputKind(out) != OutKind::Exe) {

                // 头文件路径：输出文件名去库类型后缀改 .h
                std::string hpath = out;
                for (const char* ext : {".a", ".so", ".dylib"}) {
                    if (endsWith(hpath, ext)) { hpath.resize(hpath.size() - strlen(ext)); break; }
                }
                hpath += ".h";

                // 构建 include guard 宏名：文件名转大写，非字母数字转 '_'
                std::string guard;
                size_t slash = hpath.find_last_of('/');
                for (char ch : hpath.substr(slash == std::string::npos ? 0 : slash + 1))
                    guard += isalnum((unsigned char)ch) ? (char)toupper(ch) : '_';

                auto h = emitCHeader(prog, guard);
                if (!h.empty() && !writeTextFile(hpath, h)) return 1;
            }
            return 0;
        }

        // 3e. --emit-c 且指定了输出文件：若存在 @导出对象，额外生成 .h 头文件
        if (mode == "c" && !output.empty()) {

            // 头文件路径：输出文件名去 .c 后缀改 .h
            std::string hpath = output;
            if (hpath.size() > 2 && hpath.compare(hpath.size() - 2, 2, ".c") == 0)
                hpath.resize(hpath.size() - 2);
            hpath += ".h";

            // 构建 include guard 宏名：文件名转大写，非字母数字转 '_'
            std::string guard;
            size_t slash = hpath.find_last_of('/');
            for (char ch : hpath.substr(slash == std::string::npos ? 0 : slash + 1))
                guard += isalnum((unsigned char)ch) ? (char)toupper(ch) : '_';

            auto h = emitCHeader(prog, guard);
            if (!h.empty() && !writeTextFile(hpath, h)) return 1;
        }

        // 3f. 输出结果到文件或 stdout
        if (output.empty()) std::cout << c;
        else if (!writeTextFile(output, c)) return 1;

        // 3f'. --emit-c 到文件且程序使用了 stringify：在同目录写出 stringify.h（由 .c #include）
        if (mode == "c" && !output.empty() && !sofHeaderSrc.empty()) {
            std::filesystem::path sofPath =
                std::filesystem::path(output).parent_path() / "stringify.h";
            if (!writeTextFile(sofPath, sofHeaderSrc)) return 1;
        }

        // 3f''. --emit-c 到文件：为每个用户 .sc 模块依赖生成同级 scm_<token>.h，
        // 使输出目录自包含；用户编译时只需 -I <输出目录> -I <builtins>。
        // 带手写 C ABI 头的子项目模块（builtins adt/io 等）由 .c 直接 #include
        // "<name>/<name>.h"（随 -I <builtins> 可见），不复制其内部 scm 头。
        if (mode == "c" && !output.empty() && input != "-") {
            std::unordered_map<std::string, UnitInfo> depUnits;
            std::unordered_set<std::string> visiting;
            std::string depErr;
            if (loadUnitGraph(std::filesystem::path(input),
                              depUnits, visiting, depErr)) {
                const std::string rootKey = std::filesystem::weakly_canonical(
                    std::filesystem::path(input)).string();
                const std::filesystem::path outPath = output;
                const std::filesystem::path outDir =
                    outPath.has_parent_path() ? outPath.parent_path()
                                              : std::filesystem::path(".");
                for (auto& kv : depUnits) {
                    if (kv.first == rootKey) continue;  // 根模块由 .c 自身提供
                    // 子项目契约模块（<dir>/<name>/<name>.sc + 同目录 <name>.h）跳过
                    const std::filesystem::path mp = kv.second.path;
                    const std::string mstem = mp.stem().string();
                    if (mp.has_parent_path() &&
                        mp.parent_path().filename() == mstem &&
                        std::filesystem::exists(mp.parent_path() / (mstem + ".h")))
                        continue;
                    const std::string token = moduleFileToken(kv.first);
                    const std::string hname = token + ".h";
                    std::string hsrc = emitCHeader(kv.second.prog,
                                                   guardFromHeaderName(hname));
                    if (hsrc.empty()) {
                        const std::string g = guardFromHeaderName(hname);
                        hsrc = "#ifndef " + g + "\n#define " + g + "\n#endif\n";
                    }
                    if (!writeTextFile((outDir / hname).string(), hsrc)) return 1;
                }
            }
        }

    } catch (CompileError e) {

        // 从源码中提取指定行的文本，为编译错误添加源代码行诊断信息
        if (e.file.empty()) e.file = input;
        if (e.srcLine.empty() && e.line > 0) { int curLine = 1;

            for (size_t i = 0; i < src.size(); i++) {

                if (curLine == e.line) {

                    // 定位到行首，提取该行文本（不包含行尾换行符）
                    size_t end = src.find('\n', i);
                    if (end == std::string::npos) end = src.size();
                    e.srcLine = src.substr(i, end - i);
                    
                    // 去掉行尾空白
                    while (!e.srcLine.empty() && (e.srcLine.back() == '\r' || e.srcLine.back() == '\n'))
                        e.srcLine.pop_back();
                    break;
                }
                if (src[i] == '\n') curLine++;
            }
        }

        // 详细诊断输出：文件:行号: 错误: 消息 + 上下文代码 + 修复建议
        std::cerr << (e.file.empty() ? input : e.file) << ":" << e.line 
                  << ": \033[1;31m错误\033[0m: " << e.msg << "\n";
        if (!e.srcLine.empty()) {
            std::cerr << "  |  " << e.srcLine << "\n";
        }
        if (!e.hint.empty()) {
            std::cerr << "  \033[1;36m提示\033[0m: " << e.hint << "\n";
        }
        return 1;
    }
    return 0;
}
