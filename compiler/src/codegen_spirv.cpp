// ============================================================
// codegen_spirv 实现（syntax-s 三期）：AST → SPIR-V 1.0 直发
// ============================================================
// 见 codegen_spirv.h。手写指令编码，零外部依赖（不含任何 SPIR-V 头）。
//
// 结构：
//   · 指令编码原语（word 流 + 字符串打包）
//   · 类型/常量池（结构去重，key = 编码串）
//   · Module builder（规范逻辑布局的各 section）
//   · Stage 装配（I/O 变量 + 资源变量 + memberMap 成员改写，
//     与 codegen_glsl::emitStage 同源的模型）
//   · 表达式/语句发射（Load/Store 形式，结构化控制流）
//
// 覆盖面（M1+M2 首批）：标量/向量/矩阵、swizzle 读、数组（含常量初始化）、
// uniform/storage(BufferBlock)/push/sampler2D、GLSL.std.450 数学库、
// texture()、if/while、vert/frag/comp 三阶段与内建变量。
// 超出子集 → CompileError（带行号），与 shader_sema 文案风格一致。
// ============================================================
#include "codegen_spirv.h"
#include "shader_caps.h"
#include "error.h"

#include <cstring>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

// ---- SPIR-V 常数（手抄自规范，仅用到的子集）----------------------------
constexpr uint32_t kMagic   = 0x07230203;
constexpr uint32_t kVersion = 0x00010000;   // SPIR-V 1.0（Vulkan 1.0 兼容面最大）

enum Op : uint16_t {
    OpExtInstImport = 11, OpExtInst = 12,
    OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16,
    OpCapability = 17,
    OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
    OpTypeVector = 23, OpTypeMatrix = 24, OpTypeImage = 25, OpTypeSampledImage = 27,
    OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30,
    OpTypePointer = 32, OpTypeFunction = 33,
    OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43,
    OpConstantComposite = 44,
    OpFunction = 54, OpFunctionEnd = 56,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65,
    OpDecorate = 71, OpMemberDecorate = 72,
    OpVectorShuffle = 79, OpCompositeConstruct = 80, OpCompositeExtract = 81,
    OpImageSampleImplicitLod = 87, OpImageSampleExplicitLod = 88,
    OpConvertFToU = 109, OpConvertFToS = 110, OpConvertSToF = 111, OpConvertUToF = 112,
    OpBitcast = 124,
    OpSNegate = 126, OpFNegate = 127,
    OpIAdd = 128, OpFAdd = 129, OpISub = 130, OpFSub = 131,
    OpIMul = 132, OpFMul = 133, OpUDiv = 134, OpSDiv = 135, OpFDiv = 136,
    OpUMod = 137, OpSRem = 138, OpFRem = 140, OpFMod = 141,
    OpVectorTimesScalar = 142, OpMatrixTimesScalar = 143,
    OpVectorTimesMatrix = 144, OpMatrixTimesVector = 145, OpMatrixTimesMatrix = 146,
    OpDot = 148,
    OpLogicalOr = 166, OpLogicalAnd = 167, OpLogicalNot = 168,
    OpSelect = 169,
    OpIEqual = 170, OpINotEqual = 171,
    OpUGreaterThan = 172, OpSGreaterThan = 173,
    OpUGreaterThanEqual = 174, OpSGreaterThanEqual = 175,
    OpULessThan = 176, OpSLessThan = 177,
    OpULessThanEqual = 178, OpSLessThanEqual = 179,
    OpFOrdEqual = 180, OpFOrdNotEqual = 182,
    OpFOrdLessThan = 184, OpFOrdGreaterThan = 186,
    OpFOrdLessThanEqual = 188, OpFOrdGreaterThanEqual = 190,
    OpShiftRightLogical = 194, OpShiftRightArithmetic = 195, OpShiftLeftLogical = 196,
    OpBitwiseOr = 197, OpBitwiseXor = 198, OpBitwiseAnd = 199, OpNot = 200,
    OpLoopMerge = 246, OpSelectionMerge = 247,
    OpLabel = 248, OpBranch = 249, OpBranchConditional = 250,
    OpReturn = 253, OpReturnValue = 254,
    OpName = 5, OpMemberName = 6,
};

// GLSL.std.450 扩展指令号（用到的子集）
enum Gl450 : uint32_t {
    GRound = 1, GTrunc = 3, GFAbs = 4, GSAbs = 5, GFSign = 6,
    GFloor = 8, GCeil = 9, GFract = 10,
    GSin = 13, GCos = 14, GTan = 15, GAsin = 16, GAcos = 17, GAtan = 18,
    GAtan2 = 25, GPow = 26, GExp = 27, GLog = 28, GExp2 = 29, GLog2 = 30,
    GSqrt = 31, GInverseSqrt = 32,
    GDeterminant = 33, GMatrixInverse = 34,
    GFMin = 37, GUMin = 38, GSMin = 39, GFMax = 40, GUMax = 41, GSMax = 42,
    GFClamp = 43, GUClamp = 44, GSClamp = 45,
    GFMix = 46, GStep = 48, GSmoothStep = 49, GFma = 50,
    GLength = 66, GDistance = 67, GCross = 68, GNormalize = 69,
    GReflect = 71, GRefract = 72,
};

enum StorageClass : uint32_t {
    ScUniformConstant = 0, ScInput = 1, ScUniform = 2, ScOutput = 3,
    ScWorkgroup = 4, ScPrivate = 6, ScFunction = 7, ScPushConstant = 9,
};
enum Decoration : uint32_t {
    DecBlock = 2, DecBufferBlock = 3, DecColMajor = 5, DecArrayStride = 6,
    DecMatrixStride = 7, DecBuiltIn = 11, DecNonWritable = 24,
    DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35,
};
enum BuiltInId : uint32_t {
    BiPosition = 0, BiPointSize = 1, BiFragCoord = 15, BiFragDepth = 22,
    BiVertexIndex = 42, BiInstanceIndex = 43,
    BiNumWorkgroups = 24, BiWorkgroupId = 26, BiLocalInvocationId = 27,
    BiGlobalInvocationId = 28, BiLocalInvocationIndex = 29,
};

// ---- 类型描述（发射期的语义视图，typeId 之外携带足够的选指令信息）--------
struct TI {                       // TypeInfo
    enum K { Void, Bool, F32, I32, U32, Vec, Mat, Array, RArray, Struct,
             Image, SampledImage } k = Void;
    uint32_t id = 0;              // SPIR-V 类型 id
    K   comp = F32;               // Vec/Mat/Array 的组件标量类别
    int n = 0;                    // Vec: 分量数；Mat: 列数（方阵）；Array: 元素数
    uint32_t elem = 0;            // Array/RArray/Vec/Mat 的元素类型 id
    std::string structName;       // Struct: 源类型名
    bool isFloat() const { return k == F32 || ((k == Vec || k == Mat) && comp == F32); }
    bool isSigned() const { return k == I32 || (k == Vec && comp == I32); }
    bool isUnsigned() const { return k == U32 || (k == Vec && comp == U32); }
    bool isBoolish() const { return k == Bool || (k == Vec && comp == Bool); }
    int width() const { return k == Vec ? n : 1; }   // 标量=1
};

// ---- 指令编码 ------------------------------------------------------------
using Words = std::vector<uint32_t>;

void put(Words& w, uint16_t op, std::initializer_list<uint32_t> ops) {
    w.push_back(((uint32_t)(ops.size() + 1) << 16) | op);
    for (uint32_t v : ops) w.push_back(v);
}
void putV(Words& w, uint16_t op, const std::vector<uint32_t>& ops) {
    w.push_back(((uint32_t)(ops.size() + 1) << 16) | op);
    for (uint32_t v : ops) w.push_back(v);
}
// 字符串操作数：UTF-8 按 4 字节打包，NUL 终止（含边界恰满时的额外 0 字）
void packStr(std::vector<uint32_t>& out, const std::string& s) {
    uint32_t cur = 0; int sh = 0;
    for (char c : s) {
        cur |= ((uint32_t)(uint8_t)c) << sh;
        sh += 8;
        if (sh == 32) { out.push_back(cur); cur = 0; sh = 0; }
    }
    out.push_back(cur);   // 含 NUL；恰好满 word 时追加全 0 word
}

// ---- 模块 builder --------------------------------------------------------
struct Builder {
    uint32_t next = 1;
    uint32_t glsl450 = 0;                       // GLSL.std.450 import id
    Words secEntry, secExec, secDebug, secDeco, secTypes, secFunc;
    std::unordered_map<std::string, uint32_t> typeCache;   // 编码串 → id
    std::unordered_map<std::string, TI>       tiCache;     // 编码串 → TI
    std::unordered_map<uint32_t, TI>          tiById;      // typeId → TI

    uint32_t id() { return next++; }

    // 类型去重发射：key 唯一标识构造，miss 时发射并登记
    uint32_t type(const std::string& key, std::function<uint32_t()> emit, TI ti = {}) {
        auto it = typeCache.find(key);
        if (it != typeCache.end()) return it->second;
        uint32_t v = emit();
        typeCache[key] = v;
        ti.id = v;
        tiCache[key] = ti;
        tiById[v] = ti;
        return v;
    }
    const TI& info(uint32_t typeId) { return tiById[typeId]; }

