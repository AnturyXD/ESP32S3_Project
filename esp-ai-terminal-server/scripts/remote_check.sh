#!/usr/bin/env bash
set -euo pipefail

# 服务器只读检查脚本。
# 本脚本不安装、不创建、不启动、不停止任何服务，只帮助人工确认环境是否仍符合 S0.1 审计结论。

echo "[check] 当前用户"
whoami

echo "[check] 系统版本"
cat /etc/os-release | sed -n '1,6p'

echo "[check] Python / pip / venv"
python3 --version || true
pip3 --version || true
python3 -m venv --help >/dev/null && echo "python venv: available" || echo "python venv: unavailable"

echo "[check] 端口 18080 是否监听"
ss -ltnup | grep -E ':(18080)\b' || echo "18080 not listening"

echo "[check] 必须避让的 Carshow 端口"
ss -ltnup | grep -E ':(3000|8080|8189|8888|8889|1935|9997)\b' || true

echo "[check] 候选项目目录"
ls -lah /home/ubuntu/esp-ai-terminal-server 2>/dev/null || echo "project dir not found"
