# T5S3 Pro Simple TXT Reader

A simple e-book reader firmware for the **LilyGo T5 E-Paper S3 Pro** (H752-01), ported from the T-Deck Pro version. Reads plain text (.txt) files from an SD card on the 4.7" e-paper display.

## Hardware Differences from T-Deck Pro

| Feature | T-Deck Pro | T5S3 Pro |
|---------|-----------|----------|
| Display | 3.1" SPI (320×240, B&W) | 4.7" Parallel (960×540, 16 gray) |
| Display Driver | GxEPD2 | epdiy (parallel EPD driver) |
| Input | TCA8418 Keyboard | GT911 Capacitive Touch |
| SD SPI Pins | SCK=36, MOSI=33, MISO=47, CS=48 | SCK=14, MOSI=13, MISO=21, CS=12 |
| I2C Pins | SDA=13, SCL=14 | SDA=39, SCL=40 |
| Power | PWR_EN GPIO 40 | TPS65185 + PCA9535 IO expander |
| MCU | ESP32-S3 | ESP32-S3 (16MB Flash, 8MB PSRAM) |
| Extra | LoRa, 4G modem | LoRa, GPS, Battery gauge, RTC |

## Touch Controls

The 4.7" touchscreen replaces the keyboard with zone-based navigation:

```
┌──────────┬───────────────────────────────────────┐
│  ← BACK  │                                       │
│ (exit)   │                                       │
├──────────┤                                       │
│          │                                       │
│  ◄ PREV  │        CENTER = NEXT PAGE             │  ► NEXT
│  PAGE    │     (tap anywhere in center)          │  PAGE
│ (left    │                                       │ (right
│  quarter)│                                       │  quarter)
│          │                                       │
├──────────┴───────────────────────────────────────┤
│              STATUS BAR                          │
└──────────────────────────────────────────────────┘
```

**Reading Mode:**
- Tap **right side** or **center** → Next page
- Tap **left side** → Previous page  
- Tap **top-left corner** → Exit to file list
- Press **BOOT button** → Exit to file list (hardware fallback)

**File Selection:**
- Tap a filename → Select it
- Tap the selected filename again → Open it
- (Or just tap any file to select, then tap again to open)

## Features

All features from the T-Deck Pro version are preserved:
- **E-Paper Display** — 4.7" with 16 grayscale, readable in sunlight
- **SD Card Support** — Reads .txt files from FAT32 formatted SD cards
- **Persistent Indexing** — Page indexes saved to SD card for instant re-opening
- **Word Wrap** — Text wraps at word boundaries
- **Progress Tracking** — Page number, total pages, and percentage
- **Resume Reading** — Books reopen to the last page you were reading

## ⚠️ Pre-Build Setup Required

### 1. Font Generation

The epdiy library requires fonts in a special compiled format. You need to generate font headers before building:

```bash
# Clone epdiy if you haven't already
git clone https://github.com/vroland/epdiy.git

# Generate the required fonts
cd epdiy/scripts

# Body text font (~16pt)
python3 fontconvert.py FiraSans_16 16 /path/to/FiraSans-Regular.ttf > ../../src/fonts/firasans_16.h

# Header/UI font (~20pt)  
python3 fontconvert.py FiraSans_20 20 /path/to/FiraSans-Regular.ttf > ../../src/fonts/firasans_20.h
```

Then include them in your main.cpp by replacing the `extern` declarations:
```cpp
#include "fonts/firasans_16.h"
#include "fonts/firasans_20.h"
```

> **Tip:** FiraSans is available free from Google Fonts. Any TTF font works —
> monospace fonts like FiraCode or JetBrains Mono also work well for a reader.

### 2. Board Support Files

The H752-01 version of the T5S3 Pro uses a TPS65185 e-paper power management chip that may not be in the standard epdiy release. You have two options:

**Option A: Use the LilyGo custom epdiy (recommended)**
```bash
# Clone the LilyGo repo
git clone -b H752-01 https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO.git

# Copy their epdiy library into your project
cp -r T5S3-4.7-e-paper-PRO/lib/epdiy ./lib/
```

