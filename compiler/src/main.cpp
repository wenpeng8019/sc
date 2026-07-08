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
//   --api    → emitScApi()   导出接口摘要（仅 @导出 定义项签名，形如 C 头）
//
// 所有编译错误通过 CompileError 异常传播，在此统一捕获并格式化输出。
// ============================================================
#include "ast_json.h"
#include "ast_print.h"
#include "codegen_c.h"
#include "codegen_glsl.h"
#include "codegen_sc.h"
#include "error.h"
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "cheaders.h"
#include "remote.h"
#include "proggraph.h"
#include <algorithm>
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
              << "  -D<宏>[=值]  透传宏定义给 C 编译器（可重复；-D FOO=1 分写亦可）；\n"
              << "             最高优先级，追加在末尾，可覆盖配置/内置默认（如 -DSC_PRINT_BUF=4096）\n"
              << "  --cflags <opts>  透传任意 C 编译选项给 C 编译器（可重复；如 --cflags -O3）\n"
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
              << "  --from <path>  stdin（'-'）输入时指定源文件路径，作为 inc 解析的基准目录\n"
              << "             （供编辑器实时分析未保存内容时仍能合并外部描述符）\n"
              << "  --clang [lib]  解析 C 头 inc 的外部描述符（仅 --ast）；省略 lib 时自动检测\n"
              << "             平台默认位置的 libclang，检测失败报错；亦可用 SCC_CLANG 指定。\n"
              << "             目标平台头解析复用交叉编译配置（triple/sysroot/inc/cflags）；\n"
              << "             完全不带 --clang 则退化为头文件文本匹配\n"
              << "  --emit-sc  从 AST 再生成规范化 sc 源码\n"
              << "  --api      输出模块导出接口摘要（仅 @导出 定义项签名，形如 C 头；配合 -o 缺省 stdout）\n"
              << "  --graph    程序结构依赖图（proggraph）：默认整程序（递归解析全部 inc 依赖），\n"
              << "             Decl 级节点 + 调用/类型/读写/方法/构造/宏/token/模块边，从 main（可执行）\n"
              << "             或 @导出（库）做激活分析；-o *.html→自包含可视化，其余/stdout→JSON。\n"
              << "             --graph=unit 仅分析当前单元（外部引用建为叶子节点）\n"
              << "  --check=ref  开启自动指针 T@ 栈悬挂检查：注入栈对象引用头并在退域处\n"
              << "             断言悬挂（含源码定位）；默认关闭（堆 ARC 自动回收始终生效）\n"
              << "  --check=mem  开启越界 canary：ref 头堆对象注入头尾哨兵（地址派生魔数）、\n"
              << "             一维栈数组注入尾哨兵、托管目标注入 -fstack-protector-strong\n"
              << "             保护返回地址；越界损坏报告并定位，默认关闭\n"
              << "  --check=ptr  开启运行时指针/下标守卫：解引用与指针下标处校验 nil、\n"
              << "             编译期已知维度的栈数组下标处校验越界；命中报告并 abort，默认关闭\n"
              << "  --test     单元测试模式：编译目标文件的 tst 用例为测试 runner 并运行；\n"
              << "             逐用例隔离执行（assert 失败软中止本例继续下一例），\n"
              << "             汇总 通过/失败/跳过，退出码为失败用例数\n"
              << "  -o <file>  输出文件（--build/--emit-c/--ast/--emit-sc/--api 模式下有效）\n"
              << "             裸 -o 不带值时按输入文件名 + 模式后缀推导，写入输入文件所在目录：\n"
              << "             --emit-c→.c  --ast→.json  --emit-sc→.out.sc  --api→.api.sc  --build→无后缀\n"
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
    std::string targetSuffix;// target_suffix：add 预编译库优先匹配 <名>.<suffix>.<ext>（空=回退三元组）
    std::string threadsLib;  // 线程库链接选项（平台表/显式 threads 解析，如 "-lpthread"）
    std::string debugTool;   // 链接后调试打包步骤（"dsymutil" / "none"）
    bool freestanding = false; // 裸机档：目标无托管运行时（SCC_FREESTANDING/freestanding=1）
    bool crossRun = false;   // 目标平台与本机不同族 → 不能直接 exec（须 runner 或 --build）

    // ---- 远程工具链构建（remote toolchain build）----
    // 设了 buildHost 即启用：本机生成 C，推到远端用其「原生」工具链编译，
    // 取回产物（--build）或在远端运行并回传输出（run）。详见 remote.h。
    std::string buildHost;   // build_host：远程构建主机（空=禁用远程构建）
    std::string buildUser;   // build_user：远程登录用户（空=当前用户/ssh 配置默认）
    std::string buildDir;    // build_dir：远程构建根目录（空=默认 ~/.cache/scc-remote）
    std::string buildPort;   // build_port：SSH 端口（空=22）
    std::string buildKey;    // build_key：私钥文件路径（空=ssh 默认/agent）
    std::string remoteCC;    // remote_cc：远端编译器名（空=cc；远端原生，不带本机 machine）
    std::string remoteOS;    // remote_os：windows（远端 cmd.exe）| 空=posix
    std::string ccStyle;     // cc_style：msvc（cl.exe 风格）| 空=gcc 风格
    std::string vcvars;      // vcvars：MSVC vcvars64.bat 路径（远端 cl 前 call，置 INCLUDE/LIB）
    std::string sshBackend;  // ssh_backend：system（调系统 ssh/scp）| libssh2（内置，默认）
    std::string builtinsDir; // 本机 builtins 目录（远程构建时整目录推送，重写 -I）
    // run_interactive（仅 Windows 远端有效）：远端「运行」模式下，把产物经计划任务
    //   （schtasks /it）投递到「当前登录用户的交互会话」启动，令 GUI 窗口出现在其
    //   物理控制台 / RDP 桌面。默认关闭——SSH 启动的进程落在会话 0（服务会话），
    //   其窗口在交互桌面不可见（Windows 会话隔离，非 scc 缺陷）。开启后为「发射即忘」：
    //   不回传 stdout/退出码，且产物运行中会话目录不清理（exe 被占用无法删）。见 remote.h。
    bool runInteractive = false; // SCC_RUN_INTERACTIVE / run_interactive
    bool remoteBuild() const { return !buildHost.empty(); }
};

// 目标档（--target 文件）键值表：configValue 在环境变量之后、./.sc 配置之前回退到此。
// 优先级：环境变量 SCC_* > --target 目标档 > ./.sc 配置 > 内置默认
static std::map<std::string, std::string> g_profile;

// --builtins 指定的目标适配 builtins 目录（最高优先级；空=按默认搜索）
static std::filesystem::path g_builtinsOverride;

// 去掉值尾部的行内注释（' #' 或 '\t#' 起，至行尾）：令目标档/.sc 配置可在值后写注释，
// 如 `build_host = 1.2.3.4   # 远端`。不影响行首整行注释。仅识别空白后的 #，故不误伤
// 值内紧贴的 #（罕见，如颜色 #fff）。
static std::string stripInlineComment(const std::string& v) {
    for (size_t i = 0; i + 1 < v.size(); ++i)
        if ((v[i] == ' ' || v[i] == '\t') && v[i + 1] == '#')
            return v.substr(0, i);
    return v;
}

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
        if (trim(l.substr(0, eq)) == key) return trim(stripInlineComment(l.substr(eq + 1)));
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
        g_profile[trim(l.substr(0, eq))] = trim(stripInlineComment(l.substr(eq + 1)));
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

// 汇总所有扩展配置为两段命令行片段；extraLibs 来自命令行 -l、extraCflags 来自 -D/--cflags
static ToolConfig loadToolConfig(const std::vector<std::string>& extraLibs,
                                 const std::string& adtOpt = "",
                                 const std::vector<std::string>& extraCflags = {}) {
    ToolConfig tc;
    const std::string cflags = configValue("SCC_CFLAGS", "cflags");
    if (!cflags.empty()) tc.cflags += " " + cflags;
    for (auto& p : splitBy(configValue("SCC_INC", "inc"), ":"))
        tc.cflags += " -I " + p;
    // ldflags 累加：目标档/.sc 的 ldflags 作基线，环境 SCC_LDFLAGS 追加其后。
    // 链接库天然可累加（不同于工具程序的"覆盖"语义）——否则命令行传 GUI 库会
    // 丢失目标档里的基线库（如 Windows 的 ws2_32）。
    {
        std::string base;
        auto it = g_profile.find("ldflags");
        if (it != g_profile.end() && !it->second.empty()) base = it->second;
        else base = readConfig("ldflags");
        if (!base.empty()) tc.ldflags += " " + base;
        const char* envld = std::getenv("SCC_LDFLAGS");
        if (envld && *envld) tc.ldflags += std::string(" ") + envld;
    }
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
    // add 预编译库的目标变体后缀：显式 target_suffix 优先，否则回退三元组
    tc.targetSuffix = configValue("SCC_TARGET_SUFFIX", "target_suffix");
    if (tc.targetSuffix.empty()) tc.targetSuffix = tc.triple;
    const std::string host = hostTriple();
    const std::string effective = tc.triple.empty() ? host : tc.triple;
    if (platformFamily(effective) == "bare") tc.freestanding = true;

    // platform.h 目标定向：交叉目标（显式 triple）下按目标族注入 SC_TARGET_<族>，
    // 令 builtins/platform.h 的平台分支以「目标平台」为准，而非回退到 C 编译器
    // 默认目标（常为宿主）。本机构建（triple 空）不注入，保持自动判定。
    // 仅 win/darwin/linux 三族注入（platform.h 仅识别这三者的显式覆盖）；
    // bsd 归 unknown 不注入，仍由交叉工具链预定义宏自动判定。
    if (!tc.triple.empty()) {
        const std::string fam = platformFamily(tc.triple);
        if      (fam == "windows") tc.machine += " -D SC_TARGET_WIN";
        else if (fam == "darwin")  tc.machine += " -D SC_TARGET_DARWIN";
        else if (fam == "linux")   tc.machine += " -D SC_TARGET_LINUX";
    }

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

    // --check=mem：为托管目标注入 -fstack-protector-strong（栈哨兵，捕获栈溢出破坏
    // 返回地址）+ -DMEM_DEBUG（令 chunk 池给每块加尾金丝雀 + 双重释放/野指针检测，
    // 覆盖默认走池化的胖 T@ 与 rpc 帧）。裸分配的 T<raw>() 则另由 codegen SC_CANARY 守护。
    // 裸机（freestanding）跳过：通常无 __stack_chk_* / MEM_DEBUG 所需的 fprintf/abort 运行时支持。
    if (getMemCheck() && !tc.freestanding)
        tc.cflags += " -fstack-protector-strong -DMEM_DEBUG";

    // host≠target（平台族不同）：不能在本机直接运行
    tc.crossRun = !tc.triple.empty() &&
                  platformFamily(tc.triple) != platformFamily(host);

    // ---- 远程工具链构建配置 ----
    tc.buildHost  = configValue("SCC_BUILD_HOST", "build_host");
    tc.buildUser  = configValue("SCC_BUILD_USER", "build_user");
    tc.buildDir   = configValue("SCC_BUILD_DIR",  "build_dir");
    tc.buildPort  = configValue("SCC_BUILD_PORT", "build_port");
    tc.buildKey   = configValue("SCC_BUILD_KEY",  "build_key");
    tc.remoteCC   = configValue("SCC_REMOTE_CC",  "remote_cc");
    tc.remoteOS   = configValue("SCC_REMOTE_OS",  "remote_os");
    tc.ccStyle    = configValue("SCC_CC_STYLE",   "cc_style");
    tc.vcvars     = configValue("SCC_VCVARS",     "vcvars");
    tc.sshBackend = configValue("SCC_SSH_BACKEND","ssh_backend");
    // 交互会话运行开关（真值：1/true/yes/on）。仅 Windows 远端 run 模式生效。
    const std::string riVal = configValue("SCC_RUN_INTERACTIVE", "run_interactive");
    tc.runInteractive = (riVal == "1" || riVal == "true" ||
                         riVal == "yes" || riVal == "on");

    // 命令行 -D/--cflags：最高优先级，追加在末尾（同名宏后者覆盖前者）
    for (auto& f : extraCflags)
        if (!f.empty()) tc.cflags += " " + f;
    return tc;
}

// add 预编译库/对象的目标匹配：优先 <名>.<suffix>.<ext>（如 libssh2.aarch64-linux.a），
// 该变体不存在则回退原名。仅作用于 .a/.so/.dylib/.o（源码由现场/远端重编，无需变体）。
static std::filesystem::path resolveAddArtifact(const std::filesystem::path& p,
                                                const std::string& suffix) {
    if (suffix.empty()) return p;
    const std::string ext = p.extension().string();
    if (ext != ".a" && ext != ".so" && ext != ".dylib" && ext != ".o") return p;
    const std::filesystem::path cand =
        p.parent_path() / (p.stem().string() + "." + suffix + ext);
    std::error_code ec;
    if (std::filesystem::exists(cand, ec)) return cand;
    return p;
}

// 远程工具链构建分发：把本机生成的 C 推到远端编译。
//   output 非空 → --build（取回产物到 output）；output 空 → 远端运行（progArgs 透传）。
//   deps：add 指令引入的原生依赖（源码远端重编 / 预编译产物链接）。
// 返回进程退出码（run 模式即远端程序退出码）。
// 前置声明：远程多单元准备——加载单元图、生成全部单元 .c/.h 到临时目录，填充 job
// （unitsDir/units/ldLibs）。失败置 err 返回 false。定义见 buildProject 之后。
static bool prepareRemoteUnits(const std::filesystem::path& rootPath,
                               const ToolConfig& tc, RemoteJob& job, std::string& err);
