#!/usr/bin/env bash
set -euo pipefail

# 停止说明脚本。
# S0.4 使用前台运行，推荐在运行 uvicorn 的终端按 Ctrl+C 停止。
# 本脚本不执行 kill，避免误杀服务器上的其他 Python 或业务进程。

echo "如果服务以前台方式运行，请在运行 uvicorn 的终端按 Ctrl+C。"
echo "下面只读检查 18080 是否仍有监听："
ss -ltnup | grep -E ':(18080)\b' || echo "18080 not listening"

