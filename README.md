# Bluetooth Audio Visualizer
## Custom Laboratory Project Proposal

**Destiny Gonzalez** – `dgonz269`  
**Ryan Diaz** – `rdiaz082`

---

# Introduction

A **Bluetooth Audio Visualizer** is an embedded multimedia device that receives music wirelessly from a phone and displays a real-time visual representation of the audio on an LCD screen. It allows users to play music from their phone via Bluetooth while the device analyzes the incoming audio and renders a live frequency spectrum and beat-reactive color effects synchronized to the sound.

In this project, a **Raspberry Pi Pico 2W** and **iCESugar-Pro FPGA** based audio visualizer will be created. The user pairs their phone to the device over Bluetooth, and music plays through a speaker while the FPGA renders a live spectrum analyzer on the 4.3" RGB LCD. A fallback mode allows audio to be loaded from a microSD card as WAV files when Bluetooth is unavailable.

The system aims to replicate the feel of a standalone audio visualizer device similar to an MP3 player, focusing on:

- **Real-time audio processing**
- **Wireless connectivity**
- **Responsive visual output**

---

# System Architecture

```text
                    Bluetooth Audio Path

┌──────────────┐      A2DP / SBC Stream      ┌────────────────────┐
│    Phone     │ ─────────────────────────► │ Raspberry Pi Pico 2W│
│  (Spotify)   │                             │                    │
└──────────────┘                             │ • BT A2DP Sink     │
                                             │ • SBC Decoder      │
                                             │ • Audio Source Mux │
                                             │ • WAV Parser       │
                                             │ • SPI Controller   │
                                             └─────────┬──────────┘
                                                       │
                            16-bit PCM Samples (SPI)   │
                                                       ▼
                                             ┌────────────────────┐
                                             │  iCESugar-Pro FPGA │
                                             │                    │
                                             │ • SPI Receiver     │
                                             │ • FFT Processing   │
                                             │ • Spectrum Renderer│
                                             │ • Beat Detection   │
                                             │ • LCD Controller   │
                                             │ • PWM Audio Output │
                                             └──────┬───────┬─────┘
                                                    │       │
                                      RGB Parallel  │       │ PWM Audio
                                      LCD Signals   │       │
                                                    ▼       ▼
                                        ┌─────────┐   ┌────────────┐
                                        │ 4.3" LCD│   │ PAM8403 Amp│
                                        │ Display │   └─────┬──────┘
                                        └─────────┘         │
                                                            ▼
                                                      ┌─────────┐
                                                      │ Speaker │
                                                      └─────────┘


                    Fallback Audio Source

┌──────────────────┐     SPI / FAT32     ┌────────────────────┐
│   microSD Card   │ ─────────────────► │ Raspberry Pi Pico 2W│
│   WAV Storage    │                    │   WAV File Reader   │
└──────────────────┘                    └────────────────────┘
```

The **Pico 2W** is the central audio source and processor. It receives audio from either:

1. **Bluetooth audio** (A2DP SBC stream from a phone)
2. **microSD WAV files** using `fat_io_lib`

The Pico decodes/parses the audio into PCM samples and streams those samples to the FPGA over SPI.

The **iCESugar-Pro FPGA** receives the PCM stream, performs FFT-based frequency analysis, drives the LCD spectrum display, and outputs audio to the speaker via a PWM/PAM8403 amplifier path.

---

# Hardware Components

| Component | Part | Description |
|---|---|---|
| **Microcontroller** | Raspberry Pi Pico 2W | Bluetooth A2DP audio sink, SBC decoder, microSD WAV reader (`fat_io_lib`), source mux between BT and SD, SPI primary streaming PCM to the FPGA |
| **FPGA** | iCESugar-Pro FPGA | Receives PCM samples over SPI as peripheral, performs FFT, drives the RGB LCD display, and generates speaker audio output |
| **Display** | 4.3" 480×272 RGB LCD with PMOD RGBLCD Expansion Board | Displays the live audio visualization using FPGA-generated RGB timing |
| **Amplifier** | PAM8403 Stereo Amplifier Module | Amplifies FPGA PWM audio output to drive the speaker |
| **Speaker** | 3W 8-ohm Speaker | Audio output for music playback |
| **Storage** | MicroSD SPI Breakout + 32GB microSD Card | Stores WAV files as fallback audio source |
| **Controls** | DIP Switch | Used for mode switching and volume control |

---

# Functionality

## Microcontroller — Raspberry Pi Pico 2W

### Features
- Bluetooth A2DP sink with real-time SBC decoding
- microSD WAV reader using `fat_io_lib`
- Audio source mux between Bluetooth and SD card
- SPI primary controller for PCM streaming
- DMA-driven PCM transfer pipeline
- Event-driven interrupt architecture

### Event-Driven Components
- DMA-complete IRQ handlers
- GPIO interrupt handlers for DIP switches
- Timer interrupt callback for volume control

---

## FPGA — iCESugar-Pro (Verilog)

### Audio Processing
- Custom SPI peripheral receiver
- PCM sample ring buffer in BRAM
- Fixed-point radix-2 FFT
- Hann windowing function

### Visualization
- 32-band spectrum renderer
- Dynamic color mapping
- Beat detection with reactive flash effects
- Double-buffered framebuffer

### Display + Audio Output
- LCD timing controller (HSYNC/VSYNC/pixel clock)
- PWM audio output to PAM8403 amplifier
- Real-time synchronized visualization