    uint32_t tVoid() { return type("void", [&]{ uint32_t r = id(); put(secTypes, OpTypeVoid, {r}); return r; }, {TI::Void}); }
    uint32_t tBool() { return type("bool", [&]{ uint32_t r = id(); put(secTypes, OpTypeBool, {r}); return r; }, {TI::Bool}); }
    uint32_t tF32()  { return type("f32", [&]{ uint32_t r = id(); put(secTypes, OpTypeFloat, {r, 32}); return r; }, {TI::F32}); }
    uint32_t tI32()  { return type("i32", [&]{ uint32_t r = id(); put(secTypes, OpTypeInt, {r, 32, 1}); return r; }, {TI::I32}); }
    uint32_t tU32()  { return type("u32", [&]{ uint32_t r = id(); put(secTypes, OpTypeInt, {r, 32, 0}); return r; }, {TI::U32}); }
    uint32_t tVec(uint32_t comp, int n) {
        std::string key = "v" + std::to_string(n) + "_" + std::to_string(comp);
        TI ci = info(comp);
        TI ti; ti.k = TI::Vec; ti.comp = ci.k; ti.n = n; ti.elem = comp;
        return type(key, [&]{ uint32_t r = id(); put(secTypes, OpTypeVector, {r, comp, (uint32_t)n}); return r; }, ti);
    }
    uint32_t tMat(int n) {         // 方阵 matN（列 = vecN<f32>）
        uint32_t col = tVec(tF32(), n);
        std::string key = "m" + std::to_string(n);
        TI ti; ti.k = TI::Mat; ti.comp = TI::F32; ti.n = n; ti.elem = col;
        return type(key, [&]{ uint32_t r = id(); put(secTypes, OpTypeMatrix, {r, col, (uint32_t)n}); return r; }, ti);
    }
    uint32_t tArray(uint32_t elem, uint32_t lenConstId, int n) {
        std::string key = "a" + std::to_string(elem) + "_" + std::to_string(lenConstId);
        TI ei = info(elem);
        TI ti; ti.k = TI::Array; ti.comp = ei.k; ti.n = n; ti.elem = elem;
        return type(key, [&]{ uint32_t r = id(); put(secTypes, OpTypeArray, {r, elem, lenConstId}); return r; }, ti);
    }
    uint32_t tRArray(uint32_t elem) {
        std::string key = "ra" + std::to_string(elem);
        TI ei = info(elem);
        TI ti; ti.k = TI::RArray; ti.comp = ei.k; ti.elem = elem;
        return type(key, [&]{ uint32_t r = id(); put(secTypes, OpTypeRuntimeArray, {r, elem}); return r; }, ti);
    }
    // 带 ArrayStride 装饰的数组类型（资源块成员用）：stride 编入去重 key，
    // 装饰随类型首次发射一次性落盘（同 id 重复 decorate 违反规范）。
    uint32_t tRArrayStrided(uint32_t elem, uint32_t stride) {
        std::string key = "ras" + std::to_string(elem) + "_" + std::to_string(stride);
        TI ei = info(elem);
        TI ti; ti.k = TI::RArray; ti.comp = ei.k; ti.elem = elem;
        return type(key, [&]{
            uint32_t r = id();
            put(secTypes, OpTypeRuntimeArray, {r, elem});
            put(secDeco, OpDecorate, {r, DecArrayStride, stride});
            return r;
        }, ti);
    }
    uint32_t tArrayStrided(uint32_t elem, uint32_t lenConstId, int n, uint32_t stride) {
        std::string key = "as" + std::to_string(elem) + "_" + std::to_string(lenConstId)
                        + "_" + std::to_string(stride);
        TI ei = info(elem);
        TI ti; ti.k = TI::Array; ti.comp = ei.k; ti.n = n; ti.elem = elem;
        return type(key, [&]{
            uint32_t r = id();
            put(secTypes, OpTypeArray, {r, elem, lenConstId});
            put(secDeco, OpDecorate, {r, DecArrayStride, stride});
            return r;
        }, ti);
    }
    uint32_t tPtr(uint32_t sc, uint32_t pointee) {
        std::string key = "p" + std::to_string(sc) + "_" + std::to_string(pointee);
        return type(key, [&]{ uint32_t r = id(); put(secTypes, OpTypePointer, {r, sc, pointee}); return r; });
    }

    // 常量池
    std::unordered_map<std::string, uint32_t> constCache;
    uint32_t constScalar(uint32_t typeId, uint32_t bits) {
        std::string key = "c" + std::to_string(typeId) + "_" + std::to_string(bits);
        auto it = constCache.find(key);
        if (it != constCache.end()) return it->second;
        uint32_t r = id();
        put(secTypes, OpConstant, {typeId, r, bits});
        return constCache[key] = r;
    }
    uint32_t constF(float v) { uint32_t b; std::memcpy(&b, &v, 4); return constScalar(tF32(), b); }
    uint32_t constI(int32_t v) { return constScalar(tI32(), (uint32_t)v); }
    uint32_t constU(uint32_t v) { return constScalar(tU32(), v); }
    uint32_t constBool(bool v) {
        std::string key = v ? "ctrue" : "cfalse";
        auto it = constCache.find(key);
        if (it != constCache.end()) return it->second;
        uint32_t r = id();
        put(secTypes, v ? OpConstantTrue : OpConstantFalse, {tBool(), r});
        return constCache[key] = r;
    }
    uint32_t constComposite(uint32_t typeId, const std::vector<uint32_t>& parts) {
        std::string key = "cc" + std::to_string(typeId);
        for (uint32_t p : parts) key += "_" + std::to_string(p);
        auto it = constCache.find(key);
        if (it != constCache.end()) return it->second;
        uint32_t r = id();
        std::vector<uint32_t> ops = {typeId, r};
        ops.insert(ops.end(), parts.begin(), parts.end());
        putV(secTypes, OpConstantComposite, ops);
        return constCache[key] = r;
    }

    void name(uint32_t target, const std::string& n) {
        std::vector<uint32_t> ops = {target};
        packStr(ops, n);
        putV(secDebug, OpName, ops);
    }
    void memberName(uint32_t target, uint32_t idx, const std::string& n) {
        std::vector<uint32_t> ops = {target, idx};
        packStr(ops, n);
        putV(secDebug, OpMemberName, ops);
    }

    // 汇总为最终字流
    Words finish(uint32_t entryPointId) {
        Words out;
        out.push_back(kMagic); out.push_back(kVersion);
        out.push_back(0);                    // generator：未注册
        out.push_back(next);                 // id bound
        out.push_back(0);                    // schema
        put(out, OpCapability, {1});         // Capability Shader
        {   std::vector<uint32_t> ops = {glsl450};
            packStr(ops, "GLSL.std.450");
            putV(out, OpExtInstImport, ops); }
        put(out, OpMemoryModel, {0, 1});     // Logical GLSL450
        for (Words* s : {&secEntry, &secExec, &secDebug, &secDeco, &secTypes, &secFunc})
            out.insert(out.end(), s->begin(), s->end());
        (void)entryPointId;
        return out;
    }
};

// ---- sc 类型名 → 语义类别 -------------------------------------------------
struct ScType { TI::K k; int n; };   // n: vec 宽 / mat 阶
bool scTypeOf(const std::string& name, ScType& out) {
    if (name == "f4" || name == "float")  { out = {TI::F32, 0}; return true; }
    if (name == "i1" || name == "i2" || name == "i4" || name == "int")  { out = {TI::I32, 0}; return true; }
    if (name == "u1" || name == "u2" || name == "u4" || name == "uint") { out = {TI::U32, 0}; return true; }
    if (name == "bool") { out = {TI::Bool, 0}; return true; }
    if (name == "vec2") { out = {TI::Vec, 2}; return true; }
    if (name == "vec3") { out = {TI::Vec, 3}; return true; }
    if (name == "vec4") { out = {TI::Vec, 4}; return true; }
    if (name == "ivec2" || name == "uvec2" || name == "bvec2") { out = {TI::Vec, 2}; return true; }
    if (name == "ivec3" || name == "uvec3" || name == "bvec3") { out = {TI::Vec, 3}; return true; }
    if (name == "ivec4" || name == "uvec4" || name == "bvec4") { out = {TI::Vec, 4}; return true; }
    if (name == "mat2") { out = {TI::Mat, 2}; return true; }
    if (name == "mat3") { out = {TI::Mat, 3}; return true; }
    if (name == "mat4") { out = {TI::Mat, 4}; return true; }
    return false;
}
TI::K vecCompKind(const std::string& name) {
    if (name[0] == 'i') return TI::I32;
    if (name[0] == 'u') return TI::U32;
    if (name[0] == 'b') return TI::Bool;
    return TI::F32;
}

