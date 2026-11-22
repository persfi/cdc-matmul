// Verilator harness for rtl/fifo/sync_fifo.v


#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>

#include "Vsync_fifo.h"
#include "verilated.h"
#ifdef COVERAGE
#include "verilated_cov.h"
#endif
#ifdef TRACE
#include "verilated_vcd_c.h"
#endif

#ifndef PTR_WIDTH
#error "PTR_WIDTH must be defined by the Makefile"
#endif

static const int DEPTH = 1 << PTR_WIDTH;

// Owned by main(); globals so the helpers below stay short.
static VerilatedContext* ctx = nullptr;
static Vsync_fifo* dut = nullptr;
#ifdef TRACE
static VerilatedVcdC* vcd = nullptr;
#endif
static uint64_t cycle_count = 0;
static int checks = 0;
static int errors = 0;

struct Outputs {
    uint32_t rd_data;
    bool full;
    bool empty;
};

static void dump() {
#ifdef TRACE
    vcd->dump(ctx->time());
#endif
    ctx->timeInc(1);
}

// One clock period with the given inputs. Returns outputs seen before the edge.
static Outputs cycle(bool we, bool re, uint32_t wr_data = 0) {
    dut->we = we;
    dut->re = re;
    dut->wr_data = wr_data;
    dut->clock = 0;
    dut->eval();
    dump();

    Outputs o{dut->rd_data, dut->full != 0, dut->empty != 0};

    dut->clock = 1;
    dut->eval();
    dump();
    cycle_count++;
    return o;
}

// Current outputs without clocking (state unchanged).
static Outputs peek() {
    dut->eval();
    return Outputs{dut->rd_data, dut->full != 0, dut->empty != 0};
}

static void reset(int cycles = 2) {
    dut->rst = 1;
    for (int i = 0; i < cycles; i++) cycle(false, false);
    dut->rst = 0;
}

#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            errors++;                                                       \
            std::printf("FAIL [cycle %llu] %s:%d: ",                        \
                        (unsigned long long)cycle_count, __FILE__, __LINE__);\
            std::printf(__VA_ARGS__);                                       \
            std::printf("\n");                                              \
        }                                                                   \
    } while (0)

// ---------------------------------------------------------------------------
// Reference model
static std::deque<uint32_t> model;

//check o(module) against model(software)
static void step(bool we, bool re, uint32_t wr_data) {
    bool m_full = model.size() == (size_t)DEPTH;
    bool m_empty = model.empty();

    Outputs o = cycle(we, re, wr_data);

    CHECK(o.full == m_full, "full=%d, model has %zu/%d entries", o.full,
          model.size(), DEPTH);
    CHECK(o.empty == m_empty, "empty=%d, model has %zu entries", o.empty,
          model.size());
    if (!m_empty)
        CHECK(o.rd_data == model.front(), "rd_data=0x%08x, expected 0x%08x",
              o.rd_data, model.front());

    if (re && !m_empty) model.pop_front();
    if (we && !m_full) model.push_back(wr_data);
}

// Random traffic: each cycle, push with probability push_pct% and pop with
// probability pop_pct%, independently. -> reveals ordering bugs
static void run_random(int cycles, int push_pct, int pop_pct) {
    for (int i = 0; i < cycles; i++) {
        bool we = (std::rand() % 100) < push_pct;
        bool re = (std::rand() % 100) < pop_pct;
        uint32_t data = ((uint32_t)std::rand() << 16) ^ (uint32_t)std::rand();
        step(we, re, data);
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Check reset
static void test_reset_state() {
    reset();
    Outputs o = peek();
    CHECK(o.empty, "empty should be 1 after reset");
    CHECK(!o.full, "full should be 0 after reset");
}

int main(int argc, char** argv) {
    auto ctx_owner = std::make_unique<VerilatedContext>();
    ctx = ctx_owner.get();
    ctx->commandArgs(argc, argv);
#ifdef TRACE
    ctx->traceEverOn(true);
#endif
    auto dut_owner = std::make_unique<Vsync_fifo>(ctx);
    dut = dut_owner.get();

#ifdef TRACE
    auto vcd_owner = std::make_unique<VerilatedVcdC>();
    vcd = vcd_owner.get();
    dut->trace(vcd, 99);
    vcd->open("verif/build/sync_fifo/sync_fifo.vcd");
#endif

    std::printf("sync_fifo: DEPTH=%d\n", DEPTH);

    test_reset_state();

    // Baising traffic toward one situation; 
    // run_random check module output against software model output over the cycles counts we want
    reset();
    model.clear();
    run_random(2000, 50, 50);   // balanced: push+pop together in the middle
    run_random(2000, 90, 20);   // push-heavy: more full, pushes get blocked
    run_random(2000, 20, 90);   // pop-heavy: more empty, pops get blocked
    reset();
    model.clear();
    run_random(200, 50, 50);
    reset();
    model.clear();
    run_random(200, 50, 50);
    reset();
    model.clear();
    run_random(200, 50, 50);
    reset();
    model.clear();
    run_random(200, 50, 50);

    dut->final();
#ifdef COVERAGE
    ctx->coveragep()->write("verif/build/sync_fifo/coverage.dat");
#endif
#ifdef TRACE
    vcd->close();
#endif

    std::printf("%d checks, %d failed\n", checks, errors);
    return errors ? 1 : 0;
}
