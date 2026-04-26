.RECIPEPREFIX := >
MAKEFLAGS += --no-print-directory

CMAKE ?= cmake
CMAKE_BUILD_DIR ?= build-cmake
CMAKE_BUILD_TYPE ?= Release
CMAKE_GENERATOR ?=
CMAKE_EXTRA_ARGS ?=
OPT_LEVEL ?=
O2 ?= 0
NO_COLOR ?= 0
OBJCOPY_FOR_TARGET ?=
OBJDUMP_FOR_TARGET ?=
READELF_FOR_TARGET ?=
PYTHON ?= python3
MENUCONFIG_ARGS ?=
MENUCONFIG_PRESET ?=

ifeq ($(strip $(CMAKE_GENERATOR)),)
GEN_ARG :=
else
GEN_ARG := -G "$(CMAKE_GENERATOR)"
endif

CMAKE_PASSTHROUGH_ARGS :=
MENUCONFIG_PRESET_ARG := $(if $(strip $(MENUCONFIG_PRESET)),--preset $(MENUCONFIG_PRESET),)
OPT_LEVEL_EFFECTIVE := $(strip $(OPT_LEVEL))

ifeq ($(OPT_LEVEL_EFFECTIVE),)
ifneq ($(filter 1 ON on TRUE true YES yes Y y,$(O2)),)
OPT_LEVEL_EFFECTIVE := 2
endif
endif

CMAKE_PASSTHROUGH_ARGS += -DCLKS_OPT_LEVEL=$(OPT_LEVEL_EFFECTIVE)

ifneq ($(strip $(OBJCOPY_FOR_TARGET)),)
CMAKE_PASSTHROUGH_ARGS += -DOBJCOPY_FOR_TARGET=$(OBJCOPY_FOR_TARGET)
endif
ifneq ($(strip $(OBJDUMP_FOR_TARGET)),)
CMAKE_PASSTHROUGH_ARGS += -DOBJDUMP_FOR_TARGET=$(OBJDUMP_FOR_TARGET)
endif
ifneq ($(strip $(READELF_FOR_TARGET)),)
CMAKE_PASSTHROUGH_ARGS += -DREADELF_FOR_TARGET=$(READELF_FOR_TARGET)
endif

.PHONY: all configure reconfigure menuconfig menuconfig-gui setup setup-tools kernel kernel-symbols clean clean-all help

all: kernel

configure:
> @$(CMAKE) -S . -B $(CMAKE_BUILD_DIR) $(GEN_ARG) -DCMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) -DNO_COLOR=$(NO_COLOR) $(CMAKE_EXTRA_ARGS) $(CMAKE_PASSTHROUGH_ARGS)

reconfigure:
> @$(CMAKE) -E rm -rf $(CMAKE_BUILD_DIR)
> @$(MAKE) configure CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) CMAKE_GENERATOR="$(CMAKE_GENERATOR)" CMAKE_EXTRA_ARGS="$(CMAKE_EXTRA_ARGS)" OPT_LEVEL="$(OPT_LEVEL)" O2="$(O2)" NO_COLOR="$(NO_COLOR)" OBJCOPY_FOR_TARGET="$(OBJCOPY_FOR_TARGET)" OBJDUMP_FOR_TARGET="$(OBJDUMP_FOR_TARGET)" READELF_FOR_TARGET="$(READELF_FOR_TARGET)"

menuconfig:
> @if command -v $(PYTHON) >/dev/null 2>&1; then \
>     $(PYTHON) scripts/menuconfig.py --clks-only $(MENUCONFIG_PRESET_ARG) $(MENUCONFIG_ARGS); \
> elif command -v python >/dev/null 2>&1; then \
>     python scripts/menuconfig.py --clks-only $(MENUCONFIG_PRESET_ARG) $(MENUCONFIG_ARGS); \
> else \
>     echo "python3/python not found"; \
>     exit 1; \
> fi
> @$(MAKE) configure CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) CMAKE_GENERATOR="$(CMAKE_GENERATOR)" CMAKE_EXTRA_ARGS="$(CMAKE_EXTRA_ARGS)" OPT_LEVEL="$(OPT_LEVEL)" O2="$(O2)" NO_COLOR="$(NO_COLOR)" OBJCOPY_FOR_TARGET="$(OBJCOPY_FOR_TARGET)" OBJDUMP_FOR_TARGET="$(OBJDUMP_FOR_TARGET)" READELF_FOR_TARGET="$(READELF_FOR_TARGET)"

menuconfig-gui:
> @if command -v $(PYTHON) >/dev/null 2>&1; then \
>     $(PYTHON) scripts/menuconfig.py --gui --clks-only $(MENUCONFIG_PRESET_ARG) $(MENUCONFIG_ARGS); \
> elif command -v python >/dev/null 2>&1; then \
>     python scripts/menuconfig.py --gui --clks-only $(MENUCONFIG_PRESET_ARG) $(MENUCONFIG_ARGS); \
> else \
>     echo "python3/python not found"; \
>     exit 1; \
> fi
> @$(MAKE) configure CMAKE_BUILD_TYPE=$(CMAKE_BUILD_TYPE) CMAKE_GENERATOR="$(CMAKE_GENERATOR)" CMAKE_EXTRA_ARGS="$(CMAKE_EXTRA_ARGS)" OPT_LEVEL="$(OPT_LEVEL)" O2="$(O2)" NO_COLOR="$(NO_COLOR)" OBJCOPY_FOR_TARGET="$(OBJCOPY_FOR_TARGET)" OBJDUMP_FOR_TARGET="$(OBJDUMP_FOR_TARGET)" READELF_FOR_TARGET="$(READELF_FOR_TARGET)"

setup: configure
> @$(CMAKE) --build $(CMAKE_BUILD_DIR) --target setup

setup-tools: configure
> @$(CMAKE) --build $(CMAKE_BUILD_DIR) --target setup-tools

kernel: configure
> @$(CMAKE) --build $(CMAKE_BUILD_DIR) --target kernel

kernel-symbols: configure
> @$(CMAKE) --build $(CMAKE_BUILD_DIR) --target kernel-symbols

clean:
> @if [ -d "$(CMAKE_BUILD_DIR)" ]; then \
>     $(CMAKE) --build $(CMAKE_BUILD_DIR) --target clean-x86; \
> else \
>     $(CMAKE) -E rm -rf build/x86_64; \
> fi

clean-all:
> @if [ -d "$(CMAKE_BUILD_DIR)" ]; then \
>     $(CMAKE) --build $(CMAKE_BUILD_DIR) --target clean-all; \
> else \
>     $(CMAKE) -E rm -rf build build-cmake; \
> fi

help:
> @echo "CLKS (CMake-backed wrapper)"
> @echo "  make configure"
> @echo "  make menuconfig"
> @echo "  make menuconfig-gui"
> @echo "  make setup"
> @echo "  make kernel"
> @echo "  make kernel-symbols"
> @echo "  make clean"
> @echo "  make clean-all"
> @echo ""
> @echo "Optimization:"
> @echo "  make kernel O2=1"
> @echo "  make kernel OPT_LEVEL=2"
> @echo ""
> @echo "Preset examples:"
> @echo "  make menuconfig MENUCONFIG_PRESET=full"
> @echo "  make menuconfig MENUCONFIG_PRESET=minimal"
> @echo "  make menuconfig-gui MENUCONFIG_PRESET=dev"