// std140/std430 布局（与 codegen_glsl::layoutOf 同一事实源的复制——
// 反射清单与 SPIR-V Offset 装饰必须逐字节一致）
struct Lay { int align, size; };
int rup(int v, int a) { return a ? ((v + a - 1) / a) * a : v; }
Lay layOf(const std::string& t, const std::vector<std::string>& dims, bool std430) {
    int a = 4, s = 4;
    if (t == "vec2" || t == "ivec2" || t == "uvec2" || t == "bvec2") { a = 8;  s = 8;  }
    else if (t == "vec3" || t == "ivec3" || t == "uvec3" || t == "bvec3") { a = 16; s = 12; }
    else if (t == "vec4" || t == "ivec4" || t == "uvec4" || t == "bvec4") { a = 16; s = 16; }
    else if (t == "mat2") { a = 16; s = 32; }
    else if (t == "mat3") { a = 16; s = 48; }
    else if (t == "mat4") { a = 16; s = 64; }
    if (!dims.empty()) {
        int count = 1;
        for (const auto& d : dims) count *= (d.empty() ? 1 : std::atoi(d.c_str()));
        int stride = std430 ? s : rup(s, 16);
        a = std430 ? a : std::max(a, 16);
        s = stride * count;
    }
    return {a, s};
}

// ---- 值 / 左值 ------------------------------------------------------------
struct Val { uint32_t id = 0; uint32_t type = 0; };
struct Ptr { uint32_t id = 0; uint32_t type = 0; uint32_t sc = ScFunction; };  // type = 指向的值类型

// I/O 改写条目（memberMap["base.field"]）
struct IoEntry {
    uint32_t var = 0;        // Input/Output 变量 id
    uint32_t type = 0;       // 变量值类型 id
    uint32_t sc = ScInput;
    bool extractX = false;   // comp 标量声明的 uvec3 内建：load 后取 .x
};

// 资源块视图
struct ResBlock {
    uint32_t var = 0;                    // Uniform/PushConstant 存储的块变量
    uint32_t structType = 0;
    std::unordered_map<std::string, int>      memberIdx;
    std::unordered_map<std::string, uint32_t> memberType;  // 成员值类型 id
};

// ---- 阶段发射器 ------------------------------------------------------------
struct StageEmitter {
    Builder& b;
    const Program& prog;
    const Decl& stage;
    const std::unordered_map<std::string, const Decl*>& structs;

    std::unordered_map<std::string, IoEntry>  ioMap;      // "base.field" → I/O 变量
    std::unordered_map<std::string, ResBlock> resBlocks;  // 资源块名 → 视图
    std::unordered_map<std::string, Ptr>      samplers;   // sampler 全局名 → UniformConstant 变量
    std::unordered_map<std::string, Ptr>      vars;       // 函数局部变量
    std::unordered_set<std::string>           outAggVars; // 输出聚合局部名
    std::vector<uint32_t> interfaceIds;                   // OpEntryPoint 接口（Input/Output 变量）
    uint32_t scalarOutVar = 0;                            // 片元标量返回目标（0 = 无）

    Words funcVars;      // 入口块首的 OpVariable 集（SPIR-V 要求）
    Words body;          // 当前函数体指令流
    bool terminated = false;   // 当前基本块是否已终结
    struct LoopCtx { uint32_t mergeL, contL; };
    std::vector<LoopCtx> loops;    // break/continue 目标栈

    StageEmitter(Builder& bb, const Program& p, const Decl& st,
                 const std::unordered_map<std::string, const Decl*>& ss)
        : b(bb), prog(p), stage(st), structs(ss) {}

    [[noreturn]] void err(const std::string& m, int line) const { throw CompileError(m, line); }

    // sc 类型引用 → SPIR-V 值类型 id（标量/向量/矩阵/一维数组）
    uint32_t typeIdOf(const TypeRef& t, int line) {
        ScType st;
        if (!scTypeOf(t.name, st))
            err("shader 后端暂不支持类型 `" + t.name + "`（SPIR-V 直发子集）", line);
        uint32_t base;
        switch (st.k) {
            case TI::F32:  base = b.tF32(); break;
            case TI::I32:  base = b.tI32(); break;
            case TI::U32:  base = b.tU32(); break;
            case TI::Bool: base = b.tBool(); break;
            case TI::Vec: {
                TI::K ck = vecCompKind(t.name);
                uint32_t comp = ck == TI::I32 ? b.tI32() : ck == TI::U32 ? b.tU32()
                              : ck == TI::Bool ? b.tBool() : b.tF32();
                base = b.tVec(comp, st.n);
                break;
            }
            case TI::Mat:  base = b.tMat(st.n); break;
            default: err("内部错误：未知标量类别", line);
        }
        if (!t.arrayDims.empty()) {
            if (t.arrayDims.size() > 1)
                err("shader 暂不支持多维数组（见 syntax-s §16 P0）", line);
            int n = std::atoi(t.arrayDims[0].c_str());
            if (n <= 0) err("shader 数组维度须为正整数常量", line);
            base = b.tArray(base, b.constI(n), n);
        }
        return base;
    }

    // ---- 资源装配（uniform / storage / push / sampler）----
    void setupResources() {
        for (const auto& d : prog.decls) {
            if (!d) continue;
            if (d->kind == Decl::VarD && d->shaderAttr) {         // sampler2D 全局
                const Field& f = d->structCommon.fields.front();
                if (f.type.name != "sampler2D")
                    err("shader 暂不支持资源类型 `" + f.type.name + "`（见 syntax-s §16 P1）", d->line);
                uint32_t tImg = b.type("img2d", [&]{
                    uint32_t r = b.id();
                    // OpTypeImage %f32 2D depth=0 arrayed=0 ms=0 sampled=1 format=Unknown
                    put(b.secTypes, OpTypeImage, {r, b.tF32(), 1, 0, 0, 0, 1, 0});
                    return r;
                }, {TI::Image});
                uint32_t tSi = b.type("simg2d", [&]{
                    uint32_t r = b.id();
                    put(b.secTypes, OpTypeSampledImage, {r, tImg});
                    return r;
                }, {TI::SampledImage});
                uint32_t ptrT = b.tPtr(ScUniformConstant, tSi);
                uint32_t v = b.id();
                put(b.secTypes, OpVariable, {ptrT, v, ScUniformConstant});
                b.name(v, f.name);
                auto* a = d->shaderAttr.get();
                if (a->set >= 0)     put(b.secDeco, OpDecorate, {v, DecDescriptorSet, (uint32_t)a->set});
                if (a->binding >= 0) put(b.secDeco, OpDecorate, {v, DecBinding, (uint32_t)a->binding});
                samplers[f.name] = {v, tSi, ScUniformConstant};
                continue;
            }
            if (d->kind != Decl::StructD || !d->shaderAttr ||
                d->shaderAttr->res == ShaderDeclAttr::None) continue;

            auto* a = d->shaderAttr.get();
            const bool std430 = a->res == ShaderDeclAttr::Storage;
            const bool push   = a->res == ShaderDeclAttr::Push;

            // 成员类型 + Offset 装饰（布局与反射清单同一算法）
            std::vector<uint32_t> memberTypes;
            uint32_t structT = b.id();
            ResBlock rb;
            int off = 0;
            for (size_t i = 0; i < d->structCommon.fields.size(); i++) {
                const auto& f = d->structCommon.fields[i];
                uint32_t mt;
                if (!f.type.arrayDims.empty() && f.type.arrayDims[0].empty()) {
                    // 运行时数组 x[]: T（仅 storage 块末成员）
                    uint32_t elem = typeIdOf(TypeRef{f.type.name}, f.line);
                    Lay el = layOf(f.type.name, {}, std430);
                    mt = b.tRArrayStrided(elem, (uint32_t)(std430 ? el.size : rup(el.size, 16)));
                } else if (!f.type.arrayDims.empty()) {
                    // 定长数组成员：带 stride 的独立类型（与 Function 数组不同源）
                    uint32_t elem = typeIdOf(TypeRef{f.type.name}, f.line);
                    int n = std::atoi(f.type.arrayDims[0].c_str());
                    Lay el = layOf(f.type.name, {}, std430);
                    mt = b.tArrayStrided(elem, b.constI(n), n,
                                         (uint32_t)(std430 ? el.size : rup(el.size, 16)));
                } else {
                    mt = typeIdOf(f.type, f.line);
                }
                Lay lay = layOf(f.type.name, f.type.arrayDims, std430);
                off = rup(off, lay.align);
                put(b.secDeco, OpMemberDecorate, {structT, (uint32_t)i, DecOffset, (uint32_t)off});
                ScType sct;
                if (scTypeOf(f.type.name, sct) && sct.k == TI::Mat) {
                    put(b.secDeco, OpMemberDecorate, {structT, (uint32_t)i, DecColMajor});
                    put(b.secDeco, OpMemberDecorate, {structT, (uint32_t)i, DecMatrixStride, 16});
                }
                off += lay.size;
                b.memberName(structT, (uint32_t)i, f.name);
                rb.memberIdx[f.name] = (int)i;
                rb.memberType[f.name] = mt;
                memberTypes.push_back(mt);
            }
            {   std::vector<uint32_t> ops = {structT};
                ops.insert(ops.end(), memberTypes.begin(), memberTypes.end());
                putV(b.secTypes, OpTypeStruct, ops); }
            // 类型名带 _blk 后缀、实例名用块名——与旧链 GLSL 的
            // `uniform Params_blk {...} Params;` 对齐，令 SPIRV-Cross 产出的
            // MSL 参数名 = 块名（spc/gfx 运行时按名对位缓冲的契约）。
            b.name(structT, d->name + "_blk");
            // SPIR-V 1.0/Vulkan1.0：UBO=Block+Uniform；SSBO=BufferBlock+Uniform；
            // push=Block+PushConstant（与 glslang 同形）
            put(b.secDeco, OpDecorate, {structT, std430 ? (uint32_t)DecBufferBlock : (uint32_t)DecBlock});
            uint32_t sc = push ? ScPushConstant : ScUniform;
            uint32_t ptrT = b.tPtr(sc, structT);
            uint32_t v = b.id();
            put(b.secTypes, OpVariable, {ptrT, v, sc});
            b.name(v, d->name);
            if (!push) {
                if (a->set >= 0)     put(b.secDeco, OpDecorate, {v, DecDescriptorSet, (uint32_t)a->set});
                if (a->binding >= 0) put(b.secDeco, OpDecorate, {v, DecBinding, (uint32_t)a->binding});
            }
            rb.var = v;
            rb.structType = structT;
            resBlocks[d->name] = rb;
        }
    }

