#include <Arduino.h>
#include <GxEPD2_BW.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <vector>

// ============================================================================
// T-DECK PRO V1.1 HARDWARE DEFINITIONS
// ============================================================================

// Power
#define PWR_EN 40

// E-Paper Display (GDEQ031T10)
#define EPD_SCK  36
#define EPD_MOSI 33
#define EPD_DC   35
#define EPD_CS   34
#define EPD_BUSY 37
#define EPD_RST  -1  // No hardware reset

// SD Card - shares SPI bus with display and LoRa
#define SD_CS   48
#define SD_MISO 47
#define SD_MOSI 33  
#define SD_SCK  36

// Keyboard (TCA8418)
#define KB_SDA  13
#define KB_SCL  14
#define KB_INT  15
#define KB_ADDR 0x34

// TCA8418 Register addresses
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO1    0x1D
#define TCA8418_REG_KP_GPIO2    0x1E
#define TCA8418_REG_KP_GPIO3    0x1F
#define TCA8418_REG_DEBOUNCE    0x29
#define TCA8418_REG_GPI_EM1     0x20
#define TCA8418_REG_GPI_EM2     0x21
#define TCA8418_REG_GPI_EM3     0x22

// Display specs - Portrait mode (rotation 0)
#define SCREEN_WIDTH  240
#define SCREEN_HEIGHT 320

// ============================================================================
// DISPLAY SETUP
// ============================================================================

// Using GxEPD2_BW for black & white e-paper
// GDEQ031T10 is a 320x240 e-paper display
GxEPD2_BW<GxEPD2_310_GDEQ031T10, GxEPD2_310_GDEQ031T10::HEIGHT> display(
    GxEPD2_310_GDEQ031T10(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)
);

// ============================================================================
// READER STATE
// ============================================================================

// Version info
#define VERSION "1.0.0"
#define BUILD_DATE "Jan 2025"

struct Settings {
    uint8_t textSize;
    uint8_t linesPerPage;
    uint8_t charsPerLine;
} settings = {2, 15, 19};  // Size 2 font, ~15 lines, ~19 chars per line

struct ReaderState {
    String currentFile;
    File file;
    std::vector<long> pagePositions;
    int currentPage;
    int totalPages;
    bool fileOpen;
} reader;

// Pre-indexed page cache for quick opening
#define PREINDEX_PAGES 100  // Pre-index first 100 pages of each file
struct FileCache {
    String filename;
    std::vector<long> pagePositions;  // First N page positions
    unsigned long fileSize;
    bool fullyIndexed;  // True if file has <= PREINDEX_PAGES pages
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
void initKeyboard();
void showSplashScreen();
void showIndexingScreen(const String& filename);
void listTextFiles();
void preIndexFiles();
bool loadIndexFromSD(const String& filename, FileCache& cache);
bool saveIndexToSD(const String& filename, const std::vector<long>& pagePositions, unsigned long fileSize);
String getIndexFilename(const String& txtFilename);
void displayFileList();
void openBook(const String& filename);
void displayPage();
void nextPage();
void prevPage();
void closeBook();
uint8_t readKeyboard();
void handleKeyPress(uint8_t key);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\n====================================");
    Serial.println("T-Deck Pro E-Paper Text Reader");
    Serial.printf("Version %s (%s)\n", VERSION, BUILD_DATE);
    Serial.println("====================================\n");
    
    initHardware();
    initDisplay();
    
    // Show splash screen while loading
    showSplashScreen();
    
    initSD();
    initKeyboard();
    
    listTextFiles();
    preIndexFiles();  // Pre-index files for fast opening
    
    reader.fileOpen = false;
    reader.currentPage = 0;
    
    displayFileList();
    
    Serial.println("Setup complete!");
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
    uint8_t key = readKeyboard();
    if (key != 0) {
        handleKeyPress(key);
    }
    
    delay(50);
}

// ============================================================================
// HARDWARE INITIALIZATION
// ============================================================================

