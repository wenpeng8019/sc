#pragma once
#include <string>
#include <utility>
#include <vector>

#include "lexer.h"

// ============================================================
// shader_spec —— 编译期特化维度系统（spec，见 spec.md）
// ============================================================
// 无体 spec 声明一个编译期枚举维度（P1，类型/符号直代）：
//   spec TEX_2D in [sampler2D, samplerExternalOES]
// 有体 spec 声明与分支合一（P3）：分支标签即取值集合，实例展开对应分支体：
//   spec BLEND:
//       ADD:
//           fnc blend: vec3, a: vec3, b: vec3
//               return a + b
//       MUL:
//           ...
// use 白名单（P2）：默认全笛卡尔积；声明 use 后仅输出显式列出的组合，
// 未列维度仍取全集与已列维度做积：
//   use TONE, BLEND
//       tm_a, ADD
//       tm_b, MUL
//
// 实现策略：**词法层单态化** —— lex 后从 token 流中提取并移除顶层 spec/use
// 行，按实例组合把维度名标识符替换为具体取值、有体维度在原位插入选中分支体，
// 再各自走既有 parse → sema → 发射管线。parser/sema/codegen 对 spec 零感知。
// ============================================================

// 一个特化维度：名字 + 有限取值集合（声明序）。
struct ShaderSpecDim {
    std::string name;                 // 维度名（如 TEX_2D / BLEND）
    std::vector<std::string> values;  // 取值集合（无体=标识符；有体=分支标签）
    int line = 0;                     // 声明行号（诊断用）
    // 有体 spec（P3）：与 values 对齐的分支体 token；无体时为空。
    bool hasBody = false;
    std::vector<std::vector<Token>> bodies;
    size_t insertPos = 0;             // 有体：分支体在提取后 token 流中的插入位置
};

// use 白名单表（P2）：表头维度序 + 数据行（每行一个实例组合）。
struct ShaderSpecUse {
    std::vector<std::string> dims;               // 表头维度名（书写序）
    std::vector<std::vector<std::string>> rows;  // 数据行（与表头对位）
    std::vector<int> rowLines;                   // 各数据行行号（诊断用）
    int line = 0;                                // use 行号
};

// 提取结果聚合：维度集 + 可选 use 表。
struct ShaderSpecSet {
    std::vector<ShaderSpecDim> dims;
    ShaderSpecUse use;
    bool hasUse = false;
    bool empty() const { return dims.empty(); }
};

// 一个实例组合：按维度声明序的 (维度名, 取值) 列表；空 = 无 spec 的平凡实例。
using ShaderSpecCombo = std::vector<std::pair<std::string, std::string>>;

// 提取并移除 token 流中的顶层 spec/use 声明（就地修改 toks）。
// 校验：维度重名 / 取值（标签）重复 / 空集 / 有体分支 @def 接口同构 /
// use 表重复声明 —— 违规抛 CompileError。
ShaderSpecSet extractShaderSpecs(std::vector<Token>& toks);

// 实例组合展开（组合内按维度声明序）：
//   无 use = 全笛卡尔积；有 use = 白名单行 × 未列维度全集（结果去重）。
// use 校验（表头维度未声明/重复、值域、行宽、重复行、空表）在此进行。
// 无维度 → 返回单个空组合。
std::vector<ShaderSpecCombo> shaderSpecCombos(const ShaderSpecSet& set);

// 按组合物化实例 token 流：有体维度在原位插入选中分支体，再把维度名标识符
// 替换为取值。空组合原样返回。
std::vector<Token> applyShaderSpec(const std::vector<Token>& toks,
                                   const ShaderSpecSet& set,
                                   const ShaderSpecCombo& combo);

// 实例标签：取值按声明序以 '.' 连接（如 "sampler2D.ADD"）；空组合 = ""。
std::string shaderSpecLabel(const ShaderSpecCombo& combo);
