#pragma once

// ============================================================
// shader 方言 AST 辅助（syntax-g 扩展）—— 最小侵入核心 ast.h
// ============================================================
// GPU/着色器扩展（见 syntax-g.md）把 sc 的一个方言子集编译到 GLSL/SPIR-V。
// 阶段入口（vert/frag/comp）复用核心 Decl(FuncD) 的解析与 AST 表示，
// 仅额外携带一个「着色阶段」标记 —— 即本文件定义的 ShaderStage。
//
// 该头无任何依赖，可被 ast.h 安全 #include（不引入循环）。
// shader 专属的语义检查与代码生成在独立模块 shader_sema.* / codegen_glsl.*，
// 不混入核心前端。
// ============================================================

// 着色阶段：标注一个 FuncD 是哪种着色器入口（None = 普通函数 / 非着色器）。
enum class ShaderStage {
    None = 0,   // 非着色器入口（核心 sc 函数）
    Vert,       // vert 顶点着色入口   → SPIR-V Vertex / GLSL .vert
    Frag,       // frag 片元着色入口   → SPIR-V Fragment / GLSL .frag
    Comp,       // comp 计算着色入口   → SPIR-V GLCompute / GLSL .comp
};
