// ============================================================
// scc 编译器 —— 主入口
// ============================================================
// 整个编译流水线： lex → parse → emit [→ cc 编译 → 执行]
//   lex:  源码字符串 → Token 序列
//   parse: Token 序列 → Program AST 树
//   emit:  Program → 输出（C源码 / AST JSON / 规范化sc源码）
//
// 四种运行模式：
//   默认      → 编译+执行：转 C 后直接用系统 C 编译器编译并运行，
//              不保存中间文件，整体效果类似解释器
//              C 编译器选择：$SCC_CC > $CC > gcc
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
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

static void usage() {
    std::cerr << "用法: scc <input.sc | -> [选项] [-- 程序参数...]\n"
              << "  默认：转 C 后直接编译并执行（类似解释器，不保存中间文件）\n"
              << "        C 编译器优先级：环境变量 SCC_CC > CC > 当前目录 .sc 配置文件 > gcc\n"
              << "        .sc 配置文件格式：cc = clang（# 行注释）\n"
              << "  --emit-c   转译为 C 源码（配合 -o 输出到文件，缺省 stdout；\n"
              << "             存在 @导出对象且指定 -o 时，额外生成同名 .h 头文件）\n"
              << "  --ast      输出 AST JSON 树\n"
              << "  --emit-sc  从 AST 再生成规范化 sc 源码\n"
              << "  -o <file>  输出文件（--emit-c/--ast/--emit-sc 模式下有效）\n"
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

// 编译+执行：C 源码经管道送入 cc（-x c -），产物为临时可执行文件，
// 运行完立即删除。返回被执行程序的退出码。
static int compileAndRun(const std::string& csrc,
                         const std::vector<std::string>& progArgs) {
    // 1. 创建临时可执行文件路径
    char bin[] = "/tmp/scc_run_XXXXXX";
    int fd = mkstemp(bin);
    if (fd < 0) { std::cerr << "错误: 无法创建临时文件\n"; return 1; }
    close(fd);

    // 2. 通过管道把 C 源码送给编译器，不落盘中间 .c 文件
    std::string cmd = pickCC() + " -x c - -o " + bin;
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

int main(int argc, char** argv) {
    // ---- 1. 解析命令行参数 ----
    std::string input, output, mode = "run";  // 默认：编译+执行
    std::vector<std::string> progArgs;        // '--' 后透传给被执行程序的参数
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "--") {                                     // 其后全部为程序参数
            for (i++; i < argc; i++) progArgs.push_back(argv[i]);
            break;
        }
        else if (a == "-o" && i + 1 < argc) output = argv[++i];  // 输出文件（可选）
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
    try {
        // 3a. 词法分析：源码 → token 流
        auto toks = lex(ss.str());
        // 3b. 语法分析：token 流 → AST 程序树
        auto prog = parse(toks);
        // 3c. 代码生成：根据 mode 选择后端（run 模式也先生成 C）
        auto c = mode == "ast" ? emitAstJson(prog)   // AST→JSON
               : mode == "sc"  ? emitSc(prog)         // AST→规范化sc
                               : emitC(prog);         // AST→C（run/--emit-c）

        // 3d. run 模式：不保存中间文件，直接编译并执行
        if (mode == "run") return compileAndRun(c, progArgs);

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
    } catch (const CompileError& e) {
        // 统一错误格式：文件:行号: 错误: 消息
        // VSCode 等 IDE 可解析此格式实现点击跳转到错误行
        std::cerr << input << ":" << e.line << ": 错误: " << e.msg << "\n";
        return 1;
    }
    return 0;
}
