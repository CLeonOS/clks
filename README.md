# CLKS Kernel

[English](README.md) | [简体中文](README.zh-CN.md)

CLKS is the standalone kernel repository used by CLeonOS.
It contains architecture startup code, interrupt handling, memory management, scheduler, syscall/runtime layers, storage, TTY/console, and kernel support libraries.

## Status

CLKS now builds as an independent repository.

- Kernel build no longer depends on CLeonOS userland sources.
- menuconfig assets are local to this repository (`configs/menuconfig`, `scripts/menuconfig.py`).
- Builds are delegated to the root bdt build system.

## Directory Layout

```text
.
|- arch/                # Architecture startup/linker/interrupt glue
|- include/             # Public kernel headers
|- kernel/              # Core kernel subsystems
|- rust/                # Kernel Rust staticlib
|- third_party/         # Embedded third-party sources
|- configs/menuconfig/  # CLKS feature metadata and generated config outputs
|- .github/workflows/   # CI (build-kernel/style-check)
|- build.bdt            # CLKS bdt module description
|- Makefile             # Wrapper that delegates to root bdt
```

## Build

```bash
make kernel
```

Build symbols map:

```bash
make kernel-symbols
```

## Menuconfig

```bash
make menuconfig
```

or GUI mode:

```bash
make menuconfig-gui
```

Outputs are generated under `configs/menuconfig/`.

## CI

`build-kernel` workflow builds `clks_kernel.elf` and `kernel.sym` on push/PR.

## Integration With CLeonOS

In the CLeonOS mono-repo, CLKS should be referenced as a git submodule.
The main repo should only track the submodule pointer and delegate kernel build to this repo.

## License

Apache-2.0.
