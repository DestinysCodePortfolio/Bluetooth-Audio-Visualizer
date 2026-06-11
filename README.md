# Bluetooth Audio Visualizer

Custom Laboratory Project — CS 122A
Destiny Gonzalez (`dgonz269`) and Ryan Diaz (`rdiaz082`)

## Demo Video

Final demo video: https://youtu.be/ZtCfBt4SaE8

## Overview

The Bluetooth Audio Visualizer is an embedded audio project built around a Raspberry Pi Pico 2W and an iCESugar-Pro FPGA display pipeline. The device can receive music from a phone over Bluetooth A2DP, decode the SBC audio stream, play the decoded PCM through a PAM8403 amplifier and speaker, and display an audio-reactive visualizer interface on a 4.3-inch 480×272 LCD.

When Bluetooth is unavailable, the project falls back to WAV files stored on a microSD card. The SD fallback uses a FAT filesystem playlist and streams 16-bit PCM samples into the same shared audio buffer used by Bluetooth, so the speaker output and visualizer are designed to react to whichever source is active.

## Current Implemented Behavior

* Bluetooth A2DP sink on the Raspberry Pi Pico 2W
* Real-time SBC decode into mono PCM samples
* microSD WAV fallback playlist using FATFS
* Shared PCM ring buffer for playback and visualization
* PWM audio output on GP26/GP27 for a PAM8403 amplifier
* LVGL-based visualizer interface on the LCD
* Oscilloscope-style waveform mode
* 32-column PCM level visualizer mode
* Source label showing `BLUETOOTH`, `SD CARD`, `FALLBACK`, or `NO SIGNAL`
* Button controls:

  * GP3: pause/play for SD fallback
  * GP4: next track for SD fallback
  * GP8: previous track for SD fallback
  * GP5: switch visualizer mode
* Bluetooth takes priority over SD playback; SD resumes when Bluetooth stops or disconnects

## Important Status Notes

The original proposal described a full FPGA FFT spectrum analyzer with SPI PCM transfer, FPGA-side FFT, beat detection, LCD timing, and FPGA PWM audio. The current implementation uses the Pico 2W as the main audio and visualization controller:

* The Pico handles Bluetooth A2DP, SBC decoding, SD WAV playback, audio buffering, PWM audio output, and LVGL rendering.
* The iCESugar-Pro FPGA is used for the external LCD framebuffer/display pipeline.
* The visualizer is implemented in LVGL as a Pico-rendered oscilloscope and 32-column PCM level display.
* The final 32-column visualizer is not a true FFT spectrum analyzer. It is based on recent PCM sample levels.
* The FPGA FFT pipeline was not fully integrated into the final demo.
* Audio output currently comes from Pico PWM pins GP26/GP27, not FPGA PWM or an external I2S DAC.

This keeps the final demo accurate: Bluetooth and SD WAV audio can play through the speaker, and the LCD can show the visualizer interface using the Pico-to-FPGA display path. Live visual synchronization was a major debugging area because the display and audio paths share timing-sensitive PCM buffering.

## Hardware Components

| Component       | Part                                                | Current Role                                                                                       |
| --------------- | --------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| Microcontroller | Raspberry Pi Pico 2W                                | Bluetooth A2DP sink, SBC decoder, WAV reader, source mux, audio buffer, PWM audio, LVGL visualizer |
| FPGA            | iCESugar-Pro ECP5 25K                               | External LCD framebuffer/display pipeline                                                          |
| Display         | 4.3-inch 480×272 RGB LCD with PMOD RGBLCD expansion | Shows LVGL visualizer interface                                                                    |
| Amplifier       | PAM8403 stereo amplifier                            | Amplifies Pico PWM audio output                                                                    |
| Speaker         | 3W 8-ohm speaker                                    | Music playback                                                                                     |
| Storage         | microSD SPI breakout + FAT32 microSD card           | WAV fallback playlist                                                                              |
| Controls        | Push buttons                                        | Pause, next, previous, and visual mode switching                                                   |

## Wiring Used by the Code

### Pico 2W to iCESugar-Pro Display Framebuffer

| Pico 2W Pin | FPGA Signal   | Purpose                            |
| ----------- | ------------- | ---------------------------------- |
| GP18        | `sclk`        | SPI display clock                  |
| GP19        | `pico` / MOSI | SPI display data from Pico to FPGA |
| GP17        | `cs_n`        | SPI chip select                    |
| GP20        | `data_cmd`    | Display command/data select        |
| GND         | GND           | Shared ground                      |

A common ground between the Pico and FPGA is required. If the LCD shows the UI text, the Pico-to-FPGA display path is mostly working.

### PWM Audio Output

| Pico 2W Pin       | Signal          | Connects To                           |
| ----------------- | --------------- | ------------------------------------- |
| GP26              | Left PWM audio  | PAM8403 L input                       |
| GP27              | Right PWM audio | PAM8403 R input                       |
| GND               | Ground          | PAM8403 GND and shared system ground  |
| 5V or external 5V | Power           | PAM8403 VCC, depending on power setup |

