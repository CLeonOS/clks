.RECIPEPREFIX := >
MAKEFLAGS += --no-print-directory

.PHONY: all bdt setup setup-tools kernel kernel-symbols menuconfig menuconfig-gui clean clean-all help

all: kernel

bdt:
> $(MAKE) -C .. bdt

setup setup-tools:
> $(MAKE) -C .. setup-tools

kernel: bdt
> cd .. && build/bdt/bdt kernel

kernel-symbols: bdt
> cd .. && build/bdt/bdt kernel-symbols

menuconfig:
> $(MAKE) -C .. menuconfig-clks

menuconfig-gui:
> $(MAKE) -C .. menuconfig-gui-clks

clean:
> $(MAKE) -C .. clean

clean-all:
> $(MAKE) -C .. clean-all

help:
> @echo "CLKS now uses the root bdt build system:"
> @echo "  make -C .. kernel"
> @echo "  make -C .. run"
