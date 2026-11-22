module async_fifo#(
    parameter DATA_WIDTH = 32,
    parameter PTR_WIDTH = 5
)(
    input rst_wr,
    input rst_rd,
    input wr_clk,
    input rd_clk,
    input we,
    input re, 
    input [DATA_WIDTH-1:0] wr_data,
    output [DATA_WIDTH-1:0] rd_data, 
    output full,
    output empty //or else the software would write with nothing actually happening or read old values
);

    localparam DEPTH = 1<<PTR_WIDTH; //power of 2
    reg [DATA_WIDTH-1:0] fifo [DEPTH-1:0];

    //write side declarations
    reg [PTR_WIDTH:0] wr_ptr, wr_ptr_grey;
    wire [PTR_WIDTH:0] wr_ptr_g, wr_rd_ptr; //same domain combinational 
    //read side declarations
    reg [PTR_WIDTH:0] rd_ptr, rd_ptr_grey;
    wire [PTR_WIDTH:0] rd_ptr_g, rd_wr_ptr;

    //write
    always @(posedge wr_clk) begin
        if(rst_wr) wr_ptr<=0;
        else if(we && !full) wr_ptr<=wr_ptr+1;
    end

    always @(posedge wr_clk) begin //separated from flops with rst
        if(we && !full) fifo[wr_ptr[PTR_WIDTH-1:0]] <= wr_data;
    end

    always @(posedge wr_clk) begin
        if(rst_wr) begin
            wr_ptr_grey<=0;
        end else begin
            wr_ptr_grey <= wr_ptr ^ (wr_ptr>>1); //to grey code to pass on to read side -> non-blocking as it goes throught diff clk domains
        end
    end

    sync_2ff #(.WIDTH(PTR_WIDTH+1)) u_sync_rd2wr (
        .clk(wr_clk), 
        .rst(rst_wr), 
        .d(rd_ptr_grey), 
        .q(wr_rd_ptr)
    );
    assign wr_ptr_g = wr_ptr ^ (wr_ptr>>1);
    assign full = (wr_ptr_g[PTR_WIDTH-2:0] == wr_rd_ptr[PTR_WIDTH-2:0]) && wr_ptr_g[PTR_WIDTH:PTR_WIDTH-1] == ~wr_rd_ptr[PTR_WIDTH:PTR_WIDTH-1];

    //read
    always @(posedge rd_clk) begin
        if(rst_rd) rd_ptr<=0;
        else if(re && !empty) rd_ptr<=rd_ptr+1;
    end     

    always @(posedge rd_clk) begin
        if(rst_rd) begin
            rd_ptr_grey<=0;
        end else begin
            rd_ptr_grey <= rd_ptr ^ (rd_ptr>>1);
        end
    end
    
    sync_2ff #(.WIDTH(PTR_WIDTH+1)) u_sync_wr2rd (
        .clk(rd_clk), 
        .rst(rst_rd), 
        .d(wr_ptr_grey), 
        .q(rd_wr_ptr)
    );
    assign rd_ptr_g = rd_ptr ^ (rd_ptr>>1);
    assign empty = rd_wr_ptr == rd_ptr_g;   
    assign rd_data = fifo[rd_ptr[PTR_WIDTH-1:0]]; //fwft
    

endmodule
