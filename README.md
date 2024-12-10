# 优先级时间Petri网

## 依赖

- boost
- graphviz

## 安装

```bash
vcpkg install boost-graph boost-log nlohmann-json graphviz boost-program-options
```

```bash
cmake -Bbuild -H .   
cmake --build build
```

## 使用

```bash
./build/ptpn --file test/demo.dot --style PTPN
```
