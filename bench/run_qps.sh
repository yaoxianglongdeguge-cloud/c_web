#!/usr/bin/env bash
# 一键 QPS 测试
# 用法：
#   make qps OPT="-O2 -march=native"
#   或 bash bench/run_qps.sh [duration] [conns] [groups]
#       ROUTES="fast slow mix" BENCH_SLOW_US=10000 bash bench/run_qps.sh
#       BENCH_GROUPS=16 bash bench/run_qps.sh 2 500 16
# 说明：显式组数用第三个参数或 BENCH_GROUPS（1~32）；不读 CWEB_GROUP_NUM，避免被环境变量污染。
#
# 默认：按 nproc 选组/并发；端口自动挑空闲的 15000+；结束时只 kill 自己启动的 PID。
set -u

ROOT=$(cd "$(dirname "$0")/.." && pwd)
BIN="$ROOT/bench/bench_server"
LOG="/tmp/cweb_qps_$$.log"
TMP="/tmp/cweb_wrk_$$.txt"

DURATION=${1:-2}
NC=$(nproc 2>/dev/null || echo 1)

NGROUPS=${3:-${BENCH_GROUPS:-}}
if [ -z "${NGROUPS}" ]; then
    if [ "$NC" -le 3 ]; then NGROUPS=$NC
    elif [ "$NC" -le 6 ]; then NGROUPS=4
    else NGROUPS=5
    fi
fi
[ "$NGROUPS" -lt 1 ] && NGROUPS=1
[ "$NGROUPS" -gt 32 ] && NGROUPS=32

CONNS=${2:-}
if [ -z "${CONNS}" ]; then
    if [ "$NC" -le 3 ]; then CONNS=300
    else CONNS=500
    fi
fi

THREADS=${BENCH_THREADS:-$NC}
[ "$THREADS" -gt 6 ] && THREADS=6
[ "$THREADS" -lt 1 ] && THREADS=1

ROUTES=${ROUTES:-"fast slow mix"}
PORT=${BENCH_PORT:-$((15000 + $$ % 2000))}

command -v wrk >/dev/null 2>&1 || { echo "缺少 wrk，请先安装：sudo apt install wrk"; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "缺少 curl"; exit 1; }

# 构建 bench_server（对象由根 Makefile 编译；OPT 与根构建保持一致）
make -C "$ROOT" -j"$NC" OPT="${OPT:--O2 -march=native}" bench/bench_server >/dev/null 2>&1 || {
    echo "bench_server 构建失败，请先检查 make 输出"; exit 1;
}

# 找一个空闲端口
while ss -lnt 2>/dev/null | grep -q ":$PORT "; do PORT=$((PORT+1)); done

PID=""
cleanup(){
    if [ -n "${PID}" ]; then
        kill "$PID" 2>/dev/null
        wait "$PID" 2>/dev/null
    fi
    rm -f "$TMP"
}
trap cleanup EXIT INT TERM

( cd "$ROOT" && setsid env CWEB_GROUP_NUM="$NGROUPS" BENCH_PORT="$PORT" "$BIN" >"$LOG" 2>&1 < /dev/null ) &
for _ in $(seq 1 60); do
    curl -sf "http://127.0.0.1:$PORT/fast" >/dev/null 2>&1 && break
    sleep 0.1
done

PID=$(ss -lntp 2>/dev/null | grep ":$PORT " | grep -o 'pid=[0-9]*' | head -1 | cut -d= -f2)
if [ -z "$PID" ]; then
    echo "服务端启动失败，日志：$LOG"
    tail -20 "$LOG" 2>/dev/null
    exit 1
fi

echo "============================================================"
echo " c_web QPS  groups=$NGROUPS conns=$CONNS threads=$THREADS duration=${DURATION}s port=$PORT"
echo " slow=${BENCH_SLOW_US:-10000}us  routes=$ROUTES"
echo "============================================================"
printf '%-8s %12s %10s %10s %8s\n' "route" "QPS" "avg" "p99" "non2xx"

for r in $ROUTES; do
    wrk -t"$THREADS" -c"$CONNS" -d"${DURATION}s" --latency \
        "http://127.0.0.1:$PORT/$r" >"$TMP" 2>&1

    qps=$(awk '/Requests\/sec:/{print $2}' "$TMP" | tail -1)
    avg=$(awk '/Latency/{print $2; exit}' "$TMP")
    p99=$(awk '/99%/{print $2; exit}' "$TMP")
    err=$(awk '/Non-2xx/{print $NF}' "$TMP" | tail -1)
    [ -z "$qps" ] && qps="FAIL"
    [ -z "$avg" ] && avg="-"
    [ -z "$p99" ] && p99="-"
    [ -z "$err" ] && err="0"

    printf '%-8s %12s %10s %10s %8s\n' "$r" "$qps" "$avg" "$p99" "$err"
done

echo "------------------------------------------------------------"
echo "服务端日志：$LOG"
