RTL_SOURCES := $(shell find rtl -name '*.v')
RTL_INCDIRS := $(addprefix -I,$(sort $(dir $(RTL_SOURCES))))

BUILD := verif/build

# Verilator C++ testbench: --build compiles, then the binary runs.
# TRACE=1 writes a VCD into the build directory.
VFLAGS := --cc --exe --build -Wall
ifeq ($(TRACE),1)
VFLAGS += --trace -CFLAGS -DTRACE
endif
# COVERAGE=1: %below threshold, ~ partly below threshold(default 10 times) 
ifeq ($(COVERAGE),1)
VFLAGS += --coverage -CFLAGS -DCOVERAGE
endif

.PHONY: lint test_sync_fifo

# Lint every module on its own so a warning names the file it came from.
lint:
	@for f in $(RTL_SOURCES); do \
		echo "== $$f =="; \
		verilator --lint-only -Wall $(RTL_INCDIRS) $$f || exit 1; \
	done

# Override the depth under test: make test_sync_fifo SYNC_FIFO_PTR_WIDTH=2
SYNC_FIFO_PTR_WIDTH ?= 5

test_sync_fifo:
	mkdir -p $(BUILD)/sync_fifo
	verilator $(VFLAGS) --top-module sync_fifo \
		-GPTR_WIDTH=$(SYNC_FIFO_PTR_WIDTH) \
		-CFLAGS -DPTR_WIDTH=$(SYNC_FIFO_PTR_WIDTH) \
		--Mdir $(BUILD)/sync_fifo -o tb_sync_fifo \
		rtl/fifo/sync_fifo.v verif/sync_fifo/tb_sync_fifo.cpp
	./$(BUILD)/sync_fifo/tb_sync_fifo
ifeq ($(COVERAGE),1)
	verilator_coverage --annotate $(BUILD)/sync_fifo/annotated \
		$(BUILD)/sync_fifo/coverage.dat
endif
