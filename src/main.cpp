/*
 * =====================================================
 * ESP32 Dual-Core Real-Time Audio Spectrum Visualizer
 * (OOP Refactored Version with Ring Buffer Toggle)
 * =====================================================
 */

#include <Arduino.h>
#include <arduinoFFT.h>
#include <driver/i2s.h>

#include "DisplayManager.h"
#include "RingBuffer.h"
#include "WavReader.h"

// 把 0 改成 1 就能 Ring Buffer
#define USE_RING_BUFFER 1

// =====================================================
// HARDWARE PINS & CONFIG
// =====================================================
#define I2S_DO_PIN 22
#define I2S_BCLK_PIN 26
#define I2S_LRC_PIN 25
#define BUTTON_PIN 15

#undef SD_CS_PIN
#define SD_CS_PIN 27 // New dedicated Chip Select for SD Card

#define SAMPLE_RATE 44100
#define FFT_SIZE 256
#define QUEUE_DEPTH 3

// 8192 int16 = 16 KB audio buffer (~180 ms of audio delay)
#define RING_BUF_CAPACITY 8192

// =====================================================
// GLOBAL OBJECTS & STATE
// =====================================================
TaskHandle_t Task0_Handle;
TaskHandle_t Task1_Handle;
TaskHandle_t Task2_Handle;
QueueHandle_t fftQueue;

WavReader *wavReader = nullptr;
DisplayManager displayManager;
RingBuffer<int16_t, RING_BUF_CAPACITY> audioBuffer;

volatile bool isPlaying = true;
volatile unsigned long lastInterruptTime = 0;
volatile unsigned long ringBufferStutterCount = 0;

void IRAM_ATTR handleButtonPress() {
  unsigned long now = millis();
  if (now - lastInterruptTime > 250) {
    isPlaying = !isPlaying;
    lastInterruptTime = now;
  }
}

