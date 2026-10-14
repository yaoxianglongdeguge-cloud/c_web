#!/usr/bin/env bash
# c_web 网站集成测试（黑盒）：
#   1) 路由功能：静态/动态文本、查询串回显、POST 回显、405、404、256KB 内存响应
#   2) 10MB 大文件下载：状态码 / Content-Length / 字节数 / 内容逐字节一致 / 速度
#   3) 重复下载与 4 路并发下载（验证 sendfile 分包续发与状态回收）
# 用法：bash test/website_test.sh      （端口可用 PORT=xxxx 覆盖）
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-5343}"
HOST="127.0.0.1"
BASE="http://$HOST:$PORT"
BIG="$ROOT/WebSite/static/10mb.bin"
BIG_SIZE=10485760

WORK="$(mktemp -d)"
SERVER_PID=""
PASS=0
FAIL=0

pass() { PASS=$((PASS + 1)); printf '  [PASS] %s\n' "$1"; }
fail() { FAIL=$((FAIL + 1)); printf '  [FAIL] %s\n' "$1"; }
check_eq() { # 期望 实际 描述
    if [ "$1" = "$2" ]; then pass "$3（$2）"; else fail "$3：期望 [$1] 实际 [$2]"; fi
}
cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
        wait "$SERVER_PID" 2>/dev/null
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

printf '== 0.构建 ==\n'
make -C "$ROOT" -s >/dev/null || { printf '构建失败\n'; exit 1; }

if [ ! -s "$BIG" ]; then
    printf '生成 10MB 测试文件...\n'
    yes 'c_web 10MB download test line 0123456789abcdefghijklmnopqrstuvwxyz' | head -c "$BIG_SIZE" > "$BIG"
fi
check_eq "$(stat -c%s "$BIG")" "$BIG_SIZE" "10MB 测试文件大小（$BIG）"

printf '== 1.启动服务器 ==\n'
cd "$ROOT" || exit 1
./main > "$WORK/server.log" 2>&1 &
SERVER_PID=$!

ready=0
for _ in $(seq 1 40); do
    if curl -s -m 1 -o /dev/null "$BASE/hello" 2>/dev/null; then ready=1; break; fi
    sleep 0.25
done
if [ "$ready" != "1" ]; then
    printf '  [FAIL] 服务器未能就绪\n'
    cat "$WORK/server.log"
    exit 1
fi
pass "服务器已在 $BASE 就绪（pid $SERVER_PID）"

printf '== 2.路由功能 ==\n'
code=$(curl -s -m 5 -o "$WORK/index.html" -w '%{http_code}' "$BASE/")
check_eq "200" "$code" "GET / 状态码"
grep -q 'c_web is running' "$WORK/index.html" && pass "/ 返回包含 c_web is running 的首页" \
    || fail "/ 首页内容异常：$(head -c 200 "$WORK/index.html")"

ct=$(curl -s -m 5 -o /dev/null -D - "$BASE/" | grep -i '^content-type:' | tr -d '\r' | head -1)
case "$ct" in
    *text/html*) pass "首页 Content-Type 为 text/html" ;;
    *) fail "首页 Content-Type 异常：[${ct:-无}]" ;;
esac

code=$(curl -s -m 5 -o "$WORK/hello.txt" -w '%{http_code}' "$BASE/hello")
check_eq "200" "$code" "GET /hello 状态码"
check_eq "Hello, c_web!" "$(cat "$WORK/hello.txt")" "/hello 响应体"

code=$(curl -s -m 5 -o "$WORK/time.txt" -w '%{http_code}' "$BASE/time")
check_eq "200" "$code" "GET /time 状态码"
if grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}' "$WORK/time.txt"; then
    pass "/time 返回真实时间（$(cat "$WORK/time.txt")）"
else
    fail "/time 内容异常：$(cat "$WORK/time.txt")"
fi

code=$(curl -s -m 5 -o "$WORK/echo.json" -w '%{http_code}' "$BASE/echo?name=cline&msg=hello")
check_eq "200" "$code" "GET /echo 状态码"
if grep -q '"name":"cline"' "$WORK/echo.json" && grep -q '"msg":"hello"' "$WORK/echo.json"; then
    pass "查询串回显正确（$(cat "$WORK/echo.json")）"
else
    fail "/echo 回显异常：$(cat "$WORK/echo.json")"
fi

