FROM ubuntu:24.04

# Set up apt and get tools and SDKs necessary for projects
RUN apt upgrade && apt update

# Get HW build tools
RUN apt install -y yosys nextpnr-ice40 fpga-icestorm iverilog

# Get SW build tools & PICO SDK
RUN apt install -y git openocd gdb-multiarch
RUN mkdir /pico
WORKDIR /pico
RUN git clone https://github.com/raspberrypi/pico-sdk.git --branch master
WORKDIR /pico/pico-sdk
RUN git submodule update --init
WORKDIR /pico
RUN git clone https://github.com/raspberrypi/pico-examples.git --branch master
RUN apt update
RUN apt install -y cmake gcc-arm-none-eabi libnewlib-arm-none-eabi build-essential
RUN apt install -y g++ libstdc++-arm-none-eabi-newlib
WORKDIR  /pico/pico-sdk
RUN git pull
RUN git submodule update

ENV PICO_SDK_PATH=/pico/pico-sdk

RUN mkdir -p /myprojects
WORKDIR /myprojects

ENTRYPOINT [ "tail", "-f", "/dev/null"]