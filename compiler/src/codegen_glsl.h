#pragma once
#include <string>
#include <vector>
#include "ast.h"
#include "shader_spec.h"

// ============================================================
// codegen_glsl —— GPU/着色器扩展（syntax-s）一期后端
// ============================================================
// 把 .ss 源里的着色阶段（vert/frag/comp）翻译为 Vulkan-GLSL 文本。
// 与核心 sc→C 管线（codegen_c）完全独立（见 syntax-s.md §12），
// 后续 GLSL→SPIR-V→各平台后端交由运行时（shaderc/SPIRV-Cross/MoltenVK）。
//
// 本文件当前为「walking skeleton」：验证 .ss 路由 → 解析阶段 → 发射合法 GLSL
// 的端到端链路。类型系统（vec/mat/swizzle）、语义子集强制、阶段 I/O 与绑定、
// 反射清单等在后续迭代补齐。
// ============================================================

// 一个着色阶段的 GLSL 产物。
struct GlslUnit {
    ShaderStage stage;      // 着色阶段
    std::string entry;      // 入口名（来自 .ss 源）
    std::string ext;        // 建议文件扩展名（vert/frag/comp）
    std::string text;       // 生成的 GLSL 源文本
};

// 将已按 shaderMode 解析的 Program 中所有阶段入口发射为 GLSL 文本（按目标 target）。
std::vector<GlslUnit> emitGlsl(const Program& prog, const GlslTarget& target);

// 生成反射清单 JSON（syntax-s §10）：目标（api/version）、阶段、入口、顶点属性
// location、varying 配对、uniform/storage/sampler 的 set/binding 与 std140/std430
// 字段偏移。低版本目标（无显式 binding）resources 项保留 name 供运行时按名绑定。
// spec 非空时附加 "spec": {维度: 取值, ...} 字段（spec.md §5，供运行时选实例）。
std::string emitReflectionJson(const Program& prog, const GlslTarget& target,
                               const ShaderSpecCombo* spec = nullptr);

// .ss 源到产物的端到端驱动：lex → parse(shaderMode=true) → shader_sema → 发射。
// 默认产物链全目标统一 SPIR-V 中枢（codegen_spirv 直发）：vulkan 直落 .spv，
// metal 经 SPIRV-Cross→MSL，glcore/gles 经 SPIRV-Cross 反译 GLSL。
// emitGlslText：--emit-glsl —— gl/gles/metal(内部 vulkan 语义) 改用自研 codegen_glsl
//   文本发射（对照/兜底通道，产物同名）。
// emitSpvFiles：--emit-spv —— 非 vulkan 目标也额外落盘 <entry><tag>.spv 中间文件。
// 产物形态：默认资源化 <stem>.shader.h/.c（字节数组 + enum id + 反射 JSON +
//   按名查询函数，供 add 直接链入应用）；filesMode（--files）落散文件
//   <entry><tag>.<ext> + <stem><tag>.reflect.json。
// outPath：-o 原始值（资源模式的 stem 依据）；空 = stdout。
// outDir 为空 → 打印到 stdout（二进制 .spv 仅提示不入 stdout）；否则写入 outDir。
// 返回进程退出码（0 成功）。
int compileShaderSource(const std::string& src, const std::string& srcPath,
                        const std::string& outDir,
                        bool emitGlslText = false, bool emitSpvFiles = false,
                        bool filesMode = false, const std::string& outPath = {});
