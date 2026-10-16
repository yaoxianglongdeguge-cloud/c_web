#!/usr/bin/env bash
# 一键运行全部单元测试；附加 --website 参数时再跑网站集成测试（含 10MB 大文件下载）
# 用法： ./test/run_tests.sh            仅单元测试
#        ./test/run_tests.sh --website  单元测试 + 网站集成测试
set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"

printf '########## 单元测试 ##########\n'
make -C "$TEST_DIR" run || exit 1

if [ "${1:-}" = "--website" ]; then
    printf '\n########## 网站集成测试 ##########\n'
    bash "$TEST_DIR/website_test.sh" || exit 1
fi