static int remoteDispatch(const std::string& csrc, const ToolConfig& tc,
                          const std::string& output,
                          const std::vector<std::string>& progArgs,
                          const std::vector<RemoteDep>& deps = {},
                          const std::string& rootPath = "") {
    RemoteJob job;
    job.target.host    = tc.buildHost;
    job.target.user    = tc.buildUser;
    job.target.dir     = tc.buildDir;
    job.target.port    = tc.buildPort;
    job.target.key     = tc.buildKey;
    job.target.backend = tc.sshBackend;
    // 远端 OS：显式 remote_os=windows，或目标三元组为 windows 族（msvc/mingw/win）
    job.target.windows = (tc.remoteOS == "windows") ||
                         (platformFamily(tc.triple) == "windows");
    job.csrc        = csrc;
    job.builtinsDir = tc.builtinsDir;
    job.remoteCC    = tc.remoteCC;
    // 编译器风味：显式 cc_style 优先，否则 windows 或 remote_cc=cl 默认 msvc
    job.ccStyle     = !tc.ccStyle.empty() ? tc.ccStyle
                    : ((job.target.windows || tc.remoteCC == "cl") ? std::string("msvc")
                                                                   : std::string());
    job.vcvars      = tc.vcvars;
    job.output      = output;
    job.progArgs    = progArgs;
    job.deps        = deps;
    job.runInteractive = tc.runInteractive;
    // 多单元构建：文件输入走完整单元图——为每个单元（含库模块 adt 等）生成 .c（含
    //   sc_mod_*_init/drop 定义、拼入 <stem>_impl.c），全部上传远端编译链接，根治自包含
    //   单 TU 漏掉库模块生命周期定义的链接错误。stdin（无文件图）回退单 TU csrc。
    if (!rootPath.empty() && rootPath != "-") {
        std::string perr;
        if (!prepareRemoteUnits(std::filesystem::path(rootPath), tc, job, perr)) {
            std::cerr << "错误: 远程多单元准备失败: " << perr << "\n";
            return 1;
        }
    }
    // 链接库：远端原生解析，仅取 -l* 词元（本机 -L/绝对 .a 路径远端无意义，丢弃）
    bool droppedLocal = false;
    for (auto& t : splitBy(tc.ldflags, " ")) {
        if (t.rfind("-l", 0) == 0) job.ldLibs.push_back(t);
        else if (t.rfind("-L", 0) == 0 || endsWith(t, ".a") || endsWith(t, ".o"))
            droppedLocal = true;
    }
    if (!tc.threadsLib.empty()) job.ldLibs.push_back(tc.threadsLib);
    if (droppedLocal)
        std::cerr << "提示: 远程构建忽略了配置 ldflags 的本机 -L/.a/.o 项"
                     "（远端用其原生库解析 -l*；add 引入的依赖仍会上传）\n";
    bool anyLib = false;
    for (auto& d : deps) if (!d.isSource) anyLib = true;
    if (anyLib && tc.targetSuffix.empty())
        std::cerr << "提示: add 的预编译库(.a/.so/.o)按原样上传链接——若远端架构与本机不同将链接失败；"
                     "可配 target_suffix 提供目标变体 <名>.<suffix>.<ext>，或改用源码 add\n";
    int rc = runRemoteJob(job);
    if (!job.unitsDir.empty()) {       // 清理本机多单元临时目录
        std::error_code ec;
        std::filesystem::remove_all(job.unitsDir, ec);
    }
    return rc;
}

// 收集模块图中所有 add 指令引入的原生依赖（供远程构建上传）。
// 失败（图加载错误）返回 false——远程仍可用内联 C 构建，仅缺 add 依赖。
// 定义见文件后段 loadUnitGraph 之后（此处仅前置声明，供 main() 调用）。
// targetSuffix：预编译库优先匹配 <名>.<suffix>.<ext> 变体（见 resolveAddArtifact）。
static bool gatherAddDeps(const std::filesystem::path& rootPath,
                          const std::string& targetSuffix,
                          std::vector<RemoteDep>& out);

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
static std::filesystem::path resolveBuiltinsDir(const std::string& input) {

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

    return b;
}

// 项目根（= builtins 目录的上级）：头支撑模块手写头 #include 路径相对此根计算。
//   在任何后端 codegen 前调用，使 emit-c/run/build 各模式一致（含 templates/utils/* 深层分组）。
static void setupProjectRoot(const std::string& input) {
    namespace fs = std::filesystem;
    const fs::path b = resolveBuiltinsDir(input);
    if (b.empty()) return;
    const fs::path parent = b.parent_path();
    if (!parent.empty() && parent != b) setProjectRoot(parent.string());
}

