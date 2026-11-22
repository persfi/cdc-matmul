// Testbench for rtl/fifo/async_fifo.v: random traffic on two drifting clocks,
// checked against a reference model.
// Checks: data order, no overflow/underflow, and full/empty never lag by more
// than `bound`.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>

#include "Vasync_fifo.h"
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

// Owned by main().
static VerilatedContext* ctx = nullptr;
static Vasync_fifo* dut = nullptr;
#ifdef TRACE
static VerilatedVcdC* vcd = nullptr;
#endif

static int checks = 0;
static int errors = 0;

// Prints only the first 20 failures.
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        checks++;                                                           \
        if (!(cond)) {                                                      \
            errors++;                                                       \
            if (errors <= 20) {                                             \
                std::printf("FAIL [t=%llu] %s:%d: ",                        \
                            (unsigned long long)ctx->time(), __FILE__,      \
                            __LINE__);                                      \
                std::printf(__VA_ARGS__);                                   \
                std::printf("\n");                                          \
            }                                                               \
        }                                                                   \
    } while (0)

// ---------------------------------------------------------------------------
// Reference model
// ---------------------------------------------------------------------------

struct Entry {
    uint32_t data;
    uint64_t push_time;  // for the empty liveness check
};

static std::deque<Entry> model;            // oldest at front
static std::deque<uint64_t> recent_pops;   // pop times within the last bound

// ---------------------------------------------------------------------------
// Clocks and traffic
// ---------------------------------------------------------------------------

struct Clock {
    uint64_t half;    // half period, in time units
    uint64_t next;    // time of next toggle
    bool level;
    int rst_left;     // rising edges left in reset
};

static Clock wr, rd;
static uint64_t wr_release, rd_release;  // reset release times
static int push_pct = 50, pop_pct = 50;  // attempt probability per cycle

// Reported per configuration to confirm full and empty were reached.
static uint64_t pushes, pops, blocked_pushes, blocked_pops;

static uint32_t rand32() {
    return ((uint32_t)std::rand() << 16) ^ (uint32_t)std::rand();
}

static void wr_falling() {
    dut->rst_wr = wr.rst_left > 0;
    dut->we = (std::rand() % 100) < push_pct;
    dut->wr_data = rand32();
}

static void rd_falling() {
    dut->rst_rd = rd.rst_left > 0;
    dut->re = (std::rand() % 100) < pop_pct;
}

static void wr_rising(uint64_t t) {
    if (wr.rst_left > 0) {
        if (--wr.rst_left == 0) wr_release = t;
        return;
    }
    bool full = dut->full;
    uint64_t bound = 2 * rd.half + 5 * 2 * wr.half;  // 1 rd period + 5 wr periods

    CHECK(full || model.size() < (size_t)DEPTH,
          "full=0 but FIFO holds %zu/%d entries (overflow)", model.size(), DEPTH);

    // full=1 is valid if the FIFO was full within the last bound
    while (!recent_pops.empty() && recent_pops.front() + bound < t)
        recent_pops.pop_front();
    if (full && t - wr_release > bound)
        CHECK(model.size() + recent_pops.size() >= (size_t)DEPTH,
              "full=1 but FIFO holds %zu/%d entries and only %zu were popped "
              "in the last %llu time units", model.size(), DEPTH,
              recent_pops.size(), (unsigned long long)bound);

    if (dut->we) {
        if (!full) {
            model.push_back({dut->wr_data, t});
            pushes++;
        } else {
            blocked_pushes++;
        }
    }
}

static void rd_rising(uint64_t t) {
    if (rd.rst_left > 0) {
        if (--rd.rst_left == 0) rd_release = t;
        return;
    }
    bool empty = dut->empty;
    uint64_t bound = 2 * wr.half + 5 * 2 * rd.half;  // 1 wr period + 5 rd periods

    CHECK(empty || !model.empty(), "empty=0 but FIFO holds no data (underflow)");

    // empty=1 is valid only while the oldest entry<bound
    if (empty && !model.empty()) {
        uint64_t since = std::max(model.front().push_time, rd_release);
        CHECK(t - since <= bound,
              "empty=1 for %llu time units after a push (bound %llu)",
              (unsigned long long)(t - since), (unsigned long long)bound);
    }

    // FWFT: rd_data is valid before the popping edge.
    if (dut->re) {
        if (!empty && !model.empty()) {
            CHECK(dut->rd_data == model.front().data,
                  "rd_data=0x%08x, expected 0x%08x", dut->rd_data,
                  model.front().data);
            recent_pops.push_back(t);
            model.pop_front();
            pops++;
        } else if (empty) {
            blocked_pops++;
        }
    }
}

