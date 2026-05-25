module lcd_timing_480x272 (
    input  wire clk_pixel, // 9 MHz
    input  wire rst,
    output wire  hsync,
    output wire  vsync,
    output wire  de,        // Data Enable (Active pixels)
    output wire new_frame  // Pulses high at the start of a new frame
);

    // Typical 480x272 RGB Panel Timings
    localparam H_ACTIVE = 480;
    localparam H_FP     = 8;
    localparam H_SYNC   = 4;
    localparam H_BP     = 4;
    localparam H_TOTAL  = H_ACTIVE + H_FP + H_SYNC + H_BP; // 572

    localparam V_ACTIVE = 272;
    localparam V_FP     = 8;
    localparam V_SYNC   = 4;
    localparam V_BP     = 8;
    localparam V_TOTAL  = V_ACTIVE + V_FP + V_SYNC + V_BP; // 298

    reg [9:0] h_cnt;
    reg [9:0] v_cnt;

    assign new_frame = (h_cnt == 0 && v_cnt == 0);

    always @(posedge clk_pixel or posedge rst) begin
        if (rst) begin
            h_cnt <= 0;
            v_cnt <= 0;
        end else begin
            // Horizontal Counter
            if (h_cnt == H_TOTAL - 1) begin
                h_cnt <= 0;
                // Vertical Counter
                if (v_cnt == V_TOTAL - 1)
                    v_cnt <= 0;
                else
                    v_cnt <= v_cnt + 1;
            end else begin
                h_cnt <= h_cnt + 1;
            end
        end
    end

    // Sync Pulses (Active Low for most RGB LCDs, check your datasheet)
    assign hsync = h_cnt >= H_SYNC; // ~( (h_cnt >= (H_ACTIVE + H_FP)) && (h_cnt < (H_ACTIVE + H_FP + H_SYNC)) );
    assign vsync = v_cnt >= V_SYNC; // ~( (v_cnt >= (V_ACTIVE + V_FP)) && (v_cnt < (V_ACTIVE + V_FP + V_SYNC)) );

    // Data Enable (Active High)
    assign de = (h_cnt >= (H_SYNC + H_BP)) && (h_cnt < (H_SYNC + H_BP + H_ACTIVE)) &&
            (v_cnt >= (V_SYNC + V_BP)) && (v_cnt < (V_SYNC + V_BP + V_ACTIVE));

endmodule