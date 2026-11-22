# cdc-matmul

A RISC-V core (RV32I, single cycle) and a small matmul block on separate clock domains, connected through async FIFOs. This project is focused on CDC implementation.

## Done so far

- General Sync FIFO (FWFT)
- General Async FIFO (FWFT) (Gray-coded pointer) (2-flop synchronizer) 
- Randomized testbenches for both FIFOs with Verilator coverage and waveform output

Still to do: the matmul block (4x4, int8) and connecting everything to the core.

## Simulating metastability

In simulation a flip-flop never goes metastable, so CDC bugs can't be tested thoroughly. 

To get around this, the async FIFO test uses a simulation-only version of the
synchronizer (`verif/models/sync_2ff.v`). If its input changes close to the
clock edge, there is a chance for it to pick up the old value, like a real flip-flop. 

A FIFO with binary pointers instead of Gray code fails this test.

## Structure

- `rtl/core/` - the RISC-V core, from [riscv-snn-accelerator](https://github.com/persfi/riscv-snn-accelerator)
- `rtl/fifo/` - sync_fifo, async_fifo, sync_2ff
- `verif/` - Verilator testbenches

## Running

Needs Verilator 5.

    make lint
    make test_sync_fifo
    make test_async_fifo

Add `RANDOM_SYNC=0` to use the normal synchronizer, `TRACE=1` for a waveform,
`SEED=n` to change the randomization in testbenches, or `COVERAGE=1` for a Verilator coverage report.