    // ---- 阶段 I/O 装配（与 codegen_glsl::emitStage 同一规则）----
    uint32_t mkIoVar(uint32_t valueType, uint32_t sc, const std::string& dbgName) {
        uint32_t ptrT = b.tPtr(sc, valueType);
        uint32_t v = b.id();
        put(b.secTypes, OpVariable, {ptrT, v, sc});
        b.name(v, dbgName);
        interfaceIds.push_back(v);
        return v;
    }
    void decoLoc(uint32_t v, int loc)    { put(b.secDeco, OpDecorate, {v, DecLocation, (uint32_t)loc}); }
    void decoBuiltin(uint32_t v, uint32_t bi) { put(b.secDeco, OpDecorate, {v, DecBuiltIn, bi}); }

    // 内建语义名 → (BuiltIn 枚举, 变量值类型, comp 标量需 .x)
    bool builtinOf(const std::string& sem, bool asOutput, const TypeRef& declType,
                   uint32_t& bi, uint32_t& type, bool& extractX, int line) {
        extractX = false;
        const bool isComp = stage.shaderStage == ShaderStage::Comp;
        auto uvec3 = [&]{ return b.tVec(b.tU32(), 3); };
        if (sem == "position")    { bi = asOutput ? BiPosition : BiFragCoord;
                                    type = b.tVec(b.tF32(), 4); return true; }
        if (sem == "frag_coord")  { bi = BiFragCoord; type = b.tVec(b.tF32(), 4); return true; }
        if (sem == "frag_depth")  { bi = BiFragDepth; type = b.tF32(); return true; }
        if (sem == "vertex_id")   { bi = BiVertexIndex;   type = b.tI32(); return true; }
        if (sem == "instance_id") { bi = BiInstanceIndex; type = b.tI32(); return true; }
        if (sem == "global_invocation_id" || sem == "local_invocation_id" ||
            sem == "workgroup_id" || sem == "num_workgroups") {
            bi = sem == "global_invocation_id" ? BiGlobalInvocationId
               : sem == "local_invocation_id"  ? BiLocalInvocationId
               : sem == "workgroup_id"         ? BiWorkgroupId : BiNumWorkgroups;
            type = uvec3();
            ScType st;
            if (isComp && scTypeOf(declType.name, st) && st.k != TI::Vec) extractX = true;
            return true;
        }
        if (sem == "local_invocation_index") { bi = BiLocalInvocationIndex; type = b.tU32(); return true; }
        err("shader 暂不支持内建语义 `" + sem + "`（见 syntax-s §16）", line);
    }

    void setupIO() {
        const bool isVert = stage.shaderStage == ShaderStage::Vert;
        int autoInLoc = 0;

        // 输入（入参结构体字段 → Input 变量）
        for (const auto& p : stage.structCommon.fields) {
            auto it = structs.find(p.type.name);
            if (it == structs.end()) {   // 标量/向量入参：单顶点属性
                uint32_t t = typeIdOf(p.type, stage.line);
                uint32_t v = mkIoVar(t, ScInput, p.name);
                decoLoc(v, autoInLoc++);
                ioMap[p.name] = {v, t, ScInput, false};   // 裸名直用
                continue;
            }
            for (const auto& f : it->second->structCommon.fields) {
                std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
                if (!sem.empty()) {
                    uint32_t bi, t; bool ex;
                    builtinOf(sem, /*out*/false, f.type, bi, t, ex, f.line);
                    uint32_t v = mkIoVar(t, ScInput, "gl_" + sem);
                    decoBuiltin(v, bi);
                    ioMap[p.name + "." + f.name] = {v, t, ScInput, ex};
                    continue;
                }
                int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoInLoc++;
                uint32_t t = typeIdOf(f.type, f.line);
                uint32_t v = mkIoVar(t, ScInput, isVert ? f.name : ("v_" + f.name));
                decoLoc(v, loc);
                ioMap[p.name + "." + f.name] = {v, t, ScInput, false};
            }
        }

        // 输出（返回结构体字段 → Output 变量；标量返回 → loc0 Output）
        std::string retType = stage.structCommon.type ? stage.structCommon.type->name : "void";
        auto rit = structs.find(retType);
        if (retType != "void" && !retType.empty() && rit != structs.end()) {
            for (const auto& s : stage.body)
                if (s->kind == Stmt::VarS)
                    for (const auto& f : s->decls)
                        if (f.type.name == retType) outAggVars.insert(f.name);
            int autoOutLoc = 0;
            for (const auto& f : rit->second->structCommon.fields) {
                std::string sem = f.shaderAttr ? f.shaderAttr->builtin : "";
                uint32_t v, t;
                if (!sem.empty()) {
                    uint32_t bi; bool ex;
                    builtinOf(sem, /*out*/true, f.type, bi, t, ex, f.line);
                    v = mkIoVar(t, ScOutput, "gl_" + sem);
                    decoBuiltin(v, bi);
                } else {
                    int loc = (f.shaderAttr && f.shaderAttr->loc >= 0) ? f.shaderAttr->loc : autoOutLoc++;
                    t = typeIdOf(f.type, f.line);
                    v = mkIoVar(t, ScOutput, isVert ? ("v_" + f.name) : ("f_" + f.name));
                    decoLoc(v, loc);
                }
                for (const auto& av : outAggVars)
                    ioMap[av + "." + f.name] = {v, t, ScOutput, false};
            }
        } else if (retType != "void" && !retType.empty()) {
            uint32_t t = typeIdOf(*stage.structCommon.type, stage.line);
            scalarOutVar = mkIoVar(t, ScOutput, "f_color");
            decoLoc(scalarOutVar, 0);
        }
    }

    // ---- 指令发射便捷 ----
    uint32_t ins(uint16_t op, uint32_t type, std::vector<uint32_t> ops) {
        uint32_t r = b.id();
        std::vector<uint32_t> all = {type, r};
        all.insert(all.end(), ops.begin(), ops.end());
        putV(body, op, all);
        return r;
    }
    Val load(const Ptr& p) { return {ins(OpLoad, p.type, {p.id}), p.type}; }
    void store(const Ptr& p, Val v) { put(body, OpStore, {p.id, v.id}); }

    // swizzle 判定：全部字符 ∈ xyzw/rgba
    static bool isSwz(const std::string& s) {
        if (s.empty() || s.size() > 4) return false;
        for (char c : s)
            if (!strchr("xyzw", c) && !strchr("rgba", c)) return false;
        return true;
    }
    static int swzIdx(char c) {
        switch (c) { case 'x': case 'r': return 0; case 'y': case 'g': return 1;
                     case 'z': case 'b': return 2; case 'w': case 'a': return 3; }
        return 0;
    }

    // 类型适配：把 v 转到目标标量类别（字面量宽容 + 显式转换指令）
    Val coerce(Val v, uint32_t wantType, int line) {
        if (v.type == wantType) return v;
        const TI& from = b.info(v.type);
        const TI& to = b.info(wantType);
        if (from.k == TI::I32 && to.k == TI::U32) return {ins(OpBitcast, wantType, {v.id}), wantType};
        if (from.k == TI::U32 && to.k == TI::I32) return {ins(OpBitcast, wantType, {v.id}), wantType};
        if (from.k == TI::I32 && to.k == TI::F32) return {ins(OpConvertSToF, wantType, {v.id}), wantType};
        if (from.k == TI::U32 && to.k == TI::F32) return {ins(OpConvertUToF, wantType, {v.id}), wantType};
        if (from.k == TI::F32 && to.k == TI::I32) return {ins(OpConvertFToS, wantType, {v.id}), wantType};
        if (from.k == TI::F32 && to.k == TI::U32) return {ins(OpConvertFToU, wantType, {v.id}), wantType};
        err("shader 类型不匹配（无隐式转换路径）", line);
    }

