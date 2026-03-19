#!/usr/bin/env bash
# PTPN 一键测试脚本
# 执行: cargo build, cargo test, cargo bench, 主程序运行

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$PROJECT_ROOT"

echo "=== PTPN 一键测试 ==="
echo "工作目录: $PROJECT_ROOT"
echo ""

# 1. 构建
echo ">>> 1. cargo build --release"
cargo build --release
echo ""

# 2. 测试
echo ">>> 2. cargo test"
cargo test
echo ""

# 3. Benchmark (可选，仅编译)
echo ">>> 3. cargo bench --no-run"
cargo bench --no-run
echo ""

# 4. 运行主程序
echo ">>> 4. 运行主程序 (example/common.dot, cpus=1, cores=2, max-states=100)"
cargo run --release -- --file example/common.dot --cpus 1 --cores 2 --max-states 100
echo ""

# 5. 可选: 完整 benchmark
if [ "${RUN_BENCH:-0}" = "1" ]; then
    echo ">>> 5. cargo bench (RUN_BENCH=1 时执行)"
    cargo bench
fi

echo "=== 测试完成 ==="
