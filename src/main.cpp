#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <vector>

// ============================================================================
// DISPLAY DRIVER SELECTION
// ============================================================================
// The T5S3 Pro uses a PARALLEL e-paper display (ED047TC1, 960x540, 16 gray).
// This is fundamentally different from the T-Deck Pro's SPI e-paper (GxEPD2).
//
// Two library options:
//   1. epdiy (vroland) - The "standard" parallel EPD driver for ESP32-S3
//   2. FastEPD (bitbank2) - Newer, optimized, explicitly supports T5S3 Pro
//
// This port uses epdiy as it has the most documentation and community support.
// The LilyGo official repo includes a customized epdiy in their lib/ folder
// that adds TPS65185 power management support for the H752-01 board.
//
// IMPORTANT: You may need to clone the LilyGo T5S3-4.7-e-paper-PRO repo
// and copy the lib/epdiy folder into this project's lib/ directory for
// proper H752-01 board support.
// ============================================================================

#include <epdiy.h>

// For touch input (GT911)
#include <TouchDrvGT911.h>

// ============================================================================
// T5S3 PRO (H752-01) HARDWARE DEFINITIONS
// ============================================================================
// IMPORTANT: This board uses aggressive pin multiplexing!
// - E-paper data pins D1-D4 share GPIO with SD card SPI (12,13,14,21)
// - CKV (IO39) shares with I2C SDA
// - STH (IO09) shares with Touch RST
// 
// The epdiy library and LCD peripheral manage this automatically, but:
// - Don't access SD card DURING display refresh
// - The code does file I/O before calling updateScreen(), which is correct
// ============================================================================

// I2C Bus (shared: touch, RTC, battery gauge, IO expander, EPD power)
#define BOARD_SDA   39
#define BOARD_SCL   40

// SPI Bus (shared: SD card, LoRa)
#define BOARD_SPI_MISO  21
#define BOARD_SPI_MOSI  13
#define BOARD_SPI_SCLK  14

// SD Card
#define SD_CS    12
#define SD_MISO  BOARD_SPI_MISO
#define SD_MOSI  BOARD_SPI_MOSI
#define SD_SCK   BOARD_SPI_SCLK

// Touch Panel (GT911)
#define TOUCH_SDA  BOARD_SDA
#define TOUCH_SCL  BOARD_SCL
#define TOUCH_INT  3
#define TOUCH_RST  9

// Backlight / misc
#define BOARD_BL_EN       11
#define BOARD_PCA9535_INT 38
#define BOARD_BOOT_BTN    48  // Note: IO48 per official pinmap

// E-Paper Display (ED047TC1) - parallel interface
// These are directly wired to the ESP32-S3 LCD peripheral via epdiy
// Pin definitions are handled internally by the epdiy board definition
// Display: 960 x 540, 16 grayscale levels

// Display specs - Landscape orientation (native)
#define SCREEN_WIDTH  960
#define SCREEN_HEIGHT 540

// ============================================================================
// DISPLAY SETUP
// ============================================================================

// epdiy high-level state
EpdiyHighlevelState hl;

// Framebuffer pointer (4bpp = 2 pixels per byte)
uint8_t* fb = NULL;

// Font for text rendering
// epdiy requires pre-converted fonts. You need to generate these using:
//   python3 scripts/fontconvert.py FiraCode 16 /path/to/FiraCode-Regular.ttf > firacode_16.h
//
// For now, we declare an extern font. You MUST provide a font header.
// The LilyGo examples include fonts in their data/ directory.
// See the "Generating Fonts" section in the README.
#include "fonts/firasans_12.h"
#include "fonts/firasans_20.h"

// If you don't have the font headers yet, uncomment this to use a fallback:
// #define USE_BUILTIN_FONT 1

// ============================================================================
// TOUCH INPUT
// ============================================================================

TouchDrvGT911 touch;
bool touchAvailable = false;

// Touch zones for navigation (landscape orientation)
// ┌─────────────┬──────────────────────────┬─────────────┐
// │  BACK/EXIT  │       (title area)       │             │
// │  (top-left) │                          │             │
// ├─────────────┼──────────────────────────┤             │
// │             │                          │             │
// │  PREV PAGE  │     (reading area)       │  NEXT PAGE  │
// │  (left 1/4) │                          │ (right 1/4) │
// │             │                          │             │
// ├─────────────┴──────────────────────────┴─────────────┤
// │              STATUS BAR (tap = menu?)                 │
// └──────────────────────────────────────────────────────┘

#define TOUCH_ZONE_BACK_W    200   // Top-left corner width
#define TOUCH_ZONE_BACK_H    80    // Top-left corner height
#define TOUCH_ZONE_LEFT_W    240   // Left quarter for prev page
#define TOUCH_ZONE_RIGHT_X   720   // Right quarter starts here
#define TOUCH_ZONE_STATUS_H  40    // Bottom status bar height

enum TouchAction {
    TOUCH_NONE,
    TOUCH_NEXT_PAGE,
    TOUCH_PREV_PAGE,
    TOUCH_BACK,
    TOUCH_STATUS
};

// ============================================================================
// READER STATE
// ============================================================================