void initHardware() {
    Serial.println("Initializing hardware...");
    
    // Enable power - CRITICAL!
    pinMode(PWR_EN, OUTPUT);
    digitalWrite(PWR_EN, HIGH);
    delay(200);
    
    Serial.println("✓ Power enabled");
}

void initDisplay() {
    Serial.println("Initializing e-paper display...");
    
    // Initialize SPI bus (shared by display, SD card, and LoRa)
    SPI.begin(EPD_SCK, 47, EPD_MOSI);  // SCK=36, MISO=47, MOSI=33
    
    // Initialize display
    display.init(115200, true, 2, false);
    display.setRotation(0);  // Portrait mode
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(2);
    
    Serial.println("✓ Display initialized");
}

void showSplashScreen() {
    Serial.println("Showing splash screen...");
    
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        
        // Draw a simple book icon (rectangle with lines)
        int iconX = (SCREEN_WIDTH - 60) / 2;
        int iconY = 60;
        // Book outline
        display.drawRect(iconX, iconY, 60, 80, GxEPD_BLACK);
        display.drawRect(iconX + 2, iconY + 2, 56, 76, GxEPD_BLACK);
        // Spine
        display.fillRect(iconX + 28, iconY, 4, 80, GxEPD_BLACK);
        // Page lines (left side)
        for (int i = 0; i < 5; i++) {
            display.drawFastHLine(iconX + 8, iconY + 15 + (i * 12), 16, GxEPD_BLACK);
        }
        // Page lines (right side)
        for (int i = 0; i < 5; i++) {
            display.drawFastHLine(iconX + 36, iconY + 15 + (i * 12), 16, GxEPD_BLACK);
        }
        
        // Title
        display.setTextSize(2);
        display.setTextColor(GxEPD_BLACK);
        
        // Center "TextReader"
        display.setCursor(42, 160);
        display.println("TextReader");
        
        // Version and date
        display.setTextSize(1);
        display.setCursor(75, 190);
        display.printf("v%s", VERSION);
        
        display.setCursor(75, 205);
        display.print(BUILD_DATE);
        
        // Loading message
        display.setCursor(70, 250);
        display.print("Loading...");
        
    } while (display.nextPage());
    
    // Wait for e-ink to physically complete the refresh
    display.hibernate();
    delay(2000);  // Show splash for 2 seconds
    Serial.println("Splash screen complete");
}

void showIndexingScreen(const String& filename) {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        
        // Title
        display.setTextSize(2);
        display.setCursor(20, 40);
        display.println("Indexing");
        display.setCursor(20, 65);
        display.println("Pages...");
        
        // Word-wrapped filename
        display.setTextSize(1);
        int y = 110;
        int maxChars = 36;  // Characters per line at size 1
        String remaining = filename;
        
        while (remaining.length() > 0 && y < 200) {
            String line;
            if (remaining.length() <= maxChars) {
                line = remaining;
                remaining = "";
            } else {
                // Find last space within limit
                int breakPoint = maxChars;
                for (int i = maxChars; i > 0; i--) {
                    if (remaining[i] == ' ' || remaining[i] == '-' || remaining[i] == '_') {
                        breakPoint = i;
                        break;
                    }
                }
                line = remaining.substring(0, breakPoint);
                remaining = remaining.substring(breakPoint);
                remaining.trim();
            }
            display.setCursor(20, y);
            display.println(line);
            y += 12;
        }
        
        // Please wait message
        display.setCursor(20, 230);
        display.println("Please wait.");
        display.setCursor(20, 245);
        display.println("Loading shortly...");
        
    } while (display.nextPage());
    
    display.hibernate();
    delay(100);
}

void initSD() {
    Serial.println("Initializing SD card...");
    
    // SD card shares SPI bus with display (already initialized)
    // Just need to set CS pin and begin SD
    pinMode(SD_CS, OUTPUT);
    digitalWrite(SD_CS, HIGH);  // Deselect SD initially
    
    if (!SD.begin(SD_CS, SPI, 4000000)) {  // Use default SPI, 4MHz
        Serial.println("✗ SD Card failed!");
        
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            display.setCursor(10, 60);
            display.setTextSize(2);
            display.println("SD CARD ERROR");
            display.setCursor(10, 90);
            display.setTextSize(1);
            display.println("Insert card & reset");
        } while (display.nextPage());
        
        while (1) delay(1000);
    }
    
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("✓ SD Card: %llu MB\n", cardSize);
}

void writeKBReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(KB_ADDR);
    Wire.write(reg);
    Wire.write(value);
    Wire.endTransmission();
}

uint8_t readKBReg(uint8_t reg) {
    Wire.beginTransmission(KB_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)KB_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

void initKeyboard() {
    Serial.println("Initializing keyboard...");
    
    Wire.begin(KB_SDA, KB_SCL);
    Wire.setClock(100000);
    pinMode(KB_INT, INPUT_PULLUP);
    
    // Check if TCA8418 is present
    Wire.beginTransmission(KB_ADDR);
    uint8_t error = Wire.endTransmission();
    
    if (error != 0) {
        Serial.printf("✗ Keyboard not found (error: %d)\n", error);
        return;
    }
    
    Serial.println("  TCA8418 found, configuring...");
    
    // Configure all ROW pins (0-7) as keyboard
    writeKBReg(TCA8418_REG_KP_GPIO1, 0xFF);  // ROW0-7 as keyboard
    
    // Configure COL pins (8-17) as keyboard  
    writeKBReg(TCA8418_REG_KP_GPIO2, 0xFF);  // COL0-7 as keyboard
    writeKBReg(TCA8418_REG_KP_GPIO3, 0x03);  // COL8-9 as keyboard
    
    // Enable key event interrupt and overflow interrupt
    // Bit 0: KE_IEN (key events)
    // Bit 3: GPI_IEN (GPI events) 
    // Bit 4: K_LCK_IEN (key lock)
    writeKBReg(TCA8418_REG_CFG, 0x11);  // Enable key event interrupt + INT stays active
    
    // Clear any pending interrupts by reading the interrupt status
    readKBReg(TCA8418_REG_INT_STAT);
    
    // Clear the key event FIFO by reading all events
    while (readKBReg(TCA8418_REG_KEY_LCK_EC) & 0x0F) {
        readKBReg(TCA8418_REG_KEY_EVENT_A);
    }
    
    // Clear interrupt status again
    writeKBReg(TCA8418_REG_INT_STAT, 0x1F);  // Clear all interrupt flags
    
    Serial.println("✓ Keyboard initialized");
}

// ============================================================================
// FILE MANAGEMENT
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
            
            // Skip macOS hidden files (._* and .DS_Store etc)
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

// Get the index filename for a given text file
String getIndexFilename(const String& txtFilename) {
    // Create index filename like ".mybook.idx" in a .indexes folder
    return "/.indexes/" + txtFilename + ".idx";
}

// Load index from SD card
bool loadIndexFromSD(const String& filename, FileCache& cache) {
    String idxPath = getIndexFilename(filename);
    
    File idxFile = SD.open(idxPath.c_str(), FILE_READ);
    if (!idxFile) {
        return false;  // No index file exists
    }
    
    // Read header: filesize (4 bytes) + page count (4 bytes) + fully indexed flag (1 byte)
    unsigned long savedFileSize = 0;
    unsigned long pageCount = 0;
    uint8_t fullyIndexed = 0;
    
    idxFile.read((uint8_t*)&savedFileSize, 4);
    idxFile.read((uint8_t*)&pageCount, 4);
    idxFile.read(&fullyIndexed, 1);
    
    // Verify file size matches (if file changed, index is invalid)
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
        SD.remove(idxPath.c_str());  // Delete stale index
        return false;
    }
    
    // Read page positions
    cache.filename = filename;
    cache.fileSize = savedFileSize;
    cache.fullyIndexed = (fullyIndexed == 1);
    cache.pagePositions.clear();
    
    for (unsigned long i = 0; i < pageCount; i++) {
        long pos = 0;
        idxFile.read((uint8_t*)&pos, 4);
        cache.pagePositions.push_back(pos);
    }
    
    idxFile.close();
    return true;
}

