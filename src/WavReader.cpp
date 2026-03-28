#include "WavReader.h"
#include <SPI.h>

// We MUST use the HSPI (Hardware SPI #2) for the SD card!
// The TFT screen occupies VSPI (Hardware SPI #1) on pins 18 & 19.
// TFT_eSPI writes to VSPI registers directly, completely corrupting SD card signals.
// By shifting the SD to HSPI on pins 14, 12, 13, 27, they become 100% physically isolated!
static SPIClass spiSD(HSPI);

// WAV Header struct (44 bytes total)
struct WavHeader {
    char riff[4];
    uint32_t chunkSize;
    char wave[4];
    char fmt[4];
    uint32_t fmtSize;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char data[4];
    uint32_t dataSize;
};

WavReader::WavReader(uint8_t cs_pin, const char* filename) 
    : _cs_pin(cs_pin), _filename(filename), _finished(false) {}

WavReader::~WavReader() {
    if (_file) {
        _file.close();
    }
}

bool WavReader::begin() {
    Serial.println("[WAV] Initializing SD Card...");
    
    static bool sd_mounted = false;
    
    if (!sd_mounted) {
        // Initialize the dedicated HSPI hardware peripheral for the SD card.
        // Pins: SCK=14, MISO=21, MOSI=13, CS=_cs_pin (We will use 27)
        // CRITICAL: We changed MISO to 21 because GPIO 12 is a strapping pin!
        spiSD.begin(14, 21, 13, _cs_pin);
        
        // Breadboard wiring is too noisy for 20MHz, reverting to 4MHz for stability.
        if (!SD.begin(_cs_pin, spiSD, 4000000)) {
            Serial.println("[WAV] SD Card Mount Failed! Check wiring and FAT32 format.");
            return false;
        }
        sd_mounted = true;
    }

    if (_file) {
        _file.close();
    }
    _file = SD.open(_filename);
    
    // Aggressive Auto-Recovery Mechanism for Breadboard Dropouts
    if (!_file) {
        Serial.printf("[WAV] Failed to open %s. Attempting SD Auto-Recovery...\n", _filename);
        SD.end(); // Completely unmount the FAT32 filesystem
        delay(100); // Give the hardware a tiny breather
        
        spiSD.begin(14, 21, 13, _cs_pin);
        if (SD.begin(_cs_pin, spiSD, 4000000)) {
            _file = SD.open(_filename);
            if (_file) {
                Serial.println("[WAV] SD Auto-Recovery Successful! Resuming...");
            }
        }
        
        if (!_file) {
            Serial.println("[WAV] Critical Failure: SD Card is totally unresponsive.");
            return false;
        }
    }

    if (!parseHeader()) {
        _file.close();
        return false;
    }

    Serial.println("[WAV] Successfully opened WAV file.");
    _finished = false;
    return true;
}

bool WavReader::parseHeader() {
    WavHeader header;
    if (_file.read((uint8_t*)&header, sizeof(WavHeader)) != sizeof(WavHeader)) {
        Serial.println("[WAV] File too small to be a WAV.");
        return false;
    }

    // Basic validation
    if (strncmp(header.riff, "RIFF", 4) != 0 || strncmp(header.wave, "WAVE", 4) != 0) {
        Serial.println("[WAV] Not a valid WAV file.");
        return false;
    }
    
    if (header.numChannels != 1 || header.sampleRate != 44100 || header.bitsPerSample != 16) {
        Serial.printf("[WAV] Unsupported Format. Ch: %d, Rate: %d, Bits: %d\n", 
                      header.numChannels, header.sampleRate, header.bitsPerSample);
        Serial.println("[WAV] Required: Mono, 44100Hz, 16-bit");
        return false;
    }

    return true;
}

size_t WavReader::read(int16_t* buf, size_t frames) {
    if (!_file || _finished) return 0;

    size_t bytesToRead = frames * sizeof(int16_t);
    size_t totalRead = 0;
    uint8_t* ptr = (uint8_t*)buf;

    // Robust loop to handle partial reads across SD card sector boundaries
    while (totalRead < bytesToRead) {
        size_t r = _file.read(ptr + totalRead, bytesToRead - totalRead);
        if (r == 0) {
            Serial.printf("[WAV_DEBUG] _file.read() returned 0! Pos: %u, Size: %u\n", _file.position(), _file.size());
            break; // Genuine EOF or hardware failure
        }
        totalRead += r;
    }

    // If we didn't read full frames even after looping, we reached EOF
    if (totalRead < bytesToRead) {
        _finished = true;
        Serial.printf("[WAV_DEBUG] Reached EOF! Short read: %u / %u bytes. Pos: %u\n", totalRead, bytesToRead, _file.position());
    }

    return totalRead / sizeof(int16_t);
}

bool WavReader::isFinished() {
    return _finished;
}
