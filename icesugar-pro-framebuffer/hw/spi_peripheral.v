module spi_peripheral # (
    parameter DATA_WIDTH = 8, 
    parameter SPI_MODE   = 0
) (
    input  wire                  clk,          // System clock (e.g., 25MHz)
    input  wire                  reset,        // Active high system reset
    input  wire                  i_CS_n,       // SPI Chip Select (Active Low)
    input  wire                  i_SCLK,       // SPI Serial Clock
    input  wire                  i_PICO,       // SPI Peripheral In / Controller Out
    input  wire                  i_tx_ready,   // Unused/Stub for this receiver application
    input  wire [DATA_WIDTH-1:0] i_tx_data,    // Unused/Stub
    output reg                   o_rx_ready,   // High for EXACTLY ONE clk cycle when a byte is done
    output reg  [DATA_WIDTH-1:0] o_rx_data,    // Complete parallel data out
    output wire                  o_POCI        // SPI Peripheral Out / Controller In
);

    // ------------------------------------------------------------------------
    // 1. Input Synchronization & Edge Detection
    // ------------------------------------------------------------------------
    reg [2:0] cs_sync;
    reg [2:0] sclk_sync;
    reg [1:0] pico_sync;

    // Direct assignment to prevent compiler optimization on sync registers
    (* ASYNC_REG = "TRUE" *) reg [2:0] cs_reg;
    (* ASYNC_REG = "TRUE" *) reg [2:0] sclk_reg;
    (* ASYNC_REG = "TRUE" *) reg [1:0] pico_reg;

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            cs_sync   <= 3'b111;
            sclk_sync <= 3'b000;
            pico_sync <= 2'b00;
        end else begin
            cs_sync   <= {cs_sync[1:0], i_CS_n};
            sclk_sync <= {sclk_sync[1:0], i_SCLK};
            pico_sync <= {pico_sync[0], i_PICO};
        end
    end

    // Condition flags
    wire cs_active = ~cs_sync[1]; // SPI transaction is running

    // Decode SPI modes dynamically 
    wire sclk_curr = (SPI_MODE == 2 || SPI_MODE == 3) ? ~sclk_sync[1] : sclk_sync[1];
    wire sclk_prev = (SPI_MODE == 2 || SPI_MODE == 3) ? ~sclk_sync[2] : sclk_sync[2];
    wire CPHA      = (SPI_MODE == 1 || SPI_MODE == 3);

    // Capture the exact clock edge where data must be sampled
    wire sample_edge = CPHA ? (sclk_prev == 1'b1 && sclk_curr == 1'b0) : 
                              (sclk_prev == 1'b0 && sclk_curr == 1'b1);

    // ------------------------------------------------------------------------
    // 2. SPI Shift Register and Bit Counter Logic
    // ------------------------------------------------------------------------
    reg [5:0]            bit_cnt;
    reg [DATA_WIDTH-1:0] rx_shift_reg;

    assign o_POCI = 1'b0; // Output always low as configured in your core architecture

    always @(posedge clk or posedge reset) begin
        if (reset) begin
            o_rx_ready   <= 1'b0;
            o_rx_data    <= {DATA_WIDTH{1'b0}};
            bit_cnt      <= DATA_WIDTH - 1;
            rx_shift_reg <= {DATA_WIDTH{1'b0}};
        end else begin
            // Crucial: Default to 0 so ready pulses for EXACTLY one clock cycle
            o_rx_ready <= 1'b0; 

            if (!cs_active) begin
                // Reset counter and shift register state when chip select is idle
                bit_cnt <= DATA_WIDTH - 1;
            end else begin
                if (sample_edge) begin
                    // Shift incoming data bit into the lower index
                    rx_shift_reg <= {rx_shift_reg[DATA_WIDTH-2:0], pico_sync[1]};
                    
                    if (bit_cnt == 0) begin
                        // Byte complete! Strobe the ready line
                        o_rx_ready <= 1'b1;
                        // Bypass 1-cycle delay by assigning the live merged data stream out immediately
                        o_rx_data  <= {rx_shift_reg[DATA_WIDTH-2:0], pico_sync[1]};
                        
                        // FIX: Reset counter back to maximum width.
                        // If the master sends extra clocks, it safely rolls into a new byte assembly 
                        // rather than sticking at zero and repeating old triggers!
                        bit_cnt    <= DATA_WIDTH - 1; 
                    end else begin
                        bit_cnt <= bit_cnt - 1;
                    end
                end
            end
        end
    end

endmodule