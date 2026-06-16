#pragma once
#include "ast.h"
#include <filesystem>
#include <string>
#include <unordered_set>
#include <vector>

// ============================================================
// C 头文件外部描述符采集
// ============================================================
// 为当前单元的所有 C 头 inc（inc stdio.h / inc "my.h"）合成外部描述符 Decl
//（external=true, origin=头名），使其与 .sc 模块导出的描述符走同一套
// 使用分析 / 插件展示逻辑。
//
// 两种符号来源：
//   1. libclang（ClangOptions::libPath 有效且可加载）：对每个 C 头单独建
//      `#include <X>` 翻译单元，dlopen libclang 枚举其全部顶层符号（含递归
//      包含带入的——聚合头如 windows.h 也能枚举到真符号），按类别映射 Decl::Kind。
//   2. 退化文本匹配（无 libclang）：在能定位到的头文件文本里查 refs 中的标识符
//      是否出现；只能识别"已被引用"的符号，无法枚举未使用符号。
//
// 降噪：总是合成 used 符号；unused 符号仅当该头声明总数 ≤ 阈值时逐个合成，
// 否则只在 inc 节点上记录总数（externDeclared），供插件显示"已用 N / 共 M"。
// ============================================================

struct ClangOptions {
    std::string              libPath;  // libclang 动态库路径（空 = 退化文本匹配）
    std::vector<std::string> args;     // 透传 clang 的额外参数（-I/-D/-isysroot ...）
};

// refs：本单元自身代码引用到的名字集合（来自 collectExternalRefs）。
// 就地修改 prog：标记 C 头 inc 为 external 并设置 origin/externDeclared/
// externAnalyzed，追加合成的外部描述符 Decl。
void gatherCHeaderDescriptors(Program& prog,
                              const std::filesystem::path& baseDir,
                              const ClangOptions& opt,
                              const std::unordered_set<std::string>& refs);

// 探测本地平台默认位置的 libclang 动态库（macOS Xcode/CommandLineTools/Homebrew，
// Linux 常见库目录与 llvm-* 工具链，Windows LLVM 安装目录，以及交由动态链接器
// 搜索的 soname）。返回首个可成功 dlopen 的路径；未找到返回空字符串。
std::string detectLibclang();

// 测试指定路径的 libclang 能否被 dlopen 加载（用于校验 --clang <path>）。
bool tryLoadLibclang(const std::string& path);