// Save index to SD card
bool saveIndexToSD(const String& filename, const std::vector<long>& pagePositions, unsigned long fileSize, bool fullyIndexed) {
    // Create indexes folder if it doesn't exist
    if (!SD.exists("/.indexes")) {
        SD.mkdir("/.indexes");
    }
    
    String idxPath = getIndexFilename(filename);
    
    // Remove old index if exists
    if (SD.exists(idxPath.c_str())) {
        SD.remove(idxPath.c_str());
    }
    
    File idxFile = SD.open(idxPath.c_str(), FILE_WRITE);
    if (!idxFile) {
        Serial.printf("  Failed to create index file: %s\n", idxPath.c_str());
        return false;
    }
    
    // Write header
    unsigned long pageCount = pagePositions.size();
    uint8_t fullyFlag = fullyIndexed ? 1 : 0;
    
    idxFile.write((uint8_t*)&fileSize, 4);
    idxFile.write((uint8_t*)&pageCount, 4);
    idxFile.write(&fullyFlag, 1);
    
    // Write page positions
    for (unsigned long i = 0; i < pageCount; i++) {
        long pos = pagePositions[i];
        idxFile.write((uint8_t*)&pos, 4);
    }
    
    idxFile.close();
    return true;
}

void preIndexFiles() {
    Serial.println("Loading/building file indexes...");
    fileCache.clear();
    
    for (int f = 0; f < fileList.size(); f++) {
        FileCache cache;
        
        // Try to load existing index from SD
        if (loadIndexFromSD(fileList[f], cache)) {
            Serial.printf("  %s: loaded %d pages from cache%s\n", 
                          fileList[f].c_str(), 
                          cache.pagePositions.size(),
                          cache.fullyIndexed ? " (complete)" : "");
            fileCache.push_back(cache);
            continue;
        }
        
        // No cached index - build it
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
        cache.pagePositions.push_back(0);  // First page always at 0
        
        int lineCount = 0;
        int charCount = 0;
        int pageCount = 1;
        
        // Index the first PREINDEX_PAGES pages
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
        
        // Check if we indexed the whole file
        if (!file.available()) {
            cache.fullyIndexed = true;
        }
        
        file.close();
        
        // Save the partial index to SD for next time
        saveIndexToSD(cache.filename, cache.pagePositions, cache.fileSize, cache.fullyIndexed);
        
        fileCache.push_back(cache);
        
        Serial.printf("    %d pages indexed%s (saved to SD)\n", 
                      pageCount,
                      cache.fullyIndexed ? " - complete" : "");
    }
    
    Serial.println("Index loading complete!");
}

void displayFileList() {
    Serial.printf("Displaying file list, selectedFileIndex=%d\n", selectedFileIndex);
    
    display.setFullWindow();
    display.firstPage();
    
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK);
        display.setTextSize(2);
        
        display.setCursor(10, 5);
        display.println("TEXT FILES");
        display.drawFastHLine(0, 25, SCREEN_WIDTH, GxEPD_BLACK);
        
        if (fileList.size() == 0) {
            display.setCursor(10, 40);
            display.setTextSize(1);
            display.println("No .txt files found");
            display.println();
            display.println("Add files to SD card");
            display.println("and reset device");
        } else {
            // Show files with selection indicator
            int maxVisible = 8;
            int startIdx = max(0, min(selectedFileIndex - 3, (int)fileList.size() - maxVisible));
            int endIdx = min((int)fileList.size(), startIdx + maxVisible);
            
            int y = 35;
            for (int i = startIdx; i < endIdx; i++) {
                display.setCursor(5, y);
                display.setTextSize(2);
                
                if (i == selectedFileIndex) {
                    // Highlighted row - white text on black background
                    display.fillRect(0, y - 2, SCREEN_WIDTH, 24, GxEPD_BLACK);
                    display.setTextColor(GxEPD_WHITE, GxEPD_BLACK);
                    display.print("> ");
                } else {
                    // Normal row - black text on white background
                    display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
                    display.print("  ");
                }
                
                // Truncate filename if too long
                String name = fileList[i];
                if (name.length() > 22) {
                    name = name.substring(0, 19) + "...";
                }
                display.println(name);
                
                y += 24;
            }
            
            // Reset text color for status area
            display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
            
            // File count
            display.setTextSize(1);
            display.setCursor(5, SCREEN_HEIGHT - 25);
            display.printf("%d/%d files", selectedFileIndex + 1, fileList.size());
            
            // Navigation hint
            display.drawFastHLine(0, SCREEN_HEIGHT - 14, SCREEN_WIDTH, GxEPD_BLACK);
            display.setCursor(5, SCREEN_HEIGHT - 10);
            display.print("ENTER=Open W/S=Nav");
        }
    } while (display.nextPage());
    
    display.hibernate();  // Ensure display refresh completes
    Serial.println("File list display complete");
}