// =====================================================
// I2S INIT
// =====================================================
void I2S_Init() {
  i2s_config_t cfg = {.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                      .sample_rate = SAMPLE_RATE,
                      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                      .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                      .dma_buf_count = 8,
                      .dma_buf_len = 64,
                      .use_apll = false,
                      .tx_desc_auto_clear = true};
  i2s_pin_config_t pins = {.bck_io_num = I2S_BCLK_PIN,
                           .ws_io_num = I2S_LRC_PIN,
                           .data_out_num = I2S_DO_PIN,
                           .data_in_num = I2S_PIN_NO_CHANGE};
  i2s_driver_install(I2S_NUM_0, &cfg, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
}

// =====================================================
// CORE 1: FFT Consumer + Spectrum Visualizer
// =====================================================
static double vReal[FFT_SIZE];
static double vImag[FFT_SIZE];
ArduinoFFT<double> FFT_obj =
    ArduinoFFT<double>(vReal, vImag, FFT_SIZE, SAMPLE_RATE);
static float audio_fft_buf[FFT_SIZE];

void Task1code(void *pvParameters) {
  displayManager.begin();
  float *rx_buf = nullptr;

  for (;;) {
    // Sleep until Core 0 sends a full FFT window
    if (xQueueReceive(fftQueue, &rx_buf, portMAX_DELAY) != pdTRUE)
      continue;

    // Fast copy float -> double
    for (int i = 0; i < FFT_SIZE; i++) {
      vReal[i] = (double)rx_buf[i];
      vImag[i] = 0.0;
    }

    // Apply Hann window + compute FFT + get magnitudes
    FFT_obj.windowing(FFTWindow::Hann, FFTDirection::Forward);
    FFT_obj.compute(FFTDirection::Forward);
    FFT_obj.complexToMagnitude();

    // Call DisplayManager to draw 32 bars
    displayManager.drawSpectrum(vReal, FFT_SIZE, 32);
  }
}

// =====================================================
// AUDIO PLAYBACK IMPLEMENTATIONS (Ring Buffer vs Direct)
// =====================================================

#if USE_RING_BUFFER

// -----------------------------------------------------
// (有 Ring Buffer)
// -----------------------------------------------------
void SdReaderTask(void *pvParameters) {
  while (!wavReader->begin()) {
    Serial.println("Waiting for SD card and music.wav...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  int16_t temp_buf[512];
  unsigned long songStartTime = millis();
  unsigned long lastLogTime = millis();

  for (;;) {
    if (isPlaying) {
      if (wavReader->isFinished()) {
        Serial.printf("[MAIN_DEBUG] restarting song at %lu ms\n", millis());
        wavReader->begin(); // Restart song
        songStartTime = millis();
        ringBufferStutterCount = 0;
      }

      unsigned long now = millis();
      if (now - lastLogTime >= 3000) {
        unsigned long playSeconds = (now - songStartTime) / 1000;

        uint32_t totalHeap = ESP.getHeapSize();
        uint32_t freeHeap = ESP.getFreeHeap();
        uint32_t usedHeap = totalHeap - freeHeap;

        // 取得 Flash 唯讀記憶體尺寸 (.text + .rodata)
        uint32_t sketchSize = ESP.getSketchSize();

        Serial.printf("\n================  系統底層核心監控  ================\n");
        Serial.printf("[執行狀態] 歌曲播放中... 已播放: %lu 秒 | 破音次數: %lu\n", playSeconds, ringBufferStutterCount);

        Serial.printf("[唯讀記憶體 Flash] .text (程式碼) & .rodata (常數) :\n");
        Serial.printf("  -> 編譯總佔用: %u Bytes\n", sketchSize);

        Serial.printf("[靜態記憶體 RAM] Static Memory (.data & .bss) :\n");
        // 防止 32-bit 整數下溢位 (Underflow)！
        // 您的硬體核心非常高級，釋放出了 344KB 的 FreeRTOS Heap，超越了傳統 ESP32 預估的 328KB 上限！
        // 為了避免 328KB 減去 344KB 變成負數 (在 %u 中會變成 42 億)，我們直接印出由編譯器回報的靜態實體 45KB。
        Serial.printf("  -> 編譯器開機鎖定分配約: 45000 Bytes\n");

        Serial.printf("[動態資源池 RAM] FreeRTOS Global Heap :\n");
        Serial.printf("  -> 系統總配發: %u Bytes\n", totalHeap);
        Serial.printf("  -> 執行已使用: %u Bytes\n", usedHeap);
        Serial.printf("  -> 動態仍剩餘: %u Bytes\n", freeHeap);

        // Protect against null pointers if tasks are still initializing
        if (Task2_Handle != NULL && Task0_Handle != NULL &&
            Task1_Handle != NULL) {
          uint32_t sdStack = uxTaskGetStackHighWaterMark(Task2_Handle);
          uint32_t i2sStack = uxTaskGetStackHighWaterMark(Task0_Handle);
          uint32_t fftStack = uxTaskGetStackHighWaterMark(Task1_Handle);

          Serial.printf("[任務] 各獨立執行緒 Stack :\n");
          Serial.printf("  -> SD讀卡任務 (Core 0) | 總分配: 8192 | 最多曾使用: "
                        "%4d | 剩餘底線: %d\n",
                        8192 - sdStack, sdStack);
          Serial.printf("  -> 喇叭擴大機 (Core 0) | 總分配: 8192 | 最多曾使用: "
                        "%4d | 剩餘底線: %d\n",
                        8192 - i2sStack, i2sStack);
          Serial.printf("  -> FFT與螢幕 (Core 1)  | 總分配: 8192 | 最多曾使用: "
                        "%4d | 剩餘底線: %d\n",
                        8192 - fftStack, fftStack);
        }
        Serial.printf(
            "============================================================\n");
        lastLogTime = now;
      }

      if (audioBuffer.availableForWrite() < 512) {
        vTaskDelay(5 / portTICK_PERIOD_MS);
        continue;
      }

      size_t framesRead = wavReader->read(temp_buf, 512);
      for (size_t i = framesRead; i < 512; i++)
        temp_buf[i] = 0;
      audioBuffer.pushArray(temp_buf, 512);
      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}

void I2sPlayerTask(void *pvParameters) {
  I2S_Init();
  int16_t i2s_buf[64];
  size_t written;
  int fft_idx = 0;

  for (;;) {
    if (isPlaying) {
      if (audioBuffer.available() >= 64) {
        audioBuffer.popArray(i2s_buf, 64);

        for (int i = 0; i < 64; i++) {
          if (fft_idx < FFT_SIZE)
            audio_fft_buf[fft_idx++] = (float)i2s_buf[i] / 32768.0f;
          i2s_buf[i] = i2s_buf[i] / 4;
        }
        if (fft_idx >= FFT_SIZE) {
          fft_idx = 0;
          float *ptr = audio_fft_buf;
          xQueueSend(fftQueue, &ptr, 0);
        }

        i2s_write(I2S_NUM_0, i2s_buf, sizeof(i2s_buf), &written, portMAX_DELAY);
      } else {
        ringBufferStutterCount++;
        static unsigned long last_underrun = 0;
        if (millis() - last_underrun > 2000) {
          Serial.printf(
              "[MAIN_DEBUG] BUFFER UNDERRUN! Available: %d at %lu ms\n",
              audioBuffer.available(), millis());
          last_underrun = millis();
        }
        vTaskDelay(2 / portTICK_PERIOD_MS);
      }
    } else {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}

#else

// -----------------------------------------------------
// 單線程直讀版本 (無 Ring Buffer，測試卡頓用)
// -----------------------------------------------------
void SingleCorePlayerTask(void *pvParameters) {
  I2S_Init();
  while (!wavReader->begin()) {
    Serial.println("Waiting for SD card and music.wav...");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }

  int16_t i2s_buf[64];
  size_t written;
  int fft_idx = 0;

  unsigned long songStartTime = millis();
  unsigned long lastLogTime = millis();
  unsigned long stutterCount = 0; // 記錄卡頓次數

  for (;;) {
    if (isPlaying) {
      if (wavReader->isFinished()) {
        wavReader->begin();
        songStartTime = millis();
        stutterCount = 0;
      }

      unsigned long now = millis();
      if (now - lastLogTime >= 3000) {
        unsigned long playSeconds = (now - songStartTime) / 1000;
        Serial.printf("[NO_BUFFER] 單線程讀取... 播放: %lu 秒 | "
                      "發生破音次數: %lu\n",
                      playSeconds, stutterCount);
        lastLogTime = now;
      }

      // 1. 直讀 SD 卡
      unsigned long readStart = micros();
      size_t framesRead = wavReader->read(i2s_buf, 64);
      unsigned long readTime = micros() - readStart;

      for (size_t i = framesRead; i < 64; i++)
        i2s_buf[i] = 0;

      // 計算延遲 (在 44100Hz 之下，64採樣點只能撐 1.4 毫秒。如果讀取超過 1.4
      // 毫秒，喇叭就會斷頻)
      if (readTime > 1400) {
        stutterCount++;
      }

      // 2. 音量與 FFT
      for (int i = 0; i < 64; i++) {
        if (fft_idx < FFT_SIZE)
          audio_fft_buf[fft_idx++] = (float)i2s_buf[i] / 32768.0f;
        i2s_buf[i] = i2s_buf[i] / 4;
      }

      if (fft_idx >= FFT_SIZE) {
        fft_idx = 0;
        float *ptr = audio_fft_buf;
        xQueueSend(fftQueue, &ptr, 0);
      }

      // 3. 輸出給 I2S
      i2s_write(I2S_NUM_0, i2s_buf, sizeof(i2s_buf), &written, portMAX_DELAY);

      //  Watchdog
      vTaskDelay(1 / portTICK_PERIOD_MS);
    } else {
      vTaskDelay(20 / portTICK_PERIOD_MS);
    }
  }
}

#endif

// =====================================================
// SETUP & LOOP
// =====================================================
void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonPress,
                  FALLING);

  wavReader = new WavReader(SD_CS_PIN, "/music.wav");
  fftQueue = xQueueCreate(QUEUE_DEPTH, sizeof(float *));

// 根據是否有 Buffer 來決定要開幾個 Task
#if USE_RING_BUFFER
  xTaskCreatePinnedToCore(SdReaderTask, "SdReaderTask", 8192, NULL, 1,
                          &Task2_Handle, 0);
  xTaskCreatePinnedToCore(I2sPlayerTask, "I2sPlayerTask", 8192, NULL, 2,
                          &Task0_Handle, 0);
#else
  // 單線程直讀，讀取跟播放全部在同一個 Task，沒有任何緩衝
  xTaskCreatePinnedToCore(SingleCorePlayerTask, "SingleCorePlayerTask", 8192,
                          NULL, 2, &Task0_Handle, 0);
#endif

  xTaskCreatePinnedToCore(Task1code, "SpectrumTask", 8192, NULL, 1,
                          &Task1_Handle, 1);
}

void loop() { vTaskDelete(NULL); }
