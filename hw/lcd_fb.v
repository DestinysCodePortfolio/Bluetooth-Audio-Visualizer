

module icesugar_pro_lcd_fb (
    // iCESugar-Pro 25MHz onboard clock (Pin P6)
    input  wire        clk_25m,       

    // IS42S16160B SDRAM Interface
    output wire        sdram_clk,
    output wire        sdram_cke,
    output wire        sdram_cs_n,
    output wire        sdram_ras_n,
    output wire        sdram_cas_n,
    output wire        sdram_we_n,
    output wire [1:0]  sdram_ba,
    output wire [12:0] sdram_a,
    output wire [1:0]  sdram_dqm,
    inout  wire [15:0] sdram_dq,

    // RGB LCD Interface (480x272)
    output wire        lcd_clk,
    output wire        lcd_hsync,
    output wire        lcd_vsync,
    output wire        lcd_de,
    output wire [4:0]  lcd_r,
    output wire [5:0]  lcd_g,
    output wire [4:0]  lcd_b,
    
    // CPU/GPU Write Interface (To draw to the screen)
    input  wire        wr_en,
    input  wire [23:0] wr_addr,       
    input  wire [15:0] wr_data,
    output wire        wr_ack,
    output  wire       clk_100m,
    output wire        locked
);

    wire clk_sys;     // 100 MHz for SDRAM
    wire clk_pixel;   // 9 MHz for LCD
    //wire locked;
    wire rst = ~locked; 
    
    assign clk_100m = clk_sys;
    
    // --------------------------------------------------------
    // 1. Clocks
    // --------------------------------------------------------
    pll pll_inst (
        .clkin(clk_25m),
        .clkout0(clk_sys),
        .clkout1(clk_pixel),
        .locked(locked)
    );

    // Forward system clock to SDRAM
    ODDRX1F sdram_clk_forward (
        .SCLK(clk_sys),
        .RST(1'b0),
        .D0(1'b0),
        .D1(1'b1),
        .Q(sdram_clk)
    );
    
    // Forward pixel clock to LCD panel
    ODDRX1F lcd_clk_forward (
        .SCLK(clk_pixel),
        .RST(1'b0),
        .D0(1'b1),
        .D1(1'b0),
        .Q(lcd_clk)
    );

    assign sdram_cke = 1'b1;

    // --------------------------------------------------------
    // 2. Video Timing & RGB Output
    // --------------------------------------------------------
    wire        new_frame;
    wire [15:0] pixel_data;

    lcd_timing_480x272 timing_inst (
        .clk_pixel(clk_pixel),
        .rst(rst),
        .hsync(lcd_hsync),
        .vsync(lcd_vsync),
        .de(lcd_de),
        .new_frame(new_frame)
    );

    // Map the 16-bit FIFO output to RGB565 physical pins
    assign lcd_r = pixel_data[15:11];
    assign lcd_g = pixel_data[10:5];
    assign lcd_b = pixel_data[4:0];

    // --------------------------------------------------------
    // 3. Clock Domain Crossing FIFO
    // --------------------------------------------------------
    wire        rd_req;
    wire        rd_ack;
    wire [15:0] rd_data;
    wire        rd_data_valid;
    wire        fifo_almost_empty;
    
    // Request data from SDRAM when FIFO space is available
    assign rd_req = fifo_almost_empty;

    async_fifo #(
        .DATA_WIDTH(16),
        .ADDR_WIDTH(9),            // 512 words
        .ALMOST_EMPTY_THRESH(64)   // Request data early to prevent underflow
    ) video_fifo (
        .wr_clk(clk_sys),
        .wr_rst(rst || new_frame_sys_2),
        .wr_en(rd_data_valid),
        .din(rd_data),

        .rd_clk(clk_pixel),
        .rd_rst(rst || new_frame),
        .rd_en(lcd_de),            // Pop a pixel from FIFO only when actively drawing
        .dout(pixel_data),
        .empty(),                  // Ignored, visually manifests as black screen if it occurs
        .almost_empty(fifo_almost_empty)
    );

    // --------------------------------------------------------
    // 4. SDRAM Read Address Generator
    // --------------------------------------------------------
    // We must track where we are in memory to supply the FIFO
    reg [23:0] sdram_rd_addr;
    
    // Cross the new_frame signal from clk_pixel to clk_sys domain
    reg new_frame_sys_1, new_frame_sys_2;
    always @(posedge clk_sys) begin
        new_frame_sys_1 <= new_frame;
        new_frame_sys_2 <= new_frame_sys_1;
    end

    always @(posedge clk_sys or posedge rst) begin
        if (rst) begin
            sdram_rd_addr <= 24'd0;
        end else begin
            // Reset address pointer at the start of a new frame
            if (new_frame_sys_2) begin
                sdram_rd_addr <= 24'd0;
            end 
            // Increment address pointer whenever the SDRAM accepts a read request
            else if (rd_ack) begin
                sdram_rd_addr <= sdram_rd_addr + 1'b1;
            end
        end
    end

    // --------------------------------------------------------
    // 5. SDRAM Controller Engine
    // --------------------------------------------------------
    sdram_controller sdram_ctrl_inst (
        .clk(clk_sys),
        .rst(rst),
        
        .wr_req(wr_en),
        .wr_addr(wr_addr),
        .wr_data(wr_data),
        .wr_ack(wr_ack),
        
        .rd_req(rd_req),
        .rd_addr(sdram_rd_addr),   // Continuous address stream
        .rd_data(rd_data),
        .rd_valid(rd_data_valid),
        .rd_ack(rd_ack),

        .sdram_cs_n(sdram_cs_n),
        .sdram_ras_n(sdram_ras_n),
        .sdram_cas_n(sdram_cas_n),
        .sdram_we_n(sdram_we_n),
        .sdram_ba(sdram_ba),
        .sdram_a(sdram_a),
        .sdram_dqm(sdram_dqm),
        .sdram_dq(sdram_dq)
    );

endmodule