#include "DisplayManager.h"
#include <math.h>

DisplayManager::DisplayManager() : _tft(TFT_eSPI()) {
  for (int i = 0; i < 32; i++) {
    _prev_bar_h[i] = 0;
  }
}

void DisplayManager::begin() {
  _tft.init();
  _tft.setRotation(1);
  _tft.fillScreen(TFT_BLACK);
  _tft.setTextColor(TFT_WHITE, TFT_BLACK);
  _tft.setTextSize(1);

  // Draw static title (drawn once)
  _tft.drawString("ESP32 OOP Spectrum Visualizer", 4, 2);
  _tft.drawString("Core0:SD+I2S     Core1:FFT+Draw", 4, 10);
  _tft.drawFastHLine(0, 19, 320, TFT_DARKGREY);
}

void DisplayManager::drawSpectrum(double *vReal, int fft_size, int bar_count) {
  int screen_w = 320;
  int screen_h = 170;
  int bar_w = screen_w / bar_count;
  int max_bar_h = screen_h - 22; // Leave space for title
  int baseline = screen_h;

  // 頻譜響應範圍優化：
  // 由於人類音樂的能量完全集中在中低頻 (50Hz ~ 5000Hz)，捨棄無能量的高頻段
  // 512 個 bins 代表頻寬 22kHz，所以我們只取前 128 個 bins (大約涵蓋到 5.5kHz)
  int useful_bins = 128;
  int bins_per_bar = useful_bins / bar_count;

  for (int b = 0; b < bar_count; b++) {
    double mag = 0.0;
    for (int k = 0; k < bins_per_bar; k++) {
      mag += vReal[1 + b * bins_per_bar + k];
    }
    mag /= bins_per_bar;

    // 零極限暴力視覺增幅：改用 log10f 配合超高增益 (內倍率 5.0，外推力 85.0)
    int bar_h = (int)(log10f(1.0f + (float)mag * 5.0f) * 85.0f);
    if (bar_h > max_bar_h)
      bar_h = max_bar_h;
    if (bar_h < 0)
      bar_h = 0;

    int x = b * bar_w;

    // Gradient color: bass=red, mids=green, treble=cyan
    uint16_t color;
    if (b < bar_count / 3)
      color = TFT_RED;
    else if (b < 2 * bar_count / 3)
      color = TFT_GREEN;
    else
      color = TFT_CYAN;

    // TRUE DELTA RENDERING
    if (bar_h > _prev_bar_h[b]) {
      int diff = bar_h - _prev_bar_h[b];
      int fill_h = (_prev_bar_h[b] == 0) ? diff : diff + 1;
      _tft.fillRect(x, baseline - bar_h, bar_w - 1, fill_h, color);
      _tft.drawFastHLine(x, baseline - bar_h, bar_w - 1, TFT_WHITE);
    } else if (bar_h < _prev_bar_h[b]) {
      int diff = _prev_bar_h[b] - bar_h;
      _tft.fillRect(x, baseline - _prev_bar_h[b], bar_w - 1, diff, TFT_BLACK);
      if (bar_h > 0) {
        _tft.drawFastHLine(x, baseline - bar_h, bar_w - 1, TFT_WHITE);
      }
    }

    _prev_bar_h[b] = bar_h;
  }
}
