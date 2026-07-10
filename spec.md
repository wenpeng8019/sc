# spec —— 编译期特化维度系统（设计稿）

> 目标：同一份 .ss 源码，按有限枚举维度在**编译期**展开为多个单态实例
> （monomorphization），消除运行时分支与源码复制。典型驱动场景：
> samplerExternalOES vs sampler2D（Android 外置纹理）、精度档位、算法变体。
> 每个实例都是完整、独立、可 spirv-val 验证的产物。

## 1. 核心概念：维度（dimension）

一个 spec 声明 = 一个编译期枚举维度。**枚举是唯一形态**（无 bool 糖，
布尔场景写二值枚举即可）。维度取值集合必须是编译期有限字面集合。

```
spec TEX_2D in [sampler2D, samplerExternalOES]   # 类型枚举维度
spec ABC    in [A, B, C]                          # 符号枚举维度
spec FAST   in [off, on]                          # 布尔场景 = 二值枚举
```

## 2. 首选形态：类型直代（维度值即类型）

维度名可直接出现在类型位置——这是本机制的主形态：一处定义，取值分身。
比条件块少一份重复、天然保证各实例结构同构（字段名/布局/绑定一致）。

```
spec TEX_2D in [sampler2D, samplerExternalOES]

@def TexBlock: {
    tex: TEX_2D                 # 实例化时替换为具体 sampler 类型
} uniform set 0 binding 0

frag fs_main: vec4, in: VsOut
    return texture(TexBlock.tex, in.uv)   # 同一行，各实例类型都成立
```

## 3. 兜底形态：有体 spec（代码分支）

当差异不是"一个类型能表达"的（不同算法路径、不同资源集合），用**有体
spec**：声明与分支合一——spec 带体时，分支标签即维度取值集合，不再单独
写 `in [...]`。无体 spec = 纯维度（类型直代用），有体 spec = 维度 + 分支。

```
spec CASE_BLOCK:
    CASE1:
        .....                   # 该实例专属的声明/语句
    CASE2:
        .....
```

等价于维度 `CASE_BLOCK in [CASE1, CASE2]`，每个实例按取值展开对应分支体。
穷尽性天然满足（标签即全集）；use 表中可直接引用其标签。

约束：分支体内定义的同名 @def 必须**接口同构**（对外可见字段集合一致），
否则消费方（vert/frag 函数体）无法写出对所有实例都成立的代码。

## 4. 实例选择：use 白名单

默认 = 所有维度的**全笛卡尔积**输出。声明 use 后切换为白名单模式，
只输出显式列出的组合——这是抑制组合爆炸、表达"合法搭配"的机制。

```
use TEX_2D, ABC, CASE_BLOCK
    sampler2D,          A, CASE1
    samplerExternalOES, C, CASE2
```

- 表头行声明维度顺序，数据行每行一个实例（各列取该维度的一个值）。
- 未在 use 中出现的维度仍取全集（与已列维度做积）。
- 编译器校验：值必须属于对应维度的取值集合；重复行报错。

## 5. 实例化模型与产物

- 展开发生在 AST 层（parse 后、codegen 前）：每个有效组合克隆一份
  shader 单元，维度名替换为具体值，再走既有 SPIR-V/GLSL/MSL 管线。
- 产物命名：`<stem>.<入口>.<维度值串>`（如 fs_main.samplerExternalOES.C.CASE2），
  shader.h 的 enum id 与 `sc_shader_<sym>_get(i)` 条目按实例展开。
- 反射 JSON 每实例一份，附加 `spec:{维度:值,...}` 字段，供运行时按
  设备能力选取实例（如 Android 检测到 OES 需求时取 external 实例）。

## 6. 检查规则

1. 维度取值必须是有限字面集合；类型维度的值必须是合法类型名。
2. 有体 spec 分支体内同名 @def 接口同构。
3. use 行的值域校验、去重；空 use 表报错。
4. 无 use 时若组合数超过阈值（如 32）告警，强制显式确认。
5. 每个实例独立过全部现有验证链（spirv-val / spirv-cross / metal）。

## 7. 分期落地

- **P1（已实现）**：单/多维度 + 类型直代（§2）+ 产物/反射展开（§5）。
- **P2（已实现）**：use 白名单（§4）+ 组合校验（§6.3/6.4）。
- **P3（已实现）**：有体 spec（§3）+ 接口同构检查（§6.2）。
- **远期**：需求驱动实例化（由 gfx/spc 消费端声明所需组合，反向裁剪），
  以及维度约束表达式（如 `when TEX_2D == samplerExternalOES require gles`）。

---
## 附：示例全貌

```
tar metal@2.0
tar gles@300

spec TEX_2D in [sampler2D, samplerExternalOES]

@def TexBlock: {
    tex: TEX_2D
} uniform set 0 binding 0

@def VsOut: {
    clip: vec4 builtin position
    uv:   vec2
}

vert vs_main: VsOut, ...
    ...

frag fs_main: vec4, in: VsOut
    return texture(TexBlock.tex, in.uv)
```