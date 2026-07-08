#!/usr/bin/env bash
# 运行 demo。macOS 下指定 MoltenVK；Linux 直接运行。
set -euo pipefail
cd "$(dirname "$0")"

OS="$(uname -s)"
if [[ "$OS" == "Darwin" ]]; then
	BP=$(brew --prefix 2>/dev/null || echo /opt/homebrew)
	export VK_ICD_FILENAMES="$BP/etc/vulkan/icd.d/MoltenVK_icd.json"
	export DYLD_FALLBACK_LIBRARY_PATH="$BP/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}"
fi

exec ./build/tri "$@"