    // ---- 左值 ----
    Ptr lvalue(const Expr* e) {
        switch (e->kind) {
            case Expr::Ident: {
                auto it = vars.find(e->text);
                if (it != vars.end()) return it->second;
                auto io = ioMap.find(e->text);          // 裸名输入（标量入参）
                if (io != ioMap.end()) return {io->second.var, io->second.type, io->second.sc};
                err("shader 未定义变量 `" + e->text + "`", e->line);
            }
            case Expr::Member: {
                if (e->a && e->a->kind == Expr::Ident) {
                    const std::string key = e->a->text + "." + e->text;
                    auto io = ioMap.find(key);          // 阶段 I/O 改写
                    if (io != ioMap.end()) {
                        if (io->second.extractX)
                            err("comp 标量内建不可写", e->line);
                        return {io->second.var, io->second.type, io->second.sc};
                    }
                    auto rb = resBlocks.find(e->a->text);   // 资源块成员
                    if (rb != resBlocks.end()) {
                        auto mi = rb->second.memberIdx.find(e->text);
                        if (mi == rb->second.memberIdx.end())
                            err("资源块 `" + e->a->text + "` 无成员 `" + e->text + "`", e->line);
                        uint32_t mt = rb->second.memberType[e->text];
                        uint32_t sc = ScUniform;   // push 块同 AccessChain 形态（sc 记在指针类型里）
                        uint32_t ptrT = b.tPtr(sc, mt);
                        uint32_t r = ins(OpAccessChain, ptrT, {rb->second.var, b.constI(mi->second)});
                        return {r, mt, sc};
                    }
                }
                // 单分量 swizzle 左值：v.x = ...
                if (isSwz(e->text) && e->text.size() == 1) {
                    Ptr base = lvalue(e->a.get());
                    const TI& bi = b.info(base.type);
                    if (bi.k != TI::Vec) err("`." + e->text + "` 作用于非向量", e->line);
                    uint32_t ptrT = b.tPtr(base.sc, bi.elem);
                    uint32_t r = ins(OpAccessChain, ptrT, {base.id, b.constU(swzIdx(e->text[0]))});
                    return {r, bi.elem, base.sc};
                }
                err("shader 暂不支持该成员左值（多分量 swizzle 写/嵌套结构见 syntax-s §16 P0）", e->line);
            }
            case Expr::Index: {
                // 资源块运行时数组 YBuf.y[i] / 局部数组 pos[i] / 向量分量 v[i]
                Ptr base = lvalue(e->a.get());
                Val idx = rvalue(e->b.get());
                const TI& bi = b.info(base.type);
                uint32_t elemT;
                if (bi.k == TI::Array || bi.k == TI::RArray) elemT = bi.elem;
                else if (bi.k == TI::Vec) elemT = bi.elem;
                else if (bi.k == TI::Mat) elemT = bi.elem;
                else err("下标作用于非数组/向量/矩阵", e->line);
                uint32_t ptrT = b.tPtr(base.sc, elemT);
                uint32_t r = ins(OpAccessChain, ptrT, {base.id, idx.id});
                return {r, elemT, base.sc};
            }
            default:
                err("shader 该表达式不可作左值", e->line);
        }
    }

    // ---- 右值 ----
    Val rvalue(const Expr* e) {
        switch (e->kind) {
            case Expr::IntLit: {
                long v = std::strtol(e->text.c_str(), nullptr, 0);
                return {b.constI((int32_t)v), b.tI32()};
            }
            case Expr::FloatLit: {
                float v = std::strtof(e->text.c_str(), nullptr);
                return {b.constF(v), b.tF32()};
            }
            case Expr::Ident: {
                if (e->text == "true")  return {b.constBool(true), b.tBool()};
                if (e->text == "false") return {b.constBool(false), b.tBool()};
                return load(lvalue(e));
            }
            case Expr::Member: {
                // I/O 改写（含 comp 标量内建 .x 提取）
                if (e->a && e->a->kind == Expr::Ident) {
                    const std::string key = e->a->text + "." + e->text;
                    auto io = ioMap.find(key);
                    if (io != ioMap.end()) {
                        Val v = load({io->second.var, io->second.type, io->second.sc});
                        if (io->second.extractX) {
                            uint32_t u32 = b.tU32();
                            return {ins(OpCompositeExtract, u32, {v.id, 0}), u32};
                        }
                        return v;
                    }
                    auto rb = resBlocks.find(e->a->text);
                    if (rb != resBlocks.end()) return load(lvalue(e));
                }
                // swizzle 读
                if (isSwz(e->text)) {
                    Val base = rvalue(e->a.get());
                    const TI& bi = b.info(base.type);
                    if (bi.k != TI::Vec) err("`." + e->text + "` 作用于非向量", e->line);
                    if (e->text.size() == 1)
                        return {ins(OpCompositeExtract, bi.elem, {base.id, (uint32_t)swzIdx(e->text[0])}), bi.elem};
                    uint32_t vt = b.tVec(bi.elem, (int)e->text.size());
                    std::vector<uint32_t> ops = {base.id, base.id};
                    for (char c : e->text) ops.push_back((uint32_t)swzIdx(c));
                    return {ins(OpVectorShuffle, vt, ops), vt};
                }
                err("shader 暂不支持该成员访问（嵌套结构见 syntax-s §16 P0）", e->line);
            }
            case Expr::Index:
                return load(lvalue(e));
            case Expr::Unary: {
                if (e->op == "++" || e->op == "--") {   // 前缀：新值
                    Ptr p = lvalue(e->a.get());
                    Val cur = load(p);
                    const TI& ti = b.info(cur.type);
                    Val one = ti.isFloat() ? Val{b.constF(1.0f), b.tF32()}
                            : ti.isUnsigned() ? Val{b.constU(1), b.tU32()}
                            : Val{b.constI(1), b.tI32()};
                    Val nv = arith(e->op == "++" ? "+" : "-", cur, one, e->line);
                    store(p, nv);
                    return nv;
                }
                if (e->op == "-") {
                    Val v = rvalue(e->a.get());
                    const TI& ti = b.info(v.type);
                    return {ins(ti.isFloat() ? OpFNegate : OpSNegate, v.type, {v.id}), v.type};
                }
                if (e->op == "!") {
                    Val v = rvalue(e->a.get());
                    return {ins(OpLogicalNot, v.type, {v.id}), v.type};
                }
                if (e->op == "~") {
                    Val v = rvalue(e->a.get());
                    return {ins(OpNot, v.type, {v.id}), v.type};
                }
                err("shader 暂不支持一元 `" + e->op + "`", e->line);
            }
            case Expr::PostUnary: {                      // 后缀 i++/i--：旧值
                Ptr p = lvalue(e->a.get());
                Val cur = load(p);
                const TI& ti = b.info(cur.type);
                Val one = ti.isFloat() ? Val{b.constF(1.0f), b.tF32()}
                        : ti.isUnsigned() ? Val{b.constU(1), b.tU32()}
                        : Val{b.constI(1), b.tI32()};
                Val nv = arith(e->op == "++" ? "+" : "-", cur, one, e->line);
                store(p, nv);
                return cur;
            }
            case Expr::Binary:  return binary(e);
            case Expr::Ternary: {
                Val c = rvalue(e->a.get());
                Val t = rvalue(e->b.get());
                Val f = rvalue(e->c.get());
                f = coerce(f, t.type, e->line);
                return {ins(OpSelect, t.type, {c.id, t.id, f.id}), t.type};
            }
            case Expr::Call:    return call(e);
            case Expr::Cast: {
                Val v = rvalue(e->a.get());
                ScType st;
                if (!scTypeOf(e->op, st) || st.k == TI::Vec || st.k == TI::Mat)
                    err("shader 暂不支持强转目标 `" + e->op + "`", e->line);
                uint32_t want = st.k == TI::F32 ? b.tF32() : st.k == TI::U32 ? b.tU32()
                              : st.k == TI::Bool ? b.tBool() : b.tI32();
                return coerce(v, want, e->line);
            }
            default:
                err("shader 暂不支持该表达式（SPIR-V 直发子集）", e->line);
        }
    }

