# icesugar-pro-framebuffer

Framebuffer that uses the Icesugar pro SDRAM and displays a simple pattern

## Instructions

As promised, here are directions on how to synthesize and use the project in this repo.

### Requirements for Synthesizing

This project requires Yosys and Nextpnr. Both of these tools are available as part of the 
[OSS CAD-SUITE](https://github.com/YosysHQ/oss-cad-suite-build?tab=readme-ov-file#installation). 
Once those tools are installed, you should be able to synthesize this project.

If you are on Ubuntu, these tools are available through the following apt package:
1. Yosys - fpga-icestorm
2. Nextpnr - nextpnr-ice40

### Synthesizing

The following command will synthesize the bitstream for this project:

```bash
make clean && make
```

The first `make clean` is not necessary always, but is a good idea to make sure your code changes
always get synthesized.

### Programming the Icesugar Pro with the Bitstream

Once you've synthesized the code you can program the Icesugar Pro device using the file system. Using
the file browser drag and drop top.bin to the drive named iCELink. 

### What you should see

If all went well you should see a series of blue and green vertical stripes, each of which is 16-pixels wide.
Those pixels where written to the SDRAM just after the device powers up. 

## Using this code in your custom project

To use this code in your custom project to display, for example, framebuffers from LVGL, add code that replaces
the code in `top.v` that writes the stripes to memory. This code looks like the following: 

```verilog
    reg [23:0] pixel_addr = 24'h00000;
    integer clk_count = 0;

    always @(posedge clk_25m) begin
        if (clk_count < 10000) begin
            clk_count <= clk_count + 1;
        end else
        if (pixel_addr <= 24'h01FFFF) begin
            wr_addr <= pixel_addr;
            wr_data <= pixel_addr[4] ? 16'h07E0 : 16'h001F;
            wr_en <= 1;
            pixel_addr <= pixel_addr + 1;
        end else begin
            wr_en <= 0;
        end
```

The relavent ports are `wr_en`, `wr_addr` and `wr_data`. The following table summarizes these ports:

| Name    | Size    | Direction | Description                                                    |
|---------|--------:|:---------:|----------------------------------------------------------------|
| wr_en   | 1-bit   | input     | Requests data in `wr_data` be written to address in `wr_addr`. |
| wr_addr | 23-bits | input     | Address of data in `wr_data` is written when `wr_en` is 1.     |
| wr_data | 16-bits | input     | The data to be written to `wr_address` when `wr_en` is 1.      |

You can replace that code above with your code that receives commands and data over SPI from the Raspberry Pi Pico
micro controller. It's similar to what you did in the Sprite lab, but now instead of 256 bytes, you are writing 
rectangles to the framebuffer using the above ports. 

## Questions?

Email me, allan.knight@ucr.edu, or DM me on Slack if you have questions. RM and Marios maybe able to answer questions
about this code, but I wrote it and have a good understanding of how it works.


**Note:** I will add more directions on 5/13/2026