#define VERSION "0.0.1-t5s3"
#define BUILD_DATE "Feb 2026"

#define INDEX_VERSION 2

// Text layout for the larger 960x540 display
// Using ~16pt font: approximately 10px wide, 20px tall per character
struct Settings {
    uint8_t textSize;       // Not used directly (font-based)
    uint8_t linesPerPage;   // ~24 lines of text in reading area
    uint8_t charsPerLine;   // ~90 characters per line
    int lineHeight;         // Pixel height per line
    int charWidth;          // Approximate pixel width per character
    int marginX;            // Left/right margin
    int marginY;            // Top margin for text area
    int statusBarHeight;    // Height of bottom status bar
} settings = {
    1,      // textSize (unused, font determines this)
    24,     // linesPerPage
    88,     // charsPerLine  
    22,     // lineHeight in pixels
    10,     // charWidth in pixels (approximate for proportional font)
    20,     // marginX
    10,     // marginY  
    30      // statusBarHeight
};

struct ReaderState {
    String currentFile;
    File file;
    std::vector<long> pagePositions;
    int currentPage;
    int totalPages;
    bool fileOpen;
} reader;

#define PREINDEX_PAGES 100

struct FileCache {
    String filename;
    std::vector<long> pagePositions;
    unsigned long fileSize;
    bool fullyIndexed;
    int lastReadPage;
};
std::vector<FileCache> fileCache;

std::vector<String> fileList;
int selectedFileIndex = 0;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void initHardware();
void initDisplay();
void initSD();
void initTouch();
void showSplashScreen();
void showIndexingScreen(const String& filename);
void listTextFiles();
void preIndexFiles();
bool loadIndexFromSD(const String& filename, FileCache& cache);
bool saveIndexToSD(const String& filename, const std::vector<long>& pagePositions, unsigned long fileSize, bool fullyIndexed, int lastReadPage);
bool saveReadingPosition(const String& filename, int page);
String getIndexFilename(const String& txtFilename);
void displayFileList();
void openBook(const String& filename);
void displayPageFull();
void displayPage();
void nextPage();
void prevPage();
void closeBook();
TouchAction readTouch();
void handleTouch(TouchAction action);

// Display helpers
void clearScreen();
void updateScreen(bool fullRefresh = true);
void drawText(int x, int y, const char* text, const EpdFont* font = nullptr);
void drawTextFormatted(int x, int y, const char* fmt, ...);
void fillRect(int x, int y, int w, int h, uint8_t color);
void drawHLine(int x, int y, int w, uint8_t color);
void drawFilledRect(int x, int y, int w, int h, uint8_t color);

