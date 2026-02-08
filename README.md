# Sunlu S1 Filament Dryer Controller

Drop-in replacement controller board for the Sunlu S1 filament dryer. Features an STM32C011F4P6 microcontroller, 1.8" ST7735 TFT display, dual NTC temperature sensing, and precise PWM heater control; all optimized to fit in just 16 KB of flash. 

## Table of Contents
- [Overview](#overview)
- [Features](#features)
- [Quick Start](#quick-start)
- [User Guide](#user-guide)
- [Hardware](#hardware)
- [Technical Specs](#technical-specs)
- [Development](#development)
- [License](#license)

## Overview

Complete replacement for the stock Sunlu S1 control board, maintaining compatibility with the original heating element, chamber, and power connections. Adds a color TFT display in landscape mode and two-button navigation for precise temperature and time control (30–70°C configurable range, exceeding the stock 35–55°C limit).

The firmware uses integer-only math, dirty-check rendering, and batched SPI transfers to fit in the STM32C011's tiny 16 KB flash with room to spare.

### Gallery

Final build

![](https://blueprint.f8d710a55cb9b516d88635f103c2c9f2.r2.cloudflarestorage.com/gdg6msgjampc79knbxxnwza9dq3i?response-content-disposition=inline%3B%20filename%3D%22PXL_20260208_110349659.MP.jpg%22%3B%20filename%2A%3DUTF-8%27%27PXL_20260208_110349659.MP.jpg&response-content-type=image%2Fwebp&X-Amz-Algorithm=AWS4-HMAC-SHA256&X-Amz-Credential=b27cbf3ecad4135891e6187077206c69%2F20260208%2Fauto%2Fs3%2Faws4_request&X-Amz-Date=20260208T194402Z&X-Amz-Expires=604800&X-Amz-SignedHeaders=host&X-Amz-Signature=e3a33aeceb593eeee4ad15862c75328d3da5c47bc159685c56f00c8f399ec821)

<details>
<Summary>PCB</Summary>

![](https://blueprint.hackclub.com/user-attachments/blobs/proxy/eyJfcmFpbHMiOnsiZGF0YSI6MjYwNTIsInB1ciI6ImJsb2JfaWQifX0=--2d029e8420b1dd132e7354eb3dd6a570f5db8566/image.png)

![](https://blueprint.hackclub.com/user-attachments/blobs/proxy/eyJfcmFpbHMiOnsiZGF0YSI6MjYwNTMsInB1ciI6ImJsb2JfaWQifX0=--1047a7ac75af647d70a90727f570555daae576b7/image.png)
</details>

## Features

### Display & Control
- **1.8" ST7735 TFT**: 160×128 px landscape, 16-bit RGB565 color
- **Two-button interface**: short-press (select/increment), long-press (toggle/cancel)
- **Three screens**: Main Status, Set Temperature, Set Time
- **Smart rendering**: dirty-check updates (only changed fields), instant button response

### Temperature & Heater Regulation
- **Dual NTC thermistors**: air temp (displayed) + heater element (safety)
- **Proportional PWM control** (~488 Hz): ramps heater power based on error from target
- **Dual safety cutoffs**: absolute (85°C) and dynamic (target + 5°C margin)
- **Integer-only lookup**: 11-point table with linear interpolation, no floating-point library
- **Fan control**: full speed during drying and overheat emergency

### Firmware Design
- **~600 lines**: single-loop, no RTOS/filesystem
- **~13 KB flash / ~2 KB RAM**: 80% flash usage, 33% RAM usage
- **Batched SPI**: 128-byte buffer (~100× faster than byte-by-byte)
- **Minimal font**: 5×7 bitmap, 29 glyphs only
- **No soft-float**: integer NTC lookup saves ~4 KB

## Quick Start

### Build
Requires `arm-none-eabi-gcc`, `cmake`, and `cube-cmake` installed.

```sh
cd Code
cmake --preset Debug
cmake --build build/Debug --target Code
```

### Flash
Using `st-flash`:

```sh
st-flash write build/Debug/Code.bin 0x08000000
```

Or use the VS Code task "Build and Flash" if configured.

## User Guide

### Main Screen (Status & Control)
- **Top**: Air temperature, Countdown timer (HH:MM)
- **Middle**: Current state (IDLE/RUN/PAUSE), target temperature setting
- **Bottom**: Action hints: BTN1=set, BTN2=start/pause/resume, hold BTN2=stop/reset

<details>
<summary>Example Main Screen</summary>

![](https://blueprint.hackclub.com/user-attachments/representations/redirect/eyJfcmFpbHMiOnsiZGF0YSI6MTAwNDI5LCJwdXIiOiJibG9iX2lkIn19--8746a7b55e983f4c0fdec9bc8e279b0da73ccb92/eyJfcmFpbHMiOnsiZGF0YSI6eyJmb3JtYXQiOiJqcGciLCJyZXNpemVfdG9fbGltaXQiOlsyMDAwLDIwMDBdLCJjb252ZXJ0Ijoid2VicCIsInNhdmVyIjp7InF1YWxpdHkiOjgwLCJzdHJpcCI6dHJ1ZX19LCJwdXIiOiJ2YXJpYXRpb24ifX0=--15ca3815f01a5683e19ea0585d2eef9af9e441d7/PXL_20260208_110426292.MP%20(1).jpg)

</details>

### Set Temperature Screen
- **Value**: Current target in °C
- **Controls**: 
  - **Short BTN1**: increment (or decrement if toggled)
  - **Long BTN1**: toggle UP/DOWN mode, hints show current direction
  - **Short BTN2**: confirm & advance to Set Time
  - **Long BTN2**: cancel & return to Main

<details>
<summary>Example Set Temp Screen</summary>

![](https://blueprint.hackclub.com/user-attachments/representations/redirect/eyJfcmFpbHMiOnsiZGF0YSI6MTAwNDI4LCJwdXIiOiJibG9iX2lkIn19--2ed9d55c6d9b7077e4530b978b0fad8c62206d09/eyJfcmFpbHMiOnsiZGF0YSI6eyJmb3JtYXQiOiJqcGciLCJyZXNpemVfdG9fbGltaXQiOlsyMDAwLDIwMDBdLCJjb252ZXJ0Ijoid2VicCIsInNhdmVyIjp7InF1YWxpdHkiOjgwLCJzdHJpcCI6dHJ1ZX19LCJwdXIiOiJ2YXJpYXRpb24ifX0=--15ca3815f01a5683e19ea0585d2eef9af9e441d7/PXL_20260208_110402166.MP.jpg)
</details>

### Set Time Screen
- **Value**: Target drying time in HH:MM format
- **Controls**: Same as Set Temp (BTN1 for ±, long BTN1 to toggle, BTN2 to confirm/cancel)

<details>
<summary>Example Set Time Screen</summary>

![](https://blueprint.hackclub.com/user-attachments/representations/redirect/eyJfcmFpbHMiOnsiZGF0YSI6MTAwNDI3LCJwdXIiOiJibG9iX2lkIn19--e19a54ca3e86a618e632d00cc75f3bee663c88d3/eyJfcmFpbHMiOnsiZGF0YSI6eyJmb3JtYXQiOiJqcGciLCJyZXNpemVfdG9fbGltaXQiOlsyMDAwLDIwMDBdLCJjb252ZXJ0Ijoid2VicCIsInNhdmVyIjp7InF1YWxpdHkiOjgwLCJzdHJpcCI6dHJ1ZX19LCJwdXIiOiJ2YXJpYXRpb24ifX0=--15ca3815f01a5683e19ea0585d2eef9af9e441d7/PXL_20260208_110415232%20(1).jpg)
</details>


### Operation
1. Press BTN1 on Main Screen → Set Temperature
2. Adjust with BTN1 (short = increment, long = toggle direction, watch hints)
3. Press BTN2 → Set Time
4. Adjust similarly, then press BTN2 → firmware returns to Main and **starts drying**
5. Monitor progress on Main Screen; press BTN2 to pause/resume
6. Hold BTN2 to stop and reset (returns to IDLE)

## Hardware

### Pinout & Connections
| Function | Pin | Module | Notes |
|----------|-----|--------|-------|
| Display CS | PB7 | SPI1 | ST7735 chip select |
| Display DC | PA3 | SPI1 | Data/Command |
| Display RES | PC14 | GPIO | Reset |
| Display BL | PA8 | GPIO | Backlight (always ON) |
| Display SPI | PA5, PA6, PA7 | SPI1 | CLK, MISO, MOSI |
| Button 1 | PA4 | GPIO | Up/Set (active LOW, pull-up) |
| Button 2 | PA5 | GPIO | Select/Confirm (active LOW, pull-up) |
| Heater PWM | PA0 | TIM1_CH1 | 16-bit PWM (0–65535 = 0–100%) |
| Fan PWM | PA1 | TIM17_CH1 | 16-bit PWM (0–65535 = 0–100%) |
| Air NTC | PA11 | ADC_CH11 | 100k thermistor, 3.3 V divider |
| Heater NTC | PA12 | ADC_CH12 | 100k thermistor, 3.3 V divider |
| LED | PA2 | GPIO | Status indicator (on during RUN/PAUSE) |

See [Code/Core/Inc/main.h](Code/Core/Inc/main.h) for full pin configuration.

## Technical Specs

### MCU & Peripherals
- **MCU**: STM32C011F4P6 (Cortex-M0+, 32 MHz, 16 KB flash, 6 KB RAM)
- **Display**: ST7735 1.8" TFT, 160×128 px, 16-bit RGB565, SPI @ 16 MHz
- **PWM**: TIM1 (heater) + TIM17 (fan), ~488 Hz, 16-bit resolution
- **ADC**: 12-bit, dual-channel NTC reading @ 500 ms

### Temperature & Timing
- **Range**: 30–70°C (configurable `TMIN`/`TMAX`)
- **NTC**: 100 K, B=3950, ~1°C resolution
- **Button poll**: 20 ms (30 ms debounce, 1 s long-press)
- **Display refresh**: 250 ms idle on Main screen, instant on changes
- **Countdown**: 1 s ticks

### Power
- **Input**: 24 V DC
- **Heater**: ≤48 W (PWM-controlled)
- **Fan**: ≤2 W (PWM-controlled)
- **MCU + Display**: ≤100 mA @ 3.3 V
## Development

### Structure
```
├── CMakeLists.txt              # Build config
├── Core/
│   ├── Inc/
│   │   ├── main.h              # Pin definitions, HAL config
│   │   ├── dryer.h             # Public firmware API
│   │   └── ...
│   └── Src/
│       ├── main.c              # Entry point, HAL init
│       ├── dryer.c             # Firmware (600 lines): display driver, UI, control
│       └── ...
├── Drivers/
│   ├── CMSIS/
│   ├── STM32C0xx_HAL_Driver/   # ST HAL
│   └── ...
└── cmake/                       # Toolchain files
```

### Modifying the Firmware
- Temperature limits: edit `TMIN`, `TMAX`, `TDEF` in [Code/Core/Src/dryer.c](Code/Core/Src/dryer.c)
- Time limits: edit `MMIN`, `MMAX`, `MDEF`
- Safety margin: change `stmp + 5` in `Regulate()` function
- NTC calibration: update `ntc[]` lookup table in [Code/Core/Src/dryer.c](Code/Core/Src/dryer.c)

## Bill of Materials

See [BOM.csv](BOM.csv) for the complete component list and part numbers.

## License

This work is licensed under a
[Creative Commons Attribution-NonCommercial 4.0 International License][cc-by-nc].

[![CC BY-NC 4.0][cc-by-nc-image]][cc-by-nc]

[cc-by-nc]: https://creativecommons.org/licenses/by-nc/4.0/
[cc-by-nc-image]: https://licensebuttons.net/l/by-nc/4.0/88x31.png
[cc-by-nc-shield]: https://img.shields.io/badge/License-CC%20BY--NC%204.0-lightgrey.svg