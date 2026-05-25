# Bluetooth Audio Visualizer

**Custom Laboratory Project Proposal**
Destiny Gonzalez – `dgonz269` | Ryan Diaz – `rdiaz082`

---

## Introduction

A **Bluetooth Audio Visualizer** is an embedded multimedia device that receives music wirelessly from a phone and displays a real-time visual representation of the audio on an LCD screen. It allows users to play music from their phone via Bluetooth while the device analyzes the incoming audio and renders a live frequency spectrum and beat-reactive color effects synchronized to the sound.

In this project, a **Raspberry Pi Pico 2W** and **IceSugar-Pro FPGA**-based audio visualizer will be created. The user pairs their phone to the device over Bluetooth, and music plays through a speaker while the FPGA renders a live spectrum analyzer on the 4.3" RGB LCD. A fallback mode allows audio to be loaded from a microSD card as WAV files when Bluetooth is unavailable.

The system aims to replicate the feel of a standalone audio visualizer device similar to an MP3 player, focusing on **real-time audio processing**, **wireless connectivity**, and **responsive visual output**.

---

## Hardware Components

| Component | Part | Description |
|-----------|------|-------------|
| **Microcontroller** | Raspberry Pi Pico 2W | Used as the Bluetooth A2DP audio sink, SBC decoder, PWM audio output engine, and SPI master for streaming PCM samples to the FPGA. |
| **FPGA** | IceSugar-Pro FPGA | Performs frequency analysis and drives the RGB LCD display. Implements spectrum bar renderer, beat detection logic, and full LCD timing controller in Verilog. |
| **Display** | 4.3" 480×272 RGB LCD with PMOD RGBLCD Expansion Board | Displays the live audio visualization. Driven directly by the FPGA via a parallel RGB interface with HSYNC/VSYNC timing generated in Verilog. |
| **Amplifier** | PAM8403 Stereo Amplifier Module | Receives PWM output from the Pico and drives the speaker at sufficient power for audible playback. |
| **Speaker** | 3W 8-ohm Speaker | Audio output for music playback. |
| **Storage** | MicroSD SPI Breakout Module + 32GB microSD Card (FAT32) | Fallback audio source. FPGA reads WAV files directly over SPI using a FAT32 reader implemented in Verilog. |
| **Controls** | DIP Switch | Connected to Pico GPIO pins for volume control and Bluetooth/SD card mode switching. |

---

## Functionality

### Microcontroller (Pico 2W)
- Bluetooth A2DP sink with real-time SBC decoding
- DMA-driven PCM pipeline with DMA-complete IRQ (event-driven)
- GPIO interrupt handlers for DIP switch controls (event-driven)
- Timed interrupt callback for volume control (event-driven)

### FPGA (IceSugar-Pro — Verilog)
- Custom SPI slave receiver module
- FAT32 WAV file reader
- Fixed-point radix-2 FFT with Hann windowing
- 32-band spectrum renderer with dynamic color mapping
- Beat detection with reactive color flash effects
- Double-buffered frame buffer in BRAM
- Full LCD timing controller — HSYNC/VSYNC/pixel clock
