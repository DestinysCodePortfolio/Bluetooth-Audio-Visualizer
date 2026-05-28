module spi
(
    input  logic CLK,
    input  logic SCK,
    input  logic MOSI,
    input  logic CS,

    output logic [7:0]  waddr,
    output logic [15:0] wdata,
    output logic        we
);  

    logic [15:0] shift_reg;
    logic [4:0]  bit_counter;
    logic [7:0]  address_counter;  

    initial begin
        shift_reg       = 0;
        bit_counter     = 0;
        address_counter = 0; 
        we              = 0;  
    end

    always @(posedge SCK) begin  
        if (CS == 0) begin       
            shift_reg   <= (shift_reg << 1) | MOSI;
            bit_counter <= bit_counter + 1;

            if (bit_counter == 15) begin
                wdata           <= (shift_reg << 1) | MOSI;
                waddr           <= address_counter;
                we              <= 1;
                if (address_counter == 255) begin
                    address_counter <= 0;
                end else begin
                    address_counter <= address_counter + 1;
                end
                bit_counter <= 0;
            end else begin
                we <= 0;
            end 

        end else begin
            bit_counter <= 0;
            we          <= 0;
        end
    end

endmodule