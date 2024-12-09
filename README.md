# Priority Time Petri Net Analyzer

一个基于时间Petri网的任务优先级和WCET分析工具。该工具可以将任务依赖图(TDG)转换为优先级时间Petri网(PTPN)，并进行状态空间分析。

## 功能特性

- 支持多种输入格式:
  - 标准DOT格式的任务依赖图(TDG)
  - 带优先级的时间Petri网(PTPN)
  - 带概率的随机时间Petri网(PSTPN)
- 自动解析任务优先级和时间约束
- 生成状态类图
- 计算任务的最坏执行时间(WCET)
- 支持死锁检测

## 安装依赖

boost:https://www.boost.org/users/history/version_1_86_0.html

## 编译

```
mkdir build
cd build
cmake ..
make
```

## Usage

```
./ptpn --help
./ptpn --file ../test/label.dot --style PTPN
```
