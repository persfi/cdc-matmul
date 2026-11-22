//two flop synchronizer
module sync_2ff #(parameter WIDTH = 1) (
    input clk,   // destination clock
    input rst,   // destination reset
    input [WIDTH-1:0] d,  //flop from source using grey code
    output [WIDTH-1:0] q  
);

    reg [(WIDTH<<1)-1:0] sync;
    always @(posedge clk) begin
        if(rst) sync<=0;
        else sync <= {sync[WIDTH-1:0],d};
    end

    assign q = sync[(WIDTH<<1)-1:WIDTH];

endmodule
