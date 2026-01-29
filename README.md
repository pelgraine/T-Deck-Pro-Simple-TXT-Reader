# T-Deck Pro E-Paper Text Reader

A lightweight text file reader specifically designed for the LilyGo T-Deck Pro's e-paper display, with full SD card and keyboard support.

## ✅ What This Actually Is

This is a **proper e-paper text reader** for the T-Deck Pro, using:
- **GxEPD2 library** (the correct library for the GDEQ031T10 e-paper display)
- **Correct pin definitions** from the official LilyGo T-Deck Pro repository
- **Keyboard support** via TCA8418 I2C keyboard controller
- **SD card** for loading .txt files

## 🔧 Why The Previous Versions Failed

The T-Deck Pro uses an **e-paper display** (GDEQ031T10 - 320x240), NOT a TFT LCD! That's why TFT_eSPI was crashing. E-paper displays:
- Refresh slowly (takes ~2 seconds per page)
- Use completely different communication protocols
- Require the GxEPD2 library, not TFT_eSPI

## 📋 Hardware Requirements

- LilyGo T-Deck Pro (v1.0 or v1.1)
- MicroSD card (FAT32 formatted)
- Text files (.txt) on the SD card

## 🚀 Installation

### 1. Create New PlatformIO Project

```bash
mkdir tdeck-text-reader
cd tdeck-text-reader
```

### 2. Add These Files

- `src/main.cpp` ← Use `tdeck_epaper_reader_main.cpp`
- `platformio.ini` ← Use `tdeck_epaper_platformio.ini`

### 3. Build and Upload

```bash
pio run -t upload
pio device monitor
```

## 📚 Usage

### File Browser
- **W/S keys**: Navigate up/down through file list
- **Enter**: Open selected file

### Reading Mode
- **W/A keys**: Previous page
- **S/D keys**: Next page  
- **Space/Enter**: Next page (quick)
- **Q/ESC**: Close book, return to file list

## 📍 Complete Pinout Reference

Based on official LilyGo T-Deck Pro repository:

```
E-PAPER DISPLAY (GDEQ031T10)
├─ SCK:  GPIO 36 (shared SPI)
├─ MOSI: GPIO 33 (shared SPI)
├─ DC:   GPIO 35
├─ CS:   GPIO 34
├─ BUSY: GPIO 37
└─ RST:  -1 (no hardware reset)

SD CARD
├─ CS:   GPIO 48
├─ SCK:  GPIO 36 (shared with display)
├─ MOSI: GPIO 33 (shared with display)
└─ MISO: GPIO 47

KEYBOARD (TCA8418)
├─ SDA:  GPIO 14
├─ SCL:  GPIO 13
├─ INT:  GPIO 15
├─ LED:  GPIO 42
└─ ADDR: 0x34

POWER
└─ EN:   GPIO 40 (MUST be HIGH!)
```

## ⚙️ Configuration

You can adjust reading settings in the code:

```cpp
struct Settings {
    uint8_t textSize;      // 1-3 (default: 2)
    uint8_t linesPerPage;  // Auto-calculated
    uint8_t charsPerLine;  // Auto-calculated
};
```

## 🔍 How It Works

### E-Paper Display Characteristics

E-paper displays are fundamentally different from TFT/LCD:

1. **Slow Refresh**: Takes 2-3 seconds per full screen update
2. **Low Power**: Display retains image without power
3. **High Contrast**: Excellent readability in sunlight
4. **Black & White**: No colors or grayscale
5. **Special Initialization**: Requires specific waveform timing

### Display Update Process

```cpp
display.setFullWindow();     // Set update area
display.firstPage();          // Begin update cycle
do {
    // Draw your content here
    display.fillScreen(GxEPD_WHITE);
    display.println("Hello");
} while (display.nextPage());  // Complete the update
```

### Page Indexing

The reader builds an index of file positions for each page:
- Counts lines until `linesPerPage` is reached
- Tracks file position at each page boundary
- Allows instant navigation to any page

## 🐛 Troubleshooting

### Display Shows Nothing

**Symptom**: Screen stays white or shows old content  
**Cause**: PWR_EN not enabled or display not initialized  
**Fix**: Check serial monitor - should see "Display initialized"

