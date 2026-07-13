#pragma once

#include <memory>
#include <string>

// ============================================================
// shader 方言 AST 辅助（syntax-s 扩展）—— 最小侵入核心 ast.h
// ============================================================
// GPU/着色器扩展（见 syntax-s.md）把 sc 的一个方言子集编译到 GLSL/SPIR-V。
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

// 字段级绑定属性（syntax-s §5）：附着于 @def 结构体字段（仅 .ss 有意义）。
// 语法（后缀限定词，仅 shader 模式解析）：
//   pos: vec3 loc 0            → layout(location=0) 顶点属性 / varying 位置
//   clip: vec4 builtin position→ 内建变量语义（gl_Position 等），不占 location
struct ShaderFieldAttr {
    int         loc = -1;       // loc N（-1 = 未指定，由 codegen 按字段序自动分配）
    std::string builtin;        // builtin X（内建语义名，如 position/frag_coord），空 = 无
    // 插值限定词（varying 修饰；整数类型自动补 flat）
    enum Interp { Default = 0, Flat, NoPerspective, Centroid } interp = Default;
    // 特化常量（P2）：顶层 `let NAME: T = 字面量 spec N` → OpSpecConstant + SpecId N
    //（管线创建期可覆写；MSL = function_constant，Vulkan = VkSpecializationInfo）
    int specId = -1;
};

// 结构体级资源绑定（syntax-s §6）：附着于 @def 结构体（仅 .ss 有意义）。
// 语法（后缀限定词，仅 shader 模式解析）：
//   @def Camera: { mvp: mat4 } uniform set 0 binding 0
//   @def Lights: { ... } storage set 0 binding 1
//   @def Push: { ... } push
// 另兼作 comp 阶段入口的阶段级属性载体（签名尾 `local X [Y [Z]]` → local[3]）。
struct ShaderDeclAttr {
    // Shared = comp 共享内存块（Workgroup 存储类，无 set/binding）：
    //   @def Tile: { data[256]: f4 } shared
    enum Res { None = 0, Uniform, Storage, Push, Shared } res = None;
    int set = -1;               // 描述符 set（push 常量无意义）
    int binding = -1;           // 描述符 binding
    // comp 工作组尺寸（ExecutionMode LocalSize）：签名尾 `local X [Y [Z]]` 声明，
    // 缺省补 1；全 0 = 未声明（codegen 用默认 64×1×1）。
    int local[3] = {0, 0, 0};
};

