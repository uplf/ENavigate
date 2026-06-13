#!/bin/bash
# start_api.sh — 启动/停止所有 FastCGI 接口进程
#
# 用法：
#   ./start_api.sh          # 启动
#   ./start_api.sh stop     # 停止（kill 进程 + 清理 socket）
#   ./start_api.sh restart  # 重启

set -e

API_DIR="$(cd "$(dirname "$0")" && pwd)"
SOCK_DIR="/run/agv"
PID_DIR="/run/agv"
LOG_DIR="/var/log/agv"
PROC_NAMES=("Status" "Task" "Topo" "Capture" "Upload")

mkdir -p "$SOCK_DIR" "$LOG_DIR"

start() {
    echo "[start_api] starting FastCGI processes..."

    for name in "${PROC_NAMES[@]}"; do
        sock="$SOCK_DIR/$(echo "$name" | tr 'A-Z' 'a-z').sock"
        pidfile="$PID_DIR/$(echo "$name" | tr 'A-Z' 'a-z').pid"
        logfile="$LOG_DIR/$(echo "$name" | tr 'A-Z' 'a-z').log"

        spawn-fcgi \
            -s "$sock" \
            -P "$pidfile" \
            -F 1 \
            -- "$API_DIR/$name" >> "$logfile" 2>&1

        echo "[start_api] started $name  pid=$(cat "$pidfile" 2>/dev/null)  sock=$sock"
    done

    sleep 0.5
    chmod 666 "$SOCK_DIR"/*.sock 2>/dev/null || true
    echo "[start_api] all started."
}

stop() {
    echo "[start_api] stopping..."
    for name in "${PROC_NAMES[@]}"; do
        pidfile="$PID_DIR/$(echo "$name" | tr 'A-Z' 'a-z').pid"
        if [ -f "$pidfile" ]; then
            pid=$(cat "$pidfile")
            kill "$pid" 2>/dev/null && echo "[start_api] killed $name (pid=$pid)"
            rm -f "$pidfile"
        else
            # 没有 pid 文件，根据进程名强杀
            pids=$(pgrep -x "$name" 2>/dev/null || true)
            if [ -n "$pids" ]; then
                echo "[start_api] killing $name (pid=$pids) by name"
                kill $pids 2>/dev/null
            fi
        fi
    done
    rm -f "$SOCK_DIR"/*.sock
    echo "[start_api] stopped."
}

case "${1:-start}" in
    start)   start   ;;
    stop)    stop    ;;
    restart) stop; sleep 1; start ;;
    *) echo "usage: $0 {start|stop|restart}"; exit 1 ;;
esac
