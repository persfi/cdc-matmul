// SIM-ONLY for rtl/fifo/sync_2ff.v (same ports) to create metastable scenarios. Not synthesized.
// Models metastability: a bit that changed less than WINDOW time units before
// the edge is captured late (one extra clock)(meta_q) with 50% probability.

module sync_2ff #(
    parameter WIDTH  = 1,
    parameter WINDOW = 3     // "too close to the edge", in simulation time units
) (
    input              clk,
    input              rst,
    input  [WIDTH-1:0] d,
    output [WIDTH-1:0] q
);

    // Change tracking
    reg [WIDTH-1:0] d_now    = {WIDTH{1'b0}};
    reg [WIDTH-1:0] d_before = {WIDTH{1'b0}};  // value just before last change
    reg [63:0]      chg_time = 64'd0;          // time of last change

    /* verilator lint_off BLKSEQ */
    always @(d) begin
        d_before = d_now;
        d_now    = d;
        chg_time = $time;
    end
    /* verilator lint_on BLKSEQ */

    reg [WIDTH-1:0] meta_q;   // first stage (the one that can go metastable)
    reg [WIDTH-1:0] sync_q;   // second stage
    /* verilator lint_off UNUSEDSIGNAL */
    reg [31:0]      coin_q;   // random bits, refreshed every edge
    /* verilator lint_on UNUSEDSIGNAL */

    wire [WIDTH-1:0] keep_if_near = (d ^ d_before) & coin_q[WIDTH-1:0]; //random 0 or 1 for the bit changed

    always @(posedge clk) begin
        if (rst) begin
            meta_q <= {WIDTH{1'b0}};
            sync_q <= {WIDTH{1'b0}};
        end else begin
            if (($time - chg_time) < WINDOW)
                meta_q <= (d & ~keep_if_near) | (d_before & keep_if_near); //old if kif=1, new if kif=0
            else
                meta_q <= d;
            sync_q <= meta_q;
        end
        coin_q <= $urandom;
    end

    assign q = sync_q;

endmodule
