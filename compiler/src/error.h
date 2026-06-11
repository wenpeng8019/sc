#pragma once
#include <string>
#include <vector>

// ============================================================
// 编译错误结构体 与诊断信息
// ============================================================
// 整个编译器使用统一的异常机制报告错误：
//   - 词法分析、语法分析、代码生成各阶段，发现错误时构造 CompileError 并 throw
//   - main.cpp 中的 try/catch 统一捕获所有异常
//   - 格式化输出，包括上下文代码、修复建议等
//   - line=0 表示无法确定行号（极少见，如文件级错误）
// ============================================================
struct CompileError {
    std::string file;    // 源文件名（缺省为 "<input>"）
    std::string msg;     // 错误描述文本
    int line = 0;        // 出错行号（1-based），默认 0 表示未知行
    std::string hint;    // 可选的修复建议或补充信息
    std::string srcLine; // 可选的出错源代码行
    
    CompileError() = default;
    // 支持两种参数顺序以实现向后兼容
    CompileError(const std::string& m, int l) : msg(m), line(l) {}
    CompileError(const std::string& m, int l, const std::string& h) : msg(m), line(l), hint(h) {}
};
