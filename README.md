# 优先级时间 Petri 网

## 依赖

- boost
- graphviz

## 安装

```bash
vcpkg install boost
```

```bash
cmake -Bbuild -H .
cmake --build build
```

## 使用

```bash
./build/ptpn --file test/demo.dot --style PTPN
```
