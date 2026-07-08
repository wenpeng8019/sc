已按你的两个目标先落地了一版“Linux/WSL 可执行适配基线”，并把 GPU 路径推进到可直接验证。

本次已完成

shader-tri 支持 Linux/WSL 构建与运行
构建脚本现在按系统分支：
macOS 继续走 Homebrew
Linux/WSL 改为 pkg-config 解析 glfw3/vulkan
运行脚本现在按系统分支：
macOS 才注入 MoltenVK 环境变量
Linux 直接运行
文件：
build.sh
run.sh
README.md
修复 gpu/spc 交叉构建脚本的 target 显式标记问题
之前传 --target 也会错误地更新无后缀软链
现在显式 target 时不再覆盖宿主软链
文件：
build.sh
build.sh
新增 Linux/WSL 平台统一验证脚本
覆盖：
scc 构建
回归（可 quick 跳过）
builtins 相关库 gpu/spc 构建
可选 Vulkan 工具链摘要检查
可选 shader-tri 构建验证
文件：
linux_wsl_validate.sh
文档入口已补到 README.md
建议你现在在本机执行

快速基线（先跑通）
bash linux_wsl_validate.sh --quick

完整回归 + Vulkan + shader-tri
bash linux_wsl_validate.sh --with-vulkaninfo --with-shader-tri

若要做你提到的 Vulkan + DX(WSL)联调，先看 vulkaninfo --summary 输出里的设备/驱动

出现 llvmpipe/lavapipe：当前为软件路径
出现硬件驱动或 dzn：可继续做 WSLg 的 Vulkan/DX 验证链路
说明

我这边本轮无法直接回传真实运行结果，因为当前工具终端被环境启动脚本噪声拦截（持续出现 xargs/login 异常并截断输出）。
代码改动已通过编辑器静态错误检查，无新增语法/诊断错误。
如果你愿意，我下一步直接做：

把 linux_wsl_validate.sh 扩展成“自动产出平台验收报告（含驱动判定）”
补一版 spc 的 Linux Vulkan 占位后端骨架（先打通 init/dispatch 形态与错误码）
加一条 Windows 远端目标（MSVC/MinGW）到同一验证链，形成统一矩阵。