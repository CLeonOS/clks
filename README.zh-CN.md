# CLKS 内核

[English](README.md) | [简体中文](README.zh-CN.md)

CLKS 是供 CLeonOS 使用的独立内核仓库。
它包含架构启动代码、中断处理、内存管理、调度器、syscall/运行时层、存储、TTY/控制台和内核支持库。

## 当前状态

CLKS 现在可作为独立仓库构建。

- 内核构建不再依赖 CLeonOS 用户态源码。
- menuconfig 资产已在本仓库内（`configs/menuconfig`、`scripts/menuconfig.py`）。
- CMake 辅助脚本已在本仓库内（`cmake/`）。

## 目录结构

```text
.
|- arch/                # 架构启动/链接脚本/中断胶水层
|- include/             # 对外内核头文件
|- kernel/              # 内核核心子系统
|- rust/                # 内核 Rust staticlib
|- third_party/         # 内嵌第三方源码
|- cmake/               # 构建辅助脚本（日志/工具检查/符号生成）
|- configs/menuconfig/  # CLKS 特性元数据与生成配置
|- scripts/             # menuconfig 启动脚本
|- .github/workflows/   # CI（build-kernel/style-check）
|- CMakeLists.txt       # CLKS 独立 CMake 入口
|- Makefile             # 便捷包装入口
```

## 构建

```bash
make kernel
```

构建符号映射：

```bash
make kernel-symbols
```

直接用 CMake：

```bash
cmake -S . -B build-cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --target kernel
```

## Menuconfig

```bash
make menuconfig
```

或 GUI：

```bash
make menuconfig-gui
```

生成输出位于 `configs/menuconfig/`。

## CI

`build-kernel` 工作流会在 push/PR 构建 `clks_kernel.elf` 和 `kernel.sym`。

## 与 CLeonOS 集成

在 CLeonOS 主仓库中，CLKS 应通过 git submodule 引用。
主仓库只维护子模块指针，并把内核构建委托给本仓库。

## 许可证

Apache-2.0。