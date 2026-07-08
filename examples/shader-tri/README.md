# syntax-s demo：GLSL → SPIR-V → MoltenVK 三角形

用 sc 的 GPU 方言（`.ss`，见 [`syntax-s.md`](../../syntax-s.md)）写一个三角形着色器，
经 `scc` 生成 Vulkan-GLSL，再由 `glslangValidator` 编译成 SPIR-V，最后用一个
极简的 Vulkan host（经 MoltenVK 跑在 Metal 上）绘制出来。

```
tri.ss ──scc──▶ vs_main.vert / fs_main.frag ──glslangValidator──▶ *.spv ──▶ main.c(Vulkan/MoltenVK)
                (+ tri.reflect.json 反射清单)
```

着色器里三个顶点的位置与颜色写在常量数组里、由内建 `vertex_id` 索引，
所以 host 不含任何着色器逻辑，只需一次空的 `draw(3)`。

## 依赖

### macOS / Homebrew

```sh
brew install glslang glfw molten-vk vulkan-headers vulkan-loader
```

### Linux / WSL(Ubuntu)

```sh
sudo apt update
sudo apt install -y glslang-tools libglfw3-dev libvulkan-dev pkg-config
```

WSL 下建议额外安装 `vulkan-tools` 用于排查驱动/ICD：

```sh
sudo apt install -y vulkan-tools
vulkaninfo --summary
```

若输出里只看到 `llvmpipe/lavapipe`，说明当前是软件 Vulkan；若看到硬件或 dzn 路径，
则可继续做 WSLg + DX/Vulkan 联调。

## 构建 + 运行

```sh
./build.sh     # scc → GLSL → SPIR-V → 编译 host（产物在 build/）
./run.sh       # 指定 MoltenVK 驱动并运行，弹出窗口显示彩色三角形
```

`run.sh` 在 macOS 下会设置两个环境变量后运行 `build/tri`：

- `VK_ICD_FILENAMES` → MoltenVK 的 ICD 清单（否则 loader 可能选中软件驱动 lavapipe）。
- `DYLD_FALLBACK_LIBRARY_PATH` → 让 glfw 在运行时找到 Vulkan loader（`libvulkan.dylib`）。

Linux 下 `run.sh` 直接运行 `build/tri`。

也可直接指定 SPIR-V 路径：`./build/tri path/to/vs.spv path/to/fs.spv`。

## 文件

| 文件 | 说明 |
| --- | --- |
| `tri.ss` | 着色器源（sc GPU 方言） |
| `main.c` | 极简 Vulkan/MoltenVK host（实例→设备→交换链→管线→渲染循环） |
| `build.sh` | 三步构建管线 |
| `run.sh` | 选定 MoltenVK 驱动并运行 |
| `build/` | 生成物（GLSL、SPIR-V、反射清单、可执行文件），不入库 |

## 说明

- 这是 syntax-s 一期的运行时验证：证明 `scc` 产出的 GLSL 能一路走到 GPU。
  一期本身零着色器库依赖，SPIR-V 之后的环节（glslang / MoltenVK）全部是开源现成件。
- `tri.reflect.json` 是配套的反射清单（阶段 I/O、location、资源绑定与 std140 偏移），
  供运行时构建管线布局/描述符使用；本 demo 无资源，故未用到。
