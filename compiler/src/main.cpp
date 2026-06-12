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
              << "  -l <名>    追加链接库（可重复；-lm 写法也支持，与配置的 libs 合并）\n"
              << "  --adt <x>  adt 自定义实现（.c/.o/.a，照 builtins/adt/adt.h 契约实现）；\n"
              << "             未指定时 inc adt.sc 自动链接内置默认实现 builtins/adt/adt_impl.c\n"
              << "  --build    构建产物模式：编译链接为持久产物，应用与 run 相同的工具链配置\n"
              << "             产物类型按 -o 后缀决定：.a → 静态库（ar rcs）；\n"
              << "             .so/.dylib → 动态库（-shared，单元编译附加 -fPIC）；其余 → 可执行文件\n"
              << "             构建库且存在 @导出对象时，额外生成同名 .h 头文件\n"
              << "             -o 缺省为输入文件名去 .sc 后缀（stdin 输入必须指定 -o）\n"
              << "  --emit-c   转译为 C 源码（配合 -o 输出到文件，缺省 stdout；\n"
              << "             存在 @导出对象且指定 -o 时，额外生成同名 .h 头文件；不受以上编译配置影响）\n"
              << "  --ast      输出 AST JSON 树\n"
              << "  --emit-sc  从 AST 再生成规范化 sc 源码\n"
              << "  -o <file>  输出文件（--build/--emit-c/--ast/--emit-sc 模式下有效）\n"
              << "  '-' 表示从 stdin 读入；'--' 之后的参数传递给被执行的程序\n";
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
        if (trim(l.substr(0, eq)) == key) return trim(l.substr(eq + 1));
    }
    return "";
}

// 诊断辅助函数
// ============================================================
// 从源代码中提取指定行号的代码行（用于错误诊断展示）
static std::string getSourceLine(const std::string& src, int lineNum) {
    if (lineNum <= 0) return "";
    int curLine = 1;
    for (size_t i = 0; i < src.size(); i++) {
        if (curLine == lineNum) {
            size_t lineEnd = src.find('\n', i);
            if (lineEnd == std::string::npos) lineEnd = src.size();
            std::string line = src.substr(i, lineEnd - i);
            // 去掉行尾空白
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) {
                line.pop_back();
            }
            return line;
        }
        if (src[i] == '\n') curLine++;
    }
    return "";
}

// 为编译错误添加诊断信息（源代码行和错误建议）
static void enrichError(CompileError& e, const std::string& src, const std::string& filename) {
    if (e.file.empty()) e.file = filename;
    if (e.srcLine.empty()) {
        std::string srcLine = getSourceLine(src, e.line);
        if (!srcLine.empty()) {
            e.srcLine = srcLine;
        }
    }
}

// 选择系统 C 编译器：环境变量 SCC_CC > CC > .sc 配置文件 cc 项 > 缺省 gcc
static std::string pickCC() {
    const char* cc = std::getenv("SCC_CC");
    if (cc && *cc) return cc;
    cc = std::getenv("CC");
    if (cc && *cc) return cc;
    std::string conf = readConfig("cc");
    if (!conf.empty()) return conf;
    return "gcc";
}

// ---------------- 工具链扩展配置 ----------------
// 环境变量优先，未设置时取 .sc 配置文件同名键：
//   SCC_CFLAGS / cflags    额外编译选项（空格分隔）
//   SCC_LDFLAGS / ldflags  额外链接选项（空格分隔）
//   SCC_INC / inc          头文件搜索路径，':' 分隔（类似 PATH）→ 逐项 -I
//   SCC_LIB / lib          库搜索路径，':' 分隔 → 逐项 -L
//   SCC_LIBS / libs        链接库名，空格或逗号分隔 → 逐项 -l
static std::string configValue(const char* env, const char* key) {
    const char* v = std::getenv(env);
    if (v && *v) return v;
    return readConfig(key);
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

struct ToolConfig {
    std::string cflags;   // 编译阶段附加选项（含 -I 展开）
    std::string ldflags;  // 链接阶段附加选项（含 -L/-l 展开）
    std::string adtImpl;  // adt 自定义实现（--adt/SCC_ADT/.sc 配置 adt；空=内置默认实现）
};

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
    return tc;
}

