# Priority Time Petri Net

## ⚙️ 依赖

- **Boost**：用于图形处理和日志记录。
- **Graphviz**：用于可视化生成图。
- **nlohmann-json**：用于 JSON 解析。
  
---

## 📦 安装

使用 `vcpkg` 安装依赖：

```bash
vcpkg install boost-graph boost-log nlohmann-json graphviz boost-program-options
```
构建项目
cmake -Bbuild -H .
cmake --build build

## 🧩 使用

```bash
./build/ptpn --file test/demo.dot --cpus 6 --cores 1
```