// Word wrap helper
struct WrapResult {
    int lineEnd;
    int nextStart;
};
WrapResult findLineBreak(const char* buffer, int bufLen, int lineStart, int maxChars);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n====================================");
    Serial.println("T5S3 Pro E-Paper Text Reader");
    Serial.printf("Version %s (%s)\n", VERSION, BUILD_DATE);
    Serial.println("====================================\n");
    
    initHardware();
    initDisplay();
    initSD();
    initTouch();
    
    showSplashScreen();
    
    listTextFiles();
    preIndexFiles();
    
    reader.fileOpen = false;
    reader.currentPage = 0;
    
    displayFileList();
    
    Serial.println("Setup complete!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    TouchAction action = readTouch();
    if (action != TOUCH_NONE) {
        handleTouch(action);
    }
    
    // Also check the hardware BOOT button as a back/exit shortcut
    if (digitalRead(BOARD_BOOT_BTN) == LOW) {
        delay(50); // Debounce
        if (digitalRead(BOARD_BOOT_BTN) == LOW) {
            if (reader.fileOpen) {
                Serial.println("BOOT button: closing book");
                closeBook();
                delay(200);
                displayFileList();
            }
            // Wait for release
            while (digitalRead(BOARD_BOOT_BTN) == LOW) delay(10);
        }
    }
    
    delay(50);
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================

void initHardware() {
    Serial.println("Initializing hardware...");
    
    // Boot button as input
    pinMode(BOARD_BOOT_BTN, INPUT_PULLUP);
    
    // Backlight control (the T5S3 Pro has an optional front-light)
    pinMode(BOARD_BL_EN, OUTPUT);
    digitalWrite(BOARD_BL_EN, LOW);  // Off by default for e-paper
    
    // Initialize I2C bus (shared by touch, battery, RTC, IO expander)
    Wire.begin(BOARD_SDA, BOARD_SCL);
    Wire.setClock(400000);
    
    Serial.println("✓ Hardware initialized");
}

void initDisplay() {
    Serial.println("Initializing e-paper display...");
    
    // Initialize epdiy with the LilyGo T5 S3 board definition
    // NOTE: For H752-01 (Pro), you may need the custom board definition
    // from the LilyGo repo that includes TPS65185 support.
    epd_init(&epd_board_lilygo_t5_47_s3, &ED047TC1, EPD_LUT_64K);
    
    // Set VCOM voltage (adjust for your specific panel, typically 1500-2000mV)
    // Check the label on your e-paper panel for the correct VCOM value
    epd_set_vcom(1560);
    
    // Initialize high-level API (manages front/back buffers)
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    
    // Get the front framebuffer
    fb = epd_hl_get_framebuffer(&hl);
    
    // Set rotation to landscape (native for this display)
    epd_set_rotation(EPD_ROT_LANDSCAPE);
    
    // Clear the display
    epd_poweron();
    epd_clear();
    epd_poweroff();
    
    Serial.printf("✓ Display initialized (%dx%d)\n", epd_rotated_display_width(), epd_rotated_display_height());
}

void initSD() {
    Serial.println("Initializing SD card...");
    
    // Initialize SPI for SD card
    // The T5S3 Pro uses a separate SPI bus from the display (display is parallel)
    SPIClass sdSpi(HSPI);
    sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);
    
    if (!SD.begin(SD_CS, sdSpi, 4000000)) {
        Serial.println("✗ SD Card failed!");
        
        // Show error on display
        clearScreen();
        EpdFontProperties props = epd_font_properties_default();
        int cx = 300, cy = 250;
        epd_write_string(&FiraSans_20, "SD CARD ERROR", &cx, &cy, fb, &props);
        cx = 300; cy = 290;
        epd_write_string(&Firasans_12, "Insert card and reset device", &cx, &cy, fb, &props);
        updateScreen(true);
        
        while (1) delay(1000);
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("✓ SD Card: %llu MB\n", cardSize);
}

void initTouch() {
    Serial.println("Initializing touch panel...");
    
    // Reset the GT911
    pinMode(TOUCH_RST, OUTPUT);
    pinMode(TOUCH_INT, INPUT);
    
    // GT911 address selection via reset sequence
    // Pull INT low, then toggle RST to select address 0x5D
    digitalWrite(TOUCH_RST, LOW);
    pinMode(TOUCH_INT, OUTPUT);
    digitalWrite(TOUCH_INT, LOW);
    delay(10);
    digitalWrite(TOUCH_RST, HIGH);
    delay(50);
    pinMode(TOUCH_INT, INPUT);
    delay(50);
    
    touchAvailable = touch.begin(Wire, GT911_SLAVE_ADDRESS_H, BOARD_SDA, BOARD_SCL);
    
    if (touchAvailable) {
        Serial.println("✓ Touch panel (GT911) initialized");
    } else {
        Serial.println("✗ Touch panel not found - using button-only mode");
    }
}

// ============================================================================
// DISPLAY HELPERS
// ============================================================================

void clearScreen() {
    memset(fb, 0xFF, epd_width() * epd_height() / 2);  // White (0xFF = two white pixels in 4bpp)
}

void updateScreen(bool fullRefresh) {
    epd_poweron();
    if (fullRefresh) {
        epd_hl_update_screen(&hl, MODE_GC16, 25);
    } else {
        // Partial update - faster but may leave artifacts
        epd_hl_update_screen(&hl, MODE_DU, 25);
    }
    epd_poweroff();
}

void fillRect(int x, int y, int w, int h, uint8_t color) {
    EpdRect rect = {x, y, w, h};
    epd_fill_rect(rect, color, fb);
}

void drawHLine(int x, int y, int w, uint8_t color) {
    fillRect(x, y, w, 2, color);
}

// ============================================================================
// SPLASH SCREEN
// ============================================================================

void showSplashScreen() {
    Serial.println("Showing splash screen...");
    
    clearScreen();
    
    EpdFontProperties props = epd_font_properties_default();
    props.flags = EPD_DRAW_ALIGN_CENTER;
    
    int cx, cy;
    
    // Title - centered
    cx = SCREEN_WIDTH / 2;
    cy = SCREEN_HEIGHT / 2 - 40;
    epd_write_string(&FiraSans_20, "TextReader", &cx, &cy, fb, &props);
    
    // Version
    cx = SCREEN_WIDTH / 2;
    cy = SCREEN_HEIGHT / 2;
    char versionStr[64];
    snprintf(versionStr, sizeof(versionStr), "v%s", VERSION);
    epd_write_string(&Firasans_12, versionStr, &cx, &cy, fb, &props);
    
    // Build date
    cx = SCREEN_WIDTH / 2;
    cy = SCREEN_HEIGHT / 2 + 30;
    epd_write_string(&Firasans_12, BUILD_DATE, &cx, &cy, fb, &props);
    
    // Platform info
    cx = SCREEN_WIDTH / 2;
    cy = SCREEN_HEIGHT / 2 + 70;
    epd_write_string(&Firasans_12, "LilyGo T5S3 Pro - 4.7\" E-Paper", &cx, &cy, fb, &props);
    
    // Loading
    cx = SCREEN_WIDTH / 2;
    cy = SCREEN_HEIGHT / 2 + 110;
    epd_write_string(&Firasans_12, "Loading...", &cx, &cy, fb, &props);
    
    updateScreen(true);
    delay(1500);
    
    Serial.println("Splash screen complete");
}

void showIndexingScreen(const String& filename) {
    clearScreen();
    
    EpdFontProperties props = epd_font_properties_default();
    int cx = 60, cy = 100;
    epd_write_string(&FiraSans_20, "Indexing Pages...", &cx, &cy, fb, &props);
    
    cx = 60; cy = 150;
    epd_write_string(&Firasans_12, filename.c_str(), &cx, &cy, fb, &props);
    
    cx = 60; cy = 200;
    epd_write_string(&Firasans_12, "Please wait...", &cx, &cy, fb, &props);
    
    updateScreen(true);
}

// ============================================================================
// FILE MANAGEMENT
// (Mostly unchanged from T-Deck Pro version)
// ============================================================================

void listTextFiles() {
    fileList.clear();
    
    Serial.println("Scanning for .txt files...");
    
    File root = SD.open("/");
    if (!root || !root.isDirectory()) {
        Serial.println("Failed to open root directory");
        return;
    }
    
    File file = root.openNextFile();
    while (file) {
        if (!file.isDirectory()) {
            String filename = String(file.name());
            if (filename.startsWith("/")) {
                filename = filename.substring(1);
            }
            
            if (filename.startsWith("._") || filename.startsWith(".")) {
                file = root.openNextFile();
                continue;
            }
            
            if (filename.endsWith(".txt") || filename.endsWith(".TXT")) {
                fileList.push_back(filename);
                Serial.printf("  Found: %s (%d bytes)\n", filename.c_str(), file.size());
            }
        }
        file = root.openNextFile();
    }
    
    root.close();
    Serial.printf("Total: %d text files\n", fileList.size());
}

String getIndexFilename(const String& txtFilename) {
    return "/.indexes/" + txtFilename + ".idx";
}

bool loadIndexFromSD(const String& filename, FileCache& cache) {
    String idxPath = getIndexFilename(filename);
    
    File idxFile = SD.open(idxPath.c_str(), FILE_READ);
    if (!idxFile) {
        return false;
    }
    
    uint8_t indexVersion = 0;
    unsigned long savedFileSize = 0;
    unsigned long pageCount = 0;
    uint8_t fullyIndexed = 0;
    int lastReadPage = 0;
    
    idxFile.read(&indexVersion, 1);
    
    if (indexVersion != INDEX_VERSION) {
        idxFile.seek(0);
        idxFile.read((uint8_t*)&savedFileSize, 4);
        idxFile.read((uint8_t*)&pageCount, 4);
        idxFile.read(&fullyIndexed, 1);
        lastReadPage = 0;
    } else {
        idxFile.read((uint8_t*)&savedFileSize, 4);
        idxFile.read((uint8_t*)&pageCount, 4);
        idxFile.read(&fullyIndexed, 1);
        idxFile.read((uint8_t*)&lastReadPage, 4);
    }
    
    String fullPath = "/" + filename;
    File txtFile = SD.open(fullPath.c_str(), FILE_READ);
    if (!txtFile) {
        idxFile.close();
        return false;
    }
    unsigned long currentFileSize = txtFile.size();
    txtFile.close();
    
    if (savedFileSize != currentFileSize) {
        Serial.printf("  Index stale for %s (size changed)\n", filename.c_str());
        idxFile.close();
        SD.remove(idxPath.c_str());
        return false;
    }
    
    cache.filename = filename;
    cache.fileSize = savedFileSize;
    cache.fullyIndexed = (fullyIndexed == 1);
    cache.lastReadPage = lastReadPage;
    cache.pagePositions.clear();
    
    for (unsigned long i = 0; i < pageCount; i++) {
        long pos = 0;
        idxFile.read((uint8_t*)&pos, 4);
        cache.pagePositions.push_back(pos);
    }
    
    idxFile.close();
    return true;
}

bool saveIndexToSD(const String& filename, const std::vector<long>& pagePositions, unsigned long fileSize, bool fullyIndexed, int lastReadPage) {
    if (!SD.exists("/.indexes")) {
        SD.mkdir("/.indexes");
    }
    
    String idxPath = getIndexFilename(filename);
    
    if (SD.exists(idxPath.c_str())) {
        SD.remove(idxPath.c_str());
    }
    
    File idxFile = SD.open(idxPath.c_str(), FILE_WRITE);
    if (!idxFile) {
        Serial.printf("  Failed to create index file: %s\n", idxPath.c_str());
        return false;
    }
    
    uint8_t version = INDEX_VERSION;
    unsigned long pageCount = pagePositions.size();
    uint8_t fullyFlag = fullyIndexed ? 1 : 0;
    
    idxFile.write(&version, 1);
    idxFile.write((uint8_t*)&fileSize, 4);
    idxFile.write((uint8_t*)&pageCount, 4);
    idxFile.write(&fullyFlag, 1);
    idxFile.write((uint8_t*)&lastReadPage, 4);
    
    for (unsigned long i = 0; i < pageCount; i++) {
        long pos = pagePositions[i];
        idxFile.write((uint8_t*)&pos, 4);
    }
    
    idxFile.close();
    return true;
}

bool saveReadingPosition(const String& filename, int page) {
    String idxPath = getIndexFilename(filename);
    
    File idxFile = SD.open(idxPath.c_str(), "r+");
    if (!idxFile) {
        Serial.printf("  Cannot update position - no index file for %s\n", filename.c_str());
        return false;
    }
    
    uint8_t version = 0;
    idxFile.read(&version, 1);
    
    if (version != INDEX_VERSION) {
        idxFile.close();
        for (int i = 0; i < fileCache.size(); i++) {
            if (fileCache[i].filename == filename) {
                fileCache[i].lastReadPage = page;
                return saveIndexToSD(filename, fileCache[i].pagePositions, 
                                    fileCache[i].fileSize, fileCache[i].fullyIndexed, page);
            }
        }
        return false;
    }
    
    idxFile.seek(1 + 4 + 4 + 1);
    idxFile.write((uint8_t*)&page, 4);
    idxFile.close();
    
    Serial.printf("  Saved reading position: page %d for %s\n", page + 1, filename.c_str());
    return true;
}

void preIndexFiles() {
    Serial.println("Loading/building file indexes...");
    fileCache.clear();
    
    for (int f = 0; f < fileList.size(); f++) {
        FileCache cache;
        
        if (loadIndexFromSD(fileList[f], cache)) {
            Serial.printf("  %s: loaded %d pages from cache%s (resume: pg %d)\n", 
                          fileList[f].c_str(), 
                          cache.pagePositions.size(),
                          cache.fullyIndexed ? " (complete)" : "",
                          cache.lastReadPage + 1);
            fileCache.push_back(cache);
            continue;
        }
        
        Serial.printf("  %s: building index...\n", fileList[f].c_str());
        
        String fullPath = "/" + fileList[f];
        File file = SD.open(fullPath.c_str(), FILE_READ);
        
        if (!file) {
            Serial.printf("    Skip: cannot open\n");
            continue;
        }
        
        cache.filename = fileList[f];
        cache.fileSize = file.size();
        cache.fullyIndexed = false;
        cache.lastReadPage = 0;
        cache.pagePositions.push_back(0);
        
        int lineCount = 0;
        int charCount = 0;
        int pageCount = 1;
        
        while (file.available() && pageCount < PREINDEX_PAGES) {
            char c = file.read();
            
            if (c == '\n') {
                lineCount++;
                charCount = 0;
                
                if (lineCount >= settings.linesPerPage) {
                    cache.pagePositions.push_back(file.position());
                    lineCount = 0;
                    pageCount++;
                }
            } else if (c >= 32 || c == '\t') {
                charCount++;
                
                if (charCount >= settings.charsPerLine) {
                    charCount = 0;
                    lineCount++;
                    
                    if (lineCount >= settings.linesPerPage) {
                        cache.pagePositions.push_back(file.position());
                        lineCount = 0;
                        pageCount++;
                    }
                }
            }
        }
        
        if (!file.available()) {
            cache.fullyIndexed = true;
        }
        
        file.close();
        
        saveIndexToSD(cache.filename, cache.pagePositions, cache.fileSize, cache.fullyIndexed, 0);
        fileCache.push_back(cache);
        
        Serial.printf("    %d pages indexed%s (saved to SD)\n", 
                      pageCount,
                      cache.fullyIndexed ? " - complete" : "");
    }
    
    Serial.println("Index loading complete!");
}

// ============================================================================
// FILE LIST DISPLAY
// ============================================================================

void displayFileList() {
    Serial.printf("Displaying file list, selectedFileIndex=%d\n", selectedFileIndex);
    
    clearScreen();
    
    EpdFontProperties props = epd_font_properties_default();
    EpdFontProperties invertedProps = epd_font_properties_default();
    invertedProps.fg_color = 15;  // White text
    
    // Title
    int cx = 30, cy = 40;
    epd_write_string(&FiraSans_20, "TEXT FILES", &cx, &cy, fb, &props);
    
    // Separator line
    drawHLine(0, 50, SCREEN_WIDTH, 0);
    
    if (fileList.size() == 0) {
        cx = 30; cy = 90;
        epd_write_string(&Firasans_12, "No .txt files found", &cx, &cy, fb, &props);
        cx = 30; cy = 120;
        epd_write_string(&Firasans_12, "Add files to SD card and reset device", &cx, &cy, fb, &props);
    } else {
        int maxVisible = 16;  // More items visible on the larger screen
        int startIdx = max(0, min(selectedFileIndex - 7, (int)fileList.size() - maxVisible));
        int endIdx = min((int)fileList.size(), startIdx + maxVisible);
        
        int lineHeight = 28;
        int y = 70;
        
        for (int i = startIdx; i < endIdx; i++) {
            bool isSelected = (i == selectedFileIndex);
            
            if (isSelected) {
                // Draw selection highlight (black background)
                fillRect(0, y - 5, SCREEN_WIDTH, lineHeight, 0);
            }
            
            String name = fileList[i];
            
            // Show resume indicator
            String suffix = "";
            for (int j = 0; j < fileCache.size(); j++) {
                if (fileCache[j].filename == name && fileCache[j].lastReadPage > 0) {
                    suffix = " *";
                    break;
                }
            }
            
            // Truncate long names
            int maxLen = 80 - suffix.length();
            String displayName;
            if (isSelected) {
                displayName = "> " + name;
            } else {
                displayName = "  " + name;
            }
            
            if (displayName.length() > maxLen) {
                displayName = displayName.substring(0, maxLen - 3) + "...";
            }
            displayName += suffix;
            
            cx = 15;
            int textY = y + 16;
            
            if (isSelected) {
                epd_write_string(&Firasans_12, displayName.c_str(), &cx, &textY, fb, &invertedProps);
            } else {
                epd_write_string(&Firasans_12, displayName.c_str(), &cx, &textY, fb, &props);
            }
            
            y += lineHeight;
        }
        
        // File counter
        char countStr[32];
        snprintf(countStr, sizeof(countStr), "%d/%d files", selectedFileIndex + 1, (int)fileList.size());
        cx = 20; cy = SCREEN_HEIGHT - 40;
        epd_write_string(&Firasans_12, countStr, &cx, &cy, fb, &props);
        
        // Navigation hints
        drawHLine(0, SCREEN_HEIGHT - 25, SCREEN_WIDTH, 0);
        cx = 20; cy = SCREEN_HEIGHT - 8;
        epd_write_string(&Firasans_12, "Tap item to open  |  Swipe up/down to scroll", &cx, &cy, fb, &props);
    }
    
    updateScreen(true);
    Serial.println("File list display complete");
}

// ============================================================================
// BOOK READING
// ============================================================================

void openBook(const String& filename) {
    Serial.printf("Opening: %s\n", filename.c_str());
    
    if (reader.fileOpen) {
        closeBook();
    }
    
    FileCache* cache = nullptr;
    for (int i = 0; i < fileCache.size(); i++) {
        if (fileCache[i].filename == filename) {
            cache = &fileCache[i];
            break;
        }
    }
    
    String fullPath = "/" + filename;
    reader.file = SD.open(fullPath.c_str(), FILE_READ);
    
    if (!reader.file) {
        Serial.println("Failed to open file!");
        clearScreen();
        EpdFontProperties props = epd_font_properties_default();
        int cx = 300, cy = 260;
        epd_write_string(&FiraSans_20, "FAILED TO OPEN FILE", &cx, &cy, fb, &props);
        updateScreen(true);
        delay(2000);
        displayFileList();
        return;
    }
    
    reader.currentFile = filename;
    reader.fileOpen = true;
    reader.currentPage = 0;
    reader.pagePositions.clear();
    
    if (cache != nullptr) {
        Serial.printf("Using cached index (%d pages pre-indexed)\n", cache->pagePositions.size());
        
        for (int i = 0; i < cache->pagePositions.size(); i++) {
            reader.pagePositions.push_back(cache->pagePositions[i]);
        }
        
        if (cache->lastReadPage > 0 && cache->lastReadPage < cache->pagePositions.size()) {
            reader.currentPage = cache->lastReadPage;
            Serial.printf("Resuming at page %d\n", reader.currentPage + 1);
        }
        
        if (cache->fullyIndexed) {
            reader.totalPages = reader.pagePositions.size();
            Serial.printf("File fully pre-indexed: %d pages\n", reader.totalPages);
            displayPageFull();
            return;
        }
        
        Serial.println("Continuing indexing from cache...");
        showIndexingScreen(filename);
        
        long lastCachedPos = cache->pagePositions.back();
        reader.file.seek(lastCachedPos);
    } else {
        Serial.println("No cache - indexing from start...");
        showIndexingScreen(filename);
        reader.pagePositions.push_back(0);
    }
    
    // Continue/complete indexing
    int lineCount = 0;
    int charCount = 0;
    unsigned long fileSize = reader.file.size();
    unsigned long lastProgress = 0;
    
    while (reader.file.available()) {
        char c = reader.file.read();
        
        unsigned long pos = reader.file.position();
        unsigned long progress = (pos * 100) / fileSize;
        if (progress >= lastProgress + 10) {
            Serial.printf("  Indexing: %lu%%\n", progress);
            lastProgress = progress;
        }
        
        if (c == '\n') {
            lineCount++;
            charCount = 0;
            
            if (lineCount >= settings.linesPerPage) {
                reader.pagePositions.push_back(reader.file.position());
                lineCount = 0;
            }
        } else if (c >= 32 || c == '\t') {
            charCount++;
            
            if (charCount >= settings.charsPerLine) {
                charCount = 0;
                lineCount++;
                
                if (lineCount >= settings.linesPerPage) {
                    reader.pagePositions.push_back(reader.file.position());
                    lineCount = 0;
                }
            }
        }
    }
    
    reader.totalPages = reader.pagePositions.size();
    Serial.printf("Total pages: %d\n", reader.totalPages);
    
    if (saveIndexToSD(filename, reader.pagePositions, reader.file.size(), true, reader.currentPage)) {
        Serial.println("Full index saved to SD card");
    }
    
    displayPageFull();
}

// ============================================================================
// WORD WRAP (unchanged from T-Deck Pro version)
// ============================================================================

WrapResult findLineBreak(const char* buffer, int bufLen, int lineStart, int maxChars) {
    WrapResult result;
    result.lineEnd = lineStart;
    result.nextStart = lineStart;
    
    if (lineStart >= bufLen) {
        return result;
    }
    
    int charCount = 0;
    int lastBreakPoint = -1;
    int lastBreakCharCount = 0;
    bool inWord = false;
    
    for (int i = lineStart; i < bufLen; i++) {
        char c = buffer[i];
        
        if (c == '\n') {
            result.lineEnd = i;
            result.nextStart = i + 1;
            if (result.nextStart < bufLen && buffer[result.nextStart] == '\r') {
                result.nextStart++;
            }
            return result;
        }
        
        if (c == '\r') {
            result.lineEnd = i;
            result.nextStart = i + 1;
            if (result.nextStart < bufLen && buffer[result.nextStart] == '\n') {
                result.nextStart++;
            }
            return result;
        }
        
        if (c >= 32) {
            charCount++;
            
            if (c == ' ' || c == '\t') {
                if (inWord) {
                    lastBreakPoint = i;
                    lastBreakCharCount = charCount;
                    inWord = false;
                }
            } else if (c == '-') {
                if (inWord) {
                    lastBreakPoint = i + 1;
                    lastBreakCharCount = charCount;
                }
            } else {
                inWord = true;
            }
            
            if (charCount >= maxChars) {
                if (lastBreakPoint > lineStart) {
                    result.lineEnd = lastBreakPoint;
                    result.nextStart = lastBreakPoint;
                    
                    while (result.nextStart < bufLen && 
                           (buffer[result.nextStart] == ' ' || buffer[result.nextStart] == '\t')) {
                        result.nextStart++;
                    }
                } else {
                    result.lineEnd = i;
                    result.nextStart = i;
                }
                return result;
            }
        }
    }
    
    result.lineEnd = bufLen;
    result.nextStart = bufLen;
    return result;
}

// ============================================================================
// PAGE DISPLAY
// ============================================================================

void displayPageFull() {
    if (!reader.fileOpen || reader.currentPage >= reader.totalPages) {
        Serial.println("Cannot display page - invalid state");
        return;
    }
    
    const int TEXT_AREA_TOP = settings.marginY;
    const int TEXT_AREA_HEIGHT = SCREEN_HEIGHT - settings.statusBarHeight - TEXT_AREA_TOP;
    const int MAX_LINES = TEXT_AREA_HEIGHT / settings.lineHeight;
    const int CHARS_PER_LINE = settings.charsPerLine;
    
    long pagePos = reader.pagePositions[reader.currentPage];
    reader.file.seek(pagePos);
    
    Serial.printf("displayPageFull: page %d, pos %ld\n", reader.currentPage + 1, pagePos);
    
    // Read page content into buffer
    const int BUF_SIZE = 3000;  // Larger buffer for the bigger display
    char buffer[BUF_SIZE];
    int bufLen = 0;
    
    while (reader.file.available() && bufLen < BUF_SIZE - 1) {
        char c = reader.file.read();
        if (c >= 32 || c == '\n' || c == '\r') {
            buffer[bufLen++] = c;
        }
        if (bufLen > MAX_LINES * CHARS_PER_LINE * 2) break;
    }
    buffer[bufLen] = '\0';
    
    Serial.printf("displayPageFull: read %d chars\n", bufLen);
    
    // Clear framebuffer and draw page
    clearScreen();
    
    EpdFontProperties props = epd_font_properties_default();
    
    int y = TEXT_AREA_TOP + settings.lineHeight;
    int lineCount = 0;
    int pos = 0;
    
    // Render text line by line using word wrap
    while (pos < bufLen && lineCount < MAX_LINES) {
        WrapResult wrap = findLineBreak(buffer, bufLen, pos, CHARS_PER_LINE);
        
        // Extract line text
        int lineLen = wrap.lineEnd - pos;
        if (lineLen > 0 && lineLen < 256) {
            char lineBuf[257];
            int outIdx = 0;
            for (int j = pos; j < wrap.lineEnd && j < bufLen; j++) {
                char ch = buffer[j];
                if (ch >= 32) {
                    lineBuf[outIdx++] = ch;
                }
            }
            lineBuf[outIdx] = '\0';
            
            int cx = settings.marginX;
            int cy = y;
            epd_write_string(&Firasans_12, lineBuf, &cx, &cy, fb, &props);
        }
        
        y += settings.lineHeight;
        lineCount++;
        pos = wrap.nextStart;
        
        if (wrap.nextStart <= pos && wrap.lineEnd >= bufLen) break;
        if (pos >= bufLen) break;
    }
    
    // Draw status bar
    int statusY = SCREEN_HEIGHT - settings.statusBarHeight;
    drawHLine(0, statusY, SCREEN_WIDTH, 0);
    
    char statusLeft[64];
    snprintf(statusLeft, sizeof(statusLeft), "Page %d / %d", reader.currentPage + 1, reader.totalPages);
    int cx = 20;
    int cy = statusY + 22;
    epd_write_string(&Firasans_12, statusLeft, &cx, &cy, fb, &props);
    
    int percent = reader.totalPages > 1 ? 
        (reader.currentPage * 100) / (reader.totalPages - 1) : 100;
    char statusMid[32];
    snprintf(statusMid, sizeof(statusMid), "%d%%", percent);
    cx = SCREEN_WIDTH / 2 - 30;
    cy = statusY + 22;
    epd_write_string(&Firasans_12, statusMid, &cx, &cy, fb, &props);
    
    // Navigation hints
    cx = SCREEN_WIDTH - 350;
    cy = statusY + 22;
    epd_write_string(&Firasans_12, "< Prev  |  Next >  |  Back", &cx, &cy, fb, &props);
    
    updateScreen(true);
    
    Serial.printf("Displayed page %d/%d\n", reader.currentPage + 1, reader.totalPages);
}

void displayPage() {
    displayPageFull();
}

void nextPage() {
    if (reader.fileOpen && reader.currentPage < reader.totalPages - 1) {
        reader.currentPage++;
        Serial.printf("Next page: %d\n", reader.currentPage + 1);
        displayPage();
    }
}

void prevPage() {
    if (reader.fileOpen && reader.currentPage > 0) {
        reader.currentPage--;
        Serial.printf("Previous page: %d\n", reader.currentPage + 1);
        displayPage();
    }
}

void closeBook() {
    if (reader.fileOpen) {
        Serial.println("Closing book");
        
        saveReadingPosition(reader.currentFile, reader.currentPage);
        
        for (int i = 0; i < fileCache.size(); i++) {
            if (fileCache[i].filename == reader.currentFile) {
                fileCache[i].lastReadPage = reader.currentPage;
                break;
            }
        }
        
        reader.file.close();
        reader.fileOpen = false;
        reader.pagePositions.clear();
        reader.pagePositions.shrink_to_fit();
    }
}

// ============================================================================
// TOUCH INPUT
// ============================================================================

// Debounce state
static unsigned long lastTouchTime = 0;
static const unsigned long TOUCH_DEBOUNCE_MS = 400;  // Prevent rapid repeated touches

TouchAction readTouch() {
    if (!touchAvailable) return TOUCH_NONE;
    
    int16_t x, y;
    if (!touch.getPoint(&x, &y)) {
        return TOUCH_NONE;
    }
    
    // Debounce
    unsigned long now = millis();
    if (now - lastTouchTime < TOUCH_DEBOUNCE_MS) {
        return TOUCH_NONE;
    }
    lastTouchTime = now;
    
    Serial.printf("Touch at (%d, %d)\n", x, y);
    
    if (!reader.fileOpen) {
        // FILE LIST MODE
        // Determine which file was tapped based on Y position
        int headerHeight = 60;
        int lineHeight = 28;
        int maxVisible = 16;
        int startIdx = max(0, min(selectedFileIndex - 7, (int)fileList.size() - maxVisible));
        
        if (y > headerHeight && y < SCREEN_HEIGHT - 50) {
            int tappedIndex = startIdx + (y - headerHeight) / lineHeight;
            if (tappedIndex >= 0 && tappedIndex < (int)fileList.size()) {
                if (tappedIndex == selectedFileIndex) {
                    // Double-tap on selected item = open
                    return TOUCH_NEXT_PAGE;  // We'll interpret this as "open" in file list mode
                } else {
                    selectedFileIndex = tappedIndex;
                    displayFileList();  // Redraw with new selection
                    return TOUCH_NONE;
                }
            }
        }
        
        // Swipe-like: top half = scroll up, bottom half = scroll down
        if (x < 100 && y < 80) {
            return TOUCH_BACK;  // Not meaningful in file list, but keep consistent
        }
        
        return TOUCH_NONE;
    } else {
        // READING MODE
        // Top-left corner = back/exit
        if (x < TOUCH_ZONE_BACK_W && y < TOUCH_ZONE_BACK_H) {
            return TOUCH_BACK;
        }
        
        // Bottom status bar tap
        if (y > SCREEN_HEIGHT - TOUCH_ZONE_STATUS_H) {
            return TOUCH_STATUS;
        }
        
        // Left quarter = previous page
        if (x < TOUCH_ZONE_LEFT_W) {
            return TOUCH_PREV_PAGE;
        }
        
        // Right quarter = next page
        if (x >= TOUCH_ZONE_RIGHT_X) {
            return TOUCH_NEXT_PAGE;
        }
        
        // Center tap = next page (most common action, like a Kindle)
        return TOUCH_NEXT_PAGE;
    }
}

void handleTouch(TouchAction action) {
    Serial.printf("handleTouch: action=%d, fileOpen=%d\n", action, reader.fileOpen);
    
    if (!reader.fileOpen) {
        // FILE LIST MODE
        switch (action) {
            case TOUCH_NEXT_PAGE:
                // Open selected file
                Serial.printf("  OPEN: opening file %d\n", selectedFileIndex);
                if (fileList.size() > 0) {
                    openBook(fileList[selectedFileIndex]);
                }
                break;
                
            case TOUCH_PREV_PAGE:
                // Scroll up
                if (selectedFileIndex > 0) {
                    selectedFileIndex--;
                    displayFileList();
                }
                break;
                
            default:
                break;
        }
    } else {
        // READING MODE
        switch (action) {
            case TOUCH_NEXT_PAGE:
                nextPage();
                break;
                
            case TOUCH_PREV_PAGE:
                prevPage();
                break;
                
            case TOUCH_BACK:
                Serial.println("  EXIT: closing book");
                closeBook();
                delay(100);
                displayFileList();
                break;
                
            case TOUCH_STATUS:
                // Could toggle status bar info or show a menu
                // For now, treat as next page
                nextPage();
                break;
                
            default:
                break;
        }
    }
}