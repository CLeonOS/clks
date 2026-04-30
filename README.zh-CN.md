# CLKS 内核

[English](README.md) | [简体中文](README.zh-CN.md)

CLKS 是供 CLeonOS 使用的独立内核仓库。
它包含架构启动代码、中断处理、内存管理、调度器、syscall/运行时层、存储、TTY/控制台和内核支持库。

## 当前状态

CLKS 现在可作为独立仓库构建。

- 内核构建不再依赖 CLeonOS 用户态源码。
- menuconfig 资产已在本仓库内（`configs/menuconfig`、`scripts/menuconfig.py`）。
- 构建已委托给根目录 bdt 构建系统。

## 目录结构

```text
.
|- arch/                # 架构启动/链接脚本/中断胶水层
|- include/             # 对外内核头文件
|- kernel/              # 内核核心子系统
|- rust/                # 内核 Rust staticlib
|- third_party/         # 内嵌第三方源码
|- configs/menuconfig/  # CLKS 特性元数据与生成配置
|- .github/workflows/   # CI（build-kernel/style-check）
|- build.bdt            # CLKS bdt 模块描述
|- Makefile             # 委托根目录 bdt 的包装入口
```

## 构建

```bash
make kernel
```

构建符号映射：

```bash
make kernel-symbols
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