// ============================================================================
// BOOK READING FUNCTIONS
// ============================================================================

void openBook(const String& filename) {
    Serial.printf("Opening: %s\n", filename.c_str());
    
    if (reader.fileOpen) {
        closeBook();
    }
    
    // Find cached index for this file
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
        display.setFullWindow();
        display.firstPage();
        do {
            display.fillScreen(GxEPD_WHITE);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(30, 140);
            display.setTextSize(2);
            display.println("FAILED TO");
            display.setCursor(30, 170);
            display.println("OPEN FILE");
        } while (display.nextPage());
        delay(2000);
        displayFileList();
        return;
    }
    
    reader.currentFile = filename;
    reader.fileOpen = true;
    reader.currentPage = 0;
    reader.pagePositions.clear();
    
    // Use cached index if available
    if (cache != nullptr) {
        Serial.printf("Using cached index (%d pages pre-indexed)\n", cache->pagePositions.size());
        
        // Copy cached page positions
        for (int i = 0; i < cache->pagePositions.size(); i++) {
            reader.pagePositions.push_back(cache->pagePositions[i]);
        }
        
        // If fully indexed, we're done
        if (cache->fullyIndexed) {
            reader.totalPages = reader.pagePositions.size();
            Serial.printf("File fully pre-indexed: %d pages\n", reader.totalPages);
            displayPage();
            return;
        }
        
        // Otherwise, continue indexing from where cache left off
        Serial.println("Continuing indexing from cache...");
        showIndexingScreen(filename);
        
        // Seek to end of cached portion
        long lastCachedPos = cache->pagePositions.back();
        reader.file.seek(lastCachedPos);
    } else {
        // No cache - show loading and index from scratch
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
        
        // Print progress every 10%
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
    
    // Save the complete index to SD for next time
    if (saveIndexToSD(filename, reader.pagePositions, reader.file.size(), true)) {
        Serial.println("Full index saved to SD card");
    }
    
    displayPage();
}

