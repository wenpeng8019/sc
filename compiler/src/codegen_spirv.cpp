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
#include <set>
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
    OpExtension = 10,
    OpExtInstImport = 11, OpExtInst = 12,
    OpMemoryModel = 14, OpEntryPoint = 15, OpExecutionMode = 16,
    OpCapability = 17,
    OpTypeVoid = 19, OpTypeBool = 20, OpTypeInt = 21, OpTypeFloat = 22,
    OpTypeVector = 23, OpTypeMatrix = 24, OpTypeImage = 25, OpTypeSampledImage = 27,
    OpTypeArray = 28, OpTypeRuntimeArray = 29, OpTypeStruct = 30,
    OpTypePointer = 32, OpTypeFunction = 33,
    OpConstantTrue = 41, OpConstantFalse = 42, OpConstant = 43,
    OpConstantComposite = 44,
    OpSpecConstantTrue = 48, OpSpecConstantFalse = 49, OpSpecConstant = 50,
    OpFunction = 54, OpFunctionEnd = 56,
    OpVariable = 59, OpLoad = 61, OpStore = 62, OpAccessChain = 65,
    OpDecorate = 71, OpMemberDecorate = 72,
    OpVectorShuffle = 79, OpCompositeConstruct = 80, OpCompositeExtract = 81,
    OpImageSampleImplicitLod = 87, OpImageSampleExplicitLod = 88,
    OpImageSampleDrefImplicitLod = 89, OpImageSampleDrefExplicitLod = 90,
    OpImageSampleProjImplicitLod = 91, OpImageSampleProjExplicitLod = 92,
    OpImageSampleProjDrefImplicitLod = 93, OpImageSampleProjDrefExplicitLod = 94,
    OpImageFetch = 95,
    OpImageGather = 96,
    OpImageDrefGather = 97,
    OpImage = 100,
    OpImageQuerySizeLod = 103,
    OpImageQueryLod = 105,
    OpImageQueryLevels = 106,
    OpConvertFToU = 109, OpConvertFToS = 110, OpConvertSToF = 111, OpConvertUToF = 112,
    OpUConvert = 113, OpSConvert = 114, OpFConvert = 115,
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
    OpControlBarrier = 224, OpMemoryBarrier = 225,
    OpAtomicExchange = 229, OpAtomicCompareExchange = 230,
    OpAtomicIAdd = 234, OpAtomicISub = 235,
    OpAtomicSMin = 236, OpAtomicUMin = 237, OpAtomicSMax = 238, OpAtomicUMax = 239,
    OpAtomicAnd = 240, OpAtomicOr = 241, OpAtomicXor = 242,
    OpGroupNonUniformAll = 334, OpGroupNonUniformAny = 335,
    OpGroupNonUniformBallot = 339, OpGroupNonUniformShuffle = 345,
    OpLoopMerge = 246, OpSelectionMerge = 247,
    OpLabel = 248, OpBranch = 249, OpBranchConditional = 250,
    OpKill = 252, OpReturn = 253, OpReturnValue = 254,
    OpName = 5, OpMemberName = 6,
    OpFunctionCall = 57,
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
    DecSpecId = 1,
    DecBlock = 2, DecBufferBlock = 3, DecColMajor = 5, DecArrayStride = 6,
    DecMatrixStride = 7, DecBuiltIn = 11,
    DecNoPerspective = 13, DecFlat = 14, DecCentroid = 16,
    DecNonWritable = 24,
    DecLocation = 30, DecBinding = 33, DecDescriptorSet = 34, DecOffset = 35,
};
enum BuiltInId : uint32_t {
    BiPosition = 0, BiPointSize = 1, BiClipDistance = 3,
    BiPointCoord = 16,
    BiFragCoord = 15, BiFrontFacing = 17, BiFragDepth = 22,
    BiVertexIndex = 42, BiInstanceIndex = 43,
    BiNumWorkgroups = 24, BiWorkgroupId = 26,
    BiLocalInvocationId = 27, BiGlobalInvocationId = 28,
    BiLocalInvocationIndex = 29, BiSampleId = 18, BiSamplePosition = 19,
    BiSubgroupSize = 36, BiSubgroupLocalInvocationId = 41,
};
// SPIR-V Capability 号（按需追加发射的子集）
enum CapabilityId : uint32_t {
    CapFloat16 = 9, CapInt64 = 11, CapInt16 = 22, CapInt8 = 39,
    CapGroupNonUniform = 61, CapGroupNonUniformVote = 62,
    CapGroupNonUniformBallot = 64, CapGroupNonUniformShuffle = 65,
    CapStorageBuffer16BitAccess = 4433, CapUniformAndStorageBuffer16BitAccess = 4434,
    CapStorageBuffer8BitAccess = 4448, CapUniformAndStorageBuffer8BitAccess = 4449,
};

// ---- 类型描述（发射期的语义视图，typeId 之外携带足够的选指令信息）--------
struct TI {                       // TypeInfo
    enum K { Void, Bool, F32, I32, U32, Vec, Mat, Array, RArray, Struct,
             Image, SampledImage } k = Void;
    TI() = default;
    TI(K kk) : k(kk) {}
    uint32_t id = 0;              // SPIR-V 类型 id
    K   comp = F32;               // Vec/Mat/Array 的组件标量类别
    int n = 0;                    // Vec: 分量数；Mat: 列数（方阵）；Array: 元素数
    int bits = 32;                // 标量位宽（P3：f2=16、i8/u8=64、i1/u1=8、i2/u2=16）
    uint32_t elem = 0;            // Array/RArray/Vec/Mat 的元素类型 id
    uint32_t dim = 0;             // Image: SpvDim（0=1D,1=2D,2=3D,3=Cube...）
    bool arrayed = false;         // Image: 是否数组纹理
    bool depth = false;           // Image: 是否深度比较（shadow sampler）
    bool ms = false;              // Image: 是否多重采样（sampler2DMS）
    // comp 对于 Image 类型复用：存储采样标量类型（F32/I32/U32）——决定 texture() 等函数返回类型
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
    std::set<uint32_t> extraCaps;               // 按需追加的 Capability（有序，产物确定）
    std::set<std::string> extraExts;            // 按需追加的 OpExtension（16bit/8bit storage）
    bool needSpv13 = false;                     // subgroup 用到 → 版本词升 1.3

    uint32_t id() { return next++; }

