// ============================================================
// scc 编译器 —— 主入口
// ============================================================
// 整个编译流水线： lex → parse → emit
//   lex:  源码字符串 → Token 序列
//   parse: Token 序列 → Program AST 树
//   emit:  Program → 输出（C源码 / AST JSON / 规范化sc源码）
//
// 三种输出模式：
//   默认     → emitC()       sc源码转译为C源码
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
#include <fstream>
#include <iostream>
#include <sstream>

static void usage() {
    std::cerr << "用法: scc <input.sc | -> [-o output] [--ast | --emit-sc]\n"
              << "  默认：转换为 C 源文件（缺省输出到 stdout；'-' 表示从 stdin 读入）\n"
              << "  --ast      输出 AST JSON 树\n"
              << "  --emit-sc  从 AST 再生成规范化 sc 源码\n";
}

int main(int argc, char** argv) {
    // ---- 1. 解析命令行参数 ----
    std::string input, output, mode = "c";  // mode 默认转译为 C
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) output = argv[++i];  // 输出文件（可选）
        else if (a == "--ast") mode = "ast";                // AST JSON 模式
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
        // 3c. 代码生成：根据 mode 选择后端
        auto c = mode == "ast" ? emitAstJson(prog)   // AST→JSON
               : mode == "sc"  ? emitSc(prog)         // AST→规范化sc
                               : emitC(prog);         // AST→C（默认）

        // 3d. 输出结果：文件或 stdout
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