    // 二元运算（含赋值）
    Val binary(const Expr* e) {
        const std::string& op = e->op;
        // 赋值族
        if (op == "=" || op == "+=" || op == "-=" || op == "*=" || op == "/=") {
            // 多分量 swizzle 写入 v.xy = ...：load 整向量 → VectorShuffle 合成 → store
            if (op == "=" && e->a && e->a->kind == Expr::Member &&
                isSwz(e->a->text) && e->a->text.size() > 1) {
                Ptr basep = lvalue(e->a->a.get());
                const TI& bi = b.info(basep.type);
                if (bi.k != TI::Vec) err("swizzle 写入作用于非向量", e->line);
                Val rhs = rvalue(e->b.get());
                const TI& ri = b.info(rhs.type);
                if (ri.k != TI::Vec || ri.n != (int)e->a->text.size())
                    err("swizzle 写入右侧分量数不符", e->line);
                Val cur = load(basep);
                // shuffle 索引：未被写分量取原向量(0..n-1)，被写分量取 rhs(n+j)
                std::vector<uint32_t> idx((size_t)bi.n);
                for (int i = 0; i < bi.n; i++) idx[(size_t)i] = (uint32_t)i;
                for (size_t j = 0; j < e->a->text.size(); j++)
                    idx[(size_t)swzIdx(e->a->text[j])] = (uint32_t)(bi.n + (int)j);
                std::vector<uint32_t> ops = {cur.id, rhs.id};
                ops.insert(ops.end(), idx.begin(), idx.end());
                Val merged = {ins(OpVectorShuffle, basep.type, ops), basep.type};
                store(basep, merged);
                return merged;
            }
            Ptr dst;
            // `for i = 0; ...` 惯例：赋值目标未声明时自动声明（类型取右侧）
            if (op == "=" && e->a && e->a->kind == Expr::Ident &&
                !vars.count(e->a->text) && !ioMap.count(e->a->text) &&
                !resBlocks.count(e->a->text) && !samplers.count(e->a->text)) {
                Val rhs = rvalue(e->b.get());
                uint32_t ptrT = b.tPtr(ScFunction, rhs.type);
                uint32_t v = b.id();
                put(funcVars, OpVariable, {ptrT, v, ScFunction});
                b.name(v, e->a->text);
                vars[e->a->text] = {v, rhs.type, ScFunction};
                store(vars[e->a->text], rhs);
                return rhs;
            }
            dst = lvalue(e->a.get());
            Val rhs = rvalue(e->b.get());
            if (op != "=") {
                Val cur = load(dst);
                rhs = arith(op.substr(0, 1), cur, rhs, e->line);
            }
            rhs = coerce(rhs, dst.type, e->line);
            store(dst, rhs);
            return rhs;
        }
        Val a = rvalue(e->a.get());
        Val v = rvalue(e->b.get());
        if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%")
            return arith(op, a, v, e->line);
        // 比较
        if (op == "<" || op == ">" || op == "<=" || op == ">=" || op == "==" || op == "!=") {
            // 标量字面量向另一侧类别看齐
            const TI& ta = b.info(a.type);
            if (a.type != v.type) {
                if (ta.k == TI::F32 || b.info(v.type).k == TI::F32) {
                    a = coerce(a, b.tF32(), e->line); v = coerce(v, b.tF32(), e->line);
                } else if (ta.isUnsigned() || b.info(v.type).isUnsigned()) {
                    a = coerce(a, b.tU32(), e->line); v = coerce(v, b.tU32(), e->line);
                } else {
                    a = coerce(a, b.tI32(), e->line); v = coerce(v, b.tI32(), e->line);
                }
            }
            const TI& ti = b.info(a.type);
            uint16_t code;
            if (ti.isFloat())
                code = op == "<" ? OpFOrdLessThan : op == ">" ? OpFOrdGreaterThan
                     : op == "<=" ? OpFOrdLessThanEqual : op == ">=" ? OpFOrdGreaterThanEqual
                     : op == "==" ? OpFOrdEqual : OpFOrdNotEqual;
            else if (ti.isUnsigned())
                code = op == "<" ? OpULessThan : op == ">" ? OpUGreaterThan
                     : op == "<=" ? OpULessThanEqual : op == ">=" ? OpUGreaterThanEqual
                     : op == "==" ? OpIEqual : OpINotEqual;
            else
                code = op == "<" ? OpSLessThan : op == ">" ? OpSGreaterThan
                     : op == "<=" ? OpSLessThanEqual : op == ">=" ? OpSGreaterThanEqual
                     : op == "==" ? OpIEqual : OpINotEqual;
            return {ins(code, b.tBool(), {a.id, v.id}), b.tBool()};
        }
        if (op == "&&") return {ins(OpLogicalAnd, b.tBool(), {a.id, v.id}), b.tBool()};
        if (op == "||") return {ins(OpLogicalOr, b.tBool(), {a.id, v.id}), b.tBool()};
        if (op == "&") return {ins(OpBitwiseAnd, a.type, {a.id, v.id}), a.type};
        if (op == "|") return {ins(OpBitwiseOr, a.type, {a.id, v.id}), a.type};
        if (op == "^") return {ins(OpBitwiseXor, a.type, {a.id, v.id}), a.type};
        if (op == "<<") return {ins(OpShiftLeftLogical, a.type, {a.id, v.id}), a.type};
        if (op == ">>") {
            const TI& ti = b.info(a.type);
            return {ins(ti.isUnsigned() ? OpShiftRightLogical : OpShiftRightArithmetic,
                        a.type, {a.id, v.id}), a.type};
        }
        err("shader 暂不支持二元 `" + op + "`", e->line);
    }

    // 算术：类型统一 + 向量/矩阵混合形态选指令
    Val arith(const std::string& op, Val a, Val v, int line) {
        const TI* ta = &b.info(a.type);
        const TI* tv = &b.info(v.type);
        // 矩阵/向量混合乘法
        if (op == "*") {
            if (ta->k == TI::Mat && tv->k == TI::Vec)
                return {ins(OpMatrixTimesVector, v.type, {a.id, v.id}), v.type};
            if (ta->k == TI::Vec && tv->k == TI::Mat)
                return {ins(OpVectorTimesMatrix, a.type, {a.id, v.id}), a.type};
            if (ta->k == TI::Mat && tv->k == TI::Mat)
                return {ins(OpMatrixTimesMatrix, a.type, {a.id, v.id}), a.type};
            if (ta->k == TI::Vec && tv->k == TI::F32)
                return {ins(OpVectorTimesScalar, a.type, {a.id, v.id}), a.type};
            if (ta->k == TI::F32 && tv->k == TI::Vec)
                return {ins(OpVectorTimesScalar, v.type, {v.id, a.id}), v.type};
            if (ta->k == TI::Mat && tv->k == TI::F32)
                return {ins(OpMatrixTimesScalar, a.type, {a.id, v.id}), a.type};
        }
        // 标量↔向量拉伸（splat）
        auto splat = [&](Val s, uint32_t vecT) -> Val {
            const TI& vt = b.info(vecT);
            s = coerce(s, vt.elem, line);
            std::vector<uint32_t> ops;
            for (int i = 0; i < vt.n; i++) ops.push_back(s.id);
            return {ins(OpCompositeConstruct, vecT, ops), vecT};
        };
        if (ta->k == TI::Vec && tv->k != TI::Vec) { v = splat(v, a.type); tv = &b.info(v.type); }
        else if (tv->k == TI::Vec && ta->k != TI::Vec) { a = splat(a, v.type); ta = &b.info(a.type); }
        // 标量类别统一（字面量宽容：int 字面量随浮点/无符号侧）
        if (a.type != v.type) {
            if (ta->isFloat() || tv->isFloat()) {
                uint32_t want = ta->k == TI::Vec ? a.type : tv->k == TI::Vec ? v.type : b.tF32();
                a = coerce(a, want, line); v = coerce(v, want, line);
            } else if (ta->isUnsigned() || tv->isUnsigned()) {
                a = coerce(a, b.tU32(), line); v = coerce(v, b.tU32(), line);
            } else err("shader 算术两侧类型不一致", line);
            ta = &b.info(a.type);
        }
        const bool flt = ta->isFloat();
        uint16_t code;
        if (op == "+") code = flt ? OpFAdd : OpIAdd;
        else if (op == "-") code = flt ? OpFSub : OpISub;
        else if (op == "*") code = flt ? OpFMul : OpIMul;
        else if (op == "/") code = flt ? OpFDiv : (ta->isUnsigned() ? OpUDiv : OpSDiv);
        else if (op == "%") code = flt ? OpFMod : (ta->isUnsigned() ? OpUMod : OpSRem);
        else err("shader 暂不支持算术 `" + op + "`", line);
        return {ins(code, a.type, {a.id, v.id}), a.type};
    }

    // 调用：向量/矩阵构造、标量转换、内建数学、纹理采样
    Val call(const Expr* e) {
        if (!e->a || e->a->kind != Expr::Ident)
            err("shader 暂不支持间接调用", e->line);
        const std::string& fn = e->a->text;
        std::vector<Val> args;
        for (const auto& a : e->args) args.push_back(rvalue(a.get()));

        // 向量构造 vecN(...)：分量拼接（成分可为更小向量），不足时逐一 coerce
        ScType st;
        if (scTypeOf(fn, st) && st.k == TI::Vec) {
            TI::K ck = vecCompKind(fn);
            uint32_t comp = ck == TI::I32 ? b.tI32() : ck == TI::U32 ? b.tU32()
                          : ck == TI::Bool ? b.tBool() : b.tF32();
            uint32_t vt = b.tVec(comp, st.n);
            std::vector<uint32_t> ops;
            int total = 0;
            for (auto& a : args) {
                const TI& ti = b.info(a.type);
                if (ti.k == TI::Vec) { ops.push_back(a.id); total += ti.n; }
                else { ops.push_back(coerce(a, comp, e->line).id); total += 1; }
            }
            if (args.size() == 1 && total == 1 && st.n > 1) {   // splat: vec3(1.0)
                Val s = {ops[0], comp};
                ops.assign((size_t)st.n, s.id);
                total = st.n;
            }
            if (total != st.n) err("`" + fn + "` 构造分量数不符", e->line);
            return {ins(OpCompositeConstruct, vt, ops), vt};
        }
        // 标量转换 float(x)/int(x)/uint(x)
        if (scTypeOf(fn, st) && st.k != TI::Vec && st.k != TI::Mat && args.size() == 1) {
            uint32_t want = st.k == TI::F32 ? b.tF32() : st.k == TI::U32 ? b.tU32()
                          : st.k == TI::Bool ? b.tBool() : b.tI32();
            return coerce(args[0], want, e->line);
        }
        // 纹理采样 texture(sampler, uv)
        if (fn == "texture" && args.size() == 2) {
            uint32_t v4 = b.tVec(b.tF32(), 4);
            if (stage.shaderStage == ShaderStage::Frag)
                return {ins(OpImageSampleImplicitLod, v4, {args[0].id, args[1].id}), v4};
            // 非 frag 无隐式导数：显式 Lod 0（ImageOperands Lod = 0x2）
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0x2, b.constF(0.0f)}), v4};
        }
        // 核心指令内建
        if (fn == "dot" && args.size() == 2)
            return {ins(OpDot, b.tF32(), {args[0].id, args[1].id}), b.tF32()};
        // GLSL.std.450 内建（浮点族；min/max/clamp/abs 的整型变体按首参类别选）
        static const std::unordered_map<std::string, uint32_t> g450f = {
            {"round", GRound}, {"trunc", GTrunc}, {"sign", GFSign},
            {"floor", GFloor}, {"ceil", GCeil}, {"fract", GFract},
            {"sin", GSin}, {"cos", GCos}, {"tan", GTan},
            {"asin", GAsin}, {"acos", GAcos},
            {"pow", GPow}, {"exp", GExp}, {"log", GLog}, {"exp2", GExp2}, {"log2", GLog2},
            {"sqrt", GSqrt}, {"inversesqrt", GInverseSqrt},
            {"determinant", GDeterminant}, {"inverse", GMatrixInverse},
            {"mix", GFMix}, {"step", GStep}, {"smoothstep", GSmoothStep}, {"fma", GFma},
            {"length", GLength}, {"distance", GDistance}, {"cross", GCross},
            {"normalize", GNormalize}, {"reflect", GReflect}, {"refract", GRefract},
        };
        auto extInst = [&](uint32_t inst, uint32_t retT) -> Val {
            std::vector<uint32_t> ops = {b.glsl450, inst};
            for (auto& a : args) ops.push_back(a.id);
            return {ins(OpExtInst, retT, ops), retT};
        };
        {
            auto it = g450f.find(fn);
            if (it != g450f.end()) {
                uint32_t retT = fn == "length" || fn == "distance" || fn == "determinant"
                              ? b.tF32() : args[0].type;
                return extInst(it->second, retT);
            }
        }
        if (fn == "atan")
            return extInst(args.size() == 2 ? (uint32_t)GAtan2 : (uint32_t)GAtan, args[0].type);
        if (fn == "abs") {
            const TI& ti = b.info(args[0].type);
            return extInst(ti.isFloat() ? GFAbs : GSAbs, args[0].type);
        }
        if (fn == "min" || fn == "max" || fn == "clamp") {
            const TI& ti = b.info(args[0].type);
            // 混型参数向首参看齐（clamp(v, 0.0, 1.0) 等字面量场景）
            for (size_t i = 1; i < args.size(); i++) {
                const TI& ai = b.info(args[i].type);
                if (ti.k == TI::Vec && ai.k != TI::Vec) {
                    Val s = coerce(args[i], ti.elem, e->line);
                    std::vector<uint32_t> ops((size_t)ti.n, s.id);
                    args[i] = {ins(OpCompositeConstruct, args[0].type, ops), args[0].type};
                } else {
                    args[i] = coerce(args[i], args[0].type, e->line);
                }
            }
            uint32_t inst = fn == "min" ? (ti.isFloat() ? GFMin : ti.isUnsigned() ? GUMin : GSMin)
                          : fn == "max" ? (ti.isFloat() ? GFMax : ti.isUnsigned() ? GUMax : GSMax)
                          : (ti.isFloat() ? GFClamp : ti.isUnsigned() ? GUClamp : GSClamp);
            return extInst(inst, args[0].type);
        }
        if (fn == "mod" && args.size() == 2) {
            args[1] = coerce(args[1], args[0].type, e->line);
            return {ins(OpFMod, args[0].type, {args[0].id, args[1].id}), args[0].type};
        }
        err("shader 暂不支持函数 `" + fn + "`（辅助函数调用见 M2；内建补全见 syntax-s §16）", e->line);
    }

    // ---- 语句 ----
    void newBlock(uint32_t label) {
        put(body, OpLabel, {label});
        terminated = false;
    }
    void branch(uint32_t target) {
        if (!terminated) { put(body, OpBranch, {target}); terminated = true; }
    }

    // 顶层输出吸收（与 codegen_glsl::absorbedTop 同一规则）
    bool absorbedTop(const Stmt* s, int depth) {
        if (s->kind == Stmt::VarS)
            for (const auto& f : s->decls)
                if (outAggVars.count(f.name)) return true;   // 输出聚合局部：跳过声明
        if (s->kind == Stmt::ReturnS && depth >= 0) {
            if (scalarOutVar) {
                if (s->expr) {
                    Val v = rvalue(s->expr.get());
                    Ptr out = {scalarOutVar, scalarOutType, ScOutput};
                    v = coerce(v, out.type, s->line);
                    store(out, v);
                }
                put(body, OpReturn, {}); terminated = true;
                return true;
            }
            if (s->expr && s->expr->kind == Expr::Ident && outAggVars.count(s->expr->text)) {
                put(body, OpReturn, {}); terminated = true;   // return <聚合>：字段已散射
                return true;
            }
        }
        return false;
    }
    uint32_t scalarOutType = 0;

    void stmt(const Stmt* s, int depth) {
        if (!s || terminated) return;
        if (absorbedTop(s, depth)) return;
        switch (s->kind) {
            case Stmt::ExprS:
                if (s->expr) rvalue(s->expr.get());
                break;
            case Stmt::ReturnS:
                put(body, OpReturn, {});
                terminated = true;
                break;
            case Stmt::VarS:
            case Stmt::LetS:
                for (const auto& f : s->decls) declVar(f, s->kind == Stmt::LetS);
                break;
            case Stmt::IfS: {
                Val c = rvalue(s->expr.get());
                uint32_t thenL = b.id(), mergeL = b.id();
                uint32_t elseL = s->elseBody.empty() ? mergeL : b.id();
                put(body, OpSelectionMerge, {mergeL, 0});
                put(body, OpBranchConditional, {c.id, thenL, elseL});
                terminated = true;
                newBlock(thenL);
                for (const auto& x : s->body) stmt(x.get(), depth + 1);
                branch(mergeL);
                if (!s->elseBody.empty()) {
                    newBlock(elseL);
                    for (const auto& x : s->elseBody) stmt(x.get(), depth + 1);
                    branch(mergeL);
                }
                newBlock(mergeL);
                break;
            }
            case Stmt::WhileS: {
                uint32_t headL = b.id(), checkL = b.id(), bodyL = b.id(),
                         contL = b.id(), mergeL = b.id();
                branch(headL);
                newBlock(headL);
                put(body, OpLoopMerge, {mergeL, contL, 0});
                put(body, OpBranch, {checkL});
                terminated = true;
                newBlock(checkL);
                Val c = rvalue(s->expr.get());
                put(body, OpBranchConditional, {c.id, bodyL, mergeL});
                terminated = true;
                newBlock(bodyL);
                loops.push_back({mergeL, contL});
                for (const auto& x : s->body) stmt(x.get(), depth + 1);
                loops.pop_back();
                branch(contL);
                newBlock(contL);
                put(body, OpBranch, {headL});
                terminated = true;
                newBlock(mergeL);
                break;
            }
            case Stmt::DoWhileS: {
                // do-while：body 先行；continue 块里求条件回跳 header
                uint32_t headL = b.id(), bodyL = b.id(), contL = b.id(), mergeL = b.id();
                branch(headL);
                newBlock(headL);
                put(body, OpLoopMerge, {mergeL, contL, 0});
                put(body, OpBranch, {bodyL});
                terminated = true;
                newBlock(bodyL);
                loops.push_back({mergeL, contL});
                for (const auto& x : s->body) stmt(x.get(), depth + 1);
                loops.pop_back();
                branch(contL);
                newBlock(contL);
                Val c = rvalue(s->expr.get());
                put(body, OpBranchConditional, {c.id, headL, mergeL});
                terminated = true;
                newBlock(mergeL);
                break;
            }
            case Stmt::ForS: {
                if (s->forColl || s->forRangeLo || s->forRangeHi)
                    err("shader 暂不支持 for-in 形态（见 syntax-s §16 P0）", s->line);
                if (s->forInit) rvalue(s->forInit.get());
                uint32_t headL = b.id(), checkL = b.id(), bodyL = b.id(),
                         contL = b.id(), mergeL = b.id();
                branch(headL);
                newBlock(headL);
                put(body, OpLoopMerge, {mergeL, contL, 0});
                put(body, OpBranch, {checkL});
                terminated = true;
                newBlock(checkL);
                if (s->forCond) {
                    Val c = rvalue(s->forCond.get());
                    put(body, OpBranchConditional, {c.id, bodyL, mergeL});
                } else {
                    put(body, OpBranch, {bodyL});
                }
                terminated = true;
                newBlock(bodyL);
                loops.push_back({mergeL, contL});
                for (const auto& x : s->body) stmt(x.get(), depth + 1);
                loops.pop_back();
                branch(contL);
                newBlock(contL);
                if (s->forStep) rvalue(s->forStep.get());
                put(body, OpBranch, {headL});
                terminated = true;
                newBlock(mergeL);
                break;
            }
            case Stmt::BreakS:
                if (loops.empty()) err("break 不在循环内", s->line);
                put(body, OpBranch, {loops.back().mergeL});
                terminated = true;
                break;
            case Stmt::ContinueS:
                if (loops.empty()) err("continue 不在循环内", s->line);
                put(body, OpBranch, {loops.back().contL});
                terminated = true;
                break;
            case Stmt::CaseS: {
                // case 多路分支（自动 break 语义）→ if-else 链等价降级：
                // 每 arm 标签集比较求或 → SelectionMerge 嵌套
                Val sel = rvalue(s->expr.get());
                std::function<void(size_t)> emitArm = [&](size_t ai) {
                    if (ai >= s->caseArms.size()) return;
                    const auto& arm = s->caseArms[ai];
                    if (arm.labels.empty()) {          // default arm（恒命中）
                        for (const auto& x : arm.body) stmt(x.get(), depth + 1);
                        return;
                    }
                    Val hit = {0, 0};
                    for (const auto& l : arm.labels) {
                        Val lv = rvalue(l.get());
                        lv = coerce(lv, sel.type, s->line);
                        const TI& ti = b.info(sel.type);
                        Val eq = {ins(ti.isFloat() ? OpFOrdEqual : OpIEqual,
                                      b.tBool(), {sel.id, lv.id}), b.tBool()};
                        hit = hit.id ? Val{ins(OpLogicalOr, b.tBool(), {hit.id, eq.id}), b.tBool()}
                                     : eq;
                    }
                    uint32_t thenL = b.id(), mergeL = b.id();
                    uint32_t elseL = (ai + 1 < s->caseArms.size()) ? b.id() : mergeL;
                    put(body, OpSelectionMerge, {mergeL, 0});
                    put(body, OpBranchConditional, {hit.id, thenL, elseL});
                    terminated = true;
                    newBlock(thenL);
                    // arm 体 + through 贯穿链（自动 break 语义；through 内联后续 arm 体）
                    for (size_t k = ai; k < s->caseArms.size(); k++) {
                        for (const auto& x : s->caseArms[k].body) stmt(x.get(), depth + 1);
                        if (!s->caseArms[k].through) break;
                    }
                    branch(mergeL);
                    if (elseL != mergeL) {
                        newBlock(elseL);
                        emitArm(ai + 1);
                        branch(mergeL);
                    }
                    newBlock(mergeL);
                };
                emitArm(0);
                break;
            }
            default:
                err("shader 暂不支持该语句（for/switch 等见 syntax-s §16 P0）", s->line);
        }
    }

    // 局部变量声明（Function OpVariable 收敛到入口块首；常量数组初值走 initializer）
    void declVar(const Field& f, bool isLet) {
        uint32_t t = typeIdOf(f.type, f.line);
        uint32_t ptrT = b.tPtr(ScFunction, t);
        uint32_t v = b.id();
        // 全常量初始化列表 → OpVariable initializer（let pos[3] = {...} 惯用形）
        uint32_t initId = 0;
        if (f.init && f.init->kind == Expr::InitList && !f.type.arrayDims.empty()) {
            std::vector<uint32_t> parts;
            bool allConst = true;
            for (const auto& a : f.init->args) {
                uint32_t cid = constExpr(a.get());
                if (!cid) { allConst = false; break; }
                parts.push_back(cid);
            }
            if (allConst) initId = b.constComposite(t, parts);
        }
        if (initId) put(funcVars, OpVariable, {ptrT, v, ScFunction, initId});
        else        put(funcVars, OpVariable, {ptrT, v, ScFunction});
        b.name(v, f.name);
        vars[f.name] = {v, t, ScFunction};
        (void)isLet;
        if (!initId && f.init) {
            if (f.init->kind == Expr::InitList) {   // 非常量初始化列表：逐元素 store
                const TI& ti = b.info(t);
                for (size_t i = 0; i < f.init->args.size(); i++) {
                    Val ev = rvalue(f.init->args[i].get());
                    ev = coerce(ev, ti.elem, f.line);
                    uint32_t pt = b.tPtr(ScFunction, ti.elem);
                    uint32_t p = ins(OpAccessChain, pt, {v, b.constI((int)i)});
                    store({p, ti.elem, ScFunction}, ev);
                }
            } else {
                Val ev = rvalue(f.init.get());
                ev = coerce(ev, t, f.line);
                store({v, t, ScFunction}, ev);
            }
        }
    }

    // 编译期常量表达式 → 常量 id（失败返回 0）；vec 构造字面量支持
    uint32_t constExpr(const Expr* e) {
        if (!e) return 0;
        if (e->kind == Expr::FloatLit) return b.constF(std::strtof(e->text.c_str(), nullptr));
        if (e->kind == Expr::IntLit)   return b.constI((int32_t)std::strtol(e->text.c_str(), nullptr, 0));
        if (e->kind == Expr::Call && e->a && e->a->kind == Expr::Ident) {
            ScType st;
            if (scTypeOf(e->a->text, st) && st.k == TI::Vec) {
                TI::K ck = vecCompKind(e->a->text);
                uint32_t comp = ck == TI::I32 ? b.tI32() : ck == TI::U32 ? b.tU32() : b.tF32();
                std::vector<uint32_t> parts;
                for (const auto& a : e->args) {
                    uint32_t c = constExpr(a.get());
                    if (!c) return 0;
                    parts.push_back(c);
                }
                if ((int)parts.size() != st.n) return 0;
                return b.constComposite(b.tVec(comp, st.n), parts);
            }
        }
        if (e->kind == Expr::Unary && e->op == "-") {
            if (e->a && e->a->kind == Expr::FloatLit)
                return b.constF(-std::strtof(e->a->text.c_str(), nullptr));
            if (e->a && e->a->kind == Expr::IntLit)
                return b.constI(-(int32_t)std::strtol(e->a->text.c_str(), nullptr, 0));
        }
        return 0;
    }

    // ---- 函数装配与入口 ----
    void emit() {
        b.glsl450 = b.id();
        setupResources();
        setupIO();
        if (scalarOutVar) {
            // 记录标量输出值类型（absorbedTop 用）
            scalarOutType = typeIdOf(*stage.structCommon.type, stage.line);
        }

        uint32_t tv = b.tVoid();
        uint32_t fnT = b.type("fn_void", [&]{
            uint32_t r = b.id();
            put(b.secTypes, OpTypeFunction, {r, tv});
            return r;
        });
        uint32_t fnId = b.id();
        b.name(fnId, "main");
        uint32_t entryL = b.id();

        for (const auto& s : stage.body) stmt(s.get(), 0);
        if (!terminated) { put(body, OpReturn, {}); terminated = true; }

        // 函数骨架：OpFunction → entry label → 局部变量 → 语句流 → OpFunctionEnd
        put(b.secFunc, OpFunction, {tv, fnId, 0 /*None*/, fnT});
        put(b.secFunc, OpLabel, {entryL});
        b.secFunc.insert(b.secFunc.end(), funcVars.begin(), funcVars.end());
        b.secFunc.insert(b.secFunc.end(), body.begin(), body.end());
        put(b.secFunc, OpFunctionEnd, {});

        // OpEntryPoint + ExecutionMode
        uint32_t model = stage.shaderStage == ShaderStage::Frag ? 4
                       : stage.shaderStage == ShaderStage::Comp ? 5 : 0;
        std::vector<uint32_t> ep = {model, fnId};
        packStr(ep, "main");
        ep.insert(ep.end(), interfaceIds.begin(), interfaceIds.end());
        putV(b.secEntry, OpEntryPoint, ep);
        if (stage.shaderStage == ShaderStage::Frag)
            put(b.secExec, OpExecutionMode, {fnId, 7 /*OriginUpperLeft*/});
        if (stage.shaderStage == ShaderStage::Comp)
            put(b.secExec, OpExecutionMode, {fnId, 17 /*LocalSize*/, 64, 1, 1});
    }
};

} // namespace

std::vector<SpvUnit> emitSpirv(const Program& prog, const GlslTarget& target) {
    (void)target;   // SPIR-V 恒 Vulkan 语义；下游 SPIRV-Cross 按目标扇出
    std::unordered_map<std::string, const Decl*> structs;
    for (const auto& d : prog.decls)
        if (d && d->kind == Decl::StructD) structs[d->name] = d.get();

    std::vector<SpvUnit> units;
    for (const auto& d : prog.decls) {
        if (!d || d->kind != Decl::FuncD || d->shaderStage == ShaderStage::None) continue;
        Builder b;
        StageEmitter se(b, prog, *d, structs);
        se.emit();
        units.push_back(SpvUnit{d->shaderStage, d->name, b.finish(0)});
    }
    return units;
}