code=$(curl -s -m 5 -o "$WORK/post.json" -w '%{http_code}' -X POST --data-binary 'ping-body' "$BASE/api/echo")
check_eq "200" "$code" "POST /api/echo 状态码"
grep -q '"body":"ping-body"' "$WORK/post.json" && pass "POST 请求体回显正确" \
    || fail "POST 回显异常：$(cat "$WORK/post.json")"

code=$(curl -s -m 5 -o /dev/null -w '%{http_code}' "$BASE/api/echo")
check_eq "405" "$code" "GET /api/echo → 405（仅允许 POST）"

code=$(curl -s -m 5 -o "$WORK/404.txt" -w '%{http_code}' "$BASE/no-such-route")
check_eq "404" "$code" "未注册路由 → 404"
check_eq "Not Found" "$(cat "$WORK/404.txt")" "404 响应体"

res=$(curl -s -m 10 -o "$WORK/mem.txt" -w '%{http_code} %{size_download}' "$BASE/mem/256k")
check_eq "200 262144" "$res" "256KB 内存响应（状态码 + 下载字节数）"
check_eq "262144" "$(stat -c%s "$WORK/mem.txt")" "256KB 响应实际落盘大小"

printf '== 3.10MB 大文件下载 ==\n'
out=$(curl -sS -m 60 -o "$WORK/dl1.bin" -w '%{http_code} %{size_download} %{speed_download} %{time_total}' "$BASE/download/10mb")
set -- $out
check_eq "200" "$1" "下载状态码"
check_eq "$BIG_SIZE" "$2" "下载字节数（10MiB = 10485760）"
pass "单次下载耗时 $4 s，速度 $(awk -v s="$3" 'BEGIN{printf "%.1f", s/1048576}') MB/s"

if cmp -s "$BIG" "$WORK/dl1.bin"; then
    pass "下载内容与源文件逐字节一致（sha256 $(sha256sum "$BIG" | cut -c1-16)…）"
else
    fail "下载内容与源文件不一致（实际 sha256 $(sha256sum "$WORK/dl1.bin" | cut -d' ' -f1)）"
fi

cl=$(curl -s -m 60 -o /dev/null -D - "$BASE/download/10mb" | grep -i '^content-length:' | tr -d '\r' | awk '{print $2}')
check_eq "$BIG_SIZE" "$cl" "Content-Length 响应头（框架由 fstat 自动填充）"

out2=$(curl -sS -m 60 -o "$WORK/dl2.bin" -w '%{http_code} %{size_download}' "$BASE/download/10mb")
check_eq "200 $BIG_SIZE" "$out2" "第二次下载仍完整（验证发送暂存池回收）"
cmp -s "$BIG" "$WORK/dl2.bin" && pass "第二次下载内容一致" || fail "第二次下载内容不一致"

printf '== 4.并发下载（4 路 × 10MB） ==\n'
t0=$(date +%s.%N)
cpids=""
for i in 1 2 3 4; do
    curl -sS -m 120 -o "$WORK/c$i.bin" "$BASE/download/10mb" &
    cpids="$cpids $!"
done
cok=1
for p in $cpids; do      # 只等这 4 个 curl，注意不能用 jobs -p（会把常驻的服务器进程也等进去）
    wait "$p" || cok=0
done
t1=$(date +%s.%N)
for i in 1 2 3 4; do
    cmp -s "$BIG" "$WORK/c$i.bin" || cok=0
done
if [ "$cok" = "1" ]; then
    pass "4 路并发 10MB 下载全部完整且内容一致"
else
    fail "并发下载存在失败或内容损坏"
fi
awk -v a="$t0" -v b="$t1" 'BEGIN{printf "  (4×10MB 并发耗时 %.2f s，总吞吐约 %.1f MB/s)\n", b-a, 40/(b-a)}'

printf '== 5.收尾检查 ==\n'
if kill -0 "$SERVER_PID" 2>/dev/null; then
    pass "全部请求结束后服务器进程仍存活（无崩溃）"
else
    fail "服务器进程已退出（疑似崩溃）"
fi
if [ -s "$WORK/server.log" ]; then
    printf '  --- 服务器输出 ---\n'
    sed 's/^/  /' "$WORK/server.log"
fi

printf '\n==== 网站测试结果：PASS %d / FAIL %d ====\n' "$PASS" "$FAIL"
printf '提示：单独跑大文件下载可用 curl -o /dev/null -w "%%{size_download} %%{speed_download}\\n" %s/download/10mb\n' "$BASE"
if [ "$FAIL" != "0" ]; then exit 1; fi