void displayPage() {
    if (!reader.fileOpen || reader.currentPage >= reader.totalPages) {
        Serial.println("Cannot display page - invalid state");
        return;
    }
    
    // Display constants
    const int STATUS_BAR_HEIGHT = 18;
    const int TEXT_AREA_HEIGHT = SCREEN_HEIGHT - STATUS_BAR_HEIGHT;
    const int LINE_HEIGHT = 20;
    const int MAX_LINES = TEXT_AREA_HEIGHT / LINE_HEIGHT;
    const int CHARS_PER_LINE = 19;
    
    // Seek to page start
    long pagePos = reader.pagePositions[reader.currentPage];
    reader.file.seek(pagePos);
    
    Serial.printf("displayPage: page %d, pos %ld\n", reader.currentPage + 1, pagePos);
    
    // Read page content into a simple char buffer (more memory efficient than String)
    const int BUF_SIZE = 800;  // Enough for ~15 lines of ~50 chars
    char buffer[BUF_SIZE];
    int bufLen = 0;
    
    while (reader.file.available() && bufLen < BUF_SIZE - 1) {
        char c = reader.file.read();
        if (c >= 32 || c == '\n' || c == '\r') {
            buffer[bufLen++] = c;
        }
        // Stop after reading enough for one page
        if (bufLen > MAX_LINES * CHARS_PER_LINE * 2) break;
    }
    buffer[bufLen] = '\0';
    
    Serial.printf("displayPage: read %d chars\n", bufLen);
    
    display.setFullWindow();
    display.firstPage();
    
    do {
        display.fillScreen(GxEPD_WHITE);
        display.setTextColor(GxEPD_BLACK, GxEPD_WHITE);
        display.setTextSize(2);
        
        int y = 4;
        int lineCount = 0;
        int lineStart = 0;
        int lastSpace = -1;
        int charOnLine = 0;
        
        for (int i = 0; i <= bufLen && lineCount < MAX_LINES; i++) {
            char c = (i < bufLen) ? buffer[i] : '\n';  // Force final line output
            
            if (c == ' ') {
                lastSpace = i;
            }
            
            if (c == '\n' || c == '\r' || charOnLine >= CHARS_PER_LINE) {
                // End of line - output it
                int lineEnd = i;
                
                // If we hit the char limit and there was a space, break at the space
                if (charOnLine >= CHARS_PER_LINE && lastSpace > lineStart) {
                    lineEnd = lastSpace;
                    i = lastSpace;  // Continue from after the space
                }
                
                // Output the line
                display.setCursor(2, y);
                for (int j = lineStart; j < lineEnd && j < bufLen; j++) {
                    char ch = buffer[j];
                    if (ch >= 32) {
                        display.print(ch);
                    }
                }
                
                y += LINE_HEIGHT;
                lineCount++;
                
                // Skip whitespace at start of next line
                while (i + 1 < bufLen && (buffer[i + 1] == ' ' || buffer[i + 1] == '\r')) {
                    i++;
                }
                
                lineStart = i + 1;
                lastSpace = -1;
                charOnLine = 0;
            } else if (c >= 32) {
                charOnLine++;
            }
        }
        
        // Status bar
        display.setTextSize(1);
        int statusY = SCREEN_HEIGHT - STATUS_BAR_HEIGHT + 4;
        display.drawFastHLine(0, SCREEN_HEIGHT - STATUS_BAR_HEIGHT, SCREEN_WIDTH, GxEPD_BLACK);
        
        display.setCursor(4, statusY);
        display.printf("%d/%d", reader.currentPage + 1, reader.totalPages);
        
        int percent = reader.totalPages > 1 ? 
            (reader.currentPage * 100) / (reader.totalPages - 1) : 100;
        display.setCursor(60, statusY);
        display.printf("%d%%", percent);
        
        display.setCursor(95, statusY);
        display.print("W:Prev S:Next");
        
        display.setCursor(200, statusY);
        display.print("Q:Exit");
        
    } while (display.nextPage());
    
    display.hibernate();  // Ensure display refresh completes
    Serial.printf("Displayed page %d/%d\n", reader.currentPage + 1, reader.totalPages);
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
        reader.file.close();
        reader.fileOpen = false;
        reader.pagePositions.clear();
        reader.pagePositions.shrink_to_fit();
    }
}

// ============================================================================
// KEYBOARD INPUT
// ============================================================================

// T-Deck Pro key mapping table
// Actual observed key codes from the TCA8418

