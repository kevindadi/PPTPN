# 优先级时间 Petri 网 (P-PTPN)

基于 [R-PTPN](https://github.com/kevindadi/R-PTPN) 库的优先级时间 Petri 网分析工具。支持 TDG (Task Dependency Graph) DOT 解析、状态类图 (SCG) 构建、WCET/WCRT 分析、死锁检测。

## 依赖

- Rust 1.70+

## 安装

```bash
cargo build --release
```

## 使用

```bash
# 使用内置 three_task 示例
./target/release/priority --example

# 从 TDG DOT 文件解析
./target/release/priority --file example/common.dot --cpus 1 --cores 2
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

## 日志

通过 `RUST_LOG` 控制日志级别:

```bash
RUST_LOG=priority=debug ./target/release/priority --example
```

## 设计文档

参见 [docs/DESIGN.md](docs/DESIGN.md)。