The final audio output uses filtered PWM. This works for demonstration playback, but it is not as clean as using an external DAC.

### microSD SPI Pins

The current code is configured for SPI1:

| Pico 2W Pin | SD Signal |
| ----------- | --------- |
| GP12        | MISO      |
| GP13        | CS        |
| GP14        | SCK       |
| GP15        | MOSI      |
| 3.3V        | VCC       |
| GND         | GND       |

If the SD module is wired to different pins, update the `SD_MISO_PIN`, `SD_CS_PIN`, `SD_SCK_PIN`, and `SD_MOSI_PIN` defines in both `sd_wav_fallback.cpp` and `sd_spi_probe.cpp`.

### Buttons

Buttons are active-low using internal pull-ups:

| Pico 2W Pin | Button Function        |
| ----------- | ---------------------- |
| GP3         | Pause/play SD playback |
| GP4         | Next SD track          |
| GP8         | Previous SD track      |
| GP5         | Switch visualizer mode |

Each button connects the GPIO pin to ground when pressed.

## SD Card WAV Requirements

Format the card as FAT32 and place these files in the root directory:

```text
song1.wav
song2.wav
song3.wav
song4.wav
song5.wav
```

Supported WAV format:

* PCM format 1 only
* 16-bit samples
* Mono or stereo
* 44.1 kHz recommended
* 48 kHz should also work because the PWM sample rate is updated from the WAV header

For the cleanest playback, export the files as 16-bit PCM WAV instead of renaming MP3 files to `.wav`.

## Audio Flow

Bluetooth path:

```text
Phone audio
   |
   v
Bluetooth A2DP stream
   |
   v
Pico 2W SBC decoder
   |
   v
Mono PCM samples
   |
   v
Shared PCM ring buffer
   |
   +--> PWM audio output --> PAM8403 amplifier --> speaker
   |
   +--> LVGL visualizer snapshot --> LCD visualizer interface
```

SD fallback path:

```text
microSD WAV file
   |
   v
FATFS reader + WAV parser
   |
   v
Mono PCM samples
   |
   v
Shared PCM ring buffer
   |
   +--> PWM audio output --> PAM8403 amplifier --> speaker
   |
   +--> LVGL visualizer snapshot --> LCD visualizer interface
```

## SD Playback Fix

The SD reader is paced by the number of samples already waiting in the audio buffer. This prevents the SD task from forcing too many samples into the buffer too quickly. Without pacing, the ring buffer can overflow and skip older samples, which makes music sound too fast or warped.

The fixed SD path only reads more WAV frames when the buffer drops below its target fill level.

## Visualizer Behavior

The LCD visualizer includes:

* A top status bar
* Current source label
* Oscilloscope-style waveform mode
* 32-column PCM level visualizer mode
* Dynamic color mapping based on amplitude
* Beat/peak-reactive visual changes
* GP5 button control to switch visual modes

The visualizer is a live PCM visualizer, not a mathematically true FFT spectrum analyzer. It is designed to show audio-reactive motion for both Bluetooth and SD WAV playback using the same PCM audio path as the speaker output.

## Known Issues and Limitations

* The final speaker output uses Pico PWM instead of a dedicated DAC or I2S audio module, so some high-frequency PWM noise and distortion can remain audible.
* The FPGA FFT spectrum analyzer from the original proposal was not fully integrated into the final bitstream.
* The 32-column bars mode is PCM-level based, not true FFT frequency analysis.
* Full-screen LVGL updates over the Pico-to-FPGA SPI framebuffer path are bandwidth-limited, so the display is not as smooth as a fully FPGA-rendered visualizer would be.
* Live visual synchronization was a major debugging challenge because audio playback and visualization both depend on timing-sensitive PCM buffering.
* Bluetooth AVRCP physical playback control was not included in the final build because phone-side playback control was more reliable during testing.

## Build Notes

Typical build flow:

```powershell
mkdir build
cd build
cmake -G Ninja ..
ninja
```

Or, if the build directory already exists:

```powershell
cmake --build build --clean-first
```

Flash the generated `.uf2` file to the Pico 2W. Open serial output to check SD mount, WAV parsing, Bluetooth status, and playback state logs.

## Repository

Final project repository:
https://github.com/DestinysCodePortfolio/Bluetooth-Audio-Visualizer

Final demo video:
https://youtu.be/ZtCfBt4SaE8

## Acknowledgements

* Raspberry Pi Pico SDK — https://github.com/raspberrypi/pico-sdk
* BTstack — https://github.com/bluekitchen/btstack
* pico_fatfs — https://github.com/elehobica/pico_fatfs
* LVGL — https://lvgl.io/
* iCESugar-Pro FPGA board — MUSELAB Technology
* UCR CS122A iCESugar-Pro framebuffer reference project
