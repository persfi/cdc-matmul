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

.PHONY: lint test_sync_fifo test_async_fifo

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
	verilator_coverage --annotate-all --annotate $(BUILD)/sync_fifo/annotated \
		$(BUILD)/sync_fifo/coverage.dat
endif

# Dual-clock async FIFO test.
#   ASYNC_FIFO_PTR_WIDTH=n  depth 2^n (n >= 2)
#   RANDOM_SYNC=0           use the real synchronizer instead of the sim-only
#                           model with random extra delay (verif/models/)
#   SEED=n                  change the random traffic and synchronizer delays
ASYNC_FIFO_PTR_WIDTH ?= 2
RANDOM_SYNC ?= 1
SEED ?= 1
ifeq ($(RANDOM_SYNC),1)
ASYNC_SYNC_SRC := verif/models/sync_2ff.v -Wno-SYNCASYNCNET
else
ASYNC_SYNC_SRC := rtl/fifo/sync_2ff.v
endif

test_async_fifo:
	mkdir -p $(BUILD)/async_fifo
	verilator $(VFLAGS) --top-module async_fifo \
		-GPTR_WIDTH=$(ASYNC_FIFO_PTR_WIDTH) \
		-CFLAGS -DPTR_WIDTH=$(ASYNC_FIFO_PTR_WIDTH) \
		--Mdir $(BUILD)/async_fifo -o tb_async_fifo \
		rtl/fifo/async_fifo.v $(ASYNC_SYNC_SRC) \
		verif/async_fifo/tb_async_fifo.cpp
	./$(BUILD)/async_fifo/tb_async_fifo +seed=$(SEED) +verilator+seed+$(SEED)
ifeq ($(COVERAGE),1)
	verilator_coverage --annotate-all --annotate $(BUILD)/async_fifo/annotated \
		$(BUILD)/async_fifo/coverage.dat
endif
