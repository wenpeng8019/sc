#!/usr/bin/env bash
# ============================================================
# Linux / WSL 平台适配验证脚本
#
# 目标：
# 1) scc 在 Linux 上构建 + 回归验证
# 2) builtins 相关库（gpu/spc）在目标平台的构建可用性验证
# 3) GPU 路径（Vulkan）工具链可用性检查，及 shader-tri 构建验证
#
# 用法：
#   tests/linux_wsl_validate.sh
#   tests/linux_wsl_validate.sh --quick
#   tests/linux_wsl_validate.sh --with-shader-tri --with-vulkaninfo
#   tests/linux_wsl_validate.sh --skip-regression
# ============================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

RUN_REGRESSION=1
RUN_SHADER_TRI=0
RUN_VULKANINFO=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --quick)
            RUN_REGRESSION=0
            ;;
        --skip-regression)
            RUN_REGRESSION=0
            ;;
        --with-shader-tri)
            RUN_SHADER_TRI=1
            ;;
        --with-vulkaninfo)
            RUN_VULKANINFO=1
            ;;
        -h|--help)
            cat <<'EOF'
用法: tests/linux_wsl_validate.sh [选项]

选项:
  --quick             快速模式（跳过 tests/run.sh 快照回归）
  --skip-regression   跳过回归（同 --quick）
  --with-shader-tri   额外构建 examples/shader-tri
  --with-vulkaninfo   额外执行 vulkaninfo 摘要检查
  -h, --help          显示帮助
EOF
            exit 0
            ;;
        *)
            echo "未知参数: $1" >&2
            exit 1
            ;;
    esac
    shift
done

echo "==> [env] 平台信息"
uname -a
if grep -qi microsoft /proc/version 2>/dev/null; then
    echo "检测到 WSL 环境"
else
    echo "检测到原生 Linux 环境"
fi

echo "==> [1/5] 构建 scc"
./build.sh build

echo "==> [2/5] 语言特性与黄金回归"
if [[ "$RUN_REGRESSION" == "1" ]]; then
    bash "$ROOT/tests/run.sh"
else
    echo "跳过 tests/run.sh（quick 模式）"
fi

echo "==> [3/5] builtins 相关库构建：gpu"
(
    cd "$ROOT/templates/utils/gpu"
    ./build.sh
)

echo "==> [4/5] builtins 相关库构建：spc"
(
    cd "$ROOT/templates/utils/spc"
    ./build.sh
)

if [[ "$RUN_VULKANINFO" == "1" ]]; then
    echo "==> [5/5] Vulkan 工具链摘要"
    if command -v vulkaninfo >/dev/null 2>&1; then
        vulkaninfo --summary || {
            echo "警告: vulkaninfo 执行失败（可能未配置 ICD/GPU 驱动）" >&2
        }
    else
        echo "警告: 未安装 vulkaninfo（Ubuntu 可安装 vulkan-tools）" >&2
    fi
fi

if [[ "$RUN_SHADER_TRI" == "1" ]]; then
    echo "==> [extra] shader-tri 构建验证"
    (
        cd "$ROOT/examples/shader-tri"
        ./build.sh
    )
fi

echo "==> 完成：Linux/WSL 平台基线验证通过"