// 编译+执行：C 源码经管道送入 cc（-x c -），产物为临时可执行文件，
// 运行完立即删除。返回被执行程序的退出码。
static int compileAndRun(const std::string& csrc,
                         const std::vector<std::string>& progArgs,
                         const ToolConfig& tc) {
    // 1. 创建临时可执行文件路径
    char bin[] = "/tmp/scc_run_XXXXXX";
    int fd = mkstemp(bin);
    if (fd < 0) { std::cerr << "错误: 无法创建临时文件\n"; return 1; }
    close(fd);

    // 2. 通过管道把 C 源码送给编译器，不落盘中间 .c 文件
    // 添加 -g 标志生成调试符号，便于 gdb/lldb 进行源代码级调试
    // 单命令编译+链接：cflags 与 ldflags 都附加
    std::string cmd = pickCC() + " -g" + tc.cflags + " -x c - -o " + bin + tc.ldflags;
    FILE* pipe = popen(cmd.c_str(), "w");
    if (!pipe) { std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n"; unlink(bin); return 1; }
    fwrite(csrc.data(), 1, csrc.size(), pipe);
    int crc = pclose(pipe);
    if (crc != 0) {
        std::cerr << "错误: C 编译失败（" << cmd << "）\n";
        unlink(bin);
        return 1;
    }

    // 3. fork+exec 运行产物，透传程序参数，避免 shell 转义问题
    pid_t pid = fork();
    if (pid < 0) { std::cerr << "错误: fork 失败\n"; unlink(bin); return 1; }
    if (pid == 0) {
        std::vector<char*> argv;
        argv.push_back(bin);
        for (auto& a : progArgs) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(bin, argv.data());
        _exit(127);  // exec 失败
    }
    int st = 0;
    waitpid(pid, &st, 0);
    unlink(bin);  // 4. 清理临时产物
    if (WIFSIGNALED(st)) {
        std::cerr << "错误: 程序被信号 " << WTERMSIG(st) << " 终止\n";
        return 128 + WTERMSIG(st);
    }
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

static std::string stripDelims(const std::string& s) {
    if (s.size() >= 2) {
        if ((s.front() == '"' && s.back() == '"') || (s.front() == '<' && s.back() == '>'))
            return s.substr(1, s.size() - 2);
    }
    return s;
}

static bool endsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

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

static std::filesystem::path resolveModulePath(const std::string& raw,
                                               const std::filesystem::path& baseDir) {
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
    pushBuiltins(builtins);
    if (cwdBuiltins != builtins) pushBuiltins(cwdBuiltins);
    if (const char* envB = std::getenv("SCC_BUILTINS"))
        pushBuiltins(std::filesystem::path(envB));
    for (auto& c : candidates)
        if (!c.empty() && std::filesystem::exists(c) && std::filesystem::is_regular_file(c))
            return std::filesystem::weakly_canonical(c);
    return {};
}

static std::string readWholeFile(const std::filesystem::path& p) {
    std::ifstream fin(p);
    if (!fin) return {};
    std::ostringstream ss;
    ss << fin.rdbuf();
    return ss.str();
}

static Program parseSourceText(const std::string& src) {
    return parse(lex(src));
}

static std::string moduleFileToken(const std::string& s) {
    std::string out = "scm_";
    for (unsigned char ch : s) out += std::isalnum(ch) ? (char)ch : '_';
    return out;
}

static std::string moduleHeaderName(const std::string& s) {
    return moduleFileToken(s) + ".h";
}

static std::string guardFromHeaderName(const std::string& hname) {
    std::string g;
    for (char ch : hname) g += std::isalnum((unsigned char)ch) ? (char)std::toupper(ch) : '_';
    return g;
}

static bool writeTextFile(const std::filesystem::path& p, const std::string& content) {
    std::ofstream f(p);
    if (!f) return false;
    f << content;
    return true;
}

struct UnitInfo {
    std::filesystem::path path;
    Program prog;
    std::vector<std::filesystem::path> deps;
};

static std::vector<std::filesystem::path> resolveUnitDeps(Program& prog,
                                                          const std::filesystem::path& srcPath) {
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
            Program mp = parseSourceText(text);
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

static bool loadUnitGraph(const std::filesystem::path& srcPath,
                          std::unordered_map<std::string, UnitInfo>& units,
                          std::unordered_set<std::string>& visiting,
                          std::string& errMsg) {
    const auto canon = std::filesystem::weakly_canonical(srcPath);
    const std::string key = canon.string();
    if (units.find(key) != units.end()) return true;
    if (!visiting.insert(key).second) {
        errMsg = "检测到循环模块依赖: " + key;
        return false;
    }

    const std::string src = readWholeFile(canon);
    if (src.empty()) {
        errMsg = "无法读取模块文件: " + key;
        visiting.erase(key);
        return false;
    }

    UnitInfo u;
    u.path = canon;
    u.prog = parseSourceText(src);
    u.deps = resolveUnitDeps(u.prog, canon);   // 先合并依赖导出声明（external）
    semanticCheck(u.prog);                     // 再检查：导入类型/方法可见

    for (auto& dep : u.deps) {
        if (!loadUnitGraph(dep, units, visiting, errMsg)) {
            visiting.erase(key);
            return false;
        }
    }
    units[key] = std::move(u);
    visiting.erase(key);
    return true;
}

// 把模块图中所有单元生成 .c/.h 并编译为 .o（run 与 build 模式共用）
// extraCFlags 用于追加单元级编译选项（如动态库的 -fPIC）；成功返回 0
static int compileUnitsToObjects(std::unordered_map<std::string, UnitInfo>& units,
                                 const ToolConfig& tc,
                                 const std::string& extraCFlags,
                                 const std::filesystem::path& tmpDir,
                                 std::vector<std::filesystem::path>& objects) {
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
        const std::string hname = moduleHeaderName(kv.first);
        const std::filesystem::path cpath = tmpDir / (token + ".c");
        const std::filesystem::path hpath = tmpDir / hname;
        const std::filesystem::path opath = tmpDir / (token + ".o");

        const std::string csrc = emitC(kv.second.prog, kv.first);  // 带 #line 源码映射
        if (!writeTextFile(cpath, csrc)) {
            std::cerr << "错误: 无法写入 " << cpath << "\n";
            return 1;
        }

        std::string hsrc = emitCHeader(kv.second.prog, guardFromHeaderName(hname));
        if (hsrc.empty()) {
            const std::string guard = guardFromHeaderName(hname);
            hsrc = "#ifndef " + guard + "\n#define " + guard + "\n#endif\n";
        }
        if (!writeTextFile(hpath, hsrc)) {
            std::cerr << "错误: 无法写入 " << hpath << "\n";
            return 1;
        }
        arts.push_back({cpath, hpath, opath, kv.second.path.parent_path()});
    }

    // 第二阶段：统一编译所有 .c -> .o
    for (auto& a : arts) {
        // 添加 -g 标志生成调试符号；-I 源目录使 inc "local.h" 可被找到
        std::string ccCmd = pickCC() + " -g" + tc.cflags + extraCFlags + " -I " + tmpDir.string();
        if (!a.srcDir.empty()) ccCmd += " -I " + a.srcDir.string();
        ccCmd += " -c " + a.cpath.string() + " -o " + a.opath.string();
        if (std::system(ccCmd.c_str()) != 0) {
            std::cerr << "错误: C 单元编译失败（" << ccCmd << "）\n";
            return 1;
        }
        objects.push_back(a.opath);
    }

    // 第三阶段：adt 实现自动参与链接（单元图含 builtins 的 adt.sc 时）
    //   未指定 --adt → 内置默认实现 builtins/adt/adt_impl.c
    //   --adt <x.c|x.o|x.a> → 替换为自定义实现
    std::filesystem::path adtDir;
    for (auto& kv : units) {
        const std::filesystem::path p(kv.first);
        if (p.filename() == "adt.sc" && p.parent_path().filename() == "adt") {
            adtDir = p.parent_path();
            break;
        }
    }
    if (!adtDir.empty()) {
        std::filesystem::path impl = tc.adtImpl.empty()
            ? adtDir / "adt_impl.c" : std::filesystem::path(tc.adtImpl);
        if (!std::filesystem::exists(impl)) {
            std::cerr << "错误: adt 实现文件不存在: " << impl.string() << "\n";
            return 1;
        }
        if (impl.extension() == ".c") {
            const std::filesystem::path obj = tmpDir / "adt_impl.o";
            std::string ccCmd = pickCC() + " -g" + tc.cflags + extraCFlags
                + " -I " + adtDir.string()
                + " -c " + impl.string() + " -o " + obj.string();
            if (std::system(ccCmd.c_str()) != 0) {
                std::cerr << "错误: adt 实现编译失败（" << ccCmd << "）\n";
                return 1;
            }
            objects.push_back(obj);
        } else {
            objects.push_back(impl);  // .o/.a 直接参与链接
        }
    }
    return 0;
}

// ---------------- 构建产物模式（--build）----------------
// 产物类型由输出文件名后缀决定
enum class OutKind { Exe, StaticLib, SharedLib };

static OutKind outputKind(const std::string& out) {
    if (endsWith(out, ".a")) return OutKind::StaticLib;
    if (endsWith(out, ".so") || endsWith(out, ".dylib")) return OutKind::SharedLib;
    return OutKind::Exe;
}

// 把 .o 列表合成最终产物：可执行（链接）/ 静态库（ar rcs）/ 动态库（-shared）
static int linkOutput(OutKind kind,
                      const std::vector<std::filesystem::path>& objects,
                      const std::string& output,
                      const ToolConfig& tc) {
    std::string cmd;
    if (kind == OutKind::StaticLib) {
        cmd = "ar rcs " + output;
        for (auto& o : objects) cmd += " " + o.string();
    } else {
        cmd = pickCC() + " -g";
        if (kind == OutKind::SharedLib) cmd += " -shared";
        for (auto& o : objects) cmd += " " + o.string();
        cmd += " -o " + output + tc.ldflags;
    }
    if (std::system(cmd.c_str()) != 0) {
        std::cerr << "错误: 构建产物失败（" << cmd << "）\n";
        return 1;
    }
#ifdef __APPLE__
    // macOS 调试信息在 .o 中（产物仅存 debug map），临时 .o 即将删除，
    // 先用 dsymutil 打包为 .dSYM，使 lldb 能映射回 .sc 源码
    if (kind != OutKind::StaticLib)
        std::system(("dsymutil " + output + " >/dev/null 2>&1").c_str());
#endif
    return 0;
}

// 文件输入的构建：模块图 → .o → 按类型合成产物（不执行、产物保留）
static int buildProject(const std::filesystem::path& rootPath,
                        const std::string& output,
                        const ToolConfig& tc) {
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    char tmpTemplate[] = "/tmp/scc_units_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) {
        std::cerr << "错误: 无法创建临时目录\n";
        return 1;
    }
    const std::filesystem::path tmpDir(dirC);
    const OutKind kind = outputKind(output);
    std::vector<std::filesystem::path> objects;
    int rc = compileUnitsToObjects(units, tc,
                                   kind == OutKind::SharedLib ? " -fPIC" : "",
                                   tmpDir, objects);
    if (rc == 0) rc = linkOutput(kind, objects, output, tc);
    std::filesystem::remove_all(tmpDir);
    return rc;
}

// stdin 输入的构建：单文件 C 源码经管道编译为产物
static int buildFromCSource(const std::string& csrc,
                            const std::string& output,
                            const ToolConfig& tc) {
    const OutKind kind = outputKind(output);
    // 可执行：单命令编译+链接直达输出
    if (kind == OutKind::Exe) {
        std::string cmd = pickCC() + " -g" + tc.cflags + " -x c - -o " + output + tc.ldflags;
        FILE* pipe = popen(cmd.c_str(), "w");
        if (!pipe) { std::cerr << "错误: 无法启动 C 编译器: " << cmd << "\n"; return 1; }
        fwrite(csrc.data(), 1, csrc.size(), pipe);
        if (pclose(pipe) != 0) { std::cerr << "错误: C 编译失败（" << cmd << "）\n"; return 1; }
        return 0;
    }
    // 库：先编译临时 .o，再 ar / -shared 合成
    char tmpTemplate[] = "/tmp/scc_build_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) { std::cerr << "错误: 无法创建临时目录\n"; return 1; }
    const std::filesystem::path tmpDir(dirC);
    const std::filesystem::path obj = tmpDir / "unit.o";
    std::string cmd = pickCC() + " -g" + tc.cflags
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

static int compileAndRunProject(const std::filesystem::path& rootPath,
                                const std::vector<std::string>& progArgs,
                                const ToolConfig& tc) {
    std::unordered_map<std::string, UnitInfo> units;
    std::unordered_set<std::string> visiting;
    std::string err;
    if (!loadUnitGraph(rootPath, units, visiting, err)) {
        std::cerr << "错误: " << err << "\n";
        return 1;
    }

    char tmpTemplate[] = "/tmp/scc_units_XXXXXX";
    char* dirC = mkdtemp(tmpTemplate);
    if (!dirC) {
        std::cerr << "错误: 无法创建临时目录\n";
        return 1;
    }
    const std::filesystem::path tmpDir(dirC);
    std::vector<std::filesystem::path> objects;
    if (compileUnitsToObjects(units, tc, "", tmpDir, objects) != 0) {
        std::filesystem::remove_all(tmpDir);
        return 1;
    }

    // 链接所有对象文件
    const std::filesystem::path bin = tmpDir / "run.out";
    std::string linkCmd = pickCC() + " -g";  // 添加 -g 保留调试符号
    for (auto& o : objects) linkCmd += " " + o.string();
    linkCmd += " -o " + bin.string() + tc.ldflags;
    if (std::system(linkCmd.c_str()) != 0) {
        std::cerr << "错误: 链接失败（" << linkCmd << "）\n";
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
        argv.push_back(const_cast<char*>(binS.c_str()));
        for (auto& a : progArgs) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execv(binS.c_str(), argv.data());
        _exit(127);
    }

    int st = 0;
    waitpid(pid, &st, 0);
    std::filesystem::remove_all(tmpDir);
    if (WIFSIGNALED(st)) return 128 + WTERMSIG(st);
    return WIFEXITED(st) ? WEXITSTATUS(st) : 1;
}

int main(int argc, char** argv) {
    // ---- 1. 解析命令行参数 ----
    std::string input, output, mode = "run";  // 默认：编译+执行
    std::vector<std::string> progArgs;        // '--' 后透传给被执行程序的参数
    std::vector<std::string> cmdLibs;         // -l 指定的链接库名
    std::string adtOpt;                       // --adt 指定的 adt 自定义实现
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--") {                                     // 其后全部为程序参数
            for (i++; i < argc; i++) progArgs.push_back(argv[i]);
            break;
        }
        else if (a == "-o" && i + 1 < argc) output = argv[++i];  // 输出文件（可选）
        else if (a == "-l" && i + 1 < argc) cmdLibs.push_back(argv[++i]);  // -l m
        else if (a == "--adt" && i + 1 < argc) adtOpt = argv[++i];  // adt 自定义实现
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

    // ---- 3. 编译流水线 + 输出 ----
    // 提取源代码字符串用于错误诊断（catch 块中需要）
    std::string src = ss.str();
    
    try {
        // 3a. 词法分析：源码 → token 流
        auto toks = lex(src);
        // 3b. 语法分析：token 流 → AST 程序树
        auto prog = parse(toks);
        // 3b.0 记录当前单元的模块依赖信息（不展开源码，合并依赖导出声明）
        if (input != "-") resolveUnitDeps(prog, std::filesystem::path(input));
        // 3b.1 语义检查：类型兼容/指针安全边界（含导入符号）
        semanticCheck(prog);;
        // 3c. 代码生成：根据 mode 选择后端（run 模式也先生成 C）
        auto c = mode == "ast" ? emitAstJson(prog)   // AST→JSON
               : mode == "sc"  ? emitSc(prog)         // AST→规范化sc
                               : emitC(prog);         // AST→C（run/--emit-c）

        // 3d. run 模式：不保存中间文件，直接编译并执行（run/build 模式应用工具链扩展配置）
        if (mode == "run") {
            const ToolConfig tc = loadToolConfig(cmdLibs, adtOpt);
            if (input == "-") return compileAndRun(c, progArgs, tc);
            return compileAndRunProject(std::filesystem::path(input), progArgs, tc);
        }

        // 3d'. build 模式：编译链接为持久产物（类型按 -o 后缀决定）
        if (mode == "build") {
            const ToolConfig tc = loadToolConfig(cmdLibs, adtOpt);
            std::string out = output;
            if (out.empty()) {
                if (input == "-") {
                    std::cerr << "错误: stdin 输入的构建模式必须用 -o 指定输出文件\n";
                    return 1;
                }
                out = std::filesystem::path(input).stem().string();  // 缺省：输入名去 .sc
            }
            int rc = input == "-" ? buildFromCSource(c, out, tc)
                                  : buildProject(std::filesystem::path(input), out, tc);
            if (rc != 0) return rc;
            // 构建库时：存在 @导出对象则生成同名 .h 接口头文件（取根模块导出）
            if (outputKind(out) != OutKind::Exe) {
                std::string hpath = out;
                for (const char* ext : {".a", ".so", ".dylib"}) {
                    if (endsWith(hpath, ext)) { hpath.resize(hpath.size() - strlen(ext)); break; }
                }
                hpath += ".h";
                std::string guard;
                size_t slash = hpath.find_last_of('/');
                for (char ch : hpath.substr(slash == std::string::npos ? 0 : slash + 1))
                    guard += isalnum((unsigned char)ch) ? (char)toupper(ch) : '_';
                auto h = emitCHeader(prog, guard);
                if (!h.empty() && !writeTextFile(hpath, h)) {
                    std::cerr << "错误: 无法写入文件 " << hpath << "\n";
                    return 1;
                }
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
            // include guard 宏名：文件名转大写，非字母数字转 '_'
            std::string guard;
            size_t slash = hpath.find_last_of('/');
            for (char ch : hpath.substr(slash == std::string::npos ? 0 : slash + 1))
                guard += isalnum((unsigned char)ch) ? (char)toupper(ch) : '_';
            auto h = emitCHeader(prog, guard);
            if (!h.empty()) {
                std::ofstream hout(hpath);
                if (!hout) {
                    std::cerr << "错误: 无法写入文件 " << hpath << "\n";
                    return 1;
                }
                hout << h;
            }
        }

        // 3f. 输出结果到文件或 stdout
        if (output.empty()) {
            std::cout << c;
        } else {
            std::ofstream fout(output);
            if (!fout) {
                std::cerr << "错误: 无法写入文件 " << output << "\n";
                return 1;
            }
            fout << c;
        }
    } catch (CompileError e) {
        // 为错误添加诊断信息：源代码行 + 文件名
        enrichError(e, src, input);
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
