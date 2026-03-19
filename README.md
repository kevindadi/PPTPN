# 优先级时间 Petri 网 (P-PTPN)

基于 Rust 实现的优先级时间 Petri 网分析与状态类生成工具。支持 TDG (Task Dependency Graph) 解析、矩阵/图形式 PTPN、DBM 时间约束、状态类可达图 BFS 探索。

## 依赖

- Rust 1.70+

## 安装

```bash
cargo build --release
```

## 使用

```bash
# 基本用法
./target/release/ptpn --file example/common.dot --cpus 1 --cores 2

# 限制最大状态数
./target/release/ptpn --file example/common.dot --cpus 1 --cores 2 --max-states 100

# 指定输出路径
./target/release/ptpn --file example/common.dot --cpus 1 --cores 2 \
  --output-ptpn matrix_ptpn.dot \
  --output-scg state_class_graph.dot \
  --output-scg-json state_class_graph.json
```

## 测试

```bash
cargo test
```

## Benchmark

```bash
cargo bench
```

## 一键测试

```bash
./scripts/run_tests.sh
```

执行: 构建 → 单元测试 → Benchmark 编译 → 主程序运行。

设置 `RUN_BENCH=1` 可额外执行完整 benchmark:

```bash
RUN_BENCH=1 ./scripts/run_tests.sh
```

## 日志

通过 `RUST_LOG` 控制日志级别:

```bash
RUST_LOG=ptpn=debug ./target/release/ptpn --file example/common.dot --cpus 1 --cores 2
```

## 输出

- `matrix_ptpn.dot` - 矩阵形式 PTPN 的 DOT 图
- `state_class_graph.dot` - 状态类可达图 (DOT)
- `state_class_graph.json` - 状态类可达图 (JSON)

## 设计文档

参见 [docs/DESIGN.md](docs/DESIGN.md)。

## 算法参考

参见 [STATE_CLASS_ALGORITHM.md](STATE_CLASS_ALGORITHM.md)。