**Option B: Use upstream epdiy with `epd_board_lilygo_t5_47_s3`**

This may work for older T5S3 boards (H752 without TPS65185), but the H752-01 Pro version needs the custom board definition from Option A.

### 3. Adjust VCOM Voltage

Each e-paper panel has a specific VCOM voltage printed on a label on the back of the panel (or on the flex cable). Update this line in `main.cpp`:

```cpp
epd_set_vcom(1560);  // Change to YOUR panel's VCOM value in mV
```

## Building

```bash
# Build
pio run

# Upload
pio run -t upload

# Monitor serial output
pio device monitor
```

## Project Structure

```
t5s3-reader/
├── platformio.ini          # Build configuration
├── src/
│   ├── main.cpp            # Main firmware
│   └── fonts/              # Generated font headers (you create these)
│       ├── firasans_16.h
│       └── firasans_20.h
├── lib/
│   └── epdiy/              # (Optional) LilyGo's custom epdiy library
└── README.md
```

## Pin Reference (Verified from Official Pinmap)

```
// I2C Bus (shared: touch, RTC, battery gauge, IO expander)
SDA = IO39, SCL = IO40

// Touch Panel (GT911)
INT = IO03, RST = IO09, Addr = 0x5D (2-point touch supported)

// RTC (PCF8563)
SDA = IO39, SCL = IO40, IRQ = IO02

// TF Card (SD) - FSPI bus
MISO = IO21, MOSI = IO13, SCLK = IO14, CS = IO12

// LoRa SX1262 - HSPI bus (separate from SD card)
MISO = IO08, MOSI = IO17, SCLK = IO18
CS = IO46, IRQ = IO10, RST = IO01, BUSY = IO47

// E-Paper ED047TC1 - Parallel bus (directly to LCD peripheral)
D0 = IO11, D1 = IO12, D2 = IO13, D3 = IO14
D4 = IO21, D5 = IO47, D6 = IO45, D7 = IO38
STH = IO09, CKH = IO10, CKV = IO39
CFG_CLK = IO42, CFG_STR = IO01, CFG_DATA = IO02
BL (backlight) = IO11

// GPS (Optional module)
TX = IO43, RX = IO44

// Buttons
BOOT = IO48

// Notes:
// - E-paper and SD card share some GPIO numbers but are managed
//   by different peripherals (LCD vs SPI) - avoid simultaneous use
// - The epdiy library handles e-paper pin configuration internally
```

## Troubleshooting

1. **Display stays blank** → Check VCOM voltage, verify epdiy board definition matches your hardware version (H752 vs H752-01)
2. **Touch not responding** → Check I2C address (0x5D for GT911), verify INT/RST pins
3. **SD card errors** → Ensure FAT32 format, check SPI pin assignments
4. **Build errors about fonts** → You must generate font headers first (see setup above)
5. **Build errors about board definition** → Copy LilyGo's custom epdiy lib (see setup above)
6. **Slow display updates** → Normal for e-paper; the parallel interface is faster than SPI but still ~1-2s for full refresh. Consider using partial updates for status bar only.

## Adapting Text Layout

The 960×540 display is much larger than the T-Deck Pro's 320×240. The default settings give about 24 lines of ~88 characters per line. You can adjust these in the `settings` struct:

```cpp
struct Settings {
    ...
    uint8_t linesPerPage;   // Increase for smaller fonts
    uint8_t charsPerLine;   // Depends on font width
    int lineHeight;         // Match to your font size
    int marginX;            // Side margins
    ...
} settings = { 1, 24, 88, 22, 10, 20, 10, 30 };
```

## License

MIT License

## Credits

- Original T-Deck Pro reader firmware
- **epdiy**: Valentin Roland (vroland) — parallel EPD driver
- **FastEPD**: Larry Bank (bitbank2) — alternative EPD driver
- **Hardware**: LilyGo T5 E-Paper S3 Pro design
- **Pin mappings**: Lewis He (@lewisxhe) and LilyGo
- **SensorLib**: Lewis He — GT911 touch driver
- **Development assistance**: Claude AI