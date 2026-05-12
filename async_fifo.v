`timescale 1ns / 1ps

module async_fifo #(
    parameter DATA_WIDTH = 16,
    parameter ADDR_WIDTH = 9,      // 512 words deep
    parameter ALMOST_EMPTY_THRESH = 64
)(
    // --------------------------------------------------------
    // Write Domain (100 MHz System Clock)
    // --------------------------------------------------------
    input  wire                  wr_clk,
    input  wire                  rst,      
    input  wire                  wr_en,
    input  wire [DATA_WIDTH-1:0] din,
    output wire                  full,

    // --------------------------------------------------------
    // Read Domain (9 MHz Pixel Clock)
    // --------------------------------------------------------
    input  wire                  rd_clk,
    input  wire                  rd_en,
    output reg  [DATA_WIDTH-1:0] dout,
    output wire                  empty,
    output wire                  almost_empty
);

    // Memory Array (Inferred as DP16KD Block RAM in ECP5)
    localparam DEPTH = 1 << ADDR_WIDTH;
    reg [DATA_WIDTH-1:0] mem [0:DEPTH-1];

    // Pointers (Need extra bit for Full/Empty wrapping logic)
    reg [ADDR_WIDTH:0] wr_ptr_bin, wr_ptr_gray;
    reg [ADDR_WIDTH:0] rd_ptr_bin, rd_ptr_gray;

    // Cross-domain synchronizers
    reg [ADDR_WIDTH:0] wr_ptr_gray_sync1, wr_ptr_gray_sync2;
    reg [ADDR_WIDTH:0] rd_ptr_gray_sync1, rd_ptr_gray_sync2;

    // ------------------------------------------------------------------------
    // WRITE DOMAIN LOGIC
    // ------------------------------------------------------------------------
    wire [ADDR_WIDTH:0] wr_ptr_bin_next  = wr_ptr_bin + 1'b1;
    wire [ADDR_WIDTH:0] wr_ptr_gray_next = wr_ptr_bin_next ^ (wr_ptr_bin_next >> 1);
    
    wire wr_successful = wr_en && !full;

    always @(posedge wr_clk or posedge rst) begin
        if (rst) begin
            wr_ptr_bin  <= 0;
            wr_ptr_gray <= 0;
        end else if (wr_successful) begin
            wr_ptr_bin  <= wr_ptr_bin_next;
            wr_ptr_gray <= wr_ptr_gray_next;
        end
    end

    // Write to dual-port memory
    always @(posedge wr_clk) begin
        if (wr_successful) begin
            mem[wr_ptr_bin[ADDR_WIDTH-1:0]] <= din;
        end
    end

    // Synchronize Read Pointer into Write Domain (to calculate 'full')
    always @(posedge wr_clk or posedge rst) begin
        if (rst) begin
            rd_ptr_gray_sync1 <= 0;
            rd_ptr_gray_sync2 <= 0;
        end else begin
            rd_ptr_gray_sync1 <= rd_ptr_gray;
            rd_ptr_gray_sync2 <= rd_ptr_gray_sync1;
        end
    end

    // Full condition: Pointers match, but top two bits are inverted (wrap-around)
    assign full = (wr_ptr_gray == {~rd_ptr_gray_sync2[ADDR_WIDTH:ADDR_WIDTH-1], 
                                    rd_ptr_gray_sync2[ADDR_WIDTH-2:0]});

    // ------------------------------------------------------------------------
    // READ DOMAIN LOGIC
    // ------------------------------------------------------------------------
    wire [ADDR_WIDTH:0] rd_ptr_bin_next  = rd_ptr_bin + 1'b1;
    wire [ADDR_WIDTH:0] rd_ptr_gray_next = rd_ptr_bin_next ^ (rd_ptr_bin_next >> 1);
    
    wire rd_successful = rd_en && !empty;

    always @(posedge rd_clk or posedge rst) begin
        if (rst) begin
            rd_ptr_bin  <= 0;
            rd_ptr_gray <= 0;
        end else if (rd_successful) begin
            rd_ptr_bin  <= rd_ptr_bin_next;
            rd_ptr_gray <= rd_ptr_gray_next;
        end
    end

    // Read from dual-port memory
    always @(posedge rd_clk) begin
        if (rd_successful) begin
            dout <= mem[rd_ptr_bin[ADDR_WIDTH-1:0]];
        end
    end

    // Synchronize Write Pointer into Read Domain (to calculate 'empty' and 'almost_empty')
    always @(posedge rd_clk or posedge rst) begin
        if (rst) begin
            wr_ptr_gray_sync1 <= 0;
            wr_ptr_gray_sync2 <= 0;
        end else begin
            wr_ptr_gray_sync1 <= wr_ptr_gray;
            wr_ptr_gray_sync2 <= wr_ptr_gray_sync1;
        end
    end

    // Empty condition: Read pointer catches up to synchronized write pointer
    assign empty = (rd_ptr_gray == wr_ptr_gray_sync2);

    // ------------------------------------------------------------------------
    // ALMOST EMPTY FLAG (SDRAM Arbiter Trigger)
    // ------------------------------------------------------------------------
    reg [ADDR_WIDTH:0] wr_ptr_bin_synced;
    integer i;
    
    // Convert Gray -> Binary in Read Domain to count available words
    always @(*) begin
        wr_ptr_bin_synced[ADDR_WIDTH] = wr_ptr_gray_sync2[ADDR_WIDTH];
        for (i = ADDR_WIDTH-1; i >= 0; i = i - 1) begin
            wr_ptr_bin_synced[i] = wr_ptr_bin_synced[i+1] ^ wr_ptr_gray_sync2[i];
        end
    end

    wire [ADDR_WIDTH:0] words_in_fifo = wr_ptr_bin_synced - rd_ptr_bin;
    
    assign almost_empty = (words_in_fifo <= ALMOST_EMPTY_THRESH);

endmodule