#pragma once

#include <Arduino.h>

class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool begin() = 0;
    virtual size_t read(int16_t* buf, size_t frames) = 0;
    virtual bool isFinished() = 0;
};