### "SD Card Mount Failed"

**Symptom**: Can't read files  
**Solutions**:
1. Ensure card is FAT32 formatted
2. Check card is fully inserted
3. Try a different/smaller card
4. Verify pins: CS=48, SCK=36, MOSI=33, MISO=47

### Keyboard Not Responding

**Symptom**: Keys don't register  
**Check**:
1. Serial monitor shows "Keyboard found"
2. I2C address is 0x34 (not 0x55!)
3. Pins: SDA=14, SCL=13, INT=15

### Display Updates Very Slowly

**This is normal!** E-paper displays take 2-3 seconds to refresh. This is a hardware limitation, not a bug.

### Partial/Garbled Display

**Cause**: E-paper needs full refresh  
**Fix**: Always use `display.setFullWindow()` before updates

## 📖 Understanding E-Paper vs TFT

| Feature | E-Paper (T-Deck Pro) | TFT LCD |
|---------|---------------------|---------|
| Refresh Speed | 2-3 seconds | < 50ms |
| Power (static) | ~0mW | ~200mW |
| Sunlight Readability | Excellent | Poor |
| Colors | B&W only | Full color |
| Library | GxEPD2 | TFT_eSPI |
| Best For | Reading text | Interactive apps |

## 🔗 Related Projects

- **Official T-Deck Pro**: https://github.com/Xinyuan-LilyGO/T-Deck-Pro
- **Meshtastic**: Uses same hardware for mesh networking
- **Meck** (pelgraine): Fork with BLE companion firmware
- **GxEPD2**: https://github.com/ZinggJM/GxEPD2

## 🎯 Features To Add

Want to contribute? Here are some ideas:

- [ ] Bookmark support (save position to SPIFFS)
- [ ] Touch screen integration (CST328 at 0x1A)
- [ ] Adjustable font sizes via menu
- [ ] Folder navigation
- [ ] Battery level indicator
- [ ] Sleep mode after inactivity
- [ ] Multiple font support
- [ ] Basic markdown rendering
- [ ] Search within text
- [ ] Night mode (inverted colors - already B&W!)

## 📝 Notes on Your Specific Hardware

### T-Deck Pro v1.1 Changes (from PR #9378)

Your v1.1 has some differences from v1.0:
1. Haptic feedback: Changed to DRV2605 on GPIO 2
2. Touch panel: CST328 → CST3530 (auto-sleep enabled)
3. Touch RST: GPIO 45 → GPIO 38
4. GPS Enable: GPIO 39 (not GPIO 15)
5. Display RST: GPIO 16 (but we use -1)

### Keyboard I2C Address

**Critical**: The keyboard is at **0x34**, not 0x55!  
0x55 is the battery gauge (BQ27220).

## 💡 Tips for Best Experience

### Preparing Text Files

1. Use plain .txt files (UTF-8 encoding)
2. Keep files under 5MB for fast indexing
3. Remove unnecessary formatting
4. Line length doesn't matter (auto-wrapped)

### Reading Comfort

- E-paper is perfect for long reading sessions
- No eye strain like LCD screens
- Works great in direct sunlight
- Battery lasts days, not hours

### Page Navigation

- Use Space for quick page advancing
- W/S keys for deliberate navigation
- E-paper "holds" the page even when powered off

## 🆘 Getting Help

1. Check serial monitor output for errors
2. Verify hardware version (v1.0 vs v1.1)
3. Ensure PWR_EN (GPIO 40) is HIGH
4. Test with the minimal hardware test first
5. Check official T-Deck Pro examples work

## 📄 License

MIT License - Feel free to modify and use as you wish.

## 🙏 Credits

- **Hardware**: LilyGo T-Deck Pro design
- **Pin mappings**: Lewis He (@lewisxhe) and LilyGo
- **Display library**: ZinggJM's GxEPD2
- **Inspiration**: atomic14's ESP32 ePub reader, Meck project
- **Development assistance**: Claude AI

---

**Happy Reading! 📚**

*Perfect for off-grid reading, emergency communication device with reading capability, or just a cool e-reader project!*