#!/usr/bin/env bash
set -euo pipefail

# 本地打包脚本。
# 只打包 esp-ai-terminal-server/ 骨架，排除真实 .env、虚拟环境、缓存、日志和编译产物，
# 避免把密钥或本地运行垃圾带到服务器。

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SERVER_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OUTPUT="${SERVER_DIR}/esp-ai-terminal-server.tar.gz"

cd "${SERVER_DIR}/.."
rm -f "${OUTPUT}"

tar \
  --exclude='esp-ai-terminal-server/.env' \
  --exclude='esp-ai-terminal-server/.env.local' \
  --exclude='esp-ai-terminal-server/.env.production' \
  --exclude='esp-ai-terminal-server/.env.development' \
  --exclude='esp-ai-terminal-server/.venv' \
  --exclude='esp-ai-terminal-server/venv' \
  --exclude='esp-ai-terminal-server/**/__pycache__' \
  --exclude='esp-ai-terminal-server/**/*.pyc' \
  --exclude='esp-ai-terminal-server/logs' \
  --exclude='esp-ai-terminal-server/*.log' \
  --exclude='esp-ai-terminal-server/*.tar.gz' \
  -czf "${OUTPUT}" \
  esp-ai-terminal-server

echo "打包完成: ${OUTPUT}"