static void addBuiltinsInclude(ToolConfig& tc, const std::string& input) {

    namespace fs = std::filesystem;
    const fs::path b = resolveBuiltinsDir(input);

    if (!b.empty()) {
        tc.cflags += " -I " + b.string();
        tc.builtinsDir = b.string();   // 记录供远程构建整目录推送
        // builtins 根的上级目录：使生成代码中带根名的引用（如
        // #include "builtins/adt/adt.h"）可解析
        const fs::path parent = b.parent_path();
        if (!parent.empty() && parent != b) {
            tc.cflags += " -I " + parent.string();
            // 项目根：头支撑模块手写头 #include 路径相对此根计算（含 templates/utils/* 深层分组）
            setProjectRoot(parent.string());
        }
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

// ---------------- mbedTLS 后端：按目标工具链现场编译 vendor 源码（跨平台正确） ----------------
// 与 OpenSSL（链系统动态库）不同，mbedTLS 走 vendor 源码 + 静态烘进。为真正跨平台，
// 不在 host 上预编一个固定 .a，而是用「当前目标」的工具链（pickCC + tc.machine + tc.ar）
// 现场把 vendor/mbedtls/library/*.c 编成对应目标的静态库，按工具链指纹缓存到
// ~/.cache/scc/mbedtls-<hash>/libmbedtls_all.a（命中即复用，首次较慢）。返回 .a 路径（失败为空）。
static std::filesystem::path mbedtlsLibForTarget(const std::filesystem::path& repo,
                                                 const ToolConfig& tc) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path src = repo / "vendor" / "mbedtls";
    const fs::path inc = src / "include";
    const fs::path lib = src / "library";
    if (!fs::is_directory(lib, ec)) {
        std::cerr << "错误: 缺少 vendor/mbedtls 源码（" << lib.string()
                  << "）；mbedtls 后端需先 vendor 源码\n";
        return {};
    }
    // 缓存键：工具链指纹（编译器 + 目标机器选项 + 归档器）→ 不同目标各自缓存，互不串味
    const std::string fingerprint = pickCC() + "|" + tc.machine + "|" + tc.ar;
    char hb[24];
    std::snprintf(hb, sizeof hb, "%016zx", std::hash<std::string>{}(fingerprint));
    fs::path base;
    if (const char* home = std::getenv("HOME")) base = fs::path(home) / ".cache" / "scc";
    else base = fs::temp_directory_path(ec) / "scc-cache";
    const fs::path cacheDir = base / ("mbedtls-" + std::string(hb));
    const fs::path outA = cacheDir / "libmbedtls_all.a";
    if (fs::is_regular_file(outA, ec)) return outA;          // 命中缓存

    fs::create_directories(cacheDir, ec);
    std::vector<fs::path> srcs;
    for (auto& e : fs::directory_iterator(lib, ec))
        if (e.path().extension() == ".c") srcs.push_back(e.path());
    std::sort(srcs.begin(), srcs.end());
    std::cerr << "提示: 首次为当前目标编译 vendor mbedTLS（" << srcs.size()
              << " 个源文件 → " << cacheDir.string() << "），稍候…\n";
    std::vector<fs::path> objs;
    for (auto& c : srcs) {
        const fs::path o = cacheDir / (c.stem().string() + ".o");
        const std::string cmd = pickCC() + " -O2" + tc.machine
            + " -I " + inc.string() + " -I " + lib.string()
            + " -c " + c.string() + " -o " + o.string();
        if (std::system(cmd.c_str()) != 0) {
            std::cerr << "错误: 编译 mbedtls 源失败（" << c.filename().string() << "）\n";
            return {};
        }
        objs.push_back(o);
    }
    std::string arCmd = (tc.ar.empty() ? "ar" : tc.ar) + " rcs " + outA.string();
    for (auto& o : objs) arCmd += " " + o.string();
    if (std::system(arCmd.c_str()) != 0) {
        std::cerr << "错误: 归档 libmbedtls_all.a 失败\n";
        fs::remove(outA, ec);
        return {};
    }
    return outA;
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

// 顶级 add <file>.sc 内联：把被 add 的 .sc 源码「拼接」进容器单元（本质＝两个 .sc 文件拼接）。
//   · 被 add 文件的全部声明并入容器单元：其默认（未 @导出、static）辅助函数/类型对容器可见，
//     恰如把大段前置辅助集中抽离到独立 .sc；被 add 文件是「前置关系」（其声明插在 add 处）。
//   · 被 add 文件自身的 inc/add 关系随之并入容器（融合归并）：inc/add 的相对路径仍按被 add
//     文件所在目录解析（记于各声明 inlinedFrom），保证被 add 文件是自完备的独立子单元。
//   · 递归：被 add 文件可再 add 其它 .sc。
//   · keepDirective=true（--ast 视图）保留 add 指令节点本体，供插件在大纲展示 add 关系；
//     构建/转译（false）丢弃指令、仅保留内联声明。
//   · active 记录内联链上的规范化路径，检测 add 环（A add B、B add A / 自我 add）。
//   人为约束「一个 .sc 只能被一个文件 add」由使用方遵守（避免重复内联导致的重复符号，
//   同时省去递归依赖去重逻辑）；本函数仅在单条内联链上防环。
static bool spliceAddModules(Program& prog, const std::filesystem::path& srcPath,
                             bool keepDirective,
                             std::unordered_set<std::string>& active,
                             std::string& err) {
    auto isAddSc = [](const Decl& d) {
        return d.kind == Decl::AddD && endsWith(stripDelims(d.name), ".sc");
    };
    bool any = false;
    for (auto& d : prog.decls) if (isAddSc(*d)) { any = true; break; }
    if (!any) return true;                                 // 无 add .sc：对既有代码零影响

    const auto baseDir = srcPath.has_parent_path() ? srcPath.parent_path()
                                                   : std::filesystem::current_path();
    std::vector<std::unique_ptr<Decl>> merged;
    merged.reserve(prog.decls.size());
    for (auto& d : prog.decls) {
        if (!isAddSc(*d)) { merged.push_back(std::move(d)); continue; }

        const auto addPath = resolveModulePath(d->name, baseDir);
        if (addPath.empty()) {
            err = "add 的 .sc 文件未找到: " + d->name + "（在 " + srcPath.string() + "）";
            return false;
        }
        const std::string key = addPath.string();
        if (!active.insert(key).second) {
            err = "add 循环依赖: " + key;
            return false;
        }
        Program ap;
        try {
            ap = parse(lex(readWholeFile(addPath)));
        } catch (...) {
            active.erase(key);
            err = "add 的 .sc 文件解析失败: " + key;
            return false;
        }
        if (!spliceAddModules(ap, addPath, keepDirective, active, err)) {  // 递归内联嵌套 add
            active.erase(key);
            return false;
        }
        active.erase(key);

        if (keepDirective) {                               // 保留 add 指令节点（仅大纲展示）
            d->origin = key;
            merged.push_back(std::move(d));
        }
        for (auto& cd : ap.decls) {                        // 内联子文件全部声明，标注来源
            if (cd->inlinedFrom.empty()) cd->inlinedFrom = key;
            merged.push_back(std::move(cd));
        }
    }
    prog.decls = std::move(merged);
    return true;
}

// 便捷入口：对一个单元源文件应用 add .sc 内联（失败抛 CompileError）。
static void applyAddModules(Program& prog, const std::filesystem::path& srcPath,
                            bool keepDirective = false) {
    if (srcPath.empty()) return;                           // stdin 无基准目录：跳过（add .sc 需文件基准）
    std::unordered_set<std::string> active;
    active.insert(std::filesystem::weakly_canonical(srcPath).string());
    std::string err;
    if (!spliceAddModules(prog, srcPath, keepDirective, active, err))
        throw CompileError(err, 0);
}


// 使 sc_thread_id / P_clock / P_tick_ms 等 platform 自有符号无需显式 inc 即被认作已知，
// 且 platform.h 后续增删符号自动生效，无需改编译器白名单。
static void ensureBuiltinHeaderSymbols(const std::filesystem::path& baseDir) {
    static bool done = false;
    if (done) return;
    done = true;
    const auto p = resolveModulePath("platform.h", baseDir);
    if (p.empty()) return;
    std::ifstream f(p);
    if (!f) return;
    std::stringstream b; b << f.rdbuf();
    registerBuiltinHeaderSymbols(b.str());
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
        // 经 add 内联的 inc：相对路径按被 add 文件所在目录解析（inlinedFrom），而非容器目录。
        const auto declBase = d->inlinedFrom.empty()
            ? baseDir
            : (std::filesystem::path(d->inlinedFrom).has_parent_path()
                   ? std::filesystem::path(d->inlinedFrom).parent_path() : baseDir);
        auto modPath = resolveModulePath(d->name, declBase);
        if (!modPath.empty() && endsWith(modPath.string(), ".sc")) {
            d->external = true;
            d->origin = modPath.string();
            prog.externSymbols.push_back(modPath.string());
            deps.push_back(modPath);
        } else {
            d->external = false;
        }
    }
    // adt / op → mem 隐式硬依赖：均直接走 mem 三件套（chunk/recycle…），不受全局
    //   -DSC_POOL 开关影响。
    //     · adt：list 段式存储用 chunk/recycle；
    //     · op ：sc_chunk/sc_recycle（确定性池化，rpc 传参的联合节点等短命高频分配）恒转发 mem。
    //   故编译这两个单元时自动把 builtins/mem/mem.sc 纳入单元图（等同隐式 inc mem.sc）：
    //   递归加载使 mem 单元生成并拼接链接 mem_impl.c，导出声明（chunk/refit/recycle…）一并
    //   合并为 external，供 adt_impl.c / op_impl.c 调用。op 恒被默认导入，故 mem 恒随工程链接。
    {
        const std::string stem = srcPath.stem().string();
        std::filesystem::path memPath;
        if (stem == "adt")     memPath = baseDir.parent_path() / "mem" / "mem.sc";  // builtins/adt → builtins/mem
        else if (stem == "op") memPath = baseDir / "mem" / "mem.sc";                // builtins/op  → builtins/mem
        if (!memPath.empty()) {
            memPath = std::filesystem::weakly_canonical(memPath);
            if (std::filesystem::exists(memPath)
                && std::find(deps.begin(), deps.end(), memPath) == deps.end())
                deps.push_back(memPath);
        }
    }
    // op → adt 隐式硬依赖：tok/dep/form 运行时（已合并进 op_impl.c）的全局 token 表 g_toks
    //   用 adt 哈希（dict）做字符串 id 的 O(1) intern，故编译 op 单元时自动把 builtins/adt/adt.sc
    //   纳入单元图（递归加载使 adt 生成并拼接链接 adt_impl.c，导出声明 dict_* 供 op_impl.c 内的
    //   tok 运行时调用）。adt 恒 → mem（上面已连）；op 恒被默认导入，故 adt 运行时恒随工程链接。
    //   token/dep/form 为语言关键字（恒可用，无需 inc），其运行时随 op（op_impl.c）始终链接。
    //   单元图按规范化路径全局去重，与用户 inc adt.sc 不重复链接；adt 的 @导出只并入 op
    //   单元、不入用户根单元，故不影响生成代码（goldens 不变）。
    {
        const std::string stem = srcPath.stem().string();
        if (stem == "op") {
            std::filesystem::path adtPath = baseDir / "adt" / "adt.sc";  // builtins/op → builtins/adt
            adtPath = std::filesystem::weakly_canonical(adtPath);
            if (std::filesystem::exists(adtPath)
                && std::find(deps.begin(), deps.end(), adtPath) == deps.end())
                deps.push_back(adtPath);
        }
    }
    // 合并直接依赖的 @导出声明（external 标记）：供跨模块语法糖
    //（方法调用/声明即构造/方法字段）识别，不参与本单元代码生成。
    // 宏定义（def 宏）一律并入（无论是否 @导出）：宏经 C #define 由模块头透传，
    // 跨模块可用；语义层据此展开顶层 mix、登记宏体声明出的符号（external 不再 emit）。
    for (auto& dep : deps) {
        const std::string text = readWholeFile(dep);
        if (text.empty()) continue;
        try {
            Program mp = parse(lex(text));
            {   // 依赖若 add .sc：内联后其 @导出（含被 add 文件中的）方能并入本单元
                std::unordered_set<std::string> act;
                act.insert(std::filesystem::weakly_canonical(dep).string());
                std::string e;
                spliceAddModules(mp, dep, false, act, e);
            }
            for (auto& md : mp.decls) {
                if (!md->exported && md->kind != Decl::MacroD) continue;
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

// 默认导入 builtins/op.sc —— 语言底层（语法层面）机制的 sc 侧声明模块。
// 与 platform.h 一样属"默认导入"，无需显式 inc。op.sc 汇集编译器需要感知的
// 语法机制类型与接口声明：operand（设备操作数 . 透传）、chain（侵入式双向链表）、
// 切片/容器/COM 等。编译器据此识别方法调用糖、字段访问、链表/容器注入等语法糖。
// 所有声明以 external 并入当前程序：不生成代码、不参与链接——其 C 结构体/原型/
// 实现由各自的 C ABI 头与运行时提供：chain 由 builtins/op.h 与 op_impl.c
// （随 platform.h 默认带入 + 编译器自动链接）提供。
static void mergeOpModule(Program& prog, const std::filesystem::path& srcPath) {
    const auto baseDir = srcPath.has_parent_path() ? srcPath.parent_path()
                                                   : std::filesystem::current_path();
    const auto modPath = resolveModulePath("op.sc", baseDir);
    if (modPath.empty()) return;
    // 正在编译 op.sc 自身：不自合并
    if (!srcPath.empty() && std::filesystem::weakly_canonical(srcPath) == modPath)
        return;
    // 已合并过（任一 decl 来源标记为 op.sc）：避免重复合并；同时收集本单元已有名字
    const std::string origin = modPath.string();
    std::unordered_set<std::string> present;
    for (auto& d : prog.decls) {
        if (d->origin == origin) return;
        present.insert(d->name);
    }
    const std::string text = readWholeFile(modPath);
    if (text.empty()) return;
    try {
        Program mp = parse(lex(text));
        for (auto& md : mp.decls) {
            // 仅并入类型/接口声明（机制语法所需）：类型定义与函数类型 / cImpl 方法。
            // 排除 inc/add（链接依赖）、全局变量、含函数体的实现。
            const bool mech =
                md->kind == Decl::EnumD   || md->kind == Decl::StructD ||
                md->kind == Decl::UnionD  || md->kind == Decl::AliasD  ||
                md->kind == Decl::FuncTypeD;
            if (!mech) continue;
            if (present.count(md->name)) continue;  // 与本单元同名声明冲突：保留本单元
            md->external = true;
            md->origin = origin;
            prog.decls.push_back(std::move(md));
        }
    } catch (...) {
        // op.sc 解析失败：忽略（不影响主程序编译）
    }
}

// 从源码文本提取第 n 行（1-based），去掉行尾空白；越界/无效返回空串。
static std::string nthSourceLine(const std::string& src, int n) {
    if (n <= 0) return {};
    int curLine = 1;
    for (size_t i = 0; i < src.size(); i++) {
        if (curLine == n) {
            size_t end = src.find('\n', i);
            if (end == std::string::npos) end = src.size();
            std::string line = src.substr(i, end - i);
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            return line;
        }
        if (src[i] == '\n') curLine++;
    }
    return {};
}

// 单元是否属 builtins 库（路径任一组件名为 "builtins"）：根模块导出注入须排除内置库。
static bool isBuiltinUnit(const std::filesystem::path& p) {
    for (const auto& comp : p) if (comp == "builtins") return true;
    return false;
}

// 程序是否含可导出对象（@导出 且非 external）：决定根模块是否生成非空接口头 / 启用注入。
static bool programHasExports(const Program& prog) {
    for (auto& d : prog.decls) if (d->exported && !d->external) return true;
    return false;
}

// 根模块导出注入（语义侧）：把根（入口/集成单元）的 @导出 声明以 external 并入消费单元，
//   仅供跨模块语法/语义可见（类型/方法/全局），不发定义、不参与链接——C 层定义与 extern
//   原型由注入的 scm_<root>.h 提供。与 resolveUnitDeps 的向下合并同构，源头固定为根模块。
//   仅并入类型/函数/变量/别名声明，跳过 add（避免污染构建指令）。
//   另：兄弟模块互相可见——根以 @inc（@导出 inc）引入的各 .sc 模块互为「兄弟」，其 @导出
//   彼此可见（把该 @inc 边及兄弟 @导出 并入消费单元）。普通 inc 为根私有依赖，不参与互见。
static void expandGenericMixes(Program& prog);  // 前置声明（定义见下）
static void mergeRootPrelude(Program& prog, const std::filesystem::path& rootPath,
                             const std::filesystem::path& unitPath = {},
                             bool skipRootSelf = false) {
    const std::string origin = rootPath.string();
    if (!skipRootSelf)
        for (auto& d : prog.decls) if (d->origin == origin) return;  // 已并入根自身导出：幂等
    const std::string text = readWholeFile(rootPath);
    if (text.empty()) return;
    auto canon = [](const std::string& s) {
        return s.empty() ? std::string()
                         : std::filesystem::weakly_canonical(s).string();
    };
    const std::string unitKey = unitPath.empty() ? std::string()
                                                 : canon(unitPath.string());
    // 消费单元已直接 inc 的 .sc 模块来源：其导出已并入本单元，不再经根重复注入兄弟边。
    std::unordered_set<std::string> preexistingDep;
    for (auto& d : prog.decls)
        if (d->kind == Decl::IncD && d->external && endsWith(d->origin, ".sc"))
            preexistingDep.insert(canon(d->origin));
    std::unordered_set<std::string> injectedInc;  // 本次注入的兄弟 inc，去重
    try {
        Program mp = parse(lex(text));
        applyAddModules(mp, rootPath);   // 根若 add .sc：内联后再解析其依赖/导出
        // 解析根的依赖（带入其 inc 进来的宏定义）并展开根的顶层 mix，使「由宏展开产生的
        //   @导出 对象」（如 mix ARGS_B 展开出的 @var ARGS_verbose）也作为根导出对消费单元
        //   可见——与根自身编译时 scm_<root>.h 收录这些符号保持一致。
        resolveUnitDeps(mp, rootPath);
        expandGenericMixes(mp);
        // 兄弟互相可见仅限「@导出 inc」（@inc）：收集根里以 @inc 引入的兄弟 .sc 来源集合。
        //   普通 inc（无 @）是根的私有依赖，不参与兄弟互见——避免「全自动」过度暴露。
        std::unordered_set<std::string> exportedSiblingOrigins;
        for (auto& md : mp.decls)
            if (md->kind == Decl::IncD && md->exported && md->external
                && endsWith(md->name, ".sc"))
                exportedSiblingOrigins.insert(canon(md->origin));
        for (auto& md : mp.decls) {
            // 兄弟模块互相可见（仅 @inc）：根（集成单元）以 @inc 引入的各 .sc 模块互为
            //   「兄弟」，其 @导出 默认彼此可见。把根的每条 @导出兄弟 inc 边及其外部导出并入
            //   当前消费单元（排除消费单元自身、以及消费单元已直接 inc 的来源），代码层经注入的
            //   #include "scm_<兄弟>.h" 提供 C 声明，语义层经 external 声明提供可见性。
            if (md->kind == Decl::IncD) {
                if (!md->exported || !md->external || !endsWith(md->name, ".sc"))
                    continue;                                        // 仅 @inc 兄弟边
                const std::string depKey = canon(md->origin);
                if (depKey.empty() || depKey == unitKey) continue;   // 跳过自身
                if (preexistingDep.count(depKey)) continue;          // 已直接 inc
                if (!injectedInc.insert(depKey).second) continue;    // 去重
                auto inc = std::make_unique<Decl>();
                inc->kind = Decl::IncD;
                inc->name = md->name;
                inc->origin = md->origin;
                inc->external = true;
                inc->exported = true;
                inc->line = md->line;
                prog.decls.push_back(std::move(inc));
                continue;
            }
            if (md->kind == Decl::AddD) continue;
            if (!md->exported) continue;
            if (md->external) {
                // 兄弟模块导出：仅当来自根的 @inc 兄弟边时跨兄弟可见（普通 inc 不暴露）。
                const std::string depKey = canon(md->origin);
                if (depKey.empty() || depKey == unitKey) continue;   // 跳过自身
                if (preexistingDep.count(depKey)) continue;          // 消费单元已有
                if (!exportedSiblingOrigins.count(depKey)) continue; // 非 @inc 兄弟：不暴露
                prog.decls.push_back(std::move(md));
                continue;
            }
            // 根自身 @导出：以 external 并入消费单元。仅当本单元接受根自身前奏时注入
            //   （根导出 inc 闭包单元 skipRootSelf=true：其 C 层不注入 scm_<root>.h 以防循环，
            //   故语义层也不并入根自身导出——否则会引用到缺失 C 声明的根导出符号）。
            if (skipRootSelf) continue;
            md->external = true;
            md->origin = origin;
            prog.decls.push_back(std::move(md));
        }
    } catch (...) {
        // 根解析失败：留待根单元自身编译时报错
    }
}

// ============================================================
// 泛型宏单态化（语言级泛型）
// ============================================================
// 带 <T,...> 类型参数的宏是「语言级模板」：与文本 #define 不同，编译器对每个 mix 实例
//   克隆宏体 → 文本替换类型/名参数 → 重新解析为「具体的 sc 声明」→ 追加进程序，
//   随后这些具体声明参与正常语义检查与 C 代码生成（i4/f8 等类型映射、类型校验均生效）。
// 该 pass 在解析后、语义检查前执行；仅在实际编译路径（emit-c / run / build）生效，
//   --emit-sc / --ast 模式保留原始 def/mix 以便源码回写。

// 宏体文本替换：把类型/名参数名（整词）替换为实参，并消解 `\` 粘贴（拼接相邻段）。
//   跳过字符串/字符字面量与 # 注释，避免误替换其中文本。
static std::string substGenericText(const std::string& text,
                                    const std::map<std::string, std::string>& binds) {
    std::string out;
    const size_t n = text.size();
    for (size_t i = 0; i < n; ) {
        char c = text[i];
        if (c == '"' || c == '\'') {                 // 字符串/字符字面量：原样拷贝
            char q = c; out += c; i++;
            while (i < n) {
                if (text[i] == '\\' && i + 1 < n) { out += text[i]; out += text[i + 1]; i += 2; continue; }
                char dch = text[i]; out += dch; i++;
                if (dch == q) break;
            }
            continue;
        }
        if (c == '#') {                              // 注释：到行尾原样拷贝
            while (i < n && text[i] != '\n') out += text[i++];
            continue;
        }
        if (c == '`') {                              // 串化 `name`（→ C #name）：消解为字符串字面量
            size_t j = i + 1;                        //   宏体内 `param` 在重解析前即落地为 "实参文本"，
            std::string inner;                       //   与 C 预处理 #param 语义一致（避免遗留为标识符）
            while (j < n && text[j] != '`') inner += text[j++];
            if (j < n) j++;                          // 吃掉收尾 `
            std::string sub;                         // 对 inner 内整词做参数替换
            for (size_t p = 0; p < inner.size(); ) {
                char ic = inner[p];
                if (std::isalpha((unsigned char)ic) || ic == '_') {
                    size_t s = p;
                    while (p < inner.size()
                           && (std::isalnum((unsigned char)inner[p]) || inner[p] == '_')) p++;
                    std::string id = inner.substr(s, p - s);
                    auto it = binds.find(id);
                    sub += (it != binds.end()) ? it->second : id;
                } else { sub += ic; p++; }
            }
            out += '"'; out += sub; out += '"';
            i = j;
            continue;
        }
        if (c == '\\') { i++; continue; }            // 粘贴运算符：丢弃（相邻段拼接）
        if (std::isalpha((unsigned char)c) || c == '_') {
            size_t s = i;
            while (i < n && (std::isalnum((unsigned char)text[i]) || text[i] == '_')) i++;
            std::string id = text.substr(s, i - s);
            auto it = binds.find(id);
            out += (it != binds.end()) ? it->second : id;
            continue;
        }
        out += c; i++;
    }
    return out;
}

// 渲染 mix 实参为类型/名参数的文本 token（泛型单态化绑定用）。
static std::string genericArgText(const Expr& a) {
    if (a.kind == Expr::Ident) return a.text;
    return exprToStr(a);
}

static void expandGenericMixes(Program& prog) {
    // 收集可展开的函数宏（非 C 桥接 :: 、非对象 =value）按名索引；含外部依赖宏（mix 可能
    // 引用 inc 进来的依赖宏，如下游 inc sys.sc 后 mix ARGS_B）。
    std::unordered_map<std::string, const Decl*> macros;
    for (auto& d : prog.decls)
        if (d->kind == Decl::MacroD && !d->cImpl && !d->macroObject)
            macros[d->name] = d.get();
    if (macros.empty()) return;

    // 「对象定义」宏集合（不动点）：宏体直接含 var/let/tls 声明，或经 mix 间接展开到对象
    //   定义宏。这类非泛型宏的顶层 mix 与泛型一样重解析进 AST —— 使其声明的全局对象参与
    //   语义检查与生命期（init/drop）注入，而非停留在 C 预处理 #define 层（不可见于 AST）。
    std::unordered_set<std::string> objDefining;
    for (bool changed = true; changed; ) {
        changed = false;
        for (auto& kv : macros) {
            if (objDefining.count(kv.first)) continue;
            bool obj = false;
            for (auto& s : kv.second->body) {
                if (!s) continue;
                if (s->kind == Stmt::VarS || s->kind == Stmt::LetS || s->kind == Stmt::TlsS) {
                    obj = true; break;
                }
                if (s->kind == Stmt::MixS && s->expr && s->expr->kind == Expr::Call
                    && s->expr->a && s->expr->a->kind == Expr::Ident
                    && objDefining.count(s->expr->a->text)) {
                    obj = true; break;
                }
            }
            if (obj) { objDefining.insert(kv.first); changed = true; }
        }
    }

    bool anyGeneric = false;
    for (auto& kv : macros)
        if (!kv.second->macroTypeParams.empty()) { anyGeneric = true; break; }
    if (!anyGeneric && objDefining.empty()) return;  // 无可重解析的 mix

    std::unordered_set<std::string> seen;   // 实例去重键

    // 尝试展开单个顶层 MixD：泛型宏，或「对象定义」非泛型宏 → 重解析其特化体，结果声明压入
    //   out（不含原 mix 自身），返回 true；其余宏保持 #define 行为，返回 false。
    auto tryExpand = [&](Decl* d, std::vector<DeclPtr>& out) -> bool {
        if (d->kind != Decl::MixD || !d->expr) return false;
        const Expr& call = *d->expr;
        if (call.kind != Expr::Call || !call.a || call.a->kind != Expr::Ident) return false;
        auto mit = macros.find(call.a->text);
        if (mit == macros.end()) return false;
        const Decl* m = mit->second;
        const bool isGeneric = !m->macroTypeParams.empty();
        if (!isGeneric && !objDefining.count(m->name)) return false;  // 普通文本宏：保持 #define

        const size_t nTy = m->macroTypeParams.size();
        const size_t nName = m->structCommon.fields.size();
        if (call.args.size() != nTy + nName) {
            CompileError e((isGeneric ? std::string("泛型宏 '") : std::string("对象宏 '"))
                           + m->name + "' 需要 " + std::to_string(nTy + nName) + " 个实参"
                           + (isGeneric ? "（" + std::to_string(nTy) + " 类型 + "
                                          + std::to_string(nName) + " 名）"
                                        : std::string{})
                           + "，实际传入 " + std::to_string(call.args.size()) + " 个", d->line);
            throw e;
        }
        d->macroConsumed = true;       // 标记消费（codegen 跳过该 mix 与泛型 #define）

        // 绑定：类型参数 + 名参数 → 实参文本；并构造实例去重键
        std::map<std::string, std::string> binds;
        std::string key = m->name + "<";
        for (size_t k = 0; k < call.args.size(); k++) {
            std::string at = call.args[k] ? genericArgText(*call.args[k]) : std::string{};
            const std::string& pn = (k < nTy) ? m->macroTypeParams[k]
                                              : m->structCommon.fields[k - nTy].name;
            binds[pn] = at;
            key += (k ? "," : "") + at;
        }
        key += ">";
        if (!seen.insert(key).second) return true;   // 同一实例已生成，丢弃本 mix

        // 渲染宏体 → 文本替换 → 重新解析为具体声明
        std::string bodyText = emitMacroBodySc(m->body);
        std::string specialized = substGenericText(bodyText, binds);
        try {
            Program sp = parse(lex(specialized));
            for (auto& nd : sp.decls) {
                // 泛型实例产物打标：其类型定义跨模块时聚合进 generic.h（去重、一致）。
                //   对象定义宏（非泛型）不打标——其全局对象本就属各单元自身。
                if (isGeneric) { nd->genericInst = true; nd->genericKey = key; }
                out.push_back(std::move(nd));
            }
        } catch (CompileError& e) {
            if (e.hint.empty())
                e.hint = "宏 '" + m->name + "' 实例化 " + key + " 时其特化体解析失败";
            throw;
        }
        return true;
    };

    // 原始顶层 mix 展开到 pending；迭代抽干（嵌套 mix，如 ARGS_B→ARGS_DEF，继续展开），
    //   非 mix 与不可展开 mix 落入 generated，最终按序追加到 prog.decls 尾部。
    std::vector<DeclPtr> pending, generated;
    for (auto& d : prog.decls)
        tryExpand(d.get(), pending);
    for (size_t i = 0; i < pending.size(); i++) {
        std::vector<DeclPtr> more;
        if (pending[i]->kind == Decl::MixD && tryExpand(pending[i].get(), more)) {
            for (auto& g : more) pending.push_back(std::move(g));   // 追加再处理（索引稳定）
        } else {
            generated.push_back(std::move(pending[i]));
        }
    }
    for (auto& g : generated) if (g) prog.decls.push_back(std::move(g));
}

// 收集根模块「导出 inc 闭包」（规范路径集，含根自身）：沿 @inc（导出 include）边递归。
//   根接口头 scm_<root>.h 会 #include 这些单元的 scm_<dep>.h（emitInclude 对 @inc 的处理），
//   故不可把 scm_<root>.h 反向注入这些单元的 .c —— 否则该单元 .c 在自身类型定义前先 include
//   引用了这些类型的根接口头，造成「未定义类型」编译错误（典型：根 @inc shared + 根导出引用
//   shared 类型）。这些单元属根公共接口的「上游」，本就无需被注入。
static void collectExportedIncClosure(const std::filesystem::path& unitPath,
                                      std::unordered_set<std::string>& closure) {
    const auto canon = std::filesystem::weakly_canonical(unitPath);
    const std::string key = canon.string();
    if (!closure.insert(key).second) return;  // 已访问
    const std::string text = readWholeFile(canon);
    if (text.empty()) return;
    const auto baseDir = canon.has_parent_path() ? canon.parent_path()
                                                 : std::filesystem::current_path();
    try {
        Program mp = parse(lex(text));
        for (auto& d : mp.decls) {
            if (d->kind != Decl::IncD || !d->exported) continue;  // 仅导出 inc 边
            auto modPath = resolveModulePath(d->name, baseDir);
            if (!modPath.empty() && endsWith(modPath.string(), ".sc"))
                collectExportedIncClosure(modPath, closure);
        }
    } catch (...) {
        // 解析失败：忽略（留待该单元自身编译报错）
    }
}

// 文件中是否含独立 @@ 根标记（轻量文本扫描，避免对目录下每个文件做完整解析）。
//   规则：某一行去除首尾空白后以 @@ 起始、且其后为空白/注释/行尾（排除 @@xxx 误匹配）即判为根。
//   权威判定仍由解析器置 prog.isRoot；本扫描仅供「为其它单元定位根」的快速发现。
static bool fileHasRootMarker(const std::filesystem::path& p) {
    std::ifstream in(p);
    if (!in) return false;
    std::string line;
    while (std::getline(in, line)) {
        const size_t b = line.find_first_not_of(" \t\r");
        if (b == std::string::npos || line[b] == '#') continue;     // 空行/注释行
        if (line.compare(b, 2, "@@") == 0) {
            const size_t after = b + 2;
            if (after >= line.size()) return true;
            const char c = line[after];
            if (c == ' ' || c == '\t' || c == '\r' || c == '#') return true;
        }
        // 其它有效行：继续（允许 @@ 出现在若干 inc 之后；不强制为首个声明）
    }
    return false;
}

// 在目录中发现 @@ 标注的根模块（显式声明的全局前奏提供者）。返回其规范路径，未找到返回空。
//   不递归；跳过点文件（含语法插件的临时 .sc_ast_*.tmp.sc）。多个 @@ 时按排序取首并告警（根应唯一）。
//   该扫描是「编译器对接语法插件」的静态发现入口：编辑期间依赖单元据此并入根 @导出，符号不再未定义。
static std::filesystem::path findRootModule(const std::filesystem::path& dir) {
    std::error_code ec;
    if (dir.empty() || !std::filesystem::is_directory(dir, ec) || ec) return {};
    std::vector<std::filesystem::path> entries;
    for (auto it = std::filesystem::directory_iterator(dir, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
        const auto& p = it->path();
        const std::string fn = p.filename().string();
        if (!fn.empty() && fn[0] == '.') continue;                  // 跳过点文件（含插件临时文件）
        if (!endsWith(fn, ".sc")) continue;
        std::error_code fec;
        if (!std::filesystem::is_regular_file(p, fec) || fec) continue;
        entries.push_back(p);
    }
    std::sort(entries.begin(), entries.end());
    std::vector<std::filesystem::path> roots;
    for (auto& p : entries) if (fileHasRootMarker(p)) roots.push_back(p);
    if (roots.empty()) return {};
    if (roots.size() > 1)
        std::cerr << "警告: 目录下存在多个 @@ 根模块，取 "
                  << roots.front().filename().string() << "（根模块应唯一）\n";
    return std::filesystem::weakly_canonical(roots.front());
}

// 取某源文件所在目录（用于根模块发现的扫描基准）。
static std::filesystem::path unitDirOf(const std::filesystem::path& src) {
    const auto canon = std::filesystem::weakly_canonical(src);
    return canon.has_parent_path() ? canon.parent_path()
                                   : std::filesystem::current_path();
}

// 从项目入口文件加载模块图，递归解析依赖，检查语义；成功返回 true，失败返回 false 和错误信息。
// importChain 记录「自入口起逐级 inc 进来的祖先模块」（规范路径），用于跨模块错误链展示。
static bool loadUnitGraph(const std::filesystem::path& srcPath,
                          std::unordered_map<std::string, UnitInfo>& units,
                          std::unordered_set<std::string>& visiting,
                          std::string& errMsg,
                          const std::vector<std::string>& importChain = {},
                          const std::filesystem::path& rootPrelude = {},
                          const std::unordered_set<std::string>* preludeSkip = nullptr) {

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
        // 区分「打不开/不存在」与「存在但为空」：空文件合法（空模块，无声明），仅告警
        std::error_code ec;
        const bool exists = std::filesystem::exists(canon, ec) && !ec;
        if (!exists || !std::ifstream(canon)) {
            errMsg = "无法读取模块文件: " + key;
            visiting.erase(key);
            return false;
        }
        std::cerr << "警告: 模块文件为空: " << key << "\n";
    }

    UnitInfo u;
    u.path = canon;
    try {
        u.prog = parse(lex(src));                   // 解析源代码为 AST
        applyAddModules(u.prog, canon);             // 顶级 add <file>.sc：内联被 add 的 .sc（拼接进本单元）
        u.deps = resolveUnitDeps(u.prog, canon);    // 先合并依赖导出声明（external）
        mergeOpModule(u.prog, canon);               // 默认导入 op.sc 语法机制声明（operand/chain 等）
        // 根模块导出注入（语义可见性）：非根、非内置单元并入根前奏。
        //   · 非闭包单元：并入根自身 @导出 + @inc 兄弟可见性。
        //   · 根导出 inc 闭包单元（skipRootSelf）：仅并入 @inc 兄弟可见性（不并根自身导出，
        //     与 C 层不注入 scm_<root>.h 一致，防循环包含）。
        if (!rootPrelude.empty() && canon != rootPrelude && !isBuiltinUnit(canon)) {
            const bool inClosure = preludeSkip && preludeSkip->count(key);
            mergeRootPrelude(u.prog, rootPrelude, canon, inClosure);
        }
        expandGenericMixes(u.prog);                 // 泛型宏单态化：克隆宏体+替换类型参数→具体声明
        ensureBuiltinHeaderSymbols(canon.parent_path());  // 注册 platform.h 符号（一次性）
        semanticCheck(u.prog);                      // 再检查：导入类型/方法可见 + 本模块函数体语义复检
    } catch (CompileError& e) {
        // 跨模块错误链完整展示：把错误准确归属到出错模块文件、补全该模块的源代码行，
        // 避免被顶层 catch 误挂到入口文件（错误的文件名与不匹配的源码行）；
        // 并在提示里附上自入口起的 inc 导入链，定位错误是经哪条路径引入的。
        if (e.file.empty()) e.file = key;
        if (e.srcLine.empty() && e.line > 0) e.srcLine = nthSourceLine(src, e.line);
        if (e.hint.empty() && !importChain.empty()) {
            std::string chain;
            for (auto& c : importChain)
                chain += std::filesystem::path(c).filename().string() + " → ";
            chain += canon.filename().string();
            e.hint = "跨模块导入链：" + chain;
        }
        visiting.erase(key);
        throw;
    }

    // 递归加载依赖模块（向下传递扩展后的导入链）
    std::vector<std::string> childChain = importChain;
    childChain.push_back(key);
    for (auto& dep : u.deps) {
        if (!loadUnitGraph(dep, units, visiting, errMsg, childChain, rootPrelude, preludeSkip)) {
            visiting.erase(key);
            return false;
        }
    }

    // 加载成功：记录单元信息，标记访问完成
    units[key] = std::move(u);
    visiting.erase(key);
    return true;
}

// 拼接机制：把模块自身的源码实现 <stem>_impl.c 作为内容并入生成的单元 .c，
//   编成同一翻译单元（TU）。这样手写 C 实现可直接引用 sc 侧模块私有 static 全局，
//   且 `::` 接口的 extern 符号在本 TU 内就地定义，无需单独编译/链接。
// include 处理：剥离 impl 中对「模块自身 ABI 头」<stem>.h 的 #include。
//   · 非头支撑单元：本单元 .c 已就地内联输出 struct/typedef 与函数原型（无 include
//     guard），再经 <stem>.h 重复定义会触发「结构体重定义」；剥离后由单元 .c 的内联
//     定义统一供给（单一真相 = <stem>.sc）。
//   · 头支撑单元（子项目三件套）：本单元 .c 已在顶部 #include 该手写头（emitC 自检
//     发出），结构/原型/宏由头提供且 emitC 跳过自身 @导出类型内联；剥离 impl 的同名
//     头重复包含纯属冗余消除（即便保留亦因 include guard 无害）。
//   其余 include（platform.h / 系统头，均带 include guard）原样保留、重复包含无害。
//   剥离时以空行占位以维持 #line 行号对齐。
static std::string spliceImpl(const std::string& csrc,
                              const std::filesystem::path& implPath,
                              const std::string& stem) {
    const std::string impl = readWholeFile(implPath);
    if (impl.empty()) return csrc;
    const std::string ownHdr = stem + ".h";
    std::ostringstream stripped;
    std::istringstream in(impl);
    std::string line;
    while (std::getline(in, line)) {
        bool isOwn = false;
        const size_t s = line.find_first_not_of(" \t");
        if (s != std::string::npos && line.compare(s, 8, "#include") == 0) {
            const size_t q1 = line.find('"', s);
            const size_t q2 = (q1 == std::string::npos) ? std::string::npos
                                                        : line.find('"', q1 + 1);
            if (q1 != std::string::npos && q2 != std::string::npos) {
                const std::string inc = line.substr(q1 + 1, q2 - q1 - 1);
                if (std::filesystem::path(inc).filename().string() == ownHdr)
                    isOwn = true;
            }
        }
        if (isOwn) stripped << '\n';            // 空行占位，维持后续行号对齐
        else stripped << line << '\n';
    }
    return csrc + "\n#line 1 \"" + implPath.string() + "\"\n" + stripped.str();
}

// 把模块图中所有单元生成 .c/.h 并编译为 .o
// + extraCFlags 用于追加单元级编译选项（如动态库的 -fPIC）；成功返回 0
//   extraLd 返回子项目需要的额外链接选项（如 Linux 上 m 的 -lpthread）
// 单元生成产物：cpath/hpath/opath + 源目录 + 二进制实现 + 单元级编译选项。
//   提到文件作用域，供远程多单元构建（只生成 .c/.h 上传，远端编译）复用。
struct UnitArtifact {
    std::filesystem::path cpath;
    std::filesystem::path hpath;
    std::filesystem::path opath;
    std::filesystem::path srcDir;  // 源 .sc 所在目录（解析 inc "local.h"）
    std::filesystem::path linkImpl;  // 二进制实现（.o/.a）直接参与链接；空=无（源实现已拼接）
    std::string unitCFlags;          // 本单元额外编译选项（如 async 的 libuv）
};

static int compileUnitsToObjects(std::unordered_map<std::string, UnitInfo>& units,
                                 const ToolConfig& tc,
                                 const std::string& extraCFlags,
                                 const std::filesystem::path& tmpDir,
                                 std::vector<std::filesystem::path>& objects,
                                 std::string* extraLd = nullptr,
                                 const std::string& rootKey = "",
                                 const std::string& rootPreludeHeader = "",
                                 const std::unordered_set<std::string>* preludeSkip = nullptr,
                                 const std::string& testKey = "",
                                 std::vector<UnitArtifact>* genOnlyOut = nullptr) {
    std::vector<UnitArtifact> arts;

    // op.sc 统一进单元图：op.sc 为默认导入的语言运行时模块（chain/异步内核等机制）。
    //   将其作为正式单元纳入图，生成 op 单元 .c，其运行时 op_impl.c 经拼接机制并入同
    //   一 TU（见下方第一阶段），不再单独编译/链接。op 的唯一特殊性退化为「自动导入」。
    if (!units.empty()) {
        const std::filesystem::path anyDir =
            std::filesystem::path(units.begin()->first).parent_path();
        const auto opPath = resolveModulePath("op.sc", anyDir);
        if (!opPath.empty()) {
            const std::string opKey = std::filesystem::weakly_canonical(opPath).string();
            if (units.find(opKey) == units.end()) {
                std::unordered_set<std::string> opVisiting;
                std::string opErr;
                if (!loadUnitGraph(opPath, units, opVisiting, opErr)) {
                    std::cerr << "错误: 加载 op.sc 单元失败: " << opErr << "\n";
                    return 1;
                }
            }
        }
    }

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
        // 根模块导出注入：非根、非内置、且不在根导出 inc 闭包内的单元，.c 末位追加
        //   #include "scm_<root>.h"（根导出 inc 闭包单元由 scm_<root>.h 反向引用，注入会致循环）
        std::string rph;
        if (!rootPreludeHeader.empty() && kv.first != rootKey
            && !isBuiltinUnit(kv.second.path)
            && !(preludeSkip && preludeSkip->count(kv.first)))
            rph = rootPreludeHeader;
        const bool unitTest = !testKey.empty() && kv.first == testKey;
        if (unitTest) setTestMode(true);
        const std::string csrc = emitC(kv.second.prog, kv.first, sofName, &sofSrc, rph);  // 带 #line 源码映射
        if (unitTest) setTestMode(false);

        // 拼接机制：模块自身的源码实现 <stem>_impl.c 并入本单元 .c（同 TU）。
        //   决定本单元的 impl 处理方式：源实现 → 拼接；二进制（.o/.a）→ 单独链接。
        //   adt 可经 --adt/SCC_ADT/.sc 配置替换默认实现（.c 拼接、.o/.a 链接）。
        const std::string scStem = kv.second.path.stem().string();
        const std::filesystem::path scDir = kv.second.path.parent_path();
        std::filesystem::path spliceC, linkImpl;
        std::string unitCFlags;
        if (scStem == "adt" && !tc.adtImpl.empty()) {
            const std::filesystem::path ov(tc.adtImpl);
            if (!std::filesystem::exists(ov)) {
                std::cerr << "错误: adt 实现文件不存在: " << ov.string() << "\n";
                return 1;
            }
            if (ov.extension() == ".c") spliceC = ov; else linkImpl = ov;
        } else {
            const std::filesystem::path cImpl = scDir / (scStem + "_impl.c");
            if (std::filesystem::exists(cImpl)) spliceC = cImpl;
            else {
                const std::filesystem::path aImpl = scDir / (scStem + ".a");
                if (std::filesystem::exists(aImpl)) linkImpl = aImpl;
            }
        }
        const bool hasImpl = !spliceC.empty() || !linkImpl.empty();
        // 子项目特殊编译/链接选项（仅当本模块带实现时生效）：
        //   m   —— 线程库（Linux=-lpthread；macOS/Windows/裸机=空），进最终链接
        //   async —— 叶子原语，编译器以 -DSCC_WITH_UV 构建时改用 libuv 后端（需 -I 头）
        if (hasImpl && scStem == "m" && extraLd && !tc.threadsLib.empty()
            && extraLd->find(tc.threadsLib) == std::string::npos)
            *extraLd += " " + tc.threadsLib;
        // mem —— 跨进程共享内存（POSIX shm_open/shm_unlink）。glibc < 2.34 将这两个符号
        //   放在 librt，故 Linux 目标须补 -lrt；macOS 在 libc、Windows 用 Win32 API，均无需。
        //   目标族按「显式 triple，缺省回退宿主」判定，兼顾本机 Linux 构建与交叉到 Linux。
        if (hasImpl && scStem == "mem" && extraLd) {
            const std::string fam =
                platformFamily(tc.triple.empty() ? hostTriple() : tc.triple);
            if (fam == "linux" && extraLd->find("-lrt") == std::string::npos)
                *extraLd += " -lrt";
        }
#ifdef SCC_WITH_UV
        if (hasImpl && scStem == "async") {
            const std::filesystem::path repo = scDir.parent_path().parent_path();
            const std::filesystem::path uvInc = repo / "vendor" / "libuv" / "include";
            unitCFlags = " -DSCC_WITH_UV";
            if (std::filesystem::exists(uvInc)) unitCFlags += " -I " + uvInc.string();
        }
#endif
        // ssl —— TLS 记录层（builtins/ssl）。后端由构建 scc 时 CMake SCC_SSL_BACKEND 固化：
        //   mbedtls（默认）：以 -DSCC_WITH_MBEDTLS 构建，走 vendor 源码（vendor/mbedtls），用
        //     「当前目标」工具链现场编静态库（按 triple 缓存）静态烘进用户二进制 —— 零系统依赖、跨平台。
        //   openssl：以 -DSCC_WITH_OPENSSL 构建（find_package 定位系统 OpenSSL），编译 ssl_impl.c 需
        //     -DSCC_WITH_OPENSSL + OpenSSL 头路径，并向用户程序链接 -lssl -lcrypto（系统动态库，不 vendor）。
        //   none：安全失败桩，零外部依赖。ssl_impl.c 已由上方拼接逻辑并入 ssl 单元 .c（同 TU）。
        if (hasImpl && scStem == "ssl") {
#ifdef SCC_WITH_OPENSSL
            unitCFlags = " -DSCC_WITH_OPENSSL";
#ifdef SCC_OPENSSL_INC
            {
                const std::string sslInc = SCC_OPENSSL_INC;
                if (!sslInc.empty()) unitCFlags += " -I " + sslInc;
            }
#endif
            if (extraLd) {
#ifdef SCC_OPENSSL_LIBDIR
                {
                    const std::string sslLibDir = SCC_OPENSSL_LIBDIR;
                    if (!sslLibDir.empty()
                        && extraLd->find("-L" + sslLibDir) == std::string::npos)
                        *extraLd += " -L" + sslLibDir;
                }
#endif
                if (extraLd->find("-lssl") == std::string::npos)
                    *extraLd += " -lssl -lcrypto";
            }
#elif defined(SCC_WITH_MBEDTLS)
            // mbedTLS 后端：vendor 源码内置，用「当前目标」工具链现场编静态库（按 triple 缓存），
            //   静态烘进用户二进制 —— 区别于 OpenSSL 链系统动态库。真正跨平台。
            unitCFlags = " -DSCC_WITH_MBEDTLS";
            {
                const std::filesystem::path repo = scDir.parent_path().parent_path();
                const std::filesystem::path mbedInc = repo / "vendor" / "mbedtls" / "include";
                if (std::filesystem::exists(mbedInc))
                    unitCFlags += " -I " + mbedInc.string();
                if (extraLd) {
                    const std::filesystem::path mbedA = mbedtlsLibForTarget(repo, tc);
                    if (!mbedA.empty()
                        && extraLd->find(mbedA.string()) == std::string::npos)
                        *extraLd += " " + mbedA.string();   // 静态库在用户 .o 之后参与链接
                }
            }
#endif
        }
        // gpu / gfx / spc（builtins GPU 模块体系）+ wsi（窗口库）——平台链接注入：
        //   模块以 add lib<mod>.a 预编译库交付（build.sh 产多 triple 变体），
        //   其平台框架/系统库依赖由此处按目标平台族自动注入，用户零 SCC_LDFLAGS。
        //   darwin 框架集为实测；linux 桌面组（GL/EGL/gbm）为板验前的合理缺省，
        //   GLES 形态（lib<mod>.<triple>.gles.a）的 -lGLESv2 选择待板验接入。
        if (scStem == "gpu" || scStem == "gfx" || scStem == "spc" || scStem == "wsi") {
            const std::string fam =
                platformFamily(tc.triple.empty() ? hostTriple() : tc.triple);
            auto addLd = [&](const char* flag) {
                if (extraLd && extraLd->find(flag) == std::string::npos)
                    *extraLd += std::string(" ") + flag;
            };
            if (fam == "darwin") {
                if (scStem == "wsi") {
                    addLd("-framework Cocoa");
                    addLd("-framework IOKit");
                    addLd("-framework CoreFoundation");
                    addLd("-framework QuartzCore");
                } else {
                    /* gpu/gfx/spc 共用基础集（查重防重复注入） */
                    addLd("-framework Cocoa");
                    addLd("-framework Metal");
                    addLd("-framework QuartzCore");
                    addLd("-framework OpenGL");
                    addLd("-framework IOSurface");
                    addLd("-framework CoreFoundation");
                    if (scStem == "spc") {
                        addLd("-framework MetalPerformanceShaders");
                        addLd("-framework MetalPerformanceShadersGraph");
                        addLd("-framework CoreML");
                        addLd("-framework Foundation");
                    }
                }
            } else if (fam == "linux") {
                if (scStem == "gpu" || scStem == "gfx") {
                    /* 桌面组；GLES 形态板验时按库变体切 -lGLESv2 */
                    addLd("-lGL");
                    addLd("-lEGL");
                    addLd("-lgbm");
                }
                /* wsi linux（X11/Wayland 后端选择）待板验注入 */
            }
        }
        // op —— 默认导入的语言运行时（chain/异步内核）。异步内核基于 pthread，故需链接
        //   线程库（Linux=-lpthread；macOS/Windows/裸机=空）。编译器以 -DSCC_WITH_UV
        //   构建时改用 libuv 后端：op_impl.c 需 -DSCC_WITH_UV + libuv 头，链接 libuv.a
        //   （+ macOS 系统框架）。op_impl.c 已经上方拼接逻辑并入 op 单元 .c（同 TU）。
        if (hasImpl && scStem == "op") {
            if (extraLd && !tc.threadsLib.empty()
                && extraLd->find(tc.threadsLib) == std::string::npos)
                *extraLd += " " + tc.threadsLib;
#ifdef SCC_WITH_UV
            const std::filesystem::path repo = scDir.parent_path();
            const std::filesystem::path uvInc = repo / "vendor" / "libuv" / "include";
            const std::filesystem::path uvLib = repo / "vendor" / "libuv" / "build" / "libuv.a";
            unitCFlags += " -DSCC_WITH_UV";
            if (std::filesystem::exists(uvInc)) unitCFlags += " -I " + uvInc.string();
            if (extraLd && std::filesystem::exists(uvLib)
                && extraLd->find(uvLib.string()) == std::string::npos)
                *extraLd += " " + uvLib.string();   // libuv.a 在 op.o 之后参与链接
#ifdef __APPLE__
            if (extraLd && extraLd->find("CoreFoundation") == std::string::npos)
                *extraLd += " -framework CoreFoundation -framework CoreServices";
#endif
#endif
        }
        const std::string finalCsrc = spliceC.empty() ? csrc
                                                      : spliceImpl(csrc, spliceC, scStem);
        if (!writeTextFile(cpath, finalCsrc)) return 1;
        if (!sofSrc.empty() && !writeTextFile(tmpDir / sofName, sofSrc)) return 1;

        std::string hsrc = emitCHeader(kv.second.prog, guardFromHeaderName(hname));
        if (hsrc.empty()) {
            const std::string guard = guardFromHeaderName(hname);
            hsrc = "#ifndef " + guard + "\n#define " + guard + "\n#endif\n";
        }
        if (!writeTextFile(hpath, hsrc)) return 1;

        arts.push_back({cpath, hpath, opath, kv.second.path.parent_path(),
                        linkImpl, unitCFlags});
    }

    // future<ID> 聚合枚举：跨所有单元取 ID 并集（去重、首见序），写出工程级 type.h，
    //   各含 future<ID>/future_id 的单元 .c 已 #include "type.h"（-I tmpDir 可见）。
    {
        std::vector<std::string> allIds;
        for (auto& kv : units)
            for (auto& id : kv.second.prog.futureIds)
                if (std::find(allIds.begin(), allIds.end(), id) == allIds.end())
                    allIds.push_back(id);
        const std::string th = emitFutureIdHeader(allIds);
        if (!th.empty() && !writeTextFile(tmpDir / "type.h", th)) return 1;
    }

    // cls/dim 全局选择子聚合：跨所有单元取类名/维度名并集（去重、首见序），写出工程级
    //   class.h，各使用类机制的单元 .c 已 #include "class.h"。保证 SC_CLS_<T>/SC_DIM_<Name>
    //   在不同单元取同一编号（同名类/维度跨单元一致），从而跨模块 instanceOf / 动态分派正确。
    {
        std::vector<std::string> allCls, allDims;
        bool anyClassRt = false;
        for (auto& kv : units) {
            if (programUsesClassRuntime(kv.second.prog)) anyClassRt = true;
            for (auto& d : kv.second.prog.decls) {
                if (d->kind == Decl::StructD && d->isClass &&
                    std::find(allCls.begin(), allCls.end(), d->name) == allCls.end())
                    allCls.push_back(d->name);
                if (d->kind == Decl::FuncD && d->isDim &&
                    std::find(allDims.begin(), allDims.end(), d->methodName) == allDims.end())
                    allDims.push_back(d->methodName);
            }
        }
        if (anyClassRt) {
            const std::string ch = emitClassHeader(allCls, allDims);
            if (!writeTextFile(tmpDir / "class.h", ch)) return 1;
        }
    }

    // 泛型实例类型聚合：跨所有单元收集泛型单态化产物——全部实例前向 typedef + 自包含实例
    //   完整定义（按类型名去重），写出工程级 generic.h，各含实例的单元 .c 与模块头已
    //   #include "generic.h"（-I tmpDir 可见）。保证实例类型跨模块一致可见（导出签名引用、
    //   按值/指针传递），无需在引用单元重复定义。
    {
        std::vector<const Program*> progs;
        for (auto& kv : units) progs.push_back(&kv.second.prog);
        const std::string gh = emitGenericHeader(progs);
        if (!gh.empty() && !writeTextFile(tmpDir / "generic.h", gh)) return 1;
    }

    // 远程多单元构建：生成阶段到此为止——所有单元 .c/.h 与共享头已落 tmpDir，
    //   交回产物清单（含 srcDir/linkImpl/unitCFlags），由远端编译链接，跳过本机编译。
    if (genOnlyOut) { *genOnlyOut = std::move(arts); return 0; }

    // 第二阶段：统一编译所有 .c -> .o（含已拼接进 .c 的源实现 <stem>_impl.c）
    for (auto& a : arts) {
        // 添加 -g 标志生成调试符号；-I 源目录使 inc "local.h" 可被找到；
        //   a.unitCFlags 为本单元额外编译选项（如 async 拼接 libuv 实现时的 -DSCC_WITH_UV）
        std::string ccCmd = pickCC() + " -g" + tc.machine + tc.cflags + extraCFlags
            + a.unitCFlags + " -I " + tmpDir.string();
        if (!a.srcDir.empty()) ccCmd += " -I " + a.srcDir.string();
        ccCmd += " -c " + a.cpath.string() + " -o " + a.opath.string();
        if (std::system(ccCmd.c_str()) != 0) {
            std::cerr << "错误: C 单元编译失败（" << ccCmd << "）\n";
            return 1;
        }
        objects.push_back(a.opath);
    }

    // 第三阶段：二进制形态的模块实现参与链接
    //   源实现 <stem>_impl.c 已在第一阶段拼接进对应单元 .c（同 TU 编译，第二阶段完成）；
    //   此处仅链接无法拼接的二进制实现——子项目预编译库 <stem>.a（内嵌发行版释放）
    //   或 --adt <x.o|x.a> 替换的二进制实现。线程库/libuv 等链接选项已在第一阶段按需登记。
    for (auto& a : arts) {
        if (!a.linkImpl.empty()) objects.push_back(a.linkImpl);  // .o/.a 直接参与链接
    }

    // 第四阶段：模块内 add 指令声明的实现/库文件参与编译与链接
    //   add impl.c/.cpp/.cc/.cxx → 现场编译为 .o 并链接（解决「由 C 实现的接口」
    //     无机制并入工程的问题：声明 :: 接口的模块自带 add 即可拉入实现）。
    //   add libfoo.a/.so/.dylib/.o → 直接参与链接（替代构建脚本里手写 -l/路径，
    //     主要面向自定义库；跨平台系统库仍走编译选项机制）。
    //   路径相对该模块 .sc 所在目录解析；按规范化路径去重（被多模块/多次 add 安全）。
    {
        std::unordered_set<std::string> seenAdd;  // 已处理的源/库规范化路径
        int addSeq = 0;
        for (auto& kv : units) {
            const std::filesystem::path unitDir =
                std::filesystem::path(kv.first).parent_path();
            for (auto& d : kv.second.prog.decls) {
                if (d->kind != Decl::AddD) continue;

                // 经 add 内联的 add：相对路径按被 add 文件所在目录解析（inlinedFrom）
                const std::filesystem::path srcDir = d->inlinedFrom.empty()
                    ? unitDir : std::filesystem::path(d->inlinedFrom).parent_path();
                // 解析路径：绝对路径直接用，否则相对模块 .sc 目录
                std::filesystem::path target(d->name);
                if (!target.is_absolute()) target = srcDir / target;
                std::error_code ec;
                // 先在未解符号链接的原路径上匹配目标变体 <名>.<suffix>.<ext>，再 canonical；
                // 否则 libwsi.a 之类符号链接会先被解析为宿主变体，令后缀匹配失效。
                const std::filesystem::path variant =
                    resolveAddArtifact(target, tc.targetSuffix);
                const std::filesystem::path canonV =
                    std::filesystem::weakly_canonical(variant, ec);
                const std::filesystem::path resolved = ec ? variant : canonV;

                if (!std::filesystem::exists(resolved)) {
                    std::cerr << "错误: add 文件不存在: " << d->name
                              << "（模块 " << kv.first << "）\n";
                    return 1;
                }
                if (!seenAdd.insert(resolved.string()).second) continue;  // 去重

                const std::string ext = resolved.extension().string();
                if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx") {
                    // 现场编译为 .o（序号避免不同目录同名文件冲突）
                    const std::filesystem::path obj =
                        tmpDir / ("add_" + std::to_string(addSeq++) + "_"
                                  + resolved.stem().string() + ".o");
                    std::string ccCmd = pickCC() + " -g" + tc.machine + tc.cflags
                        + extraCFlags
                        + " -I " + srcDir.string()
                        + " -I " + tmpDir.string()
                        + " -c " + resolved.string() + " -o " + obj.string();
                    if (std::system(ccCmd.c_str()) != 0) {
                        std::cerr << "错误: add 实现编译失败（" << ccCmd << "）\n";
                        return 1;
                    }
                    objects.push_back(obj);
                } else if (ext == ".o" || ext == ".a" || ext == ".so"
                           || ext == ".dylib") {
                    objects.push_back(resolved);  // 库/对象文件直接链接
                } else {
                    std::cerr << "错误: add 不支持的文件类型: " << d->name
                              << "（仅 .c/.cpp/.cc/.cxx/.o/.a/.so/.dylib）\n";
                    return 1;
                }
            }
        }
    }
    return 0;
}

// 收集模块图中所有 add 指令引入的原生依赖（供远程构建上传）。
// 失败（图加载错误）返回 false——远程仍可用内联 C 构建，仅缺 add 依赖。
static bool gatherAddDeps(const std::filesystem::path& rootPath,
                          const std::string& targetSuffix,
                          std::vector<RemoteDep>& out) {
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err)) return false;
    std::unordered_set<std::string> seen;
    for (auto& kv : units) {
        const std::filesystem::path unitDir =
            std::filesystem::path(kv.first).parent_path();
        for (auto& d : kv.second.prog.decls) {
            if (d->kind != Decl::AddD) continue;
            const std::filesystem::path srcDir = d->inlinedFrom.empty()
                ? unitDir : std::filesystem::path(d->inlinedFrom).parent_path();
            std::filesystem::path target(d->name);
            if (!target.is_absolute()) target = srcDir / target;
            std::error_code ec;
            // 先在未解符号链接的原路径上匹配目标变体，再 canonical（见 buildProject 同段注释）
            const std::filesystem::path variant =
                resolveAddArtifact(target, targetSuffix);
            const std::filesystem::path canonV =
                std::filesystem::weakly_canonical(variant, ec);
            const std::filesystem::path resolved = ec ? variant : canonV;
            if (!std::filesystem::exists(resolved)) continue;
            if (!seen.insert(resolved.string()).second) continue;
            const std::string ext = resolved.extension().string();
            const bool isSrc = (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx");
            const bool isLib = (ext == ".o" || ext == ".a" || ext == ".so" || ext == ".dylib");
            if (!isSrc && !isLib) continue;
            out.push_back({resolved.string(), srcDir.string(), isSrc});
        }
    }
    return true;
}

// 从项目入口文件加载，编译为目标产物（不执行、产物保留）
static int buildProject(const std::filesystem::path& rootPath,
                        const std::string& output,
                        const ToolConfig& tc) {

    const OutKind kind = outputKind(output);

    // 根模块导出注入：扫描入口所在目录寻找 @@ 标注的根模块（显式开启条件，取代命令行选项）；
    //   仅 EXE 构建生效（集成单元语义）。根可与入口不同（@@ 标注的「全局前奏提供者」）。
    const std::filesystem::path rootModule =
        kind == OutKind::Exe ? findRootModule(unitDirOf(rootPath)) : std::filesystem::path{};
    const bool enableRP = !rootModule.empty();
    const std::string rootKey = rootModule.string();

    // 根导出 inc 闭包（含根自身）：这些单元不接受注入（避免与根接口头反向引用成环）
    std::unordered_set<std::string> preludeSkip;
    if (enableRP) collectExportedIncClosure(rootModule, preludeSkip);

    // 1. 加载模块图（启用时把根 @导出 语义并入各依赖单元，闭包内单元除外）
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err, {},
                       enableRP ? rootModule : std::filesystem::path{},
                       enableRP ? &preludeSkip : nullptr)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    // 根≠入口且未被 inc 进图时，补加载根模块单元，使其 @导出 的 C 定义参与编译/链接
    if (enableRP && units.find(rootKey) == units.end()
        && !loadUnitGraph(rootModule, units, visiting, err, {}, rootModule, &preludeSkip)) {
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

    // 根接口头名（注入用）：仅当启用且根含 @导出 对象（否则接口头为空，注入无意义）
    std::string rootPreludeHeader;
    if (enableRP) {
        auto it = units.find(rootKey);
        if (it != units.end() && programHasExports(it->second.prog))
            rootPreludeHeader = moduleFileToken(rootKey) + ".h";
    }

    // 3. 编译所有单元为对象文件到临时目录
    std::vector<std::filesystem::path> objects;
    std::string extraLd;
    int rc = compileUnitsToObjects(units, tc,
                                   kind == OutKind::SharedLib ? " -fPIC" : "",
                                   tmpDir, objects, &extraLd,
                                   rootKey, rootPreludeHeader,
                                   enableRP ? &preludeSkip : nullptr);
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

// 远程多单元准备：加载单元图、为每个单元生成 .c/.h（含库模块 sc_mod_*_init/drop 定义、
//   拼入 <stem>_impl.c）与共享头到临时目录，填充 job.unitsDir/units/ldLibs。
//   设置基本同 buildProject（恒按 EXE 语义，启用根导出注入）；区别是只生成不本机编译。
//   临时目录由调用方（remoteDispatch）在 runRemoteJob 后清理。失败置 err 返回 false。
static bool prepareRemoteUnits(const std::filesystem::path& rootPath,
                               const ToolConfig& tc, RemoteJob& job, std::string& err) {
    // 根模块导出注入（恒 EXE）
    const std::filesystem::path rootModule = findRootModule(unitDirOf(rootPath));
    const bool enableRP = !rootModule.empty();
    const std::string rootKey = rootModule.string();
    std::unordered_set<std::string> preludeSkip;
    if (enableRP) collectExportedIncClosure(rootModule, preludeSkip);

    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    if (!loadUnitGraph(rootPath, units, visiting, err, {},
                       enableRP ? rootModule : std::filesystem::path{},
                       enableRP ? &preludeSkip : nullptr))
        return false;
    if (enableRP && units.find(rootKey) == units.end()
        && !loadUnitGraph(rootModule, units, visiting, err, {}, rootModule, &preludeSkip))
        return false;

    char tmpTemplate[] = "/tmp/scc_runits_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) { err = "无法创建临时目录"; return false; }
    const std::filesystem::path tmpDir(dirC);
    job.unitsDir = tmpDir.string();

    std::string rootPreludeHeader;
    if (enableRP) {
        auto it = units.find(rootKey);
        if (it != units.end() && programHasExports(it->second.prog))
            rootPreludeHeader = moduleFileToken(rootKey) + ".h";
    }

    // 只生成 .c/.h（genOnlyOut），不本机编译
    std::vector<std::filesystem::path> dummyObjects;
    std::string extraLd;
    std::vector<UnitArtifact> arts;
    int rc = compileUnitsToObjects(units, tc, "", tmpDir, dummyObjects, &extraLd,
                                   rootKey, rootPreludeHeader,
                                   enableRP ? &preludeSkip : nullptr, "", &arts);
    if (rc != 0) { err = "单元 C 生成失败"; return false; }

    // builtins 根（算各单元 srcDir 的 bundle 内相对路径）
    std::error_code ec;
    const std::filesystem::path broot = tc.builtinsDir.empty()
        ? std::filesystem::path{}
        : std::filesystem::weakly_canonical(std::filesystem::path(tc.builtinsDir), ec);
    // 项目根（= builtins 上级）：用户模块手写头按「项目根相对」入包，与 codegen 的
    //   手写头 #include 路径（如 templates/utils/wsi/wsi.h）一致，令其在远端 /I . 下解析。
    const std::filesystem::path proot = broot.empty()
        ? std::filesystem::path{} : broot.parent_path();
    std::unordered_set<std::string> hdrDirsDone;

    for (auto& a : arts) {
        RemoteUnit u;
        u.cRel = "units/" + a.cpath.filename().string();
        u.cflags = a.unitCFlags;
        // srcDir → bundle 相对：builtins 单元映射到已入包的 builtins/<子目录>；
        //   用户模块（非 builtins）目录内的手写头按项目根相对路径一并入包（extraHeaders），
        //   其 .c 用相对项目根 #include，故 srcDir 留空即可（远端 /I . 覆盖）。
        if (!broot.empty() && !a.srcDir.empty()) {
            const std::filesystem::path sd = std::filesystem::weakly_canonical(a.srcDir, ec);
            const std::filesystem::path rel = sd.lexically_relative(broot);
            if (!rel.empty() && rel.native().rfind("..", 0) != 0)
                u.srcDir = rel == "." ? "builtins" : "builtins/" + rel.generic_string();
            else if (!proot.empty()) {
                const std::filesystem::path prel = sd.lexically_relative(proot);
                if (!prel.empty() && prel.native().rfind("..", 0) != 0) {
                    // 项目根相对目录（手写头入包所在）；令本单元 #include "wsi.h" 经 /I 解析
                    u.srcDir = prel.generic_string();
                    if (hdrDirsDone.insert(sd.string()).second) {
                        std::error_code lec;
                        for (auto& e : std::filesystem::directory_iterator(sd, lec)) {
                            if (!e.is_regular_file()) continue;
                            const std::string ext = e.path().extension().string();
                            if (ext == ".h" || ext == ".hpp" || ext == ".hh"
                                || ext == ".hxx" || ext == ".inc")
                                job.extraHeaders.push_back(
                                    {e.path().string(),
                                     (prel / e.path().filename()).generic_string()});
                        }
                    }
                }
            }
        }
        // 二进制实现（.o/.a）远端暂不支持（需架构匹配）——告警，链接将缺符号。
        if (!a.linkImpl.empty())
            std::cerr << "提示: 远程多单元暂不支持二进制实现 " << a.linkImpl.string()
                      << "（仅源码 <stem>_impl.c 拼接）\n";
        job.units.push_back(std::move(u));
    }

    // 额外链接选项：仅取 -l*（线程库等；vendor 绝对路径 .a 远端无意义，丢弃）
    for (auto& t : splitBy(extraLd, " "))
        if (t.rfind("-l", 0) == 0 &&
            std::find(job.ldLibs.begin(), job.ldLibs.end(), t) == job.ldLibs.end())
            job.ldLibs.push_back(t);
    return true;
}

// 从项目入口文件加载，编译为（临时）可执行文件并运行；程序参数透传，运行结束产物删除
static int compileAndRunProject(const std::filesystem::path& rootPath,
                                const std::vector<std::string>& progArgs,
                                const ToolConfig& tc,
                                const std::string& testKey = "") {

    // 根模块导出注入：扫描入口所在目录寻找 @@ 标注的根模块（显式开启条件）；run 模式恒为 EXE。
    const std::filesystem::path rootModule = findRootModule(unitDirOf(rootPath));
    const bool enableRP = !rootModule.empty();
    const std::string rootKey = rootModule.string();

    // 根导出 inc 闭包（含根自身）：这些单元不接受注入
    std::unordered_set<std::string> preludeSkip;
    if (enableRP) collectExportedIncClosure(rootModule, preludeSkip);

    // 1. 加载模块图（启用时把根 @导出 语义并入各依赖单元，闭包内单元除外）
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err, {},
                       enableRP ? rootModule : std::filesystem::path{},
                       enableRP ? &preludeSkip : nullptr)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    // 根≠入口且未被 inc 进图时，补加载根模块单元，使其 @导出 的 C 定义参与编译/链接
    if (enableRP && units.find(rootKey) == units.end()
        && !loadUnitGraph(rootModule, units, visiting, err, {}, rootModule, &preludeSkip)) {
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

    // 根接口头名（注入用）：仅当启用且根含 @导出 对象
    std::string rootPreludeHeader;
    if (enableRP) {
        auto it = units.find(rootKey);
        if (it != units.end() && programHasExports(it->second.prog))
            rootPreludeHeader = moduleFileToken(rootKey) + ".h";
    }

    std::vector<std::filesystem::path> objects;
    std::string extraLd;
    if (compileUnitsToObjects(units, tc, "", tmpDir, objects, &extraLd,
                              rootKey, rootPreludeHeader,
                              enableRP ? &preludeSkip : nullptr, testKey) != 0) {
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
    std::vector<std::string> cmdCflags;       // -D / --cflags 指定的 C 编译选项（最高优先级）
    std::string adtOpt;                       // --adt 指定的 adt 自定义实现
    std::string fromPath;                     // --from 源文件路径（stdin 输入时供 inc 解析基准目录）
    std::string clangLib;                     // --clang 指定的 libclang 路径（空 + clangRequested = 自动检测）
    bool clangRequested = false;              // 是否出现 --clang（决定检测/加载失败是否报错）
    bool bareO = false;                       // -o 未带值（按输入文件名+模式后缀推导）
    bool graphWhole = true;                    // --graph 默认整程序；--graph=unit 仅当前单元
    if (const char* rc = std::getenv("SCC_REF_CHECK"); rc && *rc && std::string(rc) != "0")
        setRefCheck(true);                    // 环境变量开启 T@ 栈悬挂检查（等价 --check=ref）
    if (const char* mc = std::getenv("SCC_MEM_CHECK"); mc && *mc && std::string(mc) != "0")
        setMemCheck(true);                    // 环境变量开启越界 canary（等价 --check=mem）
    if (const char* pc = std::getenv("SCC_PTR_CHECK"); pc && *pc && std::string(pc) != "0")
        setPtrCheck(true);                    // 环境变量开启运行时指针/下标守卫（等价 --check=ptr）
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
        else if (a == "--cflags" && i + 1 < argc) cmdCflags.push_back(argv[++i]);  // 透传 C 编译选项
        else if (a == "-D" && i + 1 < argc) cmdCflags.push_back("-D" + std::string(argv[++i]));  // -D FOO=1 分写
        else if (a.size() > 2 && a.compare(0, 2, "-D") == 0)
            cmdCflags.push_back(a);                          // -DFOO=1 连写，透传给 C 编译器
        else if (a == "--adt" && i + 1 < argc) adtOpt = argv[++i];  // adt 自定义实现
        else if (a == "--target" && i + 1 < argc) loadProfile(argv[++i]);  // 交叉编译目标档
        else if (a == "--builtins" && i + 1 < argc)                 // 目标适配 builtins 目录
            g_builtinsOverride = argv[++i];
        else if (a == "--from" && i + 1 < argc) fromPath = argv[++i];  // stdin 输入的源路径（inc 解析基准）
        else if (a == "--clang") {                                  // libclang 路径；缺省值则自动检测平台默认位置
            clangRequested = true;
            if (i + 1 < argc && argv[i + 1][0] != '-' && std::string(argv[i + 1]) != "--")
                clangLib = argv[++i];
        }
        else if (a.size() > 2 && a.compare(0, 2, "-l") == 0)
            cmdLibs.push_back(a.substr(2));                  // -lm 写法
        else if (a == "--build") mode = "build";             // 构建产物模式
        else if (a == "--emit-c") mode = "c";                // 转译 C 模式
        else if (a == "--ast") mode = "ast";                 // AST JSON 模式
        else if (a == "--emit-sc") mode = "sc";              // 再生 sc 模式
        else if (a == "--api") mode = "api";                 // 导出接口摘要模式（仅 @导出 签名）
        else if (a == "--test") mode = "test";               // 单元测试模式
        else if (a == "--graph") mode = "graph";             // 程序结构依赖图（整程序，proggraph）
        else if (a == "--graph=unit") { mode = "graph"; graphWhole = false; }  // 仅当前单元
        else if (a == "--check=ref") setRefCheck(true);      // 自动指针 T@ 栈悬挂检查（带源码定位）
        else if (a == "--check" && i + 1 < argc && std::string(argv[i + 1]) == "ref") {
            ++i; setRefCheck(true);                          // --check ref 分写形式
        }
        else if (a == "--check=mem") setMemCheck(true);      // 越界 canary（ref 头堆对象头尾哨兵）
        else if (a == "--check" && i + 1 < argc && std::string(argv[i + 1]) == "mem") {
            ++i; setMemCheck(true);                          // --check mem 分写形式
        }
        else if (a == "--check=ptr") setPtrCheck(true);      // 运行时指针/下标守卫（nil 解引用 + 数组越界）
        else if (a == "--check" && i + 1 < argc && std::string(argv[i + 1]) == "ptr") {
            ++i; setPtrCheck(true);                          // --check ptr 分写形式
        }
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
        else if (mode == "api") name = stem + ".api.sc";
        else if (mode == "build") name = stem;
        else if (mode == "graph") name = stem + ".graph.json";
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

    // ---- .ss 着色器源：GPU/着色器扩展（syntax-s）子管线 ----
    // 与 sc→C 管线并列而非交织：lex(shaderMode) → parse → codegen_glsl。
    //   · --ast / --emit-sc：以 shaderMode 解析后复用通用 AST/源码再生器，
    //     使 sc-ast 视图插件同样能在 .ss 上工作（stage 为 FuncD、I/O 为 def）。
    //     该模式支持 stdin（'-' + --from *.ss），故 AST 视图可实时渲染未保存编辑。
    //   · 其余模式（默认/build/emit-c/run）：走 codegen_glsl 产 GLSL + 反射清单，
    //     仅接受真实文件输入。
    {
        const std::string ssPath = (input != "-") ? input : fromPath;
        const bool isSs = endsWith(ssPath, ".ss");
        if (isSs && (mode == "ast" || mode == "sc")) {
            try {
                Program sgprog = parse(lex(src), /*shaderMode*/ true);
                std::string out = (mode == "ast") ? emitAstJson(sgprog) : emitSc(sgprog);
                if (output.empty()) std::cout << out;
                else if (!writeTextFile(output, out)) return 1;
                return 0;
            } catch (const CompileError& e) {
                std::cerr << (ssPath.empty() ? "ss" : ssPath) << ":" << e.line
                          << ": 错误: " << e.msg << "\n";
                return 1;
            }
        }
        if (input != "-" && isSs) {
            std::string outDir;
            if (!output.empty()) {
                std::filesystem::path op(output);
                outDir = op.has_parent_path() ? op.parent_path().string() : ".";
            }
            return compileShaderSource(src, input, outDir);
        }
    }

    // ---- 3. 编译流水线 + 输出 ----
    try {

        auto prog = parse(lex(src));                                // 词法分析(源码 → token 流) -> 语法分析(token 流 → AST 程序树)
        std::filesystem::path unitPath;                             // 当前单元源路径（文件输入 / stdin 的 --from）
        if (input != "-") unitPath = std::filesystem::path(input);  // 对于目标工程文件输入
        else if (!fromPath.empty()) unitPath = std::filesystem::path(fromPath);  // stdin + --from
        // 顶级 add <file>.sc 内联：拼接被 add 的 .sc（emit-sc 保留 add 指令以保源码回写；
        //   ast 视图保留 add 节点+内联成员，供插件展示归属并跳转到被 add 的源文件）。
        if (mode != "sc") applyAddModules(prog, unitPath, mode == "ast");
        if (!unitPath.empty())                                      // 解析 inc 依赖（不展开源码，合并 .sc 依赖导出声明）
            resolveUnitDeps(prog, unitPath);                        //   使插件实时编辑场景也能合并外部描述符
        mergeOpModule(prog, unitPath);                              // 默认导入 op.sc 语法机制声明（operand/chain 等）

        // 根模块导出注入（单文件分析/转译）：扫描单元所在目录的 @@ 根模块，非根单元并入其 @导出。
        //   使语法插件实时编辑（--ast/--emit-sc）时，依赖单元引用根注入的全局对象不再「未定义」。
        if (!unitPath.empty() && !prog.isRoot) {
            const auto uCanon = std::filesystem::weakly_canonical(unitPath);
            const auto rootModule = findRootModule(unitDirOf(unitPath));
            if (!rootModule.empty() && rootModule != uCanon) {
                std::unordered_set<std::string> skip;
                collectExportedIncClosure(rootModule, skip);        // 根导出 inc 闭包不接受根自身前奏
                const bool inClosure = skip.count(uCanon.string()) != 0;
                mergeRootPrelude(prog, rootModule, uCanon, inClosure);  // 闭包单元仍获 @inc 兄弟可见性
            }
        }

        if (mode == "ast") {                                        // 仅 AST 模式采集 C 头外部描述符
            ClangOptions copt;                                      //   （避免编译/run 每次都解析系统头；保持其它模式行为不变）

            // libclang 路径：--clang <path> > SCC_CLANG 环境变量 > （有请求时）自动检测
            std::string lib = clangLib;
            bool explicitLib = !lib.empty();
            if (lib.empty())
                if (const char* e = std::getenv("SCC_CLANG")) { lib = e; explicitLib = !lib.empty(); }
            const bool wantClang = clangRequested || !lib.empty();
            if (wantClang) {
                if (lib.empty()) lib = detectLibclang();           // 裸 --clang：自动检测平台默认位置
                if (lib.empty()) {                                  // 检测失败 → 报错
                    std::cerr << "错误: 未能在平台默认位置检测到 libclang；"
                                 "请用 --clang <path> 指定动态库，"
                                 "或省略 --clang 退化为头文件文本匹配\n";
                    return 1;
                }
                if (explicitLib && !tryLoadLibclang(lib)) {         // 显式路径但无法加载 → 报错
                    std::cerr << "错误: 无法加载 libclang: " << lib << "\n";
                    return 1;
                }
                copt.libPath = lib;

                // 交叉编译/目标配置 → 传给 libclang，使其按目标平台解析 C 头
                //（与编译/链接共用同一套 triple/sysroot/inc/cflags 配置）
                auto addWords = [&](const std::string& s) {
                    std::istringstream is(s);
                    for (std::string t; is >> t;) copt.args.push_back(t);
                };
                const std::string triple = configValue("SCC_TARGET_TRIPLE", "triple");
                if (!triple.empty()) { copt.args.push_back("-target"); copt.args.push_back(triple); }
                const std::string sysroot = configValue("SCC_SYSROOT", "sysroot");
                if (!sysroot.empty()) copt.args.push_back("--sysroot=" + sysroot);
                addWords(configValue("SCC_TARGET_FLAGS", "target_flags"));
                for (auto& p : splitBy(configValue("SCC_INC", "inc"), ":")) {
                    copt.args.push_back("-I"); copt.args.push_back(p);
                }
                addWords(configValue("SCC_CFLAGS", "cflags"));
                const std::string fst = configValue("SCC_FREESTANDING", "freestanding");
                if (fst == "1" || fst == "true" || fst == "yes") copt.args.push_back("-ffreestanding");
            }
            if (const char* ea = std::getenv("SCC_CLANG_ARGS")) {   // 透传 clang 额外参数（最高优先），空白分隔
                std::istringstream as(ea);
                for (std::string t; as >> t;) copt.args.push_back(t);
            }
            const auto baseDir = unitPath.has_parent_path() ? unitPath.parent_path()
                                                            : std::filesystem::current_path();
            gatherCHeaderDescriptors(prog, baseDir, copt, collectExternalRefs(prog));
        }
        auto warnings = analyzeExternalUsage(prog);                 // 外部描述符使用统计（标记 used）+ 导入未使用警告
        // 泛型宏单态化：仅实际编译路径生效；--emit-sc/--ast/--api 保留原始 def/mix（源码回写/接口摘要）。
        if (mode != "sc" && mode != "ast" && mode != "api") expandGenericMixes(prog);
        ensureBuiltinHeaderSymbols(unitPath.has_parent_path() ? unitPath.parent_path()
                                                              : std::filesystem::current_path());
        semanticCheck(prog);                                        // 语义检查：类型/方法可见性、@导出对象合法性等

        // 3b'. 程序结构依赖图（proggraph）：只读分析，导出 JSON / 自包含 HTML。
        //   整程序（默认）：用 loadUnitGraph 递归解析全部 inc 依赖，从 main/@导出 做激活分析。
        //   单元（--graph=unit）：仅当前已处理单元，外部引用建为叶子节点。
        //   输出格式按 -o 后缀：*.html→HTML 查看器；其余/stdout→JSON。
        if (mode == "graph") {
            std::string gjson;
            const std::string rootDisp = input == "-"
                ? (fromPath.empty() ? std::string("stdin") : fromPath)
                : std::filesystem::weakly_canonical(std::filesystem::path(input)).string();
            if (graphWhole && input != "-") {
                std::unordered_map<std::string, UnitInfo> units;
                std::unordered_set<std::string> visiting;
                std::string gerr;
                const std::filesystem::path rootModule =
                    findRootModule(unitDirOf(std::filesystem::path(input)));
                const bool enableRP = !rootModule.empty();
                std::unordered_set<std::string> preludeSkip;
                if (enableRP) collectExportedIncClosure(rootModule, preludeSkip);
                const std::filesystem::path rp =
                    enableRP ? rootModule : std::filesystem::path{};
                if (!loadUnitGraph(std::filesystem::path(input), units, visiting, gerr,
                                   {}, rp, enableRP ? &preludeSkip : nullptr)) {
                    std::cerr << "错误: " << gerr << "\n";
                    return 1;
                }
                std::vector<GraphUnit> gus;
                for (auto& kv : units) gus.push_back({kv.first, &kv.second.prog});
                gjson = emitGraphJson(gus, rootDisp, true);
            } else {
                std::vector<GraphUnit> gus{{rootDisp, &prog}};
                gjson = emitGraphJson(gus, rootDisp, false);
            }
            const std::string gout =
                (!output.empty() && endsWith(output, ".html")) ? emitGraphHtml(gjson) : gjson;
            if (output.empty()) std::cout << gout;
            else if (!writeTextFile(output, gout)) return 1;
            return 0;
        }

        // 3c. 代码生成：根据 mode 选择后端（run 模式也先生成 C）
        std::string sofHeaderSrc;  // --emit-c -o 模式下 stringify 格式化器（同级 stringify.h）
        if (getRefCheck() || getMemCheck() || getPtrCheck()) setRefSrcFile(input);  // T@ 栈悬挂/栈数组越界/指针下标守卫 site 用源码文件名
        setupProjectRoot(input);   // 头支撑模块手写头 #include 路径的项目根（须早于任何后端 codegen）
        auto c = mode == "ast" ? emitAstJson(prog, warnings)        // AST→JSON（携带外部描述符使用警告）
               : mode == "api" ? emitScApi(prog)                    // AST→导出接口摘要（@导出 签名）
               : mode == "sc"  ? emitSc(prog)                       // AST→规范化sc
               : (mode == "c" && !output.empty())                   // --emit-c 到文件：分离 stringify.h
                     // 源文件输入时以源路径作 #line srcFile：导出的 .c 自带回 .sc 的行号映射，
                     // 用户自行 -g 编译即可源码级调试（断点/单步/堆栈落在 .sc）；stdin 输入无源路径则留空
                     ? emitC(prog, input == "-" ? std::string{} : input, "stringify.h", &sofHeaderSrc)
                     : emitC(prog);                                 // run/stdout：内联自包含

        // 3d. run 模式：不保存中间文件，直接编译并执行（run/build 模式应用工具链扩展配置）
        if (mode == "run") {
            
            ToolConfig tc = loadToolConfig(cmdLibs, adtOpt, cmdCflags);
            addBuiltinsInclude(tc, input);                      // builtins 根级别（platform.h 等）头文件默认可见

            // 远程工具链构建：用已内联自包含的 C 推到远端原生编译并运行（回传输出/退出码）
            if (tc.remoteBuild()) {
                std::vector<RemoteDep> deps;          // add 原生依赖（源码重编/库链接）
                if (input != "-") gatherAddDeps(std::filesystem::path(input), tc.targetSuffix, deps);
                return remoteDispatch(c, tc, "", progArgs, deps, input);
            }

            if (input == "-") return compileAndRunSource(c, progArgs, tc);
            return compileAndRunProject(std::filesystem::path(input), progArgs, tc);
        }

        // 3d''. test 模式：编译目标文件的 tst 用例为测试 runner 并运行（仅文件输入）
        if (mode == "test") {
            if (input == "-") {
                std::cerr << "错误: 测试模式需要源文件输入（不支持 stdin）\n";
                return 1;
            }
            ToolConfig tc = loadToolConfig(cmdLibs, adtOpt, cmdCflags);
            addBuiltinsInclude(tc, input);
            const std::string testKey =
                std::filesystem::weakly_canonical(std::filesystem::path(input)).string();
            return compileAndRunProject(std::filesystem::path(input), progArgs, tc, testKey);
        }

        // 3d'. build 模式：编译链接为持久产物（类型按 -o 后缀决定）
        if (mode == "build") {

            ToolConfig tc = loadToolConfig(cmdLibs, adtOpt, cmdCflags);
            addBuiltinsInclude(tc, input);                      // builtins 根级别（platform.h 等）头文件默认可见

            std::string out = output;
            if (out.empty()) {
                if (input == "-") {
                    std::cerr << "错误: stdin 输入的构建模式必须用 -o 指定输出文件\n";
                    return 1;
                }
                out = std::filesystem::path(input).stem().string();  // 默认使用（去除 .sc 后缀的）输入文件名
            }
            // 远程工具链构建：内联自包含 C 推到远端原生编译并取回产物（暂仅可执行）
            if (tc.remoteBuild()) {
                if (outputKind(out) != OutKind::Exe) {
                    std::cerr << "错误: 远程工具链构建暂仅支持可执行产物"
                                 "（不支持 .a/.so/.bin/.hex）\n";
                    return 1;
                }
                std::vector<RemoteDep> deps;          // add 原生依赖（源码重编/库链接）
                if (input != "-") gatherAddDeps(std::filesystem::path(input), tc.targetSuffix, deps);
                return remoteDispatch(c, tc, out, {}, deps, input);
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

        // 3f''-future. --emit-c 到文件且程序含 future<ID>：在同目录写出 type.h（聚合
        //   future_id 枚举，由 .c #include）。转译工程自包含所需。
        if (mode == "c" && !output.empty() && !prog.futureIds.empty()) {
            const std::string th = emitFutureIdHeader(prog.futureIds);
            std::filesystem::path thPath =
                std::filesystem::path(output).parent_path() / "type.h";
            if (!th.empty() && !writeTextFile(thPath, th)) return 1;
        }

        // 3f''-class. --emit-c 到文件且程序用到类机制：在同目录写出 class.h（SC_CLS_/
        //   SC_DIM_ 选择子枚举，由 .c #include "class.h"）。转译工程自包含所需。
        if (mode == "c" && !output.empty() && programUsesClassRuntime(prog)) {
            std::vector<std::string> cls, dims;
            for (auto& d : prog.decls) {
                if (d->kind == Decl::StructD && d->isClass &&
                    std::find(cls.begin(), cls.end(), d->name) == cls.end())
                    cls.push_back(d->name);
                if (d->kind == Decl::FuncD && d->isDim &&
                    std::find(dims.begin(), dims.end(), d->methodName) == dims.end())
                    dims.push_back(d->methodName);
            }
            const std::string ch = emitClassHeader(cls, dims);
            std::filesystem::path chPath =
                std::filesystem::path(output).parent_path() / "class.h";
            if (!writeTextFile(chPath, ch)) return 1;
        }

        // 3f''-generic. --emit-c 到文件且程序含泛型实例：在同目录写出 generic.h（实例类型
        //   前向 typedef + 自包含实例完整定义，由 .c #include "generic.h"）。转译工程自包含所需。
        if (mode == "c" && !output.empty() && programHasGenericInst(prog)) {
            const std::string gh = emitGenericHeader({&prog});
            if (!gh.empty()) {
                std::filesystem::path ghPath =
                    std::filesystem::path(output).parent_path() / "generic.h";
                if (!writeTextFile(ghPath, gh)) return 1;
            }
        }

        // 3f''. --emit-c 到文件：为每个用户 .sc 模块依赖生成同级 scm_<token>.h，
        // 使输出目录自包含；用户编译时只需 -I <输出目录> -I <builtins>。
        // 带手写 C ABI 头的子项目模块（builtins adt/io 等）由 .c 直接 #include
        // "<name>/<name>.h"（随 -I <builtins> 可见），不复制其内部 scm 头。
        if (mode == "c" && !output.empty() && input != "-") {
            std::unordered_map<std::string, UnitInfo> depUnits;
            std::unordered_set<std::string> visiting;
            std::string depErr;
            const std::string rootKey = std::filesystem::weakly_canonical(
                std::filesystem::path(input)).string();
            // 根模块导出注入开启时，依赖单元语义检查须并入根 @导出（与 build/run 一致），
            // 否则引用根全局类型/操作的依赖会在此 loadUnitGraph 语义复检失败、跳过头生成。
            // 根由 @@ 标记发现（扫描输入所在目录）；根导出 inc 闭包内单元不接受注入。
            const std::filesystem::path rootModule =
                findRootModule(unitDirOf(std::filesystem::path(input)));
            const bool enableRP = !rootModule.empty();
            std::unordered_set<std::string> preludeSkip;
            if (enableRP) collectExportedIncClosure(rootModule, preludeSkip);
            const std::filesystem::path rp =
                enableRP ? rootModule : std::filesystem::path{};
            if (loadUnitGraph(std::filesystem::path(input),
                              depUnits, visiting, depErr, {}, rp,
                              enableRP ? &preludeSkip : nullptr)) {

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
        if (e.srcLine.empty() && e.line > 0) e.srcLine = nthSourceLine(src, e.line);

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
