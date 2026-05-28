`timescale 1ns / 1ps

module sdram_controller (
    input  wire        clk,
    input  wire        rst,

    // Write Interface (CPU/GPU)
    input  wire        wr_req,
    input  wire [23:0] wr_addr, // {Bank[1:0], Row[12:0], Col[8:0]}
    input  wire [15:0] wr_data,
    output reg         wr_ack,

    // Read Interface (Video FIFO)
    input  wire        rd_req,
    input  wire [23:0] rd_addr,
    output reg  [15:0] rd_data,
    output reg         rd_valid,
    output reg         rd_ack,

    // Physical SDRAM Pins
    output reg         sdram_cs_n,
    output reg         sdram_ras_n,
    output reg         sdram_cas_n,
    output reg         sdram_we_n,
    output reg  [1:0]  sdram_ba,
    output reg  [12:0] sdram_a,
    output wire [1:0]  sdram_dqm,
    inout  wire [15:0] sdram_dq
);

    // ------------------------------------------------------------------------
    // SDRAM Command Table {CS_n, RAS_n, CAS_n, WE_n}
    // ------------------------------------------------------------------------
    localparam CMD_LMR  = 4'b0000; // Load Mode Register
    localparam CMD_REF  = 4'b0001; // Auto-Refresh
    localparam CMD_PRE  = 4'b0010; // Precharge
    localparam CMD_ACT  = 4'b0011; // Bank Activate
    localparam CMD_WRIT = 4'b0100; // Write
    localparam CMD_READ = 4'b0101; // Read
    localparam CMD_NOP  = 4'b0111; // No Operation

    // ------------------------------------------------------------------------
    // FSM States
    // ------------------------------------------------------------------------
    localparam INIT_WAIT  = 4'd0;
    localparam INIT_PRE   = 4'd1;
    localparam INIT_REF1  = 4'd2;
    localparam INIT_REF2  = 4'd3;
    localparam INIT_LMR   = 4'd4;
    localparam IDLE       = 4'd5;
    localparam DO_REF     = 4'd6;
    localparam READ_ACT   = 4'd7;
    localparam READ_CAS   = 4'd8;
    localparam READ_WAIT  = 4'd9;
    localparam WRITE_ACT  = 4'd10;
    localparam WRITE_CAS  = 4'd11;

    reg [3:0]  state;
    reg [15:0] delay_cnt;
    reg [9:0]  refresh_cnt;

    // ------------------------------------------------------------------------
    // Timing Parameters (for 100MHz clock / 10ns period)
    // ------------------------------------------------------------------------
    // IS42S16160B requires 8192 refresh cycles every 64ms.
    // 64ms / 8192 = 7.8125 us per refresh.
    // At 100MHz (10ns/cycle), 7.8125us = 781 cycles.
    // We use 750 to provide a safe margin.
    localparam REFRESH_MAX = 10'd750; 
    
    // Always enable both upper and lower bytes for 16-bit operation
    assign sdram_dqm = 2'b00; 

    // Tri-state logic for the bidirectional data bus
    reg drive_dq;
    reg [15:0] dq_out;
    assign sdram_dq = drive_dq ? dq_out : 16'bz;

    // ------------------------------------------------------------------------
    // Main State Machine
    // ------------------------------------------------------------------------
    always @(posedge clk or posedge rst) begin
        if (rst) begin
            state       <= INIT_WAIT;
            delay_cnt   <= 16'd10000; // 100us power-up wait time required by SDRAM
            refresh_cnt <= 0;
            drive_dq    <= 0;
            rd_valid    <= 0;
            rd_ack      <= 0;
            // Default pins to NOP to prevent accidental commands
            {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_NOP;
            sdram_a     <= 0;
            sdram_ba    <= 0;
        end else begin
            // Defaults for every cycle unless overridden in a specific state
            {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_NOP;
            rd_valid <= 0;
            drive_dq <= 0;
            rd_ack   <= 0;
            wr_ack   <= 0;

            // Increment the refresh counter continuously
            if (refresh_cnt < REFRESH_MAX) begin
                refresh_cnt <= refresh_cnt + 1;
            end

            // Timer for SDRAM timings (tRP, tRCD, tRFC, etc.)
            if (delay_cnt > 0) begin
                delay_cnt <= delay_cnt - 1;
            end else begin
                case (state)
                    // --- Initialization Sequence ---
                    INIT_WAIT: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_PRE;
                        sdram_a[10] <= 1'b1;  // A10=1 means Precharge ALL banks
                        delay_cnt   <= 2;     // tRP (Precharge time)
                        state       <= INIT_REF1;
                    end
                    
                    INIT_REF1: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REF;
                        delay_cnt   <= 7;     // tRFC (Auto-Refresh cycle time)
                        state       <= INIT_REF2;
                    end
                    
                    INIT_REF2: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REF;
                        delay_cnt   <= 7;
                        state       <= INIT_LMR;
                    end
                    
                    INIT_LMR: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_LMR;
                        // Mode Register Configuration:
                        // Reserved | Write Burst (0=Programmed Length) | Operating Mode (00=Standard) | CAS Latency (010=2) | Sequential (0) | Burst Length (000=1)
                        sdram_a     <= 13'b000_0_00_010_0_000; 
                        sdram_ba    <= 2'b00;
                        delay_cnt   <= 2;     // tMRD (Mode Register Delay)
                        state       <= IDLE;
                    end
                    
                    // --- Main Arbiter ---
                    IDLE: begin
                        if (refresh_cnt >= REFRESH_MAX) begin
                            state       <= DO_REF;
                            refresh_cnt <= 0; // Reset refresh timer
                        end else if (rd_req) begin
                            state  <= READ_ACT;
                            rd_ack <= 1; // Pulse ACK so top-level increments address
                        end else if (wr_req) begin
                            state  <= WRITE_ACT;
                            wr_ack <= 1;
                        end
                    end

                    // --- Auto Refresh ---
                    DO_REF: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_REF;
                        delay_cnt <= 7;       // tRFC
                        state     <= IDLE;
                    end

                    // --- Read Sequence ---
                    READ_ACT: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_ACT;
                        sdram_ba  <= rd_addr[23:22]; // Bank Address
                        sdram_a   <= rd_addr[21:9];  // Row Address
                        delay_cnt <= 2;              // tRCD (RAS to CAS delay)
                        state     <= READ_CAS;
                    end

                    READ_CAS: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_READ;
                        sdram_ba     <= rd_addr[23:22];
                        sdram_a[8:0] <= rd_addr[8:0]; // Column Address
                        sdram_a[10]  <= 1'b1;         // A10 High = Auto-precharge after read
                        delay_cnt    <= 2;            // CAS Latency = 2 cycles
                        state        <= READ_WAIT;
                    end

                    READ_WAIT: begin
                        // Data arrives exactly 2 cycles after the READ command
                        rd_data  <= sdram_dq;
                        rd_valid <= 1;
                        // tRP (Precharge time) is handled in the background due to auto-precharge.
                        // We safely return to IDLE.
                        state    <= IDLE; 
                    end

                    // --- Write Sequence ---
                    WRITE_ACT: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_ACT;
                        sdram_ba  <= wr_addr[23:22];
                        sdram_a   <= wr_addr[21:9];
                        delay_cnt <= 2;               // tRCD
                        state     <= WRITE_CAS;
                    end

                    WRITE_CAS: begin
                        {sdram_cs_n, sdram_ras_n, sdram_cas_n, sdram_we_n} <= CMD_WRIT;
                        sdram_ba     <= wr_addr[23:22];
                        sdram_a[8:0] <= wr_addr[8:0];
                        sdram_a[10]  <= 1'b1;         // Auto-precharge after write
                        drive_dq     <= 1;            // Take control of the DQ bus
                        dq_out       <= wr_data;      // Put data on the bus
                        delay_cnt    <= 3;            // tWR (Write Recovery) + tRP (Precharge) 
                        state        <= IDLE;
                    end
                    
                    default: state <= INIT_WAIT;
                endcase
            end
        end
    end
endmodule