# shader-tri-metal —— syntax-g 发行路径（SPIR-V → MSL）

与 [`../shader-tri`](../shader-tri) 是同一个三角形着色器（共用其 `tri.sg`），但走**发行侧的离线转译链**，直接产出**原生 Metal** 着色库，而不是靠 MoltenVK 在运行时翻译 Vulkan。

## 两条路径的区别

| | `../shader-tri`（开发路径） | 本例 `shader-tri-metal`（发行路径） |
|---|---|---|
| Metal 落地 | MoltenVK 运行时翻译 Vulkan→Metal | **离线** SPIRV-Cross 转译为 MSL |
| 运行时依赖 | Vulkan loader + MoltenVK | 仅 Metal（系统自带） |
| 着色器产物 | `.spv`（喂 Vulkan） | `.metallib`（喂 Metal） |
| 定位 | 开发期快速验证 | 发行、瘦运行时 |

## 编译链

```
scc tri.sg  → vs_main.vert / fs_main.frag   (Vulkan-GLSL)
  → glslangValidator → *.spv                (SPIR-V，中枢 IR)
  → spirv-cross --msl → *.metal             (Metal Shading Language)
  → xcrun metal -c    → *.air               (Apple IR)
  → xcrun metallib    → tri.metallib
```

SPIR-V 作为中枢 IR：`tri.sg` 声明 `tar vulkan@450`，先出 Vulkan-GLSL 再编成 SPIR-V，
SPIRV-Cross 由 SPIR-V 反向生成 MSL。入口 `main` 按阶段重命名为 `vs_main` / `fs_main`，
既避免多阶段链接进同一 `metallib` 时函数名冲突，也方便 host 按名 `newFunctionWithName:` 查找。

## 构建

```sh
brew install glslang spirv-cross      # + Xcode / Command Line Tools（提供 xcrun metal）
./build.sh
```

产物在 `build/`：`*.metal`（可读 MSL）、`*.air`、`tri.metallib`。

## 里程碑

- **A（本例，已完成）**：打通 `.sg → … → .metallib` 发行链，验证 MSL 合法且可编译。
- **B（下一步）**：原生 Metal 渲染 host（Objective-C / metal-cpp），加载 `tri.metallib`
  实机画出三角形——即「直接成为 sc 后端」的最小实证。
