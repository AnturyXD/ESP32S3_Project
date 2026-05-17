#!/usr/bin/env bash
set -euo pipefail

# 开发期前台运行脚本。
# 该脚本只绑定 127.0.0.1:18080，不后台运行，不写 systemd，不修改 Nginx/UFW/Docker。

HOST="${APP_HOST:-127.0.0.1}"
PORT="${APP_PORT:-18080}"

if [[ "${HOST}" != "127.0.0.1" ]]; then
  echo "安全保护：S0.4 只允许绑定 127.0.0.1，当前 APP_HOST=${HOST}" >&2
  exit 1
fi

if [[ "${PORT}" != "18080" ]]; then
  echo "安全保护：S0.4 默认只允许端口 18080，当前 APP_PORT=${PORT}" >&2
  exit 1
fi

echo "前台启动: uvicorn app.main:app --host ${HOST} --port ${PORT}"
echo "停止方式: 在本终端按 Ctrl+C"
uvicorn app.main:app --host "${HOST}" --port "${PORT}"