char getKeyChar(uint8_t keyCode) {
    switch (keyCode) {
        // Row 1 - QWERTYUIOP (codes go right to left: P=1, O=2, ... W=9, Q=97)
        case 97: return 'q';
        case 9:  return 'w';
        case 8:  return 'e';
        case 7:  return 'r';
        case 6:  return 't';
        case 5:  return 'y';
        case 4:  return 'u';
        case 3:  return 'i';
        case 2:  return 'o';
        case 1:  return 'p';
        
        // Row 2 - ASDFGHJKL + Backspace (A=98, S=19, ... L=12, Bksp=11)
        case 98: return 'a';
        case 19: return 's';
        case 18: return 'd';
        case 17: return 'f';
        case 16: return 'g';
        case 15: return 'h';
        case 14: return 'j';
        case 13: return 'k';
        case 12: return 'l';
        case 11: return '\b'; // Backspace
        
        // Row 3 - Alt ZXCVBNM Sym Enter (Alt=30, Z=29, ... Sym=22, Enter=21)
        case 30: return 0;    // Alt
        case 29: return 'z';
        case 28: return 'x';
        case 27: return 'c';
        case 26: return 'v';
        case 25: return 'b';
        case 24: return 'n';
        case 23: return 'm';
        case 22: return 0;    // Symbol/volume
        case 21: return '\r'; // Enter
        
        // Row 4 - Shift Mic Space Sym Shift
        case 35: return 0;    // Left shift
        case 34: return 0;    // Mic
        case 33: return ' ';  // Space
        case 32: return 0;    // Sym
        case 31: return 0;    // Right shift
        
        default: return 0;
    }
}

uint8_t readKeyboard() {
    // Check for key events in FIFO
    uint8_t keyCount = readKBReg(TCA8418_REG_KEY_LCK_EC) & 0x0F;
    
    if (keyCount == 0) {
        return 0;
    }
    
    // Read key event from FIFO
    uint8_t keyEvent = readKBReg(TCA8418_REG_KEY_EVENT_A);
    
    // Bit 7: 1 = press, 0 = release
    bool pressed = (keyEvent & 0x80) != 0;
    uint8_t keyCode = keyEvent & 0x7F;
    
    // Clear interrupt
    writeKBReg(TCA8418_REG_INT_STAT, 0x1F);
    
    // Only act on key press, not release
    if (!pressed || keyCode == 0) {
        return 0;
    }
    
    Serial.printf("Key event: 0x%02X (code=%d, pressed=%d)\n", keyEvent, keyCode, pressed);
    
    // Map key code to character
    char c = getKeyChar(keyCode);
    if (c != 0) {
        Serial.printf("  Mapped to: '%c' (0x%02X)\n", c >= 32 ? c : '?', c);
        return c;
    }
    
    // Return raw keycode for unmapped keys
    Serial.printf("  Unmapped key code: %d\n", keyCode);
    return 0;
}

void handleKeyPress(uint8_t key) {
    Serial.printf("handleKeyPress: key='%c' (0x%02X), fileOpen=%d\n", 
                  key >= 32 && key < 127 ? key : '?', key, reader.fileOpen);
    
    if (!reader.fileOpen) {
        // FILE LIST MODE
        switch (key) {
            case 'w':
            case 'W':
                if (selectedFileIndex > 0) {
                    selectedFileIndex--;
                    Serial.printf("  Nav UP: selectedFileIndex now %d\n", selectedFileIndex);
                    displayFileList();
                } else {
                    Serial.println("  Nav UP: already at top");
                }
                break;
                
            case 's':
            case 'S':
                if (selectedFileIndex < (int)fileList.size() - 1) {
                    selectedFileIndex++;
                    Serial.printf("  Nav DOWN: selectedFileIndex now %d\n", selectedFileIndex);
                    displayFileList();
                } else {
                    Serial.println("  Nav DOWN: already at bottom");
                }
                break;
                
            case '\r':     // Carriage return (0x0D)
            case '\n':     // Line feed (0x0A)
                Serial.printf("  ENTER: opening file %d\n", selectedFileIndex);
                if (fileList.size() > 0) {
                    openBook(fileList[selectedFileIndex]);
                }
                break;
        }
    } else {
        // READING MODE
        switch (key) {
            case 'w':
            case 'W':
            case 'a':
            case 'A':
                prevPage();
                break;
                
            case 's':
            case 'S':
            case 'd':
            case 'D':
            case ' ':      // Space
            case '\r':     // Enter
            case '\n':     // Line feed
                nextPage();
                break;
                
            case 'q':
            case 'Q':
            case 0x1B:     // Escape
                closeBook();
                displayFileList();
                break;
        }
    }
}