    // ---- 预热：把 glsl450 与全部基础类型固定在 id 1-21 ----
    // 目的：消除 macOS libc++ unordered_map 每进程哈希种子随机化导致的
    //       类型分配顺序漂移，使 SPIR-V id 跨进程恒定，spirv-cross
    //       产生的临时变量名（_76 等）不再因加新特性而改变。
    // 布局：1=glsl450  2=void  3=bool  4=f32  5=i32  6=u32
    //       7..9=vec2..4   10..12=ivec2..4   13..15=uvec2..4
    //       16..18=bvec2..4   19=mat2  20=mat3  21=mat4
    //       22+ = shader 特定内容（采样器/UBO/IO/指令）
    Builder() {
        glsl450 = id();       // id 1 (OpExtInstImport 在 finish() 里发射)
        tVoid();              // id 2
        tBool();              // id 3
        tF32();               // id 4
        tI32();               // id 5
        tU32();               // id 6
        tVec(tF32(), 2);     // id 7  vec2
        tVec(tF32(), 3);     // id 8  vec3
        tVec(tF32(), 4);     // id 9  vec4
        tVec(tI32(), 2);     // id 10 ivec2
        tVec(tI32(), 3);     // id 11 ivec3
        tVec(tI32(), 4);     // id 12 ivec4
        tVec(tU32(), 2);     // id 13 uvec2
        tVec(tU32(), 3);     // id 14 uvec3
        tVec(tU32(), 4);     // id 15 uvec4
        tVec(tBool(), 2);    // id 16 bvec2
        tVec(tBool(), 3);    // id 17 bvec3
        tVec(tBool(), 4);    // id 18 bvec4
        tMat(2);             // id 19 mat2
        tMat(3);             // id 20 mat3
        tMat(4);             // id 21 mat4
        // 此后 next==22，shader 特定内容从这里开始
    }

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
    // P3 窄/宽标量：类别复用 F32/I32/U32（指令选择谓词不变），bits 区分位宽；
    // 首次构造时追加对应 Capability（块成员的 storage 能力另在资源装配处补）。
    uint32_t tF16() {
        TI ti{TI::F32}; ti.bits = 16;
        return type("f16", [&]{ extraCaps.insert(CapFloat16);
            uint32_t r = id(); put(secTypes, OpTypeFloat, {r, 16}); return r; }, ti);
    }
    uint32_t tI64() {
        TI ti{TI::I32}; ti.bits = 64;
        return type("i64", [&]{ extraCaps.insert(CapInt64);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 64, 1}); return r; }, ti);
    }
    uint32_t tU64() {
        TI ti{TI::U32}; ti.bits = 64;
        return type("u64", [&]{ extraCaps.insert(CapInt64);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 64, 0}); return r; }, ti);
    }
    uint32_t tI16() {
        TI ti{TI::I32}; ti.bits = 16;
        return type("i16", [&]{ extraCaps.insert(CapInt16);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 16, 1}); return r; }, ti);
    }
    uint32_t tU16() {
        TI ti{TI::U32}; ti.bits = 16;
        return type("u16", [&]{ extraCaps.insert(CapInt16);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 16, 0}); return r; }, ti);
    }
    uint32_t tI8() {
        TI ti{TI::I32}; ti.bits = 8;
        return type("i8", [&]{ extraCaps.insert(CapInt8);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 8, 1}); return r; }, ti);
    }
    uint32_t tU8() {
        TI ti{TI::U32}; ti.bits = 8;
        return type("u8", [&]{ extraCaps.insert(CapInt8);
            uint32_t r = id(); put(secTypes, OpTypeInt, {r, 8, 0}); return r; }, ti);
    }
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
    // P3：半精度 / 64 位整数常量（f16 单 word 低 16 位；64 位双 word 低先）
    static uint32_t f32ToF16Bits(float f) {
        uint32_t x; std::memcpy(&x, &f, 4);
        uint32_t sign = (x >> 16) & 0x8000u;
        int32_t  exp  = (int32_t)((x >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = x & 0x7FFFFFu;
        if (exp <= 0)  return sign;                  // 下溢→0（略 subnormal）
        if (exp >= 31) return sign | 0x7C00u;        // 上溢→inf
        return sign | ((uint32_t)exp << 10) | (mant >> 13);
    }
    uint32_t constF16(float v) { return constScalar(tF16(), f32ToF16Bits(v)); }
    uint32_t const64(uint32_t typeId, uint64_t v) {
        std::string key = "c64_" + std::to_string(typeId) + "_" + std::to_string(v);
        auto it = constCache.find(key);
        if (it != constCache.end()) return it->second;
        uint32_t r = id();
        put(secTypes, OpConstant, {typeId, r, (uint32_t)(v & 0xFFFFFFFFu), (uint32_t)(v >> 32)});
        return constCache[key] = r;
    }
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
        out.push_back(kMagic);
        // subgroup（GroupNonUniform 系）需 SPIR-V 1.3（Vulkan 1.1）；其余保持 1.0
        out.push_back(needSpv13 ? 0x00010300u : kVersion);
        out.push_back(0);                    // generator：未注册
        out.push_back(next);                 // id bound
        out.push_back(0);                    // schema
        put(out, OpCapability, {1});         // Capability Shader
        put(out, OpCapability, {50});        // Capability ImageQuery（textureSize）
        for (uint32_t c : extraCaps)
            put(out, OpCapability, {c});     // 按需追加（GroupNonUniform 系等）
        for (const auto& e : extraExts) {    // OpExtension（16bit/8bit storage 等）
            std::vector<uint32_t> ops;
            packStr(ops, e);
            putV(out, OpExtension, ops);
        }
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
// P3：名字即字节数（与 CPU 侧一致）——f2=16 位浮点、i1/u1=8 位、i2/u2=16 位、
// i8/u8=64 位；类别仍归 F32/I32/U32（指令选择谓词复用），bits 携位宽。
struct ScType { TI::K k; int n; int bits = 32; };
bool scTypeOf(const std::string& name, ScType& out) {
    if (name == "f4" || name == "float")  { out = {TI::F32, 0, 32}; return true; }
    if (name == "f2")                     { out = {TI::F32, 0, 16}; return true; }
    if (name == "i4" || name == "int")    { out = {TI::I32, 0, 32}; return true; }
    if (name == "i1")                     { out = {TI::I32, 0, 8};  return true; }
    if (name == "i2")                     { out = {TI::I32, 0, 16}; return true; }
    if (name == "i8")                     { out = {TI::I32, 0, 64}; return true; }
    if (name == "u4" || name == "uint")   { out = {TI::U32, 0, 32}; return true; }
    if (name == "u1")                     { out = {TI::U32, 0, 8};  return true; }
    if (name == "u2")                     { out = {TI::U32, 0, 16}; return true; }
    if (name == "u8")                     { out = {TI::U32, 0, 64}; return true; }
    if (name == "bool") { out = {TI::Bool, 0, 32}; return true; }
    if (name == "vec2") { out = {TI::Vec, 2, 32}; return true; }
    if (name == "vec3") { out = {TI::Vec, 3, 32}; return true; }
    if (name == "vec4") { out = {TI::Vec, 4, 32}; return true; }
    if (name == "ivec2" || name == "uvec2" || name == "bvec2") { out = {TI::Vec, 2, 32}; return true; }
    if (name == "ivec3" || name == "uvec3" || name == "bvec3") { out = {TI::Vec, 3, 32}; return true; }
    if (name == "ivec4" || name == "uvec4" || name == "bvec4") { out = {TI::Vec, 4, 32}; return true; }
    if (name == "mat2") { out = {TI::Mat, 2, 32}; return true; }
    if (name == "mat3") { out = {TI::Mat, 3, 32}; return true; }
    if (name == "mat4") { out = {TI::Mat, 4, 32}; return true; }
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
    // P3 窄/宽标量：对齐 = 尺寸 = 字节数（std140/std430 标量规则）
    if (t == "f2" || t == "i2" || t == "u2") { a = 2; s = 2; }
    else if (t == "i1" || t == "u1") { a = 1; s = 1; }
    else if (t == "i8" || t == "u8") { a = 8; s = 8; }
    else if (t == "vec2" || t == "ivec2" || t == "uvec2" || t == "bvec2") { a = 8;  s = 8;  }
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
    uint32_t var = 0;                    // Uniform/PushConstant/Workgroup 存储的块变量
    uint32_t structType = 0;
    uint32_t sc = ScUniform;             // 存储类（Uniform / PushConstant / Workgroup）
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

    // 标量类别+位宽 → SPIR-V 类型 id（P3：f2/i8/u8/i1/u1/i2/u2）
    uint32_t scalarTid(const ScType& st) {
        if (st.k == TI::F32)
            return st.bits == 16 ? b.tF16() : b.tF32();
        if (st.k == TI::I32)
            return st.bits == 64 ? b.tI64() : st.bits == 16 ? b.tI16()
                 : st.bits == 8  ? b.tI8()  : b.tI32();
        if (st.k == TI::U32)
            return st.bits == 64 ? b.tU64() : st.bits == 16 ? b.tU16()
                 : st.bits == 8  ? b.tU8()  : b.tU32();
        if (st.k == TI::Bool) return b.tBool();
        return b.tI32();
    }

    // sc 类型引用 → SPIR-V 值类型 id（标量/向量/矩阵/一维数组）
    // 用户结构体类型 id（包含完整成员类型体系）
    std::unordered_map<std::string, uint32_t> structTypeCache;
    uint32_t structTypeId(const std::string& name, int line) {
        auto it = structTypeCache.find(name);
        if (it != structTypeCache.end()) return it->second;
        auto sit = structs.find(name);
        if (sit == structs.end())
            err("shader 未定义类型 `" + name + "`", line);
        const Decl& sd = *sit->second;
        // 先注册占位（递归保护）
        uint32_t r = b.id();
        structTypeCache[name] = r;
        b.name(r, name);
        std::vector<uint32_t> mts;
        for (size_t i = 0; i < sd.structCommon.fields.size(); i++) {
            const auto& f = sd.structCommon.fields[i];
            if (f.synthetic) continue;
            uint32_t mt = typeIdOf(f.type, f.line);
            b.memberName(r, (uint32_t)mts.size(), f.name);
            mts.push_back(mt);
        }
        std::vector<uint32_t> ops = {r};
        ops.insert(ops.end(), mts.begin(), mts.end());
        putV(b.secTypes, OpTypeStruct, ops);
        TI ti; ti.k = TI::Struct; ti.structName = name; ti.id = r;
        b.tiById[r] = ti;
        return r;
    }

    static bool isSamplerTypeName(const std::string& tname) {
        // float 采样器（标准 sampler*）
        if (tname == "sampler2D" || tname == "sampler3D" || tname == "samplerCube"
         || tname == "sampler2DArray" || tname == "samplerCubeArray"
         || tname == "sampler2DShadow" || tname == "samplerCubeShadow"
         || tname == "sampler2DArrayShadow" || tname == "samplerCubeArrayShadow") return true;
        // 整数采样器（isampler*）
        if (tname == "isampler2D" || tname == "isampler3D" || tname == "isamplerCube"
         || tname == "isampler2DArray" || tname == "isamplerCubeArray") return true;
        // 无符号采样器（usampler*）
        if (tname == "usampler2D" || tname == "usampler3D" || tname == "usamplerCube"
         || tname == "usampler2DArray" || tname == "usamplerCubeArray") return true;
        // 多重采样纹理（sampler2DMS）
        if (tname == "sampler2DMS" || tname == "sampler2DMSArray") return true;
        return false;
    }

    uint32_t typeIdOf(const TypeRef& t, int line) {
        // 资源类型（采样器） — 单独处理（使用缓存避免重复）
        if (isSamplerTypeName(t.name)) {
            // 采样器标量类型：i* → I32, u* → U32, 其他 → F32
            TI::K sampK = (t.name[0] == 'i') ? TI::I32
                        : (t.name[0] == 'u') ? TI::U32 : TI::F32;
            const std::string base = (sampK != TI::F32) ? t.name.substr(1) : t.name;
            uint32_t dim = 0, arrayed = 0, depth = 0, ms = 0;
            if (base == "sampler2D") { dim = 1; }
            else if (base == "sampler3D") { dim = 2; }
            else if (base == "samplerCube") { dim = 3; }
            else if (base == "sampler2DArray") { dim = 1; arrayed = 1; }
            else if (base == "samplerCubeArray") { dim = 3; arrayed = 1; }
            else if (base == "sampler2DShadow") { dim = 1; depth = 1; }
            else if (base == "samplerCubeShadow") { dim = 3; depth = 1; }
            else if (base == "sampler2DArrayShadow") { dim = 1; arrayed = 1; depth = 1; }
            else if (base == "samplerCubeArrayShadow") { dim = 3; arrayed = 1; depth = 1; }
            else if (base == "sampler2DMS") { dim = 1; ms = 1; }
            else if (base == "sampler2DMSArray") { dim = 1; arrayed = 1; ms = 1; }
            uint32_t sampScalar = sampK == TI::I32 ? b.tI32() : sampK == TI::U32 ? b.tU32() : b.tF32();
            TI iti; iti.k = TI::Image; iti.dim = dim; iti.arrayed = arrayed != 0;
            iti.depth = depth != 0; iti.ms = ms != 0; iti.comp = sampK;
            uint32_t tImg = b.type("img_" + t.name, [&]{
                uint32_t r = b.id();
                put(b.secTypes, OpTypeImage, {r, sampScalar, dim, depth, arrayed, ms, 1, 0});
                return r;
            }, iti);
            TI sti; sti.k = TI::SampledImage; sti.elem = tImg;
            return b.type("simg_" + t.name, [&]{
                uint32_t r = b.id();
                put(b.secTypes, OpTypeSampledImage, {r, tImg});
                return r;
            }, sti);
        }
        ScType st;
        if (!scTypeOf(t.name, st)) {
            // 用户结构体（非内建标量/向量/矩阵）
            if (!t.arrayDims.empty()) {
                uint32_t sbase = structTypeId(t.name, line);
                if (t.arrayDims.size() == 1) {
                    int n = std::atoi(t.arrayDims[0].c_str());
                    if (n <= 0) err("shader 数组维度须为正整数常量", line);
                    return b.tArray(sbase, b.constI(n), n);
                }
                err("shader 暂不支持多维结构体数组（见 syntax-s §16 P0）", line);
            }
            return structTypeId(t.name, line);
        }
        uint32_t base;
        switch (st.k) {
            case TI::F32:  base = scalarTid(st); break;
            case TI::I32:  base = scalarTid(st); break;
            case TI::U32:  base = scalarTid(st); break;
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
            if (t.arrayDims.size() > 1) {
                // 多维数组：逐级嵌套（外层最后）
                base = b.tArray(base, b.constI(std::atoi(t.arrayDims.back().c_str())),
                                       std::atoi(t.arrayDims.back().c_str()));
                for (int i = (int)t.arrayDims.size() - 2; i >= 0; i--) {
                    int n = std::atoi(t.arrayDims[(size_t)i].c_str());
                    if (n <= 0) err("shader 数组维度须为正整数常量", line);
                    base = b.tArray(base, b.constI(n), n);
                }
                return base;
            }
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
            if (d->kind == Decl::VarD && d->shaderAttr) {         // sampler* 全局（可单或多个纹理）
                for (const auto& f : d->structCommon.fields) {
                    const std::string& tname = f.type.name;
                    // 纹理类型 → OpTypeImage 维度参数
                    // OpTypeImage(result, sample_type, dimension, depth, arrayed, ms, sampled, format)
                    // dimension: 1=1D, 2=2D, 3=3D, 4=Cube, 5=Rect, 6=Buffer, 7=SubpassData
                    TI::K sampK = (tname[0] == 'i') ? TI::I32
                                : (tname[0] == 'u') ? TI::U32 : TI::F32;
                    const std::string base = (sampK != TI::F32) ? tname.substr(1) : tname;
                    uint32_t dim = 0; uint32_t arrayed = 0; uint32_t depth = 0; uint32_t ms = 0;
                    if (base == "sampler2D") { dim = 1; }
                    else if (base == "sampler3D") { dim = 2; }
                    else if (base == "samplerCube") { dim = 3; }
                    else if (base == "sampler2DArray") { dim = 1; arrayed = 1; }
                    else if (base == "samplerCubeArray") { dim = 3; arrayed = 1; }
                    else if (base == "sampler2DShadow") { dim = 1; depth = 1; }
                    else if (base == "samplerCubeShadow") { dim = 3; depth = 1; }
                    else if (base == "sampler2DArrayShadow") { dim = 1; arrayed = 1; depth = 1; }
                    else if (base == "samplerCubeArrayShadow") { dim = 3; arrayed = 1; depth = 1; }
                    else if (base == "sampler2DMS") { dim = 1; ms = 1; }
                    else if (base == "sampler2DMSArray") { dim = 1; arrayed = 1; ms = 1; }
                    else {
                        err("shader 暂不支持资源类型 `" + tname + "`（见 syntax-s §16 P1）", f.line);
                    }
                    uint32_t sampScalar = sampK == TI::I32 ? b.tI32() : sampK == TI::U32 ? b.tU32() : b.tF32();
                    TI iti; iti.k = TI::Image; iti.dim = dim; iti.arrayed = arrayed != 0;
                    iti.depth = depth != 0; iti.ms = ms != 0; iti.comp = sampK;
                    uint32_t tImg = b.type("img_" + tname, [&]{
                        uint32_t r = b.id();
                        put(b.secTypes, OpTypeImage, {r, sampScalar, dim, depth, arrayed, ms, 1, 0});
                        return r;
                    }, iti);
                    TI sti; sti.k = TI::SampledImage; sti.elem = tImg;
                    uint32_t tSi = b.type("simg_" + tname, [&]{
                        uint32_t r = b.id();
                        put(b.secTypes, OpTypeSampledImage, {r, tImg});
                        return r;
                    }, sti);
                    uint32_t ptrT = b.tPtr(ScUniformConstant, tSi);
                    uint32_t v = b.id();
                    put(b.secTypes, OpVariable, {ptrT, v, ScUniformConstant});
                    b.name(v, f.name);
                    auto* a = d->shaderAttr.get();
                    if (a->set >= 0)     put(b.secDeco, OpDecorate, {v, DecDescriptorSet, (uint32_t)a->set});
                    if (a->binding >= 0) put(b.secDeco, OpDecorate, {v, DecBinding, (uint32_t)a->binding});
                    samplers[f.name] = {v, tSi, ScUniformConstant};
                }
                continue;
            }
            if (d->kind != Decl::StructD || !d->shaderAttr ||
                d->shaderAttr->res == ShaderDeclAttr::None) continue;

            auto* a = d->shaderAttr.get();
            const bool std430 = a->res == ShaderDeclAttr::Storage;
            const bool push   = a->res == ShaderDeclAttr::Push;

            // shared 共享内存块（P2）：Workgroup 存储类，无 Block/Offset/ArrayStride
            // 布局装饰（SPIR-V 1.0 Workgroup 无显式布局），无 set/binding。
            if (a->res == ShaderDeclAttr::Shared) {
                std::vector<uint32_t> memberTypes;
                uint32_t structT = b.id();
                ResBlock rb;
                size_t mi = 0;
                for (const auto& f : d->structCommon.fields) {
                    if (f.synthetic) continue;
                    if (!f.type.arrayDims.empty() && f.type.arrayDims[0].empty())
                        err("shared 块不支持运行时数组（需定长）", f.line);
                    uint32_t mt = typeIdOf(f.type, f.line);   // 定长数组用无 stride 的普通类型
                    b.memberName(structT, (uint32_t)mi, f.name);
                    rb.memberIdx[f.name] = (int)mi;
                    rb.memberType[f.name] = mt;
                    memberTypes.push_back(mt);
                    mi++;
                }
                {   std::vector<uint32_t> ops = {structT};
                    ops.insert(ops.end(), memberTypes.begin(), memberTypes.end());
                    putV(b.secTypes, OpTypeStruct, ops); }
                b.name(structT, d->name + "_blk");
                uint32_t ptrT = b.tPtr(ScWorkgroup, structT);
                uint32_t v = b.id();
                put(b.secTypes, OpVariable, {ptrT, v, ScWorkgroup});
                b.name(v, d->name);
                rb.var = v;
                rb.structType = structT;
                rb.sc = ScWorkgroup;
                resBlocks[d->name] = rb;
                continue;
            }

            // sampler 资源块：opaque 类型不能进 UBO/SSBO 结构体；拆成独立 UniformConstant 变量。
            bool hasSamplerMember = false;
            bool hasNonSamplerMember = false;
            for (const auto& f : d->structCommon.fields) {
                if (f.synthetic) continue;
                if (isSamplerTypeName(f.type.name)) hasSamplerMember = true;
                else hasNonSamplerMember = true;
            }
            if (hasSamplerMember) {
                if (hasNonSamplerMember)
                    err("资源块 `" + d->name + "` 不能混合 sampler 与非 sampler 成员", d->line);
                int bi = 0;
                for (const auto& f : d->structCommon.fields) {
                    if (f.synthetic) continue;
                    uint32_t tSi = typeIdOf(f.type, f.line);
                    uint32_t ptrT = b.tPtr(ScUniformConstant, tSi);
                    uint32_t v = b.id();
                    put(b.secTypes, OpVariable, {ptrT, v, ScUniformConstant});
                    b.name(v, d->name + "_" + f.name);
                    if (a->set >= 0) put(b.secDeco, OpDecorate, {v, DecDescriptorSet, (uint32_t)a->set});
                    if (a->binding >= 0) put(b.secDeco, OpDecorate, {v, DecBinding, (uint32_t)(a->binding + bi)});
                    samplers[d->name + "." + f.name] = {v, tSi, ScUniformConstant};
                    bi++;
                }
                continue;
            }

            // 成员类型 + Offset 装饰（布局与反射清单同一算法）
            // resBlockOffsetOf：嵌套结构体按字段递归算 std140 尺寸
            std::function<int(const std::string&, bool)> structSize = [&](const std::string& name, bool s430) -> int {
                auto sit = structs.find(name);
                if (sit == structs.end()) return 4;
                int off2 = 0;
                for (const auto& mf : sit->second->structCommon.fields) {
                    if (mf.synthetic) continue;
                    Lay l = layOf(mf.type.name, mf.type.arrayDims, s430);
                    ScType dummy; bool isSc = scTypeOf(mf.type.name, dummy);
                    if (l.size == 0 && !isSc) {
                        // 嵌套结构体
                        l.align = 16; l.size = rup(structSize(mf.type.name, s430), 16);
                    }
                    off2 = rup(off2, l.align) + l.size;
                }
                return off2;
            };
            // 递归给 UBO 内嵌套结构体类型加 Block 级 Offset 装饰
            std::function<void(uint32_t, const std::string&, bool)> decorateStructMembers =
                [&](uint32_t tid, const std::string& name, bool s430) {
                    auto sit = structs.find(name);
                    if (sit == structs.end()) return;
                    int off2 = 0;
                    size_t midx = 0;
                    for (const auto& mf : sit->second->structCommon.fields) {
                        if (mf.synthetic) continue;
                        Lay l = layOf(mf.type.name, mf.type.arrayDims, s430);
                        if (l.size == 0) {
                            l.align = 16;
                            l.size = rup(structSize(mf.type.name, s430), 16);
                        }
                        off2 = rup(off2, l.align);
                        put(b.secDeco, OpMemberDecorate, {tid, (uint32_t)midx, DecOffset, (uint32_t)off2});
                        off2 += l.size;
                        midx++;
                    }
                };
            (void)decorateStructMembers;   // suppress unused if no nested structs
            std::vector<uint32_t> memberTypes;
            uint32_t structT = b.id();
            ResBlock rb;
            int off = 0;
            for (size_t i = 0; i < d->structCommon.fields.size(); i++) {
                const auto& f = d->structCommon.fields[i];
                // P3 窄标量块成员：16/8 位需 storage 能力 + 扩展（SPIR-V 1.0 经 OpExtension）
                {
                    ScType stw;
                    if (scTypeOf(f.type.name, stw) && stw.k != TI::Vec && stw.k != TI::Mat) {
                        if (stw.bits == 16) {
                            b.extraCaps.insert(CapStorageBuffer16BitAccess);
                            b.extraCaps.insert(CapUniformAndStorageBuffer16BitAccess);
                            b.extraExts.insert("SPV_KHR_16bit_storage");
                        } else if (stw.bits == 8) {
                            b.extraCaps.insert(CapStorageBuffer8BitAccess);
                            b.extraCaps.insert(CapUniformAndStorageBuffer8BitAccess);
                            b.extraExts.insert("SPV_KHR_8bit_storage");
                        }
                    }
                }
                uint32_t mt;
                if (!f.type.arrayDims.empty() && f.type.arrayDims[0].empty()) {
                    // 运行时数组 x[]: T（仅 storage 块末成员）
                    TypeRef elemRef;
                    elemRef.name = f.type.name;
                    uint32_t elem = typeIdOf(elemRef, f.line);
                    Lay el = layOf(f.type.name, {}, std430);
                    mt = b.tRArrayStrided(elem, (uint32_t)(std430 ? el.size : rup(el.size, 16)));
                } else if (!f.type.arrayDims.empty()) {
                    // 定长数组成员：带 stride 的独立类型（与 Function 数组不同源）
                    TypeRef elemRef;
                    elemRef.name = f.type.name;
                    uint32_t elem = typeIdOf(elemRef, f.line);
                    int n = std::atoi(f.type.arrayDims[0].c_str());
                    Lay el = layOf(f.type.name, {}, std430);
                    mt = b.tArrayStrided(elem, b.constI(n), n,
                                         (uint32_t)(std430 ? el.size : rup(el.size, 16)));
                } else {
                    mt = typeIdOf(f.type, f.line);
                    // 嵌套用户结构体：给其类型加 Offset 装饰，Block decoration 已由外层加
                    ScType sct2;
                    if (!scTypeOf(f.type.name, sct2) && structs.count(f.type.name)) {
                        decorateStructMembers(mt, f.type.name, std430);
                    }
                }
                Lay lay = layOf(f.type.name, f.type.arrayDims, std430);
                if (lay.size == 0) { lay.align = 16; lay.size = rup(structSize(f.type.name, std430), 16); }
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
            rb.sc = sc;                  // push 块 = PushConstant 存储类（AccessChain 指针类须同类）
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
        if (sem == "front_facing") { bi = BiFrontFacing; type = b.tBool(); return true; }
        if (sem == "sample_id")    { bi = BiSampleId; type = b.tI32(); return true; }
        if (sem == "point_size")   { bi = BiPointSize; type = b.tF32(); return true; }
        if (sem == "point_coord")   { bi = BiPointCoord; type = b.tVec(b.tF32(), 2); return true; }
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
        if (sem == "subgroup_size" || sem == "subgroup_invocation_id") {
            bi = sem == "subgroup_size" ? BiSubgroupSize : BiSubgroupLocalInvocationId;
            type = b.tU32();
            b.extraCaps.insert(CapGroupNonUniform);   // subgroup 内建需 GroupNonUniform + SPIR-V 1.3
            b.needSpv13 = true;
            return true;
        }
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
                // 插值限定词 / 整数类型自动 flat
                if (f.shaderAttr && f.shaderAttr->interp != ShaderFieldAttr::Default) {
                    switch (f.shaderAttr->interp) {
                        case ShaderFieldAttr::Flat:          put(b.secDeco, OpDecorate, {v, (uint32_t)DecFlat}); break;
                        case ShaderFieldAttr::NoPerspective: put(b.secDeco, OpDecorate, {v, (uint32_t)DecNoPerspective}); break;
                        case ShaderFieldAttr::Centroid:      put(b.secDeco, OpDecorate, {v, (uint32_t)DecCentroid}); break;
                        default: break;
                    }
                } else {
                    const TI& ti = b.info(t);
                    if (ti.isSigned() || ti.isUnsigned())
                        put(b.secDeco, OpDecorate, {v, (uint32_t)DecFlat});
                }
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
                    // 插值限定词 / 整数类型自动 flat
                    if (f.shaderAttr && f.shaderAttr->interp != ShaderFieldAttr::Default) {
                        switch (f.shaderAttr->interp) {
                            case ShaderFieldAttr::Flat:          put(b.secDeco, OpDecorate, {v, (uint32_t)DecFlat}); break;
                            case ShaderFieldAttr::NoPerspective: put(b.secDeco, OpDecorate, {v, (uint32_t)DecNoPerspective}); break;
                            case ShaderFieldAttr::Centroid:      put(b.secDeco, OpDecorate, {v, (uint32_t)DecCentroid}); break;
                            default: break;
                        }
                    } else {
                        const TI& ti = b.info(t);
                        if (ti.isSigned() || ti.isUnsigned())
                            put(b.secDeco, OpDecorate, {v, (uint32_t)DecFlat});
                    }
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
        if (from.k == TI::Vec && to.k == TI::Vec && from.n == to.n) {
            if (from.comp == TI::I32 && to.comp == TI::U32)
                return {ins(OpBitcast, wantType, {v.id}), wantType};
            if (from.comp == TI::U32 && to.comp == TI::I32)
                return {ins(OpBitcast, wantType, {v.id}), wantType};
            if (from.comp == TI::I32 && to.comp == TI::F32)
                return {ins(OpConvertSToF, wantType, {v.id}), wantType};
            if (from.comp == TI::U32 && to.comp == TI::F32)
                return {ins(OpConvertUToF, wantType, {v.id}), wantType};
            if (from.comp == TI::F32 && to.comp == TI::I32)
                return {ins(OpConvertFToS, wantType, {v.id}), wantType};
            if (from.comp == TI::F32 && to.comp == TI::U32)
                return {ins(OpConvertFToU, wantType, {v.id}), wantType};
        }
        // 标量：类别 + 位宽通用转换（P3：f2/i8/u8/i1/u1/i2/u2）
        const bool fromF = from.k == TI::F32, toF = to.k == TI::F32;
        const bool fromI = from.k == TI::I32, toI = to.k == TI::I32;
        const bool fromU = from.k == TI::U32, toU = to.k == TI::U32;
        if (fromF && toF)   // 浮点变宽度（f4↔f2）
            return {ins(OpFConvert, wantType, {v.id}), wantType};
        if ((fromI || fromU) && (toI || toU)) {
            if (from.bits == to.bits)   // 同宽变符号
                return {ins(OpBitcast, wantType, {v.id}), wantType};
            if (fromU == toU)           // 同符号变宽度（SConvert 符号扩展 / UConvert 零扩展）
                return {ins(fromU ? OpUConvert : OpSConvert, wantType, {v.id}), wantType};
            // 宽度+符号同时变化：先按源符号变宽（U/SConvert 结果符号须与指令一致），再 Bitcast
            uint32_t midT = fromU
                ? (to.bits == 64 ? b.tU64() : to.bits == 16 ? b.tU16() : to.bits == 8 ? b.tU8() : b.tU32())
                : (to.bits == 64 ? b.tI64() : to.bits == 16 ? b.tI16() : to.bits == 8 ? b.tI8() : b.tI32());
            uint32_t mid = ins(fromU ? OpUConvert : OpSConvert, midT, {v.id});
            return {ins(OpBitcast, wantType, {mid}), wantType};
        }
        if (fromI && toF) return {ins(OpConvertSToF, wantType, {v.id}), wantType};
        if (fromU && toF) return {ins(OpConvertUToF, wantType, {v.id}), wantType};
        if (fromF && toI) return {ins(OpConvertFToS, wantType, {v.id}), wantType};
        if (fromF && toU) return {ins(OpConvertFToU, wantType, {v.id}), wantType};
        err("shader 类型不匹配（无隐式转换路径）", line);
    }

    // ---- 左值 ----
    Ptr lvalue(const Expr* e) {
        switch (e->kind) {
            case Expr::Ident: {
                auto it = vars.find(e->text);
                if (it != vars.end()) return it->second;
                auto smp = samplers.find(e->text);
                if (smp != samplers.end()) return smp->second;
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
                    auto smp = samplers.find(key);      // sampler 资源成员
                    if (smp != samplers.end()) return smp->second;
                    auto rb = resBlocks.find(e->a->text);   // 资源块成员
                    if (rb != resBlocks.end()) {
                        auto mi = rb->second.memberIdx.find(e->text);
                        if (mi == rb->second.memberIdx.end())
                            err("资源块 `" + e->a->text + "` 无成员 `" + e->text + "`", e->line);
                        uint32_t mt = rb->second.memberType[e->text];
                        uint32_t sc = rb->second.sc;   // Uniform / PushConstant / Workgroup
                        uint32_t ptrT = b.tPtr(sc, mt);
                        uint32_t r = ins(OpAccessChain, ptrT, {rb->second.var, b.constI(mi->second)});
                        return {r, mt, sc};
                    }
                }
                // 单分量 swizzle 左值：v.x = ...（先于结构体成员，避免 rgba 与字段名冲突）
                if (isSwz(e->text) && e->text.size() == 1) {
                    // 只当基类型是向量时才走 swizzle
                    Ptr tryBase = lvalue(e->a.get());
                    const TI& tbi = b.info(tryBase.type);
                    if (tbi.k == TI::Vec) {
                        uint32_t ptrT = b.tPtr(tryBase.sc, tbi.elem);
                        uint32_t r = ins(OpAccessChain, ptrT, {tryBase.id, b.constU(swzIdx(e->text[0]))});
                        return {r, tbi.elem, tryBase.sc};
                    }
                    // 基类型不是向量 → 可能是结构体字段同名，继续走下面的结构体路径
                }
                // 用户结构体成员 lvalue（任意嵌套深度）
                {
                    Ptr base = lvalue(e->a.get());
                    const TI& bi = b.info(base.type);
                    if (bi.k == TI::Struct) {
                        auto sit = structs.find(bi.structName);
                        if (sit != structs.end()) {
                            int idx = 0;
                            for (const auto& f : sit->second->structCommon.fields) {
                                if (f.synthetic) continue;
                                if (f.name == e->text) {
                                    uint32_t mt = typeIdOf(f.type, f.line);
                                    uint32_t ptrT = b.tPtr(base.sc, mt);
                                    uint32_t r = ins(OpAccessChain, ptrT, {base.id, b.constI(idx)});
                                    return {r, mt, base.sc};
                                }
                                idx++;
                            }
                            err("结构体 `" + bi.structName + "` 无成员 `" + e->text + "`", e->line);
                        }
                    }
                }
                err("shader 暂不支持该成员左值", e->line);
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
                // swizzle 读（仅当基类型为向量时生效，否则走结构体成员路径）
                if (isSwz(e->text)) {
                    // 先求基类型，再判断
                    Val baseV = rvalue(e->a.get());
                    const TI& bi = b.info(baseV.type);
                    if (bi.k == TI::Vec) {
                        if (e->text.size() == 1)
                            return {ins(OpCompositeExtract, bi.elem, {baseV.id, (uint32_t)swzIdx(e->text[0])}), bi.elem};
                        uint32_t vt = b.tVec(bi.elem, (int)e->text.size());
                        std::vector<uint32_t> ops = {baseV.id, baseV.id};
                        for (char c : e->text) ops.push_back((uint32_t)swzIdx(c));
                        return {ins(OpVectorShuffle, vt, ops), vt};
                    }
                }
                // 用户结构体成员读（通用 lvalue + load）
                return load(lvalue(e));
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
                return coerce(v, scalarTid(st), e->line);
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
            // 标量字面量向另一侧类别看齐（位宽取较宽侧，防 64 位截断）
            const TI& ta = b.info(a.type);
            if (a.type != v.type) {
                const TI& tvv = b.info(v.type);
                int wb = ta.bits > tvv.bits ? ta.bits : tvv.bits;
                if (ta.k == TI::F32 || tvv.k == TI::F32) {
                    uint32_t want = wb == 16 ? b.tF16() : b.tF32();
                    a = coerce(a, want, e->line); v = coerce(v, want, e->line);
                } else if (ta.isUnsigned() || tvv.isUnsigned()) {
                    uint32_t want = wb == 64 ? b.tU64() : wb == 16 ? b.tU16()
                                  : wb == 8 ? b.tU8() : b.tU32();
                    a = coerce(a, want, e->line); v = coerce(v, want, e->line);
                } else {
                    uint32_t want = wb == 64 ? b.tI64() : wb == 16 ? b.tI16()
                                  : wb == 8 ? b.tI8() : b.tI32();
                    a = coerce(a, want, e->line); v = coerce(v, want, e->line);
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
        // 标量类别统一（字面量宽容：int 字面量随浮点/无符号侧；位宽取较宽侧）
        if (a.type != v.type) {
            int wb = ta->bits > tv->bits ? ta->bits : tv->bits;
            if (ta->isFloat() || tv->isFloat()) {
                uint32_t want = ta->k == TI::Vec ? a.type : tv->k == TI::Vec ? v.type
                              : wb == 16 ? b.tF16() : b.tF32();
                a = coerce(a, want, line); v = coerce(v, want, line);
            } else if (ta->isUnsigned() || tv->isUnsigned()) {
                uint32_t want = wb == 64 ? b.tU64() : wb == 16 ? b.tU16()
                              : wb == 8 ? b.tU8() : b.tU32();
                a = coerce(a, want, line); v = coerce(v, want, line);
            } else if (ta->isSigned() && tv->isSigned()) {
                uint32_t want = wb == 64 ? b.tI64() : wb == 16 ? b.tI16()
                              : wb == 8 ? b.tI8() : b.tI32();
                a = coerce(a, want, line); v = coerce(v, want, line);
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

        // —— P2 计算原语：barrier / memory_barrier / atomic_*（参数求值形态特殊，
        //    首参需指针而非右值，先于通用 rvalue 求值处理）——
        if (fn == "barrier" || fn == "memory_barrier") {
            if (fn == "barrier" && stage.shaderStage != ShaderStage::Comp)
                err("`barrier` 仅 comp 阶段可用", e->line);
            if (!e->args.empty()) err("`" + fn + "` 不接受参数", e->line);
            if (fn == "barrier")
                // Scope Workgroup(2)×2，语义 AcquireRelease|WorkgroupMemory(0x108)
                put(body, OpControlBarrier, {b.constU(2), b.constU(2), b.constU(0x108)});
            else
                // Scope Device(1)，语义 AcquireRelease|UniformMemory|WorkgroupMemory(0x148)
                put(body, OpMemoryBarrier, {b.constU(1), b.constU(0x148)});
            return {0, b.tVoid()};
        }
        if (fn.rfind("atomic_", 0) == 0) {
            const std::string op = fn.substr(7);
            const size_t need = op == "cas" ? 3 : 2;
            if (e->args.size() != need)
                err("`" + fn + "` 期望 " + std::to_string(need) + " 个参数", e->line);
            Ptr p = lvalue(e->args[0].get());
            if (p.sc != ScUniform && p.sc != ScWorkgroup)
                err("原子操作仅作用于 storage 块或 shared 成员", e->line);
            const TI& ti = b.info(p.type);
            if ((ti.k != TI::I32 && ti.k != TI::U32) || ti.bits != 32)
                err("原子操作仅支持 i4/u4 标量（32 位；64 位需 Int64Atomics 待后续）", e->line);
            uint32_t scope = b.constU(1);    // Device
            uint32_t sem   = b.constU(0);    // Relaxed
            if (op == "cas") {
                Val cmp = coerce(rvalue(e->args[1].get()), p.type, e->line);
                Val val = coerce(rvalue(e->args[2].get()), p.type, e->line);
                // OpAtomicCompareExchange: ptr scope semEq semUneq value comparator
                return {ins(OpAtomicCompareExchange, p.type,
                            {p.id, scope, sem, sem, val.id, cmp.id}), p.type};
            }
            Val val = coerce(rvalue(e->args[1].get()), p.type, e->line);
            uint16_t code =
                  op == "add" ? OpAtomicIAdd : op == "sub" ? OpAtomicISub
                : op == "and" ? OpAtomicAnd  : op == "or"  ? OpAtomicOr
                : op == "xor" ? OpAtomicXor  : op == "exchange" ? OpAtomicExchange
                : op == "min" ? (ti.k == TI::U32 ? OpAtomicUMin : OpAtomicSMin)
                : op == "max" ? (ti.k == TI::U32 ? OpAtomicUMax : OpAtomicSMax)
                : (uint16_t)0;
            if (!code)
                err("shader 暂不支持函数 `" + fn + "`（原子族：add/sub/min/max/and/or/xor/exchange/cas）", e->line);
            return {ins(code, p.type, {p.id, scope, sem, val.id}), p.type};
        }

        std::vector<Val> args;
        for (const auto& a : e->args) args.push_back(rvalue(a.get()));

        // —— P2 subgroup 基础三件（vote/ballot/shuffle，SPIR-V 1.3 GroupNonUniform 系）——
        if (fn == "subgroup_all" || fn == "subgroup_any" ||
            fn == "subgroup_ballot" || fn == "subgroup_shuffle") {
            b.needSpv13 = true;
            b.extraCaps.insert(CapGroupNonUniform);
            uint32_t scope = b.constU(3);            // Scope Subgroup
            if (fn == "subgroup_all" || fn == "subgroup_any") {
                if (args.size() != 1) err("`" + fn + "` 期望 1 个 bool 参数", e->line);
                b.extraCaps.insert(CapGroupNonUniformVote);
                Val p = coerce(args[0], b.tBool(), e->line);
                uint16_t code = fn == "subgroup_all" ? OpGroupNonUniformAll : OpGroupNonUniformAny;
                return {ins(code, b.tBool(), {scope, p.id}), b.tBool()};
            }
            if (fn == "subgroup_ballot") {
                if (args.size() != 1) err("`subgroup_ballot` 期望 1 个 bool 参数", e->line);
                b.extraCaps.insert(CapGroupNonUniformBallot);
                Val p = coerce(args[0], b.tBool(), e->line);
                uint32_t uvec4 = b.tVec(b.tU32(), 4);
                return {ins(OpGroupNonUniformBallot, uvec4, {scope, p.id}), uvec4};
            }
            // subgroup_shuffle(value, lane)
            if (args.size() != 2) err("`subgroup_shuffle` 期望 (值, 通道) 2 个参数", e->line);
            b.extraCaps.insert(CapGroupNonUniformShuffle);
            Val lane = coerce(args[1], b.tU32(), e->line);
            return {ins(OpGroupNonUniformShuffle, args[0].type,
                        {scope, args[0].id, lane.id}), args[0].type};
        }

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
                if (ti.k == TI::Vec) {
                    // OpCompositeConstruct 向量构造要求标量 constituent，向量参数需拆分分量。
                    for (int i = 0; i < ti.n; i++) {
                        Val c = {ins(OpCompositeExtract, ti.elem, {a.id, (uint32_t)i}), ti.elem};
                        ops.push_back(coerce(c, comp, e->line).id);
                    }
                    total += ti.n;
                } else {
                    ops.push_back(coerce(a, comp, e->line).id);
                    total += 1;
                }
            }
            if (args.size() == 1 && total == 1 && st.n > 1) {   // splat: vec3(1.0)
                Val s = {ops[0], comp};
                ops.assign((size_t)st.n, s.id);
                total = st.n;
            }
            if (total != st.n) err("`" + fn + "` 构造分量数不符", e->line);
            return {ins(OpCompositeConstruct, vt, ops), vt};
        }
        // 用户结构体构造 T(field0, field1, ...)
        if (!scTypeOf(fn, st) && structs.count(fn)) {
            uint32_t stype = structTypeId(fn, e->line);
            const Decl& sd = *structs.at(fn);
            std::vector<uint32_t> ops;
            size_t fi = 0;
            for (const auto& f : sd.structCommon.fields) {
                if (f.synthetic) continue;
                if (fi >= args.size()) err("`" + fn + "` 构造缺少字段值", e->line);
                uint32_t mt = typeIdOf(f.type, f.line);
                ops.push_back(coerce(args[fi++], mt, e->line).id);
            }
            return {ins(OpCompositeConstruct, stype, ops), stype};
        }
        // 标量转换 float(x)/int(x)/uint(x)（含 P3 窄/宽标量 f2(x)/i8(x)/...）
        if (scTypeOf(fn, st) && st.k != TI::Vec && st.k != TI::Mat && args.size() == 1)
            return coerce(args[0], scalarTid(st), e->line);
        // 矩阵构造 matN(列0, 列1, ...)
        if (scTypeOf(fn, st) && st.k == TI::Mat) {
            uint32_t vt = b.tMat(st.n);
            std::vector<uint32_t> cols;
            uint32_t colt = b.tVec(b.tF32(), st.n);
            for (auto& a : args) {
                const TI& ti = b.info(a.type);
                if (ti.k == TI::Vec && ti.n == st.n) cols.push_back(a.id);
                else cols.push_back(coerce(a, colt, e->line).id);
            }
            if ((int)cols.size() != st.n) err("`" + fn + "` 矩阵列数不符", e->line);
            return {ins(OpCompositeConstruct, vt, cols), vt};
        }
        // 纹理采样族
        // shadow sampler 辅助：把「含 dref 最后分量的 coord vec」拆成 (reducedCoord, dref)
        auto splitDref = [&](Val coord) -> std::pair<uint32_t, uint32_t> {
            const TI& ct = b.info(coord.type);
            int n = (ct.k == TI::Vec) ? ct.n : 1;
            uint32_t dref = ins(OpCompositeExtract, b.tF32(), {coord.id, (uint32_t)(n - 1)});
            uint32_t reduced;
            if (n - 1 == 1) {
                reduced = ins(OpCompositeExtract, b.tF32(), {coord.id, 0u});
            } else {
                uint32_t rt = b.tVec(b.tF32(), n - 1);
                std::vector<uint32_t> ops = {coord.id, coord.id};
                for (int i = 0; i < n - 1; i++) ops.push_back((uint32_t)i);
                reduced = ins(OpVectorShuffle, rt, ops);
            }
            return {reduced, dref};
        };
        // 判断 sampler 是否为 shadow（depth=1）
        auto isShadowSampler = [&](const Val& smp) -> bool {
            const TI& si = b.info(smp.type);
            if (si.k != TI::SampledImage || si.elem == 0) return false;
            return b.info(si.elem).depth;
        };
        // 采样器返回类型：float 采样器 → vec4；isampler* → ivec4；usampler* → uvec4
        auto imgRetType = [&](const Val& smp) -> uint32_t {
            const TI& si = b.info(smp.type);
            if (si.k == TI::SampledImage && si.elem) {
                const TI& ii = b.info(si.elem);
                if (ii.k == TI::Image) {
                    if (ii.comp == TI::I32) return b.tVec(b.tI32(), 4);
                    if (ii.comp == TI::U32) return b.tVec(b.tU32(), 4);
                }
            }
            return b.tVec(b.tF32(), 4);
        };
        // 投影 shadow 辅助：从 projCoord（最后一分量为投影除数）中提取 dref
        // GLSL textureProj(sampler2DShadow, vec4 P): P = (s*w, t*w, ref*w, w)
        //   → dref = P[n-2] / P[n-1] = ref
        //   → coord = P 原样（SPIR-V ProjDref 指令用 coord.w 做除数）
        auto splitDrefProj = [&](Val coord) -> std::pair<uint32_t, uint32_t> {
            const TI& ct = b.info(coord.type);
            int n = (ct.k == TI::Vec) ? ct.n : 1;
            uint32_t num = ins(OpCompositeExtract, b.tF32(), {coord.id, (uint32_t)(n - 2)});
            uint32_t den = ins(OpCompositeExtract, b.tF32(), {coord.id, (uint32_t)(n - 1)});
            uint32_t dref = ins(OpFDiv, b.tF32(), {num, den});
            return {coord.id, dref};  // coord 原样，dref 分离
        };
        // texture(sampler, uv) — 隐式（frag）或 lod=0（非 frag）
        if (fn == "texture" && args.size() == 2) {
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                if (stage.shaderStage == ShaderStage::Frag)
                    return {ins(OpImageSampleDrefImplicitLod, retT, {args[0].id, rc, dref}), retT};
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0x2, b.constF(0.0f)}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            if (stage.shaderStage == ShaderStage::Frag)
                return {ins(OpImageSampleImplicitLod, v4, {args[0].id, args[1].id}), v4};
            // 非 frag 无隐式导数：显式 Lod 0（ImageOperands Lod = 0x2）
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0x2, b.constF(0.0f)}), v4};
        }
        // textureLod(sampler, uv, lod) — 显式 lod 采样
        if (fn == "textureLod" && args.size() == 3) {
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                auto lod = coerce(args[2], b.tF32(), e->line);
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0x2, lod.id}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            auto lod = coerce(args[2], b.tF32(), e->line);
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0x2 /*ImageOperands Lod*/, lod.id}), v4};
        }
        // textureProj(sampler, proj_uv) — 投影采样 vec4(uv.xy, unused, w) → uv/w
        if (fn == "textureProj" && args.size() == 2) {
            if (isShadowSampler(args[0])) {
                auto [coord, dref] = splitDrefProj(args[1]);
                uint32_t retT = b.tF32();
                if (stage.shaderStage == ShaderStage::Frag)
                    return {ins(OpImageSampleProjDrefImplicitLod, retT, {args[0].id, coord, dref}), retT};
                return {ins(OpImageSampleProjDrefExplicitLod, retT,
                            {args[0].id, coord, dref, 0x2, b.constF(0.0f)}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            if (stage.shaderStage == ShaderStage::Frag)
                return {ins(OpImageSampleProjImplicitLod, v4, {args[0].id, args[1].id}), v4};
            // 非 frag 无隐式导数：显式 Lod 0
            return {ins(OpImageSampleProjExplicitLod, v4,
                        {args[0].id, args[1].id, 0x2 /*ImageOperands Lod*/, b.constF(0.0f)}), v4};
        }
        // textureProjLod(sampler, proj_uv, lod) — 投影采样 + 显式 lod
        if (fn == "textureProjLod" && args.size() == 3) {
            if (isShadowSampler(args[0])) {
                auto [coord, dref] = splitDrefProj(args[1]);
                uint32_t retT = b.tF32();
                auto lod = coerce(args[2], b.tF32(), e->line);
                return {ins(OpImageSampleProjDrefExplicitLod, retT,
                            {args[0].id, coord, dref, 0x2, lod.id}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            auto lod = coerce(args[2], b.tF32(), e->line);
            return {ins(OpImageSampleProjExplicitLod, v4,
                        {args[0].id, args[1].id, 0x2 /*ImageOperands Lod*/, lod.id}), v4};
        }
        // texture(sampler, coord, bias) — 带偏置的隐式 lod（仅 frag 阶段）
        // ImageOperands::Bias = 0x1
        if (fn == "texture" && args.size() == 3) {
            if (stage.shaderStage != ShaderStage::Frag)
                err("texture(sampler, coord, bias) 仅在 frag 阶段有效", e->line);
            auto bias = coerce(args[2], b.tF32(), e->line);
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                return {ins(OpImageSampleDrefImplicitLod, retT,
                            {args[0].id, rc, dref, 0x1 /*Bias*/, bias.id}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            return {ins(OpImageSampleImplicitLod, v4,
                        {args[0].id, args[1].id, 0x1 /*Bias*/, bias.id}), v4};
        }
        // textureGather(sampler, coord, comp_or_ref)
        //   普通采样器: comp = int(0-3 = rgba 分量)      → OpImageGather
        //   shadow 采样器: comp = float 深度参考值        → OpImageDrefGather
        if (fn == "textureGather" && args.size() == 3) {
            uint32_t v4 = imgRetType(args[0]);
            if (isShadowSampler(args[0])) {
                auto ref = coerce(args[2], b.tF32(), e->line);
                return {ins(OpImageDrefGather, v4, {args[0].id, args[1].id, ref.id}), v4};
            }
            auto comp = coerce(args[2], b.tI32(), e->line);
            return {ins(OpImageGather, v4, {args[0].id, args[1].id, comp.id}), v4};
        }
        // Offset 系列：ConstOffset = 0x8。偏移须为编译期常量（如 ivec2(1,0)）；
        //   若传入运行时值，SPIR-V 校验器将报错。
        //   此处对 offset 参数特殊处理：先尝试 constExpr 折叠为常量，失败则原样传入。
        // textureOffset(sampler, coord, ivec_offset)
        if (fn == "textureOffset" && args.size() == 3) {
            uint32_t offId = (e->args.size() >= 3) ? constExpr(e->args[2].get()) : 0;
            if (!offId) offId = args[2].id;
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                if (stage.shaderStage == ShaderStage::Frag)
                    return {ins(OpImageSampleDrefImplicitLod, retT,
                                {args[0].id, rc, dref, 0x8 /*ConstOffset*/, offId}), retT};
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0xA /*Lod|ConstOffset*/, b.constF(0.0f), offId}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            if (stage.shaderStage == ShaderStage::Frag)
                return {ins(OpImageSampleImplicitLod, v4,
                            {args[0].id, args[1].id, 0x8 /*ConstOffset*/, offId}), v4};
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0xA /*Lod|ConstOffset*/, b.constF(0.0f), offId}), v4};
        }
        // textureLodOffset(sampler, coord, lod, ivec_offset)
        if (fn == "textureLodOffset" && args.size() == 4) {
            uint32_t offId = (e->args.size() >= 4) ? constExpr(e->args[3].get()) : 0;
            if (!offId) offId = args[3].id;
            auto lod = coerce(args[2], b.tF32(), e->line);
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0xA /*Lod|ConstOffset*/, lod.id, offId}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0xA /*Lod|ConstOffset*/, lod.id, offId}), v4};
        }
        // textureProjOffset(sampler, projCoord, ivec_offset)
        if (fn == "textureProjOffset" && args.size() == 3) {
            uint32_t offId = (e->args.size() >= 3) ? constExpr(e->args[2].get()) : 0;
            if (!offId) offId = args[2].id;
            uint32_t v4 = imgRetType(args[0]);
            if (stage.shaderStage == ShaderStage::Frag)
                return {ins(OpImageSampleProjImplicitLod, v4,
                            {args[0].id, args[1].id, 0x8 /*ConstOffset*/, offId}), v4};
            return {ins(OpImageSampleProjExplicitLod, v4,
                        {args[0].id, args[1].id, 0xA /*Lod|ConstOffset*/, b.constF(0.0f), offId}), v4};
        }
        // textureGradOffset(sampler, coord, dPdx, dPdy, ivec_offset)
        if (fn == "textureGradOffset" && args.size() == 5) {
            uint32_t offId = (e->args.size() >= 5) ? constExpr(e->args[4].get()) : 0;
            if (!offId) offId = args[4].id;
            if (isShadowSampler(args[0])) {
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0xC /*Grad|ConstOffset*/, args[2].id, args[3].id, offId}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0xC /*Grad|ConstOffset*/, args[2].id, args[3].id, offId}), v4};
        }
        // textureQueryLod(sampler, coord) → vec2(computed_lod, clamped_lod)（需 ImageQuery 能力）
        if (fn == "textureQueryLod" && args.size() == 2) {
            uint32_t v2f = b.tVec(b.tF32(), 2);
            return {ins(OpImageQueryLod, v2f, {args[0].id, args[1].id}), v2f};
        }
        // textureQueryLevels(sampler) → int（mipmap 层数，需 ImageQuery 能力）
        if (fn == "textureQueryLevels" && args.size() == 1) {
            const TI& si = b.info(args[0].type);
            if (si.k != TI::SampledImage || si.elem == 0)
                err("textureQueryLevels 须为 sampler 纹理", e->line);
            uint32_t img = ins(OpImage, si.elem, {args[0].id});
            return {ins(OpImageQueryLevels, b.tI32(), {img}), b.tI32()};
        }
        // textureGrad(sampler, coord, dPdx, dPdy) — 显式梯度采样
        // ImageOperands::Grad = 0x4（后跟 dPdx / dPdy 两组梯度向量）
        if (fn == "textureGrad" && args.size() == 4) {
            if (isShadowSampler(args[0])) {
                // shadow + grad：拆分坐标（coord 末分量为 dref），Dref 变体 + Grad
                auto [rc, dref] = splitDref(args[1]);
                uint32_t retT = b.tF32();
                return {ins(OpImageSampleDrefExplicitLod, retT,
                            {args[0].id, rc, dref, 0x4 /*Grad*/, args[2].id, args[3].id}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            return {ins(OpImageSampleExplicitLod, v4,
                        {args[0].id, args[1].id, 0x4 /*Grad*/, args[2].id, args[3].id}), v4};
        }
        // textureProjGrad(sampler, projCoord, dPdx, dPdy) — 投影 + 显式梯度
        if (fn == "textureProjGrad" && args.size() == 4) {
            if (isShadowSampler(args[0])) {
                auto [coord, dref] = splitDrefProj(args[1]);
                uint32_t retT = b.tF32();
                return {ins(OpImageSampleProjDrefExplicitLod, retT,
                            {args[0].id, coord, dref, 0x4 /*Grad*/, args[2].id, args[3].id}), retT};
            }
            uint32_t v4 = imgRetType(args[0]);
            return {ins(OpImageSampleProjExplicitLod, v4,
                        {args[0].id, args[1].id, 0x4 /*Grad*/, args[2].id, args[3].id}), v4};
        }
        // texelFetch(sampler, ivec_coords, lod_or_sample) — 无采样直接读
        // MS 纹理（sampler2DMS）：第3参数为 sample 索引，用 Sample=0x40 操作数
        // 普通纹理：第3参数为 lod，用 Lod=0x2 操作数
        if (fn == "texelFetch" && args.size() == 3) {
            const TI& si = b.info(args[0].type);
            if (si.k != TI::SampledImage || si.elem == 0)
                err("texelFetch 首参须为 sampler 纹理", e->line);
            uint32_t retT = imgRetType(args[0]);
            uint32_t img = ins(OpImage, si.elem, {args[0].id});
            const TI& ii = b.info(si.elem);
            if (ii.ms) {
                auto sample = coerce(args[2], b.tI32(), e->line);
                return {ins(OpImageFetch, retT, {img, args[1].id, 0x40 /*Sample*/, sample.id}), retT};
            }
            auto lod = coerce(args[2], b.tI32(), e->line);
            return {ins(OpImageFetch, retT, {img, args[1].id, 0x2 /*Lod*/, lod.id}), retT};
        }
        // textureSize(sampler, lod) → ivec2/ivec3（与纹理维度对应）
        if (fn == "textureSize" && args.size() == 2) {
            auto lod = coerce(args[1], b.tI32(), e->line);
            const TI& si = b.info(args[0].type);
            if (si.k != TI::SampledImage || si.elem == 0)
                err("textureSize 首参须为 sampler 纹理", e->line);
            const TI& ii = b.info(si.elem);
            if (ii.k != TI::Image)
                err("textureSize 内部错误：image 类型无效", e->line);
            int comps = 0;
            if (ii.dim == 0) comps = 1;          // 1D
            else if (ii.dim == 1) comps = 2;     // 2D
            else if (ii.dim == 2) comps = 3;     // 3D
            else if (ii.dim == 3) comps = 2;     // Cube
            else err("textureSize 暂不支持该纹理维度", e->line);
            if (ii.arrayed) comps += 1;
            uint32_t rt = comps == 1 ? b.tI32() : b.tVec(b.tI32(), comps);
            uint32_t img = ins(OpImage, si.elem, {args[0].id});
            return {ins(OpImageQuerySizeLod, rt, {img, lod.id}), rt};
        }
        // derivative：dFdx/dFdy/fwidth（frag 阶段门控）
        if ((fn == "dFdx" || fn == "dFdy" || fn == "fwidth") && args.size() == 1) {
            if (stage.shaderStage != ShaderStage::Frag)
                err(fn + " 仅在 frag 阶段有效", e->line);
            uint16_t code = fn == "dFdx" ? (uint16_t)207 : fn == "dFdy" ? (uint16_t)208 : (uint16_t)209;
            // OpDPdx=207 OpDPdy=208 OpFwidth=209
            return {ins(code, args[0].type, {args[0].id}), args[0].type};
        }
        // 矩阵运算（补全）：transpose/inverse/determinant
        if (fn == "transpose" && args.size() == 1) {
            const TI& ti = b.info(args[0].type);
            if (ti.k != TI::Mat) err("transpose 须矩阵参数", e->line);
            return {ins((uint16_t)84 /*OpTranspose*/, args[0].type, {args[0].id}), args[0].type};
        }
        // OpFunctionCall(内建函数均处理不到这里)
        if (fn == "dot" && args.size() == 2)
            return {ins(OpDot, b.tF32(), {args[0].id, args[1].id}), b.tF32()};
        // 辅助函数调用（OpFunctionCall）
        {
            const Decl* hd = nullptr;
            for (const auto& d : prog.decls)
                if (d && d->kind == Decl::FuncD && d->shaderStage == ShaderStage::None
                    && d->name == fn) { hd = d.get(); break; }
            if (hd) {
                emitHelper(*hd);   // 幂等：已发射过则跳过
                auto& h = helpers.at(fn);
                uint32_t retT = helperRetType(fn);
                uint32_t r = b.id();
                // OpFunctionCall 只接受值 id 列（Logical 地址模型）
                std::vector<uint32_t> real = {retT, r, h.second};
                for (size_t i = 0; i < args.size(); i++) {
                    const auto& f = hd->structCommon.fields[i];
                    uint32_t pt = typeIdOf(f.type, f.line);
                    Val a = coerce(args[i], pt, e->line);
                    real.push_back(a.id);
                }
                putV(body, OpFunctionCall, real);
                return {r, retT};
            }
        }
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
            const TI& ti = b.info(args[0].type);
            uint16_t code = ti.isFloat() ? OpFMod : ti.isUnsigned() ? OpUMod : OpSRem;
            return {ins(code, args[0].type, {args[0].id, args[1].id}), args[0].type};
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
                // discard 在 sc/ss 里可写作裸 discard 语句（解析为 ExprS 含 Ident "discard"）
                if (s->expr && s->expr->kind == Expr::Ident && s->expr->text == "discard") {
                    if (stage.shaderStage != ShaderStage::Frag)
                        err("discard 仅在 frag 阶段有效", s->line);
                    put(body, OpKill, {});
                    terminated = true;
                    break;
                }
                if (s->expr) rvalue(s->expr.get());
                break;
            case Stmt::ReturnS:
                if (s->expr) {
                    Val v = rvalue(s->expr.get());
                    put(body, OpReturnValue, {v.id});
                } else {
                    put(body, OpReturn, {});
                }
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

    // 辅助函数表：name → (OpTypeFunction id, OpFunction id)
    std::unordered_map<std::string, std::pair<uint32_t,uint32_t>> helpers;

    // 发射单个辅助函数（百差Prog里第一次用到时惰性发射）
    void emitHelper(const Decl& d) {
        if (helpers.count(d.name)) return;
        // 返回类型
        uint32_t retT = d.structCommon.type
            ? typeIdOf(*d.structCommon.type, d.line) : b.tVoid();
        // 参数类型
        std::vector<uint32_t> paramTypes;
        for (const auto& f : d.structCommon.fields)
            paramTypes.push_back(typeIdOf(f.type, f.line));
        // OpTypeFunction(retT, params...) —— 按签名（返回+参数类型 id）键控去重：
        // 同签名多函数只允许一个 OpTypeFunction（spirv-val 禁止重复非聚合类型）。
        std::string ftKey = "hfn_" + std::to_string(retT);
        for (uint32_t p : paramTypes) ftKey += "_" + std::to_string(p);
        uint32_t fnT = b.type(ftKey, [&]{
            uint32_t r = b.id();
            std::vector<uint32_t> ops = {r, retT};
            ops.insert(ops.end(), paramTypes.begin(), paramTypes.end());
            putV(b.secTypes, OpTypeFunction, ops);
            return r;
        });
        uint32_t fnId = b.id();
        b.name(fnId, d.name);
        helpers[d.name] = {fnT, fnId};
        // 内嵌发射：保存/恢复主入口状态
        auto savedVars = vars; auto savedBody = std::move(body);
        auto savedFV = std::move(funcVars); bool savedTerm = terminated;
        auto savedLoops = loops;
        uint32_t savedSOV = scalarOutVar, savedSOT = scalarOutType;
        vars.clear(); body.clear(); funcVars.clear(); terminated = false; loops.clear();
        // 参数设为 Function 局部变量（入口处 OpVariable + 写入）
        for (size_t i = 0; i < d.structCommon.fields.size(); i++) {
            const auto& f = d.structCommon.fields[i];
            uint32_t pt = paramTypes[i];
            uint32_t pptr = b.tPtr(ScFunction, pt);
            uint32_t pv = b.id();
            put(funcVars, OpVariable, {pptr, pv, ScFunction});
            b.name(pv, f.name);
            vars[f.name] = {pv, pt, ScFunction};
        }
        scalarOutVar = 0; scalarOutType = 0;  // 辅助函数不用 I/O
        // 如果辅助函数有返回值，return e → OpReturnValue
        for (const auto& s : d.body) stmt(s.get(), 0);
        if (!terminated) {
            if (retT == b.tVoid()) { put(body, OpReturn, {}); }
            else {
                // 这里不应到达（所有路径应已 return），不过保安全
                uint32_t zero = (b.info(retT).k == TI::F32) ? b.constF(0.0f) : b.constI(0);
                put(body, OpReturnValue, {zero});
            }
            terminated = true;
        }
        uint32_t entryL = b.id();
        put(b.secFunc, OpFunction, {retT, fnId, 0, fnT});
        put(b.secFunc, OpLabel, {entryL});
        b.secFunc.insert(b.secFunc.end(), funcVars.begin(), funcVars.end());
        b.secFunc.insert(b.secFunc.end(), body.begin(), body.end());
        put(b.secFunc, OpFunctionEnd, {});
        // 恢复
        vars = std::move(savedVars); body = std::move(savedBody);
        funcVars = std::move(savedFV); terminated = savedTerm; loops = std::move(savedLoops);
        scalarOutVar = savedSOV; scalarOutType = savedSOT;
    }

    // 带参数考虑的辅助函数返回类型（俩山1个参数的返回类型）
    uint32_t helperRetType(const std::string& name) {
        for (const auto& d : prog.decls)
            if (d && d->kind == Decl::FuncD && d->shaderStage == ShaderStage::None
                && d->name == name && d->structCommon.type)
                return typeIdOf(*d->structCommon.type, d->line);
        return b.tVoid();
    }
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
        // b.glsl450 已在 Builder() 构造时预分配为 id 1，无需再次分配
        setupResources();
        setupIO();
        if (scalarOutVar) {
            scalarOutType = typeIdOf(*stage.structCommon.type, stage.line);
        }

        // 全局 let 常量（编译期可折叠的顶层 let x = ...）作为 Function 变量，
        // 在 setupIO 之后、辅助函数之前注入，全阶段可见。
        // 尾缀 `spec N` 的 let → OpSpecConstant + SpecId 装饰（管线创建期可覆写）。
        for (const auto& d : prog.decls) {
            if (!d || d->kind != Decl::LetD) continue;
            for (const auto& f : d->structCommon.fields) {
                if (!f.init) continue;
                uint32_t cid = 0, typeId = 0;
                if (f.shaderAttr && f.shaderAttr->specId >= 0) {
                    // 特化常量：初值须为数值字面量（可带负号），类型 i4/u4/f4 标量
                    const Expr* ie = f.init.get();
                    bool neg = ie->kind == Expr::Unary && ie->op == "-" && ie->a;
                    const Expr* lit = neg ? ie->a.get() : ie;
                    if (lit->kind != Expr::IntLit && lit->kind != Expr::FloatLit)
                        err("spec 特化常量初值须为数值字面量", f.line);
                    ScType st{TI::I32, 0};
                    if (!f.type.name.empty()) {
                        if (!scTypeOf(f.type.name, st) || st.k == TI::Vec ||
                            st.k == TI::Mat || st.k == TI::Bool || st.bits != 32)
                            err("spec 特化常量仅支持 i4/u4/f4 标量（32 位）", f.line);
                    } else {
                        st.k = lit->kind == Expr::FloatLit ? TI::F32 : TI::I32;
                    }
                    uint32_t word = 0;
                    if (st.k == TI::F32) {
                        float v = lit->kind == Expr::FloatLit
                                ? std::strtof(lit->text.c_str(), nullptr)
                                : (float)std::strtol(lit->text.c_str(), nullptr, 0);
                        if (neg) v = -v;
                        std::memcpy(&word, &v, 4);
                        typeId = b.tF32();
                    } else {
                        int32_t v = (int32_t)std::strtol(lit->text.c_str(), nullptr, 0);
                        if (neg) v = -v;
                        word = (uint32_t)v;
                        typeId = st.k == TI::U32 ? b.tU32() : b.tI32();
                    }
                    cid = b.id();
                    put(b.secTypes, OpSpecConstant, {typeId, cid, word});
                    put(b.secDeco, OpDecorate, {cid, DecSpecId, (uint32_t)f.shaderAttr->specId});
                    b.name(cid, f.name);   // 名字进反汇编→SPIRV-Cross 的 function_constant 同名
                } else {
                    // P3 窄/宽标量 let（f2/i8/u8/i1/u1/i2/u2）：需位宽正确的常量初值
                    //（OpVariable 初值须为同型常量，不能插转换指令）
                    ScType stl{TI::I32, 0, 32};
                    bool narrow = !f.type.name.empty() && scTypeOf(f.type.name, stl) &&
                                  stl.k != TI::Vec && stl.k != TI::Mat &&
                                  stl.k != TI::Bool && stl.bits != 32;
                    if (narrow) {
                        const Expr* ie = f.init.get();
                        bool neg = ie->kind == Expr::Unary && ie->op == "-" && ie->a;
                        const Expr* lit = neg ? ie->a.get() : ie;
                        if (lit->kind != Expr::IntLit && lit->kind != Expr::FloatLit)
                            err("窄/宽标量 let 初值须为数值字面量", f.line);
                        typeId = scalarTid(stl);
                        if (stl.k == TI::F32) {          // f2
                            float fv = lit->kind == Expr::FloatLit
                                     ? std::strtof(lit->text.c_str(), nullptr)
                                     : (float)std::strtol(lit->text.c_str(), nullptr, 0);
                            if (neg) fv = -fv;
                            cid = b.constF16(fv);
                        } else if (stl.bits == 64) {     // i8/u8
                            long long lv = std::strtoll(lit->text.c_str(), nullptr, 0);
                            if (neg) lv = -lv;
                            cid = b.const64(typeId, (uint64_t)lv);
                        } else {                          // i1/u1/i2/u2（低位入 word）
                            long lv = std::strtol(lit->text.c_str(), nullptr, 0);
                            if (neg) lv = -lv;
                            uint32_t mask = stl.bits == 8 ? 0xFFu : 0xFFFFu;
                            cid = b.constScalar(typeId, (uint32_t)lv & mask);
                        }
                    } else {
                        cid = constExpr(f.init.get());
                        if (!cid) continue;
                        typeId = f.type.name.empty()
                            ? (b.info(cid).k == TI::F32 ? b.tF32() : b.tI32())
                            : typeIdOf(f.type, f.line);
                    }
                }
                uint32_t ptrT = b.tPtr(ScFunction, typeId);
                uint32_t v = b.id();
                put(funcVars, OpVariable, {ptrT, v, ScFunction, cid});
                b.name(v, f.name);
                vars[f.name] = {v, typeId, ScFunction};
            }
        }
        for (const auto& d : prog.decls)
            if (d && d->kind == Decl::FuncD && d->shaderStage == ShaderStage::None)
                emitHelper(*d);

        uint32_t tv = b.tVoid();
        uint32_t fnT = b.type("fn_void", [&]{
            uint32_t r = b.id();
            put(b.secTypes, OpTypeFunction, {r, tv});
            return r;
        });
        uint32_t fnId = b.id();
        b.name(fnId, "main");
        uint32_t entryL = b.id();

        // 辅助函数的 return 需发 OpReturnValue
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
        if (stage.shaderStage == ShaderStage::Comp) {
            // 工作组尺寸：签名尾 `local X [Y [Z]]` 声明值，未声明默认 64×1×1。
            uint32_t lx = 64, ly = 1, lz = 1;
            if (stage.shaderAttr && stage.shaderAttr->local[0] > 0) {
                lx = (uint32_t)stage.shaderAttr->local[0];
                ly = (uint32_t)stage.shaderAttr->local[1];
                lz = (uint32_t)stage.shaderAttr->local[2];
            }
            put(b.secExec, OpExecutionMode, {fnId, 17 /*LocalSize*/, lx, ly, lz});
        }
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


