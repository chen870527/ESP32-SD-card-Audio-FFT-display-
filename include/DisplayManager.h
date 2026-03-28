#pragma once

#include <TFT_eSPI.h>

class DisplayManager {
public:
    DisplayManager();
    
    // Initialize screen and static texts
    void begin();

    // Draw spectrum with true delta-rendering optimization
    void drawSpectrum(double* vReal, int fft_size, int bar_count);

private:
    TFT_eSPI _tft;
    int _prev_bar_h[32]; // Max 32 bars optimization
};
