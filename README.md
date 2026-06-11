# Bluetooth Audio Visualizer

Custom Laboratory Project — CS 122A  
Destiny Gonzalez (`dgonz269`) and Ryan Diaz (`rdiaz082`)

## Overview

The Bluetooth Audio Visualizer is an embedded audio project built around a Raspberry Pi Pico 2W. The device can receive music from a phone over Bluetooth A2DP, decode the SBC audio stream, play the decoded PCM through a PAM8403 amplifier and speaker, and show a live audio-reactive visualization on a 4.3-inch 480×272 display.

When Bluetooth is unavailable, the project falls back to WAV files stored on a microSD card. The SD fallback uses a FAT filesystem playlist and streams 16-bit PCM samples into the same shared audio buffer used by Bluetooth, so the speaker output and visualizer both react to whichever source is active.

## Current Implemented Behavior

- Bluetooth A2DP sink on the Raspberry Pi Pico 2W
- Real-time SBC decode into mono PCM samples
- microSD WAV fallback playlist using FATFS
- Shared PCM ring buffer for playback and visualization
- PWM audio output on GP26/GP27 for a PAM8403 amplifier
- LVGL-based real-time audio visualizer on the LCD
- Source label showing `BLUETOOTH`, `SD CARD`, `FALLBACK`, or `NO SIGNAL`
- Button controls for SD fallback mode:
  - GP3: pause/play
  - GP4: next track
  - GP8: previous track
- Bluetooth takes priority over SD playback; SD resumes when Bluetooth stops or disconnects

## Important Status Notes

The original proposal described a full FPGA FFT spectrum analyzer with SPI PCM transfer, FPGA-side FFT, beat detection, LCD timing, and FPGA PWM audio. The current codebase instead uses the Pico 2W as the main audio and visualization controller:

- The visualizer is implemented in LVGL as a 32-column PCM level display with an oscilloscope trace overlay.
- The visualization reads directly from the shared PCM audio buffer.
- The FPGA FFT pipeline is not fully implemented in the current software path.
- Audio output currently comes from Pico PWM pins GP26/GP27, not FPGA PWM audio.

This keeps the final demo accurate: Bluetooth and SD WAV audio can play through the speaker, and the LCD can show a live visual response using the same audio samples.

## Hardware Components

| Component | Part | Current Role |
|---|---|---|
| Microcontroller | Raspberry Pi Pico 2W | Bluetooth A2DP sink, SBC decoder, WAV reader, source mux, audio buffer, PWM audio, LVGL visualizer |
| Display | 4.3-inch 480×272 RGB/SPI display path | Shows LVGL audio visualizer |
| Amplifier | PAM8403 stereo amplifier | Amplifies Pico PWM audio output |
| Speaker | 3W 8-ohm speaker | Music playback |
| Storage | microSD SPI breakout + FAT32 microSD card | WAV fallback playlist |
| Controls | Push buttons | Pause, next, previous for SD fallback |
| FPGA | iCESugar-Pro | Planned/partial display pipeline work; full FFT audio pipeline not completed in current path |

## Wiring Used by the Code

### PWM Audio Output

| Pico 2W Pin | Signal | Connects To |
|---|---|---|
| GP26 | Left PWM audio | PAM8403 L input |
| GP27 | Right PWM audio | PAM8403 R input |
| GND | Ground | PAM8403 GND and SD/display ground |
| 5V or external 5V | Power | PAM8403 VCC, depending on your power setup |

Use a common ground between the Pico, amplifier, SD module, and display hardware.

### microSD SPI Pins

The current code is configured for SPI1:

| Pico 2W Pin | SD Signal |
|---|---|
| GP12 | MISO |
| GP13 | CS |
| GP14 | SCK |
| GP15 | MOSI |

If your SD module is wired to different pins, update the `SD_MISO_PIN`, `SD_CS_PIN`, `SD_SCK_PIN`, and `SD_MOSI_PIN` defines in both `sd_wav_fallback.cpp` and `sd_spi_probe.cpp`.

### SD Fallback Buttons

Buttons are active-low using internal pull-ups:

| Pico 2W Pin | Button |
|---|---|
| GP3 | Pause/play |
| GP4 | Next track |
| GP8 | Previous track |

Each button should connect the GPIO pin to ground when pressed.

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

- PCM format 1 only
- 16-bit samples
- Mono or stereo
- 44.1 kHz recommended
- 48 kHz should also work because the PWM sample rate is updated from the WAV header

For the cleanest playback, export the files as 16-bit PCM WAV instead of renaming MP3 files to `.wav`.

## Audio Flow

```text
Bluetooth phone audio
        |
        v
Pico 2W A2DP sink -> SBC decode -> mono PCM
        |
        v
Shared PCM ring buffer -> PWM audio output -> PAM8403 -> speaker
        |
        v
LVGL visualizer snapshot -> 32-column level display + waveform trace
```

Fallback path:

```text
microSD WAV file -> FATFS reader -> WAV parser -> mono PCM
        |
        v
Shared PCM ring buffer -> PWM audio output + LVGL visualizer
```

## SD Playback Fix

The SD reader is paced by the number of samples already waiting in the audio buffer. This prevents the SD task from forcing too many samples into the buffer too quickly. Without pacing, the ring buffer can overflow and skip older samples, which makes the music sound too fast or warped.

The fixed SD path only reads more WAV frames when the buffer drops below its target fill level.

## Visualizer Behavior

The LCD shows:

- A top status bar
- Current source label
- Peak/average level text
- 32 animated PCM level columns
- A thin oscilloscope waveform overlay
- A beat-reactive background flash when the audio peak jumps above the recent envelope

This is a live PCM visualizer, not a mathematically true FFT spectrum analyzer. It is designed to match the demo goal of showing audio-reactive motion for both Bluetooth and SD WAV playback.

## Build Notes

Typical build flow:

```powershell
mkdir build
cd build
cmake -G Ninja ..
ninja
```

Flash the generated UF2 to the Pico 2W. Open serial output to check SD mount, WAV parsing, Bluetooth status, and playback state logs.