// Advance to the next clock toggle(s). Checks run before the toggle so they
// see pre-edge values. On coincident rising edges the pop is processed first,
// since a push at the same instant cannot be visible to the reader.
static void step() {
    uint64_t t = std::min(wr.next, rd.next);
    ctx->time(t);
    bool wr_edge = wr.next == t, rd_edge = rd.next == t;
    bool wr_rise = wr_edge && !wr.level, rd_rise = rd_edge && !rd.level;

    if (rd_rise) rd_rising(t);
    if (wr_rise) wr_rising(t);
    if (wr_edge && wr.level) wr_falling();
    if (rd_edge && rd.level) rd_falling();

    if (wr_edge) { wr.level = !wr.level; dut->wr_clk = wr.level; wr.next += wr.half; }
    if (rd_edge) { rd.level = !rd.level; dut->rd_clk = rd.level; rd.next += rd.half; }
    dut->eval();
#ifdef TRACE
    vcd->dump(t);
#endif
}

static void run_until(uint64_t t_end) {
    while (std::min(wr.next, rd.next) < t_end) step();
}

// Assert both resets together; each releases after 4 edges of its own clock
static void start(uint64_t wr_half, uint64_t rd_half, uint64_t rd_offset) {
    uint64_t now = ctx->time() + 1;
    wr = {wr_half, now, false, 4};
    rd = {rd_half, now + rd_offset, false, 4};
    dut->wr_clk = 0;
    dut->rd_clk = 0;
    dut->rst_wr = 1;
    dut->rst_rd = 1;
    dut->we = 0;
    dut->re = 0;
    model.clear();
    recent_pops.clear();
    wr_release = rd_release = now;
}

// One clock configuration: reset, then balanced, push-heavy (holds full) and
// pop-heavy (holds empty)
static void run_config(uint64_t wr_half, uint64_t rd_half, uint64_t rd_offset,
                       uint64_t cycles) {
    start(wr_half, rd_half, rd_offset);
    uint64_t slow = 2 * std::max(wr_half, rd_half);
    uint64_t t = ctx->time();
    pushes = pops = blocked_pushes = blocked_pops = 0;

    push_pct = 50; pop_pct = 50;  run_until(t += 10 * slow);       // reset
    push_pct = 50; pop_pct = 50;  run_until(t += cycles * slow);   // balanced
    push_pct = 90; pop_pct = 20;  run_until(t += cycles * slow);   // full
    push_pct = 20; pop_pct = 90;  run_until(t += cycles * slow);   // empty

    std::printf("  wr period %3llu, rd period %3llu: %6llu pushes, %6llu pops, "
                "%6llu blocked by full, %6llu blocked by empty\n",
                (unsigned long long)(2 * wr_half),
                (unsigned long long)(2 * rd_half), (unsigned long long)pushes,
                (unsigned long long)pops, (unsigned long long)blocked_pushes,
                (unsigned long long)blocked_pops);
}

int main(int argc, char** argv) {
    auto ctx_owner = std::make_unique<VerilatedContext>();
    ctx = ctx_owner.get();
    ctx->commandArgs(argc, argv);
#ifdef TRACE
    ctx->traceEverOn(true);  // must precede model construction
#endif
    auto dut_owner = std::make_unique<Vasync_fifo>(ctx);
    dut = dut_owner.get();

#ifdef TRACE
    auto vcd_owner = std::make_unique<VerilatedVcdC>();
    vcd = vcd_owner.get();
    dut->trace(vcd, 99);
    vcd->open("verif/build/async_fifo/async_fifo.vcd");
#endif

    // +seed=N (make test_async_fifo SEED=N)
    unsigned seed = 1;
    const char* arg = ctx->commandArgsPlusMatch("seed=");
    if (arg && *arg) seed = (unsigned)std::atoi(arg + std::strlen("+seed="));
    std::srand(seed);

    std::printf("async_fifo: DEPTH=%d, seed=%u\n", DEPTH, seed);

    // Half periods.
    run_config(5, 5, 2, 3000);    // same frequency, phase offset
    run_config(5, 7, 0, 3000);    // write slightly faster
    run_config(7, 5, 0, 3000);    // read slightly faster
    run_config(5, 17, 0, 3000);   // write ~3.4x faster
    run_config(17, 5, 0, 3000);   // read ~3.4x faster

    dut->final();
#ifdef COVERAGE
    ctx->coveragep()->write("verif/build/async_fifo/coverage.dat");
#endif
#ifdef TRACE
    vcd->close();
#endif

    std::printf("%d checks, %d failed\n", checks, errors);
    return errors ? 1 : 0;
}
