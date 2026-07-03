#!/usr/bin/env bash
# 运行 demo，指定 MoltenVK 作为 Vulkan 驱动，并让 glfw 找到 loader。
set -euo pipefail
cd "$(dirname "$0")"
BP=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
export VK_ICD_FILENAMES="$BP/etc/vulkan/icd.d/MoltenVK_icd.json"
export DYLD_FALLBACK_LIBRARY_PATH="$BP/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
exec ./build/tri "$@"
