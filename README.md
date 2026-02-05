## T-Deck Pro Simple TXT Reader
A simple e-book reader firmware for the LilyGo T-Deck Pro, designed to read plain text (.txt) files from an SD card on the built-in e-paper display.

## Features

- **E-Paper Display** - Easy on the eyes, readable in direct sunlight
- **SD Card Support** - Reads .txt files from FAT32 formatted SD cards
- **Persistent Indexing** - Page indexes are saved to SD card, so books open instantly after first read
- **Word Wrap** - Text wraps at word boundaries for clean reading
- **Progress Tracking** - Shows current page, total pages, and percentage complete
- **Keyboard Navigation** - Use the built-in keyboard to navigate

## Controls
**File Selection Screen**

W - Navigate up
S - Navigate down
Enter - Open selected file

**Reading Screen**

W - Previous page
S - Next page
Q - Exit to file list

## Hardware Requirements

- LilyGo T-Deck Pro v1.1
- FAT32 formatted SD card with .txt files

## Building
This project uses PlatformIO. To build and upload:
bash# Build
pio run

# Upload
pio run -t upload

# Monitor serial output
pio device monitor
Or use the PlatformIO IDE extension in VSCode.
Pin Configuration
Based on T-Deck Pro v1.1 hardware:
ComponentPinsE-Paper DisplaySCK=36, MOSI=33, CS=34, DC=35, BUSY=37SD CardSCK=36, MOSI=33, MISO=47, CS=48Keyboard (TCA8418)SDA=13, SCL=14, INT=15Power EnableGPIO 40
Index Files
The reader creates a .indexes folder on the SD card to store page position data. This allows books to open instantly on subsequent reads. Index files are automatically invalidated if the source file changes.

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