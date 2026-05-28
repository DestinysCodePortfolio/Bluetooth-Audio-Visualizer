`include "spi_peripheral.v"
`define CMD_DRAWLINE            8'h21
`define CMD_DRAWRECTANGLE       8'h22
`define CMD_COPYWINDOW          8'h23
`define CMD_DIMWINDOW           8'h24
`define CMD_CLEARWINDOW         8'h25
`define CMD_FILLWINDOW          8'h26
`define CMD_SETCOLUMNADDRESS    8'h15
`define CMD_SETROWADDRESS       8'h75
`define CMD_SETDISPLAYSTARTLINE 8'hA1
`define CMD_SETDISPLAYOFFSET    8'hA2
`define CMD_NORMALDISPLAY       8'hA4
`define CMD_ENTIREDISPLAYON     8'hA5
`define CMD_DISPLAYOFF          8'hAE
`define CMD_DISPLAYON    		8'hAF

module display_controller #( 
    parameter DISPLAY_WIDTH  = 480, 
    parameter DISPLAY_HEIGHT = 272,
    parameter DATA_WIDTH     = 8,
    parameter CMD_WIDTH      = 8
) (
     input clk,
     input reset,
     input disp_CS_n,
     input disp_SCLK,
     input disp_PICO,
     output disp_POCI,
     input data_cmd,
     input [7:0] pixel_in,
     output reg [23:0] framebuffer_addr_out,
     output reg [7:0] framebuffer_data_out,
     output reg framebuffer_wren
);

    localparam INIT = 0,
               CMD_WAIT      = 1,
               CMD_WAIT_DATA = 2,
               DATA_WAIT     = 3,
               EXEC_CMD      = 4;

    localparam START_COL_INDEX = 0,
               START_ROW_INDEX = 1,
               FINISH_COL_INDEX = 2,
               FINISH_ROW_INDEX = 3;


    localparam START_COL_INDEX_HB = 0,
               START_COL_INDEX_LB = 1,
               START_ROW_INDEX_HB = 2,
               START_ROW_INDEX_LB = 3,
               FINISH_COL_INDEX_HB = 4,
               FINISH_COL_INDEX_LB = 5,
               FINISH_ROW_INDEX_HB = 6,
               FINISH_ROW_INDEX_LB = 7;

    reg [CMD_WIDTH-1:0] curr_cmd = 0;
    reg [CMD_WIDTH-1:0] cmd_data [9:0];
    reg [4:0] cmd_data_index = 0;
    integer cmd_data_count = 0;
    reg cmd_data_set = 0;

    reg [3:0] state = INIT;

    wire [CMD_WIDTH-1:0] data;
    wire disp_data_ready;

    reg [15:0] window_x = 0;
    reg [15:0] window_y = 0;
    reg [15:0] start_col_index = 0;
    reg [15:0] finish_col_index = 0;
    reg [15:0] start_row_index = 0;
    reg [15:0] finish_row_index = 0;

    reg busy;

    integer i;
    initial begin
        framebuffer_addr_out <= 0;
        framebuffer_data_out <= 0;
        framebuffer_wren <= 0;
        busy <= 0;
        for (i = 0; i < 10; i = i + 1) begin
            cmd_data[i] <= 0;
        end
    end

    spi_peripheral # (.SPI_MODE(0)) spi_perph(
        .clk(clk),
        .reset(reset),
        .i_CS_n(disp_CS_n),
        .i_SCLK(disp_SCLK),
        .i_PICO(disp_PICO),
        .o_POCI(disp_POCI),
        .o_rx_ready(disp_data_ready),
        .o_rx_data(data)
    );

    task UPDATE_PIXEL(
        input [7:0] pixel
    );
        begin
            UPDATE_DISP_ADDR(window_x, window_y, pixel);
            if (window_y > finish_row_index) begin
                window_x <= start_col_index;
                window_y <= start_row_index;
            end else begin
                if (window_x >= finish_col_index) begin
                    window_x <= start_col_index;
                    window_y <= window_y + 1;
                end else begin
                    window_x <= window_x + 1;
                end
            end
        end
    endtask

    task UPDATE_DISP_ADDR(
        input [15:0] x,
        input [15:0] y,
        input [7:0] p
    );
        begin
            framebuffer_addr_out <= x + ({8'b0, y} << 10) - ({8'b0, y} << 6);
            framebuffer_data_out <= p;
        end
    endtask

    task INIT_SPI();
        begin
            curr_cmd <= 0;
            framebuffer_wren <= 0;
            state <= data_cmd == 1 ? DATA_WAIT : CMD_WAIT;
            cmd_data_index <= 0;
            busy <= 0;
            cmd_data_count <= 0;
            window_x <= start_col_index;
            window_y <= start_row_index;
            cmd_data_set <= 0;
        end
    endtask

    always @(posedge clk) begin
        if (reset) begin
            busy <= 0;
            window_x <= 0;
            window_y <= 0;
            cmd_data_index <= 0;
            state <= INIT;
        end else begin
            case (state)
                INIT: begin
                    INIT_SPI();
                end

                CMD_WAIT: begin
                    if (data_cmd == 1) begin
                        state <= DATA_WAIT;
                    end else if (disp_data_ready) begin
                        curr_cmd <= data;
                        case (data)
                            `CMD_CLEARWINDOW       : cmd_data_count <= 8;
                            `CMD_DRAWLINE          : cmd_data_count <= 7;
                            `CMD_SETCOLUMNADDRESS, 
                            `CMD_SETROWADDRESS     : cmd_data_count <= 4;
                            `CMD_DRAWRECTANGLE     : cmd_data_count <= 7;
                            `CMD_FILLWINDOW,
                            `CMD_SETDISPLAYSTARTLINE,
                            `CMD_SETDISPLAYOFFSET  : cmd_data_count <= 1;

                            default                : cmd_data_count <= 0;
                        endcase
                        state <= CMD_WAIT_DATA;
                    end
                end

                CMD_WAIT_DATA: begin
                    if (cmd_data_index >= cmd_data_count) begin
                        cmd_data_index <= 0;
                        busy <= 1;
                        state <= data_cmd == 1 ? DATA_WAIT : EXEC_CMD;
                    end else if (disp_data_ready) begin 
                        cmd_data[cmd_data_index] <= data;
                        cmd_data_index <= cmd_data_index + 1;
                    end
                end

                EXEC_CMD: begin
                    if (data_cmd == 1) INIT_SPI();
                end

                DATA_WAIT: begin
                    busy <= 1;
                    framebuffer_wren <= 0;
                    if (disp_data_ready && data_cmd == 1) begin
                        framebuffer_wren <= 1;
                        UPDATE_PIXEL(data);
                    end else if (~data_cmd) begin
                        INIT_SPI();
                    end
                end
            endcase
            if (busy) begin
                case(curr_cmd)
                    `CMD_CLEARWINDOW: begin
                        framebuffer_wren <= 0;
                        if (~cmd_data_set) begin
                            start_col_index <= {cmd_data[START_COL_INDEX_HB],cmd_data[START_COL_INDEX_LB]};
                            start_row_index <= {cmd_data[START_ROW_INDEX_HB],cmd_data[START_ROW_INDEX_LB]};
                            finish_col_index <= {cmd_data[FINISH_COL_INDEX_HB],cmd_data[FINISH_COL_INDEX_LB]};
                            finish_row_index <= {cmd_data[FINISH_ROW_INDEX_HB],cmd_data[FINISH_ROW_INDEX_LB]};
                            cmd_data_set <= 1;
                        end else begin
                            if (window_y > finish_row_index - start_row_index) begin
                                INIT_SPI();
                            end else begin
                                //framebuffer_wren <= 1;
                                //UPDATE_PIXEL(8'h00);
                            end
                        end
                    end

                    `CMD_SETCOLUMNADDRESS: begin
                        if (~cmd_data_set) begin
                            start_col_index  <=  {cmd_data[0],cmd_data[1]};
                            finish_col_index <=  {cmd_data[2],cmd_data[3]};
                            cmd_data_set <= 1;
                        end else INIT_SPI();
                    end

                    `CMD_SETROWADDRESS: begin
                        if (~cmd_data_set) begin
                            start_row_index  <= {cmd_data[0],cmd_data[1]};
                            finish_row_index <= {cmd_data[2],cmd_data[3]};
                            cmd_data_set <= 1;
                        end else INIT_SPI();
                    end

                    default: begin
                    end
                endcase
            end
        end
    end

endmodule