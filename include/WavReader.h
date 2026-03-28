#pragma once

#include "AudioSource.h"
#include <SD.h>

class WavReader : public IAudioSource {
public:
    WavReader(uint8_t cs_pin, const char* filename);
    ~WavReader() override;

    bool begin() override;
    size_t read(int16_t* buf, size_t frames) override;
    bool isFinished() override;

private:
    uint8_t _cs_pin;
    const char* _filename;
    File _file;
    bool _finished;
    
    // Internal helper to parse the WAV header
    bool parseHeader();
};
