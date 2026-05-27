# iCESugar-Pro Framebuffer with LVGL

A display framebuffer implementation that utilizes the onboard SDRAM of the iCESugar-Pro FPGA to display a simple LVGL (Light and Versatile Graphics Library) application driven by an external microcontroller.

## Instructions

This repository contains the complete FPGA hardware design and a sample LVGL application. Follow the directions below to synthesize the bitstream for the FPGA and compile the software for a Raspberry Pi Pico.

### Requirements for FPGA Synthesis

This project requires **Yosys** and **nextpnr-ecp5**. Both of these tools, alongside the required ECP5 database (`prjtrellis`), are available pre-compiled as part of the [OSS CAD-SUITE](https://github.com/YosysHQ/oss-cad-suite-build). 

> [!WARNING]
> If you are on Ubuntu/Debian, do **not** install `nextpnr-ice40` or `fpga-icestorm` via `apt`. The iCESugar-Pro features a Lattice **ECP5** FPGA, which requires `nextpnr-ecp5`. It is highly recommended to use the OSS CAD-Suite linked above to ensure you have the correct, up-to-date tools.

### Requirements for Microcontroller Compilation

To compile the application for the Raspberry Pi Pico, you need the `arm-none-eabi` GCC toolchain. Refer to the official Arm page for installation instructions tailored to Windows, macOS, and Linux:

[Arm GNU Toolchain Downloads](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads)

#### Using the Bundled Docker Environment
Alternatively, a `Dockerfile` is provided in this project to generate a unified container capable of handling both FPGA synthesis and software compilation. 

To build the Docker image, run:
```bash
docker build . --tag cs122a-build-env

```

To run the container while mounting your current project directory workspace, execute:

```bash
docker run -it -v ./:/myprojects cs122a-build-env /bin/bash

```

### Building the Project

Before compiling, you must fetch the required LVGL submodule dependency. Navigate to the software directory and initialize it:

```bash
git submodule update --init --recursive

```

Once the submodules are fully downloaded, return to the project root directory and initialize the CMake build system:

```bash
mkdir build
cmake -B build -S .

```

To compile both the FPGA bitstream and the microcontroller executable simultaneously, run:

```bash
cmake --build build

```

### Programming the Hardware

Once the build system completes, the resulting binaries can be found in their respective build subdirectories:

* **Raspberry Pi Pico:** The executable binary is located at `build/sw/cs122_demo.uf2`. Flash this to your Pico by holding the `BOOTSEL` button while plugging it in.
* **iCESugar-Pro FPGA:** The hardware bitstream is located at `build/hw/top.bit`.

### Expected Output

If all components are compiled and wired correctly, you should see the Widgets Demo from LVGL. See [The LVGSL Demos Page](https://lvgl.io/demos).

## Using this Code in Your Custom Project

To adapt this codebase for a custom UI project, replace or modify the files inside the `sw/` directory with your own LVGL layout code.

The entry point in `main.cpp` sets up the foundational hardware rendering loop and system timers. Specifically:

* `cs122_flush_cb_partial`: Handles chunking and pushing framebuffer updates across the SPI bus to the FPGA.
* `cs122_get_millis`: Handles the internal system time tick requirements for LVGL animations and tasks.

*Note: This release does not yet support the display's integrated touch screen controller.*

## Hardware Signal Integrity & Grounding

If the LCD panel displays a scrambled, unstable, or tearing image, ensure that you have established a **clean, solid common ground connection** between one of the `GND` pins on the Raspberry Pi Pico and a `GND` pin on the iCESugar-Pro.

For reliable high-speed operation (SPI clocks above 10 MHz), it is highly recommended to run a ground wire physically paired/twisted alongside your `SCLK` wire to minimize loop inductance and clock edge ringing.

## Questions & Support

If you encounter issues or have questions regarding the timing blocks or SPI interfaces:

* **Email:** allan.knight@ucr.edu
* **Slack:** Send a DM directly to Allan Knight.

The course TAs/readers may be able to assist with general deployment questions, but as the author of this framework, contacting me directly is the fastest path to resolving hardware-specific bugs.


**The future is now:** Support for the touch screen overlay has been release.
