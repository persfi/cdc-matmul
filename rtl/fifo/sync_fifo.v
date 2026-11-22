module sync_fifo #(
    parameter DATA_WIDTH = 32,
    parameter PTR_WIDTH = 5
)(
    input rst,
    input clock,
    input we,
    input re, 
    input [DATA_WIDTH-1:0] wr_data,
    output [DATA_WIDTH-1:0] rd_data, 
    output full,
    output empty //or else the software would write with nothing actually happening or read old values
);

    localparam DEPTH = 1<<PTR_WIDTH; //power of 2
    reg [PTR_WIDTH:0] wr_ptr, rd_ptr;
    reg [DATA_WIDTH-1:0] fifo [DEPTH-1:0];

    always @(posedge clock) begin
        if(rst) begin
            wr_ptr<=0;
            rd_ptr<=0;
        end
        else begin
            if(we && !full) wr_ptr<=wr_ptr+1;
            if(re && !empty) rd_ptr<=rd_ptr+1;
        end
    end

    always @(posedge clock) begin //separated from flops with rst
        if(we && !full) fifo[wr_ptr[PTR_WIDTH-1:0]] <= wr_data;
    end

    
    assign rd_data = fifo[rd_ptr[PTR_WIDTH-1:0]]; //fwft

    assign full = (wr_ptr[PTR_WIDTH-1:0] == rd_ptr[PTR_WIDTH-1:0]) && wr_ptr[PTR_WIDTH]!=rd_ptr[PTR_WIDTH];
    assign empty = wr_ptr == rd_ptr;   

endmodule